#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <bit>
#include <cstdint>

namespace
{
constexpr int kPluginIndex = DIGITALIS_PLUGIN_INDEX;

juce::String pluginTag()
{
    switch (kPluginIndex)
    {
        case 1: return "Floating Point Collapse";
        case 2: return "Nyquist Destroyer";
        case 3: return "Buffer Glitch Engine";
        case 4: return "Automation Quantiser";
        case 5: return "Streaming Artifact Generator";
        case 6: return "FFT Brutalist";
        case 7: return "Overclock Failure";
        case 8: return "Deterministic Machine";
        case 9: return "Classic Buffer Stutter";
        case 10: return "Melodic Skipping Engine";
        default: return "Digitalis";
    }
}

float quantise(float x, float steps)
{
    return std::round(x * steps) / steps;
}

float truncateMantissa(float x, int keepBits)
{
    if (keepBits >= 23 || !std::isfinite(x) || x == 0.0f)
        return x;

    const auto bits = std::bit_cast<std::uint32_t>(x);
    const auto exponent = (bits >> 23u) & 0xffu;
    if (exponent == 0u || exponent == 0xffu)
        return x;

    const auto dropBits = static_cast<std::uint32_t>(23 - juce::jlimit(1, 23, keepBits));
    const auto mask = ~((1u << dropBits) - 1u);
    const auto signAndExponent = bits & 0xff800000u;
    const auto mantissa = bits & 0x007fffffu;
    return std::bit_cast<float>(signAndExponent | (mantissa & mask));
}

float quantiseExponent(float x, int exponentStep)
{
    if (exponentStep <= 1 || !std::isfinite(x) || x == 0.0f)
        return x;

    const auto bits = std::bit_cast<std::uint32_t>(x);
    const auto exponent = (bits >> 23u) & 0xffu;
    if (exponent == 0u || exponent == 0xffu)
        return x;

    const auto unbiased = static_cast<int>(exponent) - 127;
    const auto q = static_cast<int>(std::round(static_cast<float>(unbiased) / static_cast<float>(exponentStep))) * exponentStep;
    const auto clamped = juce::jlimit(-126, 127, q);
    const auto newExponent = static_cast<std::uint32_t>(clamped + 127);

    const auto signAndMantissa = bits & 0x807fffffu;
    return std::bit_cast<float>(signAndMantissa | (newExponent << 23u));
}

std::uint32_t hashStep(std::uint32_t hash, float x)
{
    const auto q = static_cast<std::int32_t>(std::round(juce::jlimit(-1.0f, 1.0f, x) * 32767.0f));
    hash ^= static_cast<std::uint32_t>(q);
    hash *= 16777619u;
    return hash;
}

float defaultAutoLevelPercent()
{
    switch (kPluginIndex)
    {
        case 1: return 58.0f; // Floating Point Collapse
        case 2: return 46.0f; // Nyquist Destroyer
        case 3: return 52.0f; // Buffer Glitch Engine
        case 4: return 54.0f; // Automation Quantiser
        case 5: return 42.0f; // Streaming Artifact Generator
        case 6: return 36.0f; // FFT Brutalist
        case 7: return 50.0f; // Overclock Failure
        case 8: return 48.0f; // Deterministic Machine
        case 9: return 50.0f; // Classic Buffer Stutter
        case 10: return 47.0f; // Melodic Skipping Engine
        default: return 45.0f;
    }
}

float defaultSafetyPercent()
{
    switch (kPluginIndex)
    {
        case 1: return 66.0f;
        case 2: return 61.0f;
        case 3: return 70.0f;
        case 4: return 58.0f;
        case 5: return 72.0f;
        case 6: return 76.0f;
        case 7: return 74.0f;
        case 8: return 63.0f;
        case 9: return 66.0f;
        case 10: return 68.0f;
        default: return 62.0f;
    }
}

float defaultOutputTrimDb()
{
    switch (kPluginIndex)
    {
        case 1: return -10.1f;
        case 2: return -9.6f;
        case 3: return -9.7f;
        case 4: return 1.5f;
        case 5: return -8.4f;
        case 6: return -10.0f;
        case 7: return -10.0f;
        case 8: return -16.1f;
        case 9: return -10.0f;
        case 10: return -10.7f;
        default: return 0.0f;
    }
}

float targetRmsForPlugin()
{
    switch (kPluginIndex)
    {
        case 1: return 0.17f;
        case 2: return 0.18f;
        case 3: return 0.16f;
        case 4: return 0.18f;
        case 5: return 0.15f;
        case 6: return 0.145f;
        case 7: return 0.155f;
        case 8: return 0.165f;
        case 9: return 0.17f;
        case 10: return 0.17f;
        default: return 0.18f;
    }
}
}

DigitalisAudioProcessor::DigitalisAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    factoryPresets = createFactoryPresets();
    applyFactoryPreset(0);
}

void DigitalisAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxBlockSize = samplesPerBlock;
    processedSamples = 0;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32>(juce::jmax(1, getTotalNumOutputChannels()));

    dryWet.prepare(spec);
    dryWet.reset();

    auto loopSize = juce::jmax(32, static_cast<int>(0.03 * sampleRate));
    for (auto& buffer : microLoopBuffers)
    {
        buffer.setSize(1, loopSize);
        buffer.clear();
    }

    heldSamples = { 0.0f, 0.0f };
    heldCountdown = { 0, 0 };
    fpcTemporalHeld = { 0.0f, 0.0f };
    fpcTemporalCountdown = { 0, 0 };
    microLoopWritePos = { 0, 0 };
    microLoopReadPos = { 0, 0 };
    denormalBurstRemaining = 0;
    nyqHeldCurrent = { 0.0f, 0.0f };
    nyqHeldPrevious = { 0.0f, 0.0f };
    nyqHoldCounter = { 1, 1 };
    nyqFeedbackState = { 0.0f, 0.0f };
    nyqFeedbackToneState = { 0.0f, 0.0f };
    nyqPhase = 0.0f;
    nyqTransientEnv = 0.0f;
    for (auto& chunk : bgePrevChunk)
    {
        chunk.assign((size_t) juce::jmax(32, maxBlockSize), 0.0f);
    }
    bgeEnvelope = { 0.0f, 0.0f };
    bgePrevChunkSize = 0;
    bgeHasPrevChunk = false;
    aqHeldAmp = { 1.0f, 1.0f };
    aqHeldCounter = { 1, 1 };
    aqLfoPhase = { 0.0f, juce::MathConstants<float>::pi * 0.5f };
    sagToneState = { 0.0f, 0.0f };
    sagSmearState = { 0.0f, 0.0f };
    sagLastFrameSample = { 0.0f, 0.0f };
    sagTransientEnv = { 0.0f, 0.0f };
    sagCodec = 0;
    sagCodecCounter = 0;
    sagLossBurstRemaining = 0;
    for (auto& frozen : fftBrutalistFrozenSpectrum)
    {
        frozen.assign((size_t) fftBrutalistSize, juce::dsp::Complex<float>(0.0f, 0.0f));
    }
    fftBrutalistFreezeRemaining = { 0, 0 };
    for (auto& line : ocfDelayLine)
    {
        line.assign((size_t) juce::jmax(2048, static_cast<int>(currentSampleRate * 0.25)), 0.0f);
    }
    ocfDelayWritePos = { 0, 0 };
    ocfDelayReadOffset = { 1, 1 };
    ocfHoldValue = { 0.0f, 0.0f };
    ocfHoldRemaining = { 0, 0 };
    ocfThermalState = 0.0f;
    ocfStressEnv = 0.0f;
    for (auto& loop : dmLoopBuffer)
    {
        loop.assign((size_t) juce::jmax(64, static_cast<int>(0.06 * currentSampleRate)), 0.0f);
    }
    dmLoopWritePos = { 0, 0 };
    dmLoopReadPos = { 0, 0 };
    dmStateIndex = 0;
    dmSamplesToNextState = 0;
    dmHashCounter = 0;
    dmHashState = 2166136261u;
    dmStateSmoother = 0.0f;
    const auto stutterMax = juce::jmax(256, static_cast<int>(0.5 * currentSampleRate));
    for (auto& slice : stutterSliceBuffer)
        slice.assign((size_t) stutterMax, 0.0f);
    stutterCapturePos = { 0, 0 };
    stutterPlayPos = { 0, 0 };
    stutterRepeatsRemaining = { 0, 0 };
    stutterIntervalCounter = { 1, 1 };
    stutterIsCapturing = { false, false };
    stutterIsPlaying = { false, false };
    stutterIsReverse = { false, false };
    const auto mskSize = juce::jmax(2048, static_cast<int>(2.5 * currentSampleRate));
    for (auto& b : mskBuffer)
        b.assign((size_t) mskSize, 0.0f);
    mskWritePos = { 0, 0 };
    mskPlayPos = { 0.0f, 0.0f };
    mskRemaining = { 0, 0 };
    mskRate = { 1.0f, 1.0f };
    mskDirection = { 1, 1 };
    mskBlurState = { 0.0f, 0.0f };
    postDcPrevInput = { 0.0f, 0.0f };
    postDcPrevOutput = { 0.0f, 0.0f };
    postAutoLevelGain = 1.0f;
}

void DigitalisAudioProcessor::releaseResources()
{
}

bool DigitalisAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto inSet = layouts.getMainInputChannelSet();
    const auto outSet = layouts.getMainOutputChannelSet();

    if (inSet.isDisabled() || outSet.isDisabled())
        return false;

    const auto inChannels = inSet.size();
    const auto outChannels = outSet.size();

    // Support mono, stereo, and mono->stereo inserts so hosts can pick the track format.
    return (inChannels == 1 && (outChannels == 1 || outChannels == 2))
        || (inChannels == 2 && outChannels == 2);
}

void DigitalisAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    const bool monoToStereo = (totalNumInputChannels == 1 && totalNumOutputChannels >= 2);

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    auto* mixAmount = parameters.getRawParameterValue("mix");

    const auto wet = juce::jlimit(0.0f, 1.0f, *mixAmount * 0.01f);
    dryWet.setWetMixProportion(wet);
    dryWet.pushDrySamples(juce::dsp::AudioBlock<float>(buffer));

    if (kPluginIndex == 1)
    {
        processFloatingPointCollapse(buffer);
        applyPostSafety(buffer);
        dryWet.mixWetSamples(juce::dsp::AudioBlock<float>(buffer));
        if (monoToStereo)
            for (auto ch = 1; ch < totalNumOutputChannels; ++ch)
                buffer.copyFrom(ch, 0, buffer, 0, 0, buffer.getNumSamples());
        return;
    }
    if (kPluginIndex == 2)
    {
        processNyquistDestroyer(buffer);
        applyPostSafety(buffer);
        dryWet.mixWetSamples(juce::dsp::AudioBlock<float>(buffer));
        if (monoToStereo)
            for (auto ch = 1; ch < totalNumOutputChannels; ++ch)
                buffer.copyFrom(ch, 0, buffer, 0, 0, buffer.getNumSamples());
        return;
    }
    if (kPluginIndex == 3)
    {
        processBufferGlitchEngine(buffer);
        applyPostSafety(buffer);
        dryWet.mixWetSamples(juce::dsp::AudioBlock<float>(buffer));
        if (monoToStereo)
            for (auto ch = 1; ch < totalNumOutputChannels; ++ch)
                buffer.copyFrom(ch, 0, buffer, 0, 0, buffer.getNumSamples());
        return;
    }
    if (kPluginIndex == 4)
    {
        processAutomationQuantiser(buffer);
        applyPostSafety(buffer);
        dryWet.mixWetSamples(juce::dsp::AudioBlock<float>(buffer));
        if (monoToStereo)
            for (auto ch = 1; ch < totalNumOutputChannels; ++ch)
                buffer.copyFrom(ch, 0, buffer, 0, 0, buffer.getNumSamples());
        return;
    }
    if (kPluginIndex == 5)
    {
        processStreamingArtifactGenerator(buffer);
        applyPostSafety(buffer);
        dryWet.mixWetSamples(juce::dsp::AudioBlock<float>(buffer));
        if (monoToStereo)
            for (auto ch = 1; ch < totalNumOutputChannels; ++ch)
                buffer.copyFrom(ch, 0, buffer, 0, 0, buffer.getNumSamples());
        return;
    }
    if (kPluginIndex == 6)
    {
        processFFTBrutalist(buffer);
        applyPostSafety(buffer);
        dryWet.mixWetSamples(juce::dsp::AudioBlock<float>(buffer));
        if (monoToStereo)
            for (auto ch = 1; ch < totalNumOutputChannels; ++ch)
                buffer.copyFrom(ch, 0, buffer, 0, 0, buffer.getNumSamples());
        return;
    }
    if (kPluginIndex == 7)
    {
        processOverclockFailure(buffer);
        applyPostSafety(buffer);
        dryWet.mixWetSamples(juce::dsp::AudioBlock<float>(buffer));
        if (monoToStereo)
            for (auto ch = 1; ch < totalNumOutputChannels; ++ch)
                buffer.copyFrom(ch, 0, buffer, 0, 0, buffer.getNumSamples());
        return;
    }
    if (kPluginIndex == 8)
    {
        processDeterministicMachine(buffer);
        applyPostSafety(buffer);
        dryWet.mixWetSamples(juce::dsp::AudioBlock<float>(buffer));
        if (monoToStereo)
            for (auto ch = 1; ch < totalNumOutputChannels; ++ch)
                buffer.copyFrom(ch, 0, buffer, 0, 0, buffer.getNumSamples());
        return;
    }
    if (kPluginIndex == 9)
    {
        processClassicBufferStutter(buffer);
        applyPostSafety(buffer);
        dryWet.mixWetSamples(juce::dsp::AudioBlock<float>(buffer));
        if (monoToStereo)
            for (auto ch = 1; ch < totalNumOutputChannels; ++ch)
                buffer.copyFrom(ch, 0, buffer, 0, 0, buffer.getNumSamples());
        return;
    }
    if (kPluginIndex == 10)
    {
        processMelodicSkippingEngine(buffer);
        applyPostSafety(buffer);
        dryWet.mixWetSamples(juce::dsp::AudioBlock<float>(buffer));
        if (monoToStereo)
            for (auto ch = 1; ch < totalNumOutputChannels; ++ch)
                buffer.copyFrom(ch, 0, buffer, 0, 0, buffer.getNumSamples());
        return;
    }

    auto* digitalAmount = parameters.getRawParameterValue("digital");

    for (int ch = 0; ch < totalNumInputChannels; ++ch)
    {
        auto* channelData = buffer.getWritePointer(ch);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            auto x = channelData[i];

            switch (kPluginIndex)
            {
                case 1: x = crushSample(x); break;
                case 2: x = aliasSample(x, ch, i); break;
                case 3: x = (i % juce::jmax(1, 64 - static_cast<int>(*digitalAmount * 0.5f)) == 0 ? 0.0f : x); break;
                case 4: x = gridSample(x, i); break;
                case 5: x = quantise(x, juce::jmax(8.0f, 256.0f - *digitalAmount * 2.0f)); break;
                case 6: x = quantise(x, juce::jmax(4.0f, 64.0f - *digitalAmount * 0.4f)); x = std::sin(x * juce::MathConstants<float>::pi); break;
                case 7: x = dropoutSample(x); break;
                case 8: x = deterministicSample(x, ch); break;
                default: break;
            }

            channelData[i] = juce::jlimit(-1.0f, 1.0f, x);
            ++processedSamples;
        }
    }

    dryWet.mixWetSamples(juce::dsp::AudioBlock<float>(buffer));
}

juce::AudioProcessorEditor* DigitalisAudioProcessor::createEditor()
{
    return new DigitalisAudioProcessorEditor(*this);
}

bool DigitalisAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String DigitalisAudioProcessor::getName() const
{
    return pluginTag();
}

bool DigitalisAudioProcessor::acceptsMidi() const
{
    return false;
}

bool DigitalisAudioProcessor::producesMidi() const
{
    return false;
}

bool DigitalisAudioProcessor::isMidiEffect() const
{
    return false;
}

double DigitalisAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int DigitalisAudioProcessor::getNumPrograms()
{
    return juce::jmax(1, static_cast<int>(factoryPresets.size()));
}

int DigitalisAudioProcessor::getCurrentProgram()
{
    return currentProgramIndex;
}

void DigitalisAudioProcessor::setCurrentProgram(int index)
{
    applyFactoryPreset(static_cast<size_t>(juce::jlimit(0, getNumPrograms() - 1, index)));
}

const juce::String DigitalisAudioProcessor::getProgramName(int index)
{
    const auto clamped = juce::jlimit(0, getNumPrograms() - 1, index);
    return factoryPresets.empty() ? juce::String("Init") : juce::String(factoryPresets[(size_t) clamped].name);
}

void DigitalisAudioProcessor::changeProgramName(int, const juce::String&)
{
}

void DigitalisAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void DigitalisAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr)
        if (xmlState->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessorValueTreeState::ParameterLayout DigitalisAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    if (kPluginIndex == 1)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>("collapse", "Collapse", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 55.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("mantissaBits", "Mantissa Bits", juce::NormalisableRange<float>(3.0f, 23.0f, 1.0f), 11.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("exponentStep", "Exponent Step", juce::NormalisableRange<float>(1.0f, 16.0f, 1.0f), 3.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>("temporalHold", "Temporal Hold", juce::StringArray { "1", "2", "4", "8", "16", "32", "64", "128" }, 3));
        params.push_back(std::make_unique<juce::AudioParameterChoice>("blockSize", "Block Size", juce::StringArray { "8", "16", "32", "64", "128", "256", "512", "1024" }, 4));
        params.push_back(std::make_unique<juce::AudioParameterChoice>("quantCurve", "Quant Curve", juce::StringArray { "Uniform", "Log", "MuLaw", "Chaotic" }, 2));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("rounding", "Rounding Chaos", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 18.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("denormal", "Denormal Burst", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 8.0f));
    }
    else if (kPluginIndex == 2)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>("destroy", "Destroy", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 58.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("minSR", "Min SR", juce::NormalisableRange<float>(1000.0f, 48000.0f, 1.0f), 6000.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("maxSR", "Max SR", juce::NormalisableRange<float>(4000.0f, 96000.0f, 1.0f), 44100.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("modRate", "SR Mod Rate", juce::NormalisableRange<float>(0.05f, 20.0f, 0.001f, 0.33f), 2.4f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("modDepth", "SR Mod Depth", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 72.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("interpErr", "Interp Error", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 48.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("transient", "Transient SR Drop", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 60.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("feedback", "Alias Feedback", juce::NormalisableRange<float>(0.0f, 95.0f, 0.01f), 24.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("fbTone", "Feedback Tone", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 50.0f));
    }
    else if (kPluginIndex == 3)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>("stress", "Engine Stress", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 52.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>("baseBlock", "Base Block", juce::StringArray { "16", "32", "64", "128", "256", "512" }, 2));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("blockJitter", "Block Jitter", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 62.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("seam", "Seam Error", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 42.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("tailDrop", "Tail Drop", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 28.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("reorder", "Reorder", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 46.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("lookFail", "Lookahead Failure", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 38.0f));
    }
    else if (kPluginIndex == 4)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>("brutal", "Brutalism", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 58.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>("gridMode", "Grid Mode", juce::StringArray { "Block", "Samples", "Beat" }, 1));
        params.push_back(std::make_unique<juce::AudioParameterChoice>("stepDiv", "Step Division", juce::StringArray { "1", "2", "4", "8", "16", "32", "64", "128" }, 4));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("zipper", "Zipper Tone", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 40.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("levels", "Envelope Levels", juce::NormalisableRange<float>(2.0f, 64.0f, 1.0f), 10.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("phaseLock", "Phase Lock", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 62.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("jitter", "Human Error", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 8.0f));
    }
    else if (kPluginIndex == 5)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>("artifact", "Artifact", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 56.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("bitrate", "Target Bitrate", juce::NormalisableRange<float>(8.0f, 320.0f, 1.0f), 96.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("masking", "Masking Aggression", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 64.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("smear", "Smear Time", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 42.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>("codecMode", "Codec Mode", juce::StringArray { "Fixed MP3", "Cycle", "Random" }, 1));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("switchMs", "Switch Rate", juce::NormalisableRange<float>(40.0f, 1200.0f, 1.0f), 220.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("packetLoss", "Packet Loss", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 18.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("burst", "Burstiness", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 38.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("preecho", "Pre Echo", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 26.0f));
    }
    else if (kPluginIndex == 6)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>("brutalism", "Brutalism", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 60.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("binDensity", "Bin Density", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 52.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>("cluster", "Cluster Size", juce::StringArray { "1", "2", "4", "8", "16", "32" }, 2));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("freezeRate", "Freeze Rate", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 25.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("freezeLen", "Freeze Length", juce::NormalisableRange<float>(10.0f, 1200.0f, 1.0f), 150.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("phaseScramble", "Phase Scramble", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 32.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("phaseSteps", "Phase Steps", juce::NormalisableRange<float>(2.0f, 64.0f, 1.0f), 16.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("sortAmount", "Sort Amount", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 35.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("jitter", "Spectral Jitter", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 18.0f));
    }
    else if (kPluginIndex == 7)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>("overclock", "Overclock", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 55.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("sensitivity", "Stress Sensitivity", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 60.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("failureRate", "Failure Rate", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 34.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("latencySpike", "Latency Spike", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 28.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("desync", "L R Desync", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 32.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("thermal", "Thermal Drift", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 48.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("recovery", "Recovery", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 42.0f));
    }
    else if (kPluginIndex == 8)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>("determinism", "Determinism", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 62.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("stateCount", "State Count", juce::NormalisableRange<float>(2.0f, 128.0f, 1.0f), 16.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("stateDwell", "State Dwell", juce::NormalisableRange<float>(5.0f, 1200.0f, 1.0f), 120.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("loopMs", "Micro Loop Length", juce::NormalisableRange<float>(5.0f, 60.0f, 0.1f), 28.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("hashWindow", "Hash Window", juce::NormalisableRange<float>(8.0f, 1024.0f, 1.0f), 96.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>("jumpRule", "State Jump Rule", juce::StringArray { "Sequential", "Hash", "Threshold" }, 1));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("memory", "Memory", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 40.0f));
    }
    else if (kPluginIndex == 9)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>("amount", "Amount", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 54.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("rateHz", "Stutter Rate", juce::NormalisableRange<float>(0.25f, 24.0f, 0.001f, 0.35f), 6.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("sliceMs", "Slice Length", juce::NormalisableRange<float>(10.0f, 250.0f, 0.1f, 0.4f), 52.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("repeats", "Repeats", juce::NormalisableRange<float>(1.0f, 16.0f, 1.0f), 4.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("reverse", "Reverse Chance", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 18.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("timingJitter", "Timing Jitter", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 12.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("duck", "Dry Duck", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 34.0f));
    }
    else if (kPluginIndex == 10)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>("skip", "Skip Amount", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 58.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("jumpRate", "Jump Rate", juce::NormalisableRange<float>(0.2f, 18.0f, 0.001f, 0.35f), 5.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("segMs", "Segment Length", juce::NormalisableRange<float>(60.0f, 2500.0f, 0.1f, 0.4f), 280.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("melody", "Melody", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 56.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("spread", "Pitch Spread", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 48.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("reverse", "Reverse Chance", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 22.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("flutter", "Flutter", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 16.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("blur", "Blur", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 24.0f));
    }
    else
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>("digital", "Digital", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 45.0f));
    }

    params.push_back(std::make_unique<juce::AudioParameterFloat>("autolevel", "Auto Level", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), defaultAutoLevelPercent()));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("safety", "Safety", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), defaultSafetyPercent()));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("output", "Output", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), defaultOutputTrimDb()));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("mix", "Mix", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 100.0f));
    return { params.begin(), params.end() };
}

std::vector<DigitalisAudioProcessor::FactoryPreset> DigitalisAudioProcessor::createFactoryPresets()
{
    const auto make = [](const char* name, std::initializer_list<PresetParam> values) -> FactoryPreset
    {
        return { name, std::vector<PresetParam>(values.begin(), values.end()) };
    };

    if (kPluginIndex == 1)
    {
        return {
            make("Init", {{ "collapse", 5.0f }, { "mantissaBits", 23.0f }, { "exponentStep", 1.0f }, { "temporalHold", 0.0f }, { "blockSize", 0.0f }, { "quantCurve", 0.0f }, { "rounding", 0.0f }, { "denormal", 0.0f }, { "mix", 100.0f }, { "autolevel", 58.0f }, { "safety", 66.0f }, { "output", -10.1f } }),
            make("Safe Mix", {{ "collapse", 22.0f }, { "mantissaBits", 16.0f }, { "exponentStep", 2.0f }, { "temporalHold", 2.0f }, { "blockSize", 2.0f }, { "quantCurve", 2.0f }, { "rounding", 8.0f }, { "denormal", 2.0f }, { "mix", 32.0f }, { "autolevel", 62.0f }, { "safety", 70.0f }, { "output", -1.6f } }),
            make("Subtle Dust", {{ "collapse", 30.0f }, { "mantissaBits", 14.0f }, { "exponentStep", 2.0f }, { "temporalHold", 2.0f }, { "blockSize", 2.0f }, { "quantCurve", 1.0f }, { "rounding", 12.0f }, { "denormal", 2.0f }, { "mix", 45.0f }, { "autolevel", 60.0f }, { "safety", 69.0f }, { "output", -1.4f } }),
            make("Subtle Glass", {{ "collapse", 35.0f }, { "mantissaBits", 12.0f }, { "exponentStep", 3.0f }, { "temporalHold", 3.0f }, { "blockSize", 3.0f }, { "quantCurve", 2.0f }, { "rounding", 15.0f }, { "denormal", 4.0f }, { "mix", 50.0f }, { "autolevel", 59.0f }, { "safety", 69.0f }, { "output", -1.3f } }),
            make("Medium Crunch", {{ "collapse", 55.0f }, { "mantissaBits", 10.0f }, { "exponentStep", 4.0f }, { "temporalHold", 3.0f }, { "blockSize", 4.0f }, { "quantCurve", 2.0f }, { "rounding", 24.0f }, { "denormal", 8.0f }, { "mix", 68.0f }, { "autolevel", 58.0f }, { "safety", 70.0f }, { "output", -1.1f } }),
            make("Medium Pump", {{ "collapse", 60.0f }, { "mantissaBits", 9.0f }, { "exponentStep", 5.0f }, { "temporalHold", 4.0f }, { "blockSize", 4.0f }, { "quantCurve", 3.0f }, { "rounding", 30.0f }, { "denormal", 10.0f }, { "mix", 72.0f }, { "autolevel", 57.0f }, { "safety", 72.0f }, { "output", -1.0f } }),
            make("Extreme Ruin", {{ "collapse", 85.0f }, { "mantissaBits", 6.0f }, { "exponentStep", 9.0f }, { "temporalHold", 6.0f }, { "blockSize", 6.0f }, { "quantCurve", 3.0f }, { "rounding", 60.0f }, { "denormal", 25.0f }, { "mix", 100.0f }, { "autolevel", 55.0f }, { "safety", 78.0f }, { "output", -2.0f } }),
            make("Extreme Floatfire", {{ "collapse", 95.0f }, { "mantissaBits", 4.0f }, { "exponentStep", 12.0f }, { "temporalHold", 7.0f }, { "blockSize", 7.0f }, { "quantCurve", 3.0f }, { "rounding", 80.0f }, { "denormal", 40.0f }, { "mix", 100.0f }, { "autolevel", 50.0f }, { "safety", 82.0f }, { "output", -3.0f } }),
            make("Rhythmic Steps", {{ "collapse", 70.0f }, { "mantissaBits", 8.0f }, { "exponentStep", 6.0f }, { "temporalHold", 5.0f }, { "blockSize", 4.0f }, { "quantCurve", 0.0f }, { "rounding", 35.0f }, { "denormal", 8.0f }, { "mix", 78.0f }, { "autolevel", 56.0f }, { "safety", 74.0f }, { "output", -1.6f } }),
            make("Rhythmic Pulsar", {{ "collapse", 75.0f }, { "mantissaBits", 7.0f }, { "exponentStep", 7.0f }, { "temporalHold", 6.0f }, { "blockSize", 5.0f }, { "quantCurve", 1.0f }, { "rounding", 45.0f }, { "denormal", 12.0f }, { "mix", 80.0f }, { "autolevel", 56.0f }, { "safety", 75.0f }, { "output", -1.8f } })
        };
    }

    if (kPluginIndex == 2)
    {
        return {
            make("Init", {{ "destroy", 4.0f }, { "minSR", 22050.0f }, { "maxSR", 48000.0f }, { "modRate", 0.2f }, { "modDepth", 0.0f }, { "interpErr", 0.0f }, { "transient", 0.0f }, { "feedback", 0.0f }, { "fbTone", 50.0f }, { "mix", 100.0f }, { "autolevel", 46.0f }, { "safety", 61.0f }, { "output", -9.6f } }),
            make("Safe Mix", {{ "destroy", 28.0f }, { "minSR", 12000.0f }, { "maxSR", 48000.0f }, { "modRate", 0.7f }, { "modDepth", 35.0f }, { "interpErr", 20.0f }, { "transient", 26.0f }, { "feedback", 8.0f }, { "fbTone", 46.0f }, { "mix", 35.0f }, { "autolevel", 52.0f }, { "safety", 66.0f }, { "output", -1.2f } }),
            make("Subtle Fold", {{ "destroy", 35.0f }, { "minSR", 9000.0f }, { "maxSR", 44100.0f }, { "modRate", 0.9f }, { "modDepth", 42.0f }, { "interpErr", 28.0f }, { "transient", 30.0f }, { "feedback", 12.0f }, { "fbTone", 42.0f }, { "mix", 52.0f }, { "autolevel", 50.0f }, { "safety", 67.0f }, { "output", -1.0f } }),
            make("Subtle Mirror", {{ "destroy", 40.0f }, { "minSR", 7600.0f }, { "maxSR", 48000.0f }, { "modRate", 1.3f }, { "modDepth", 46.0f }, { "interpErr", 30.0f }, { "transient", 35.0f }, { "feedback", 14.0f }, { "fbTone", 55.0f }, { "mix", 56.0f }, { "autolevel", 49.0f }, { "safety", 68.0f }, { "output", -1.1f } }),
            make("Medium Shred", {{ "destroy", 58.0f }, { "minSR", 5500.0f }, { "maxSR", 44100.0f }, { "modRate", 2.1f }, { "modDepth", 62.0f }, { "interpErr", 48.0f }, { "transient", 56.0f }, { "feedback", 22.0f }, { "fbTone", 50.0f }, { "mix", 74.0f }, { "autolevel", 47.0f }, { "safety", 70.0f }, { "output", -1.4f } }),
            make("Medium Motion", {{ "destroy", 64.0f }, { "minSR", 4200.0f }, { "maxSR", 52000.0f }, { "modRate", 3.4f }, { "modDepth", 70.0f }, { "interpErr", 56.0f }, { "transient", 62.0f }, { "feedback", 28.0f }, { "fbTone", 36.0f }, { "mix", 78.0f }, { "autolevel", 46.0f }, { "safety", 72.0f }, { "output", -1.6f } }),
            make("Extreme Shatter", {{ "destroy", 90.0f }, { "minSR", 1800.0f }, { "maxSR", 96000.0f }, { "modRate", 8.0f }, { "modDepth", 92.0f }, { "interpErr", 88.0f }, { "transient", 90.0f }, { "feedback", 55.0f }, { "fbTone", 30.0f }, { "mix", 100.0f }, { "autolevel", 43.0f }, { "safety", 79.0f }, { "output", -2.8f } }),
            make("Extreme Spiral", {{ "destroy", 96.0f }, { "minSR", 1000.0f }, { "maxSR", 96000.0f }, { "modRate", 15.0f }, { "modDepth", 98.0f }, { "interpErr", 95.0f }, { "transient", 95.0f }, { "feedback", 72.0f }, { "fbTone", 22.0f }, { "mix", 100.0f }, { "autolevel", 38.0f }, { "safety", 84.0f }, { "output", -3.4f } }),
            make("Rhythmic Fold", {{ "destroy", 72.0f }, { "minSR", 3200.0f }, { "maxSR", 42000.0f }, { "modRate", 4.0f }, { "modDepth", 76.0f }, { "interpErr", 64.0f }, { "transient", 70.0f }, { "feedback", 30.0f }, { "fbTone", 44.0f }, { "mix", 82.0f }, { "autolevel", 45.0f }, { "safety", 74.0f }, { "output", -1.9f } }),
            make("Rhythmic Alias Kick", {{ "destroy", 78.0f }, { "minSR", 2600.0f }, { "maxSR", 36000.0f }, { "modRate", 6.2f }, { "modDepth", 84.0f }, { "interpErr", 72.0f }, { "transient", 82.0f }, { "feedback", 42.0f }, { "fbTone", 40.0f }, { "mix", 86.0f }, { "autolevel", 44.0f }, { "safety", 76.0f }, { "output", -2.2f } })
        };
    }

    if (kPluginIndex == 3)
    {
        return {
            make("Init", {{ "stress", 4.0f }, { "baseBlock", 2.0f }, { "blockJitter", 0.0f }, { "seam", 0.0f }, { "tailDrop", 0.0f }, { "reorder", 0.0f }, { "lookFail", 0.0f }, { "mix", 100.0f }, { "autolevel", 52.0f }, { "safety", 70.0f }, { "output", -9.7f } }),
            make("Safe Mix", {{ "stress", 25.0f }, { "baseBlock", 2.0f }, { "blockJitter", 25.0f }, { "seam", 15.0f }, { "tailDrop", 8.0f }, { "reorder", 12.0f }, { "lookFail", 10.0f }, { "mix", 30.0f }, { "autolevel", 58.0f }, { "safety", 74.0f }, { "output", -2.2f } }),
            make("Subtle Drift", {{ "stress", 34.0f }, { "baseBlock", 2.0f }, { "blockJitter", 36.0f }, { "seam", 24.0f }, { "tailDrop", 10.0f }, { "reorder", 18.0f }, { "lookFail", 14.0f }, { "mix", 50.0f }, { "autolevel", 56.0f }, { "safety", 73.0f }, { "output", -2.0f } }),
            make("Subtle Slips", {{ "stress", 38.0f }, { "baseBlock", 3.0f }, { "blockJitter", 32.0f }, { "seam", 30.0f }, { "tailDrop", 14.0f }, { "reorder", 24.0f }, { "lookFail", 18.0f }, { "mix", 54.0f }, { "autolevel", 55.0f }, { "safety", 74.0f }, { "output", -2.1f } }),
            make("Medium Stutter", {{ "stress", 56.0f }, { "baseBlock", 1.0f }, { "blockJitter", 58.0f }, { "seam", 44.0f }, { "tailDrop", 28.0f }, { "reorder", 42.0f }, { "lookFail", 36.0f }, { "mix", 76.0f }, { "autolevel", 52.0f }, { "safety", 76.0f }, { "output", -2.4f } }),
            make("Medium Seams", {{ "stress", 62.0f }, { "baseBlock", 0.0f }, { "blockJitter", 64.0f }, { "seam", 56.0f }, { "tailDrop", 30.0f }, { "reorder", 48.0f }, { "lookFail", 40.0f }, { "mix", 80.0f }, { "autolevel", 51.0f }, { "safety", 77.0f }, { "output", -2.6f } }),
            make("Extreme Engine Fail", {{ "stress", 90.0f }, { "baseBlock", 0.0f }, { "blockJitter", 95.0f }, { "seam", 88.0f }, { "tailDrop", 72.0f }, { "reorder", 86.0f }, { "lookFail", 78.0f }, { "mix", 100.0f }, { "autolevel", 45.0f }, { "safety", 84.0f }, { "output", -4.0f } }),
            make("Extreme Buffer Crash", {{ "stress", 96.0f }, { "baseBlock", 0.0f }, { "blockJitter", 100.0f }, { "seam", 94.0f }, { "tailDrop", 84.0f }, { "reorder", 94.0f }, { "lookFail", 92.0f }, { "mix", 100.0f }, { "autolevel", 42.0f }, { "safety", 87.0f }, { "output", -5.0f } }),
            make("Rhythmic Chunks", {{ "stress", 70.0f }, { "baseBlock", 1.0f }, { "blockJitter", 72.0f }, { "seam", 50.0f }, { "tailDrop", 40.0f }, { "reorder", 62.0f }, { "lookFail", 50.0f }, { "mix", 84.0f }, { "autolevel", 49.0f }, { "safety", 79.0f }, { "output", -3.0f } }),
            make("Rhythmic Shard Gate", {{ "stress", 78.0f }, { "baseBlock", 0.0f }, { "blockJitter", 82.0f }, { "seam", 62.0f }, { "tailDrop", 52.0f }, { "reorder", 72.0f }, { "lookFail", 64.0f }, { "mix", 88.0f }, { "autolevel", 48.0f }, { "safety", 80.0f }, { "output", -3.4f } })
        };
    }

    if (kPluginIndex == 4)
    {
        return {
            make("Init", {{ "brutal", 42.0f }, { "gridMode", 1.0f }, { "stepDiv", 4.0f }, { "zipper", 42.0f }, { "levels", 12.0f }, { "phaseLock", 72.0f }, { "jitter", 6.0f }, { "mix", 100.0f }, { "autolevel", 54.0f }, { "safety", 58.0f }, { "output", 1.5f } }),
            make("Safe Mix", {{ "brutal", 24.0f }, { "gridMode", 1.0f }, { "stepDiv", 3.0f }, { "zipper", 14.0f }, { "levels", 24.0f }, { "phaseLock", 25.0f }, { "jitter", 4.0f }, { "mix", 34.0f }, { "autolevel", 58.0f }, { "safety", 64.0f }, { "output", -1.0f } }),
            make("Subtle Stepped", {{ "brutal", 32.0f }, { "gridMode", 1.0f }, { "stepDiv", 4.0f }, { "zipper", 20.0f }, { "levels", 20.0f }, { "phaseLock", 35.0f }, { "jitter", 8.0f }, { "mix", 52.0f }, { "autolevel", 56.0f }, { "safety", 63.0f }, { "output", -0.8f } }),
            make("Subtle Quant Grid", {{ "brutal", 38.0f }, { "gridMode", 0.0f }, { "stepDiv", 4.0f }, { "zipper", 28.0f }, { "levels", 16.0f }, { "phaseLock", 46.0f }, { "jitter", 7.0f }, { "mix", 56.0f }, { "autolevel", 55.0f }, { "safety", 64.0f }, { "output", -0.9f } }),
            make("Medium Brutal Seq", {{ "brutal", 58.0f }, { "gridMode", 2.0f }, { "stepDiv", 5.0f }, { "zipper", 48.0f }, { "levels", 10.0f }, { "phaseLock", 62.0f }, { "jitter", 10.0f }, { "mix", 74.0f }, { "autolevel", 53.0f }, { "safety", 66.0f }, { "output", -1.2f } }),
            make("Medium Stair Drive", {{ "brutal", 64.0f }, { "gridMode", 0.0f }, { "stepDiv", 6.0f }, { "zipper", 56.0f }, { "levels", 8.0f }, { "phaseLock", 70.0f }, { "jitter", 12.0f }, { "mix", 78.0f }, { "autolevel", 52.0f }, { "safety", 67.0f }, { "output", -1.3f } }),
            make("Extreme Zipper", {{ "brutal", 92.0f }, { "gridMode", 0.0f }, { "stepDiv", 7.0f }, { "zipper", 95.0f }, { "levels", 4.0f }, { "phaseLock", 90.0f }, { "jitter", 18.0f }, { "mix", 100.0f }, { "autolevel", 48.0f }, { "safety", 73.0f }, { "output", -2.0f } }),
            make("Extreme Clocked Bits", {{ "brutal", 96.0f }, { "gridMode", 2.0f }, { "stepDiv", 7.0f }, { "zipper", 88.0f }, { "levels", 3.0f }, { "phaseLock", 100.0f }, { "jitter", 24.0f }, { "mix", 100.0f }, { "autolevel", 47.0f }, { "safety", 74.0f }, { "output", -2.4f } }),
            make("Rhythmic Grid Chop", {{ "brutal", 72.0f }, { "gridMode", 2.0f }, { "stepDiv", 6.0f }, { "zipper", 60.0f }, { "levels", 6.0f }, { "phaseLock", 84.0f }, { "jitter", 14.0f }, { "mix", 84.0f }, { "autolevel", 50.0f }, { "safety", 69.0f }, { "output", -1.6f } }),
            make("Rhythmic Phase Snap", {{ "brutal", 78.0f }, { "gridMode", 2.0f }, { "stepDiv", 5.0f }, { "zipper", 68.0f }, { "levels", 5.0f }, { "phaseLock", 96.0f }, { "jitter", 10.0f }, { "mix", 86.0f }, { "autolevel", 49.0f }, { "safety", 70.0f }, { "output", -1.8f } })
        };
    }

    if (kPluginIndex == 5)
    {
        return {
            make("Init", {{ "artifact", 4.0f }, { "bitrate", 320.0f }, { "masking", 0.0f }, { "smear", 0.0f }, { "codecMode", 0.0f }, { "switchMs", 400.0f }, { "packetLoss", 0.0f }, { "burst", 0.0f }, { "preecho", 0.0f }, { "mix", 100.0f }, { "autolevel", 42.0f }, { "safety", 72.0f }, { "output", -8.4f } }),
            make("Safe Mix", {{ "artifact", 24.0f }, { "bitrate", 160.0f }, { "masking", 24.0f }, { "smear", 18.0f }, { "codecMode", 1.0f }, { "switchMs", 360.0f }, { "packetLoss", 8.0f }, { "burst", 16.0f }, { "preecho", 10.0f }, { "mix", 30.0f }, { "autolevel", 50.0f }, { "safety", 76.0f }, { "output", -3.0f } }),
            make("Subtle Stream Wear", {{ "artifact", 34.0f }, { "bitrate", 128.0f }, { "masking", 34.0f }, { "smear", 28.0f }, { "codecMode", 1.0f }, { "switchMs", 300.0f }, { "packetLoss", 12.0f }, { "burst", 24.0f }, { "preecho", 14.0f }, { "mix", 48.0f }, { "autolevel", 47.0f }, { "safety", 76.0f }, { "output", -2.8f } }),
            make("Subtle Codec Drift", {{ "artifact", 40.0f }, { "bitrate", 112.0f }, { "masking", 42.0f }, { "smear", 36.0f }, { "codecMode", 2.0f }, { "switchMs", 240.0f }, { "packetLoss", 14.0f }, { "burst", 28.0f }, { "preecho", 18.0f }, { "mix", 54.0f }, { "autolevel", 46.0f }, { "safety", 77.0f }, { "output", -3.0f } }),
            make("Medium Artifact Bed", {{ "artifact", 58.0f }, { "bitrate", 84.0f }, { "masking", 62.0f }, { "smear", 50.0f }, { "codecMode", 1.0f }, { "switchMs", 200.0f }, { "packetLoss", 22.0f }, { "burst", 40.0f }, { "preecho", 28.0f }, { "mix", 74.0f }, { "autolevel", 44.0f }, { "safety", 80.0f }, { "output", -3.4f } }),
            make("Medium GSM Dust", {{ "artifact", 64.0f }, { "bitrate", 64.0f }, { "masking", 70.0f }, { "smear", 58.0f }, { "codecMode", 0.0f }, { "switchMs", 180.0f }, { "packetLoss", 28.0f }, { "burst", 52.0f }, { "preecho", 34.0f }, { "mix", 78.0f }, { "autolevel", 43.0f }, { "safety", 81.0f }, { "output", -3.8f } }),
            make("Extreme Packet Storm", {{ "artifact", 92.0f }, { "bitrate", 20.0f }, { "masking", 95.0f }, { "smear", 84.0f }, { "codecMode", 2.0f }, { "switchMs", 90.0f }, { "packetLoss", 72.0f }, { "burst", 90.0f }, { "preecho", 68.0f }, { "mix", 100.0f }, { "autolevel", 38.0f }, { "safety", 88.0f }, { "output", -5.2f } }),
            make("Extreme Modem Hell", {{ "artifact", 98.0f }, { "bitrate", 8.0f }, { "masking", 100.0f }, { "smear", 96.0f }, { "codecMode", 2.0f }, { "switchMs", 60.0f }, { "packetLoss", 86.0f }, { "burst", 100.0f }, { "preecho", 84.0f }, { "mix", 100.0f }, { "autolevel", 34.0f }, { "safety", 90.0f }, { "output", -6.5f } }),
            make("Rhythmic Drop Frames", {{ "artifact", 74.0f }, { "bitrate", 42.0f }, { "masking", 78.0f }, { "smear", 62.0f }, { "codecMode", 1.0f }, { "switchMs", 180.0f }, { "packetLoss", 44.0f }, { "burst", 70.0f }, { "preecho", 36.0f }, { "mix", 84.0f }, { "autolevel", 41.0f }, { "safety", 84.0f }, { "output", -4.4f } }),
            make("Rhythmic Switch Jam", {{ "artifact", 80.0f }, { "bitrate", 36.0f }, { "masking", 84.0f }, { "smear", 70.0f }, { "codecMode", 2.0f }, { "switchMs", 120.0f }, { "packetLoss", 50.0f }, { "burst", 76.0f }, { "preecho", 44.0f }, { "mix", 88.0f }, { "autolevel", 40.0f }, { "safety", 85.0f }, { "output", -4.9f } })
        };
    }

    if (kPluginIndex == 6)
    {
        return {
            make("Init", {{ "brutalism", 4.0f }, { "binDensity", 0.0f }, { "cluster", 0.0f }, { "freezeRate", 0.0f }, { "freezeLen", 60.0f }, { "phaseScramble", 0.0f }, { "phaseSteps", 64.0f }, { "sortAmount", 0.0f }, { "jitter", 0.0f }, { "mix", 100.0f }, { "autolevel", 36.0f }, { "safety", 76.0f }, { "output", -10.0f } }),
            make("Safe Mix", {{ "brutalism", 24.0f }, { "binDensity", 24.0f }, { "cluster", 1.0f }, { "freezeRate", 10.0f }, { "freezeLen", 120.0f }, { "phaseScramble", 14.0f }, { "phaseSteps", 24.0f }, { "sortAmount", 16.0f }, { "jitter", 8.0f }, { "mix", 28.0f }, { "autolevel", 43.0f }, { "safety", 80.0f }, { "output", -3.2f } }),
            make("Subtle Spectral Tilt", {{ "brutalism", 36.0f }, { "binDensity", 32.0f }, { "cluster", 2.0f }, { "freezeRate", 16.0f }, { "freezeLen", 180.0f }, { "phaseScramble", 20.0f }, { "phaseSteps", 20.0f }, { "sortAmount", 24.0f }, { "jitter", 12.0f }, { "mix", 50.0f }, { "autolevel", 41.0f }, { "safety", 80.0f }, { "output", -3.0f } }),
            make("Subtle Frozen Glass", {{ "brutalism", 42.0f }, { "binDensity", 38.0f }, { "cluster", 2.0f }, { "freezeRate", 28.0f }, { "freezeLen", 260.0f }, { "phaseScramble", 28.0f }, { "phaseSteps", 16.0f }, { "sortAmount", 30.0f }, { "jitter", 16.0f }, { "mix", 56.0f }, { "autolevel", 40.0f }, { "safety", 81.0f }, { "output", -3.2f } }),
            make("Medium Bin Vandal", {{ "brutalism", 62.0f }, { "binDensity", 58.0f }, { "cluster", 3.0f }, { "freezeRate", 36.0f }, { "freezeLen", 320.0f }, { "phaseScramble", 52.0f }, { "phaseSteps", 12.0f }, { "sortAmount", 48.0f }, { "jitter", 24.0f }, { "mix", 76.0f }, { "autolevel", 38.0f }, { "safety", 83.0f }, { "output", -3.8f } }),
            make("Medium Phase Teeth", {{ "brutalism", 68.0f }, { "binDensity", 64.0f }, { "cluster", 4.0f }, { "freezeRate", 42.0f }, { "freezeLen", 380.0f }, { "phaseScramble", 66.0f }, { "phaseSteps", 8.0f }, { "sortAmount", 60.0f }, { "jitter", 30.0f }, { "mix", 80.0f }, { "autolevel", 37.0f }, { "safety", 84.0f }, { "output", -4.2f } }),
            make("Extreme FFT Wreck", {{ "brutalism", 92.0f }, { "binDensity", 92.0f }, { "cluster", 5.0f }, { "freezeRate", 78.0f }, { "freezeLen", 700.0f }, { "phaseScramble", 94.0f }, { "phaseSteps", 4.0f }, { "sortAmount", 92.0f }, { "jitter", 62.0f }, { "mix", 100.0f }, { "autolevel", 32.0f }, { "safety", 90.0f }, { "output", -6.0f } }),
            make("Extreme Frozen Wall", {{ "brutalism", 98.0f }, { "binDensity", 100.0f }, { "cluster", 5.0f }, { "freezeRate", 96.0f }, { "freezeLen", 1100.0f }, { "phaseScramble", 100.0f }, { "phaseSteps", 2.0f }, { "sortAmount", 100.0f }, { "jitter", 78.0f }, { "mix", 100.0f }, { "autolevel", 30.0f }, { "safety", 92.0f }, { "output", -7.0f } }),
            make("Rhythmic Spectral Gate", {{ "brutalism", 74.0f }, { "binDensity", 70.0f }, { "cluster", 3.0f }, { "freezeRate", 54.0f }, { "freezeLen", 260.0f }, { "phaseScramble", 72.0f }, { "phaseSteps", 10.0f }, { "sortAmount", 66.0f }, { "jitter", 34.0f }, { "mix", 84.0f }, { "autolevel", 35.0f }, { "safety", 86.0f }, { "output", -4.6f } }),
            make("Rhythmic Bin Shuffle", {{ "brutalism", 80.0f }, { "binDensity", 78.0f }, { "cluster", 4.0f }, { "freezeRate", 62.0f }, { "freezeLen", 320.0f }, { "phaseScramble", 80.0f }, { "phaseSteps", 6.0f }, { "sortAmount", 74.0f }, { "jitter", 42.0f }, { "mix", 88.0f }, { "autolevel", 34.0f }, { "safety", 87.0f }, { "output", -5.0f } })
        };
    }

    if (kPluginIndex == 7)
    {
        return {
            make("Init", {{ "overclock", 4.0f }, { "sensitivity", 10.0f }, { "failureRate", 0.0f }, { "latencySpike", 0.0f }, { "desync", 0.0f }, { "thermal", 0.0f }, { "recovery", 80.0f }, { "mix", 100.0f }, { "autolevel", 50.0f }, { "safety", 74.0f }, { "output", -10.0f } }),
            make("Safe Mix", {{ "overclock", 26.0f }, { "sensitivity", 34.0f }, { "failureRate", 14.0f }, { "latencySpike", 12.0f }, { "desync", 10.0f }, { "thermal", 16.0f }, { "recovery", 72.0f }, { "mix", 34.0f }, { "autolevel", 56.0f }, { "safety", 77.0f }, { "output", -2.3f } }),
            make("Subtle Drift CPU", {{ "overclock", 36.0f }, { "sensitivity", 44.0f }, { "failureRate", 22.0f }, { "latencySpike", 18.0f }, { "desync", 18.0f }, { "thermal", 24.0f }, { "recovery", 62.0f }, { "mix", 50.0f }, { "autolevel", 54.0f }, { "safety", 77.0f }, { "output", -2.1f } }),
            make("Subtle Thread Pull", {{ "overclock", 42.0f }, { "sensitivity", 50.0f }, { "failureRate", 26.0f }, { "latencySpike", 24.0f }, { "desync", 30.0f }, { "thermal", 30.0f }, { "recovery", 58.0f }, { "mix", 56.0f }, { "autolevel", 53.0f }, { "safety", 78.0f }, { "output", -2.3f } }),
            make("Medium Unstable Core", {{ "overclock", 60.0f }, { "sensitivity", 66.0f }, { "failureRate", 42.0f }, { "latencySpike", 40.0f }, { "desync", 42.0f }, { "thermal", 46.0f }, { "recovery", 46.0f }, { "mix", 76.0f }, { "autolevel", 50.0f }, { "safety", 80.0f }, { "output", -2.8f } }),
            make("Medium Heat Bloom", {{ "overclock", 68.0f }, { "sensitivity", 72.0f }, { "failureRate", 48.0f }, { "latencySpike", 54.0f }, { "desync", 52.0f }, { "thermal", 62.0f }, { "recovery", 40.0f }, { "mix", 80.0f }, { "autolevel", 49.0f }, { "safety", 81.0f }, { "output", -3.1f } }),
            make("Extreme Overheat", {{ "overclock", 94.0f }, { "sensitivity", 92.0f }, { "failureRate", 82.0f }, { "latencySpike", 78.0f }, { "desync", 82.0f }, { "thermal", 90.0f }, { "recovery", 20.0f }, { "mix", 100.0f }, { "autolevel", 42.0f }, { "safety", 88.0f }, { "output", -4.8f } }),
            make("Extreme Clock Loss", {{ "overclock", 100.0f }, { "sensitivity", 100.0f }, { "failureRate", 94.0f }, { "latencySpike", 92.0f }, { "desync", 96.0f }, { "thermal", 100.0f }, { "recovery", 10.0f }, { "mix", 100.0f }, { "autolevel", 38.0f }, { "safety", 90.0f }, { "output", -6.2f } }),
            make("Rhythmic Stall", {{ "overclock", 74.0f }, { "sensitivity", 78.0f }, { "failureRate", 58.0f }, { "latencySpike", 64.0f }, { "desync", 54.0f }, { "thermal", 56.0f }, { "recovery", 34.0f }, { "mix", 84.0f }, { "autolevel", 47.0f }, { "safety", 83.0f }, { "output", -3.6f } }),
            make("Rhythmic Desync Pulse", {{ "overclock", 80.0f }, { "sensitivity", 84.0f }, { "failureRate", 64.0f }, { "latencySpike", 70.0f }, { "desync", 70.0f }, { "thermal", 64.0f }, { "recovery", 30.0f }, { "mix", 88.0f }, { "autolevel", 46.0f }, { "safety", 84.0f }, { "output", -3.9f } })
        };
    }

    if (kPluginIndex == 8)
    {
        return {
            make("Init", {{ "determinism", 4.0f }, { "stateCount", 2.0f }, { "stateDwell", 1200.0f }, { "loopMs", 5.0f }, { "hashWindow", 1024.0f }, { "jumpRule", 0.0f }, { "memory", 0.0f }, { "mix", 100.0f }, { "autolevel", 48.0f }, { "safety", 63.0f }, { "output", -16.1f } }),
            make("Safe Mix", {{ "determinism", 24.0f }, { "stateCount", 8.0f }, { "stateDwell", 380.0f }, { "loopMs", 16.0f }, { "hashWindow", 240.0f }, { "jumpRule", 1.0f }, { "memory", 24.0f }, { "mix", 36.0f }, { "autolevel", 54.0f }, { "safety", 68.0f }, { "output", -1.6f } }),
            make("Subtle Robot Grain", {{ "determinism", 34.0f }, { "stateCount", 14.0f }, { "stateDwell", 300.0f }, { "loopMs", 20.0f }, { "hashWindow", 200.0f }, { "jumpRule", 1.0f }, { "memory", 34.0f }, { "mix", 52.0f }, { "autolevel", 52.0f }, { "safety", 67.0f }, { "output", -1.4f } }),
            make("Subtle Loop Grid", {{ "determinism", 40.0f }, { "stateCount", 18.0f }, { "stateDwell", 240.0f }, { "loopMs", 24.0f }, { "hashWindow", 160.0f }, { "jumpRule", 0.0f }, { "memory", 44.0f }, { "mix", 58.0f }, { "autolevel", 51.0f }, { "safety", 68.0f }, { "output", -1.5f } }),
            make("Medium Finite Groove", {{ "determinism", 60.0f }, { "stateCount", 24.0f }, { "stateDwell", 180.0f }, { "loopMs", 30.0f }, { "hashWindow", 120.0f }, { "jumpRule", 1.0f }, { "memory", 48.0f }, { "mix", 76.0f }, { "autolevel", 49.0f }, { "safety", 70.0f }, { "output", -1.9f } }),
            make("Medium Hash Runner", {{ "determinism", 66.0f }, { "stateCount", 36.0f }, { "stateDwell", 120.0f }, { "loopMs", 34.0f }, { "hashWindow", 96.0f }, { "jumpRule", 1.0f }, { "memory", 54.0f }, { "mix", 80.0f }, { "autolevel", 48.0f }, { "safety", 71.0f }, { "output", -2.1f } }),
            make("Extreme Determinator", {{ "determinism", 92.0f }, { "stateCount", 96.0f }, { "stateDwell", 50.0f }, { "loopMs", 50.0f }, { "hashWindow", 40.0f }, { "jumpRule", 1.0f }, { "memory", 74.0f }, { "mix", 100.0f }, { "autolevel", 42.0f }, { "safety", 77.0f }, { "output", -3.6f } }),
            make("Extreme State Prison", {{ "determinism", 98.0f }, { "stateCount", 128.0f }, { "stateDwell", 20.0f }, { "loopMs", 60.0f }, { "hashWindow", 8.0f }, { "jumpRule", 2.0f }, { "memory", 88.0f }, { "mix", 100.0f }, { "autolevel", 39.0f }, { "safety", 80.0f }, { "output", -4.8f } }),
            make("Rhythmic Loop Grid", {{ "determinism", 74.0f }, { "stateCount", 40.0f }, { "stateDwell", 90.0f }, { "loopMs", 36.0f }, { "hashWindow", 72.0f }, { "jumpRule", 0.0f }, { "memory", 58.0f }, { "mix", 84.0f }, { "autolevel", 46.0f }, { "safety", 73.0f }, { "output", -2.5f } }),
            make("Rhythmic Hash Pulse", {{ "determinism", 80.0f }, { "stateCount", 52.0f }, { "stateDwell", 70.0f }, { "loopMs", 40.0f }, { "hashWindow", 56.0f }, { "jumpRule", 1.0f }, { "memory", 64.0f }, { "mix", 88.0f }, { "autolevel", 45.0f }, { "safety", 74.0f }, { "output", -2.8f } })
        };
    }

    if (kPluginIndex == 9)
    {
        return {
            make("Init", {{ "amount", 28.0f }, { "rateHz", 4.5f }, { "sliceMs", 36.0f }, { "repeats", 3.0f }, { "reverse", 6.0f }, { "timingJitter", 4.0f }, { "duck", 22.0f }, { "mix", 100.0f }, { "autolevel", 50.0f }, { "safety", 66.0f }, { "output", -10.0f } }),
            make("Safe Mix", {{ "amount", 36.0f }, { "rateHz", 5.2f }, { "sliceMs", 42.0f }, { "repeats", 4.0f }, { "reverse", 10.0f }, { "timingJitter", 8.0f }, { "duck", 28.0f }, { "mix", 32.0f }, { "autolevel", 54.0f }, { "safety", 71.0f }, { "output", -1.6f } }),
            make("Subtle Tape Twitch", {{ "amount", 40.0f }, { "rateHz", 4.0f }, { "sliceMs", 48.0f }, { "repeats", 3.0f }, { "reverse", 12.0f }, { "timingJitter", 10.0f }, { "duck", 24.0f }, { "mix", 48.0f }, { "autolevel", 53.0f }, { "safety", 70.0f }, { "output", -1.5f } }),
            make("Subtle Chop Drift", {{ "amount", 44.0f }, { "rateHz", 6.2f }, { "sliceMs", 30.0f }, { "repeats", 4.0f }, { "reverse", 18.0f }, { "timingJitter", 14.0f }, { "duck", 30.0f }, { "mix", 52.0f }, { "autolevel", 52.0f }, { "safety", 70.0f }, { "output", -1.6f } }),
            make("Medium Gate Repeat", {{ "amount", 62.0f }, { "rateHz", 8.0f }, { "sliceMs", 24.0f }, { "repeats", 6.0f }, { "reverse", 20.0f }, { "timingJitter", 16.0f }, { "duck", 42.0f }, { "mix", 74.0f }, { "autolevel", 50.0f }, { "safety", 73.0f }, { "output", -2.2f } }),
            make("Medium Vinyl Skip", {{ "amount", 68.0f }, { "rateHz", 10.0f }, { "sliceMs", 18.0f }, { "repeats", 7.0f }, { "reverse", 30.0f }, { "timingJitter", 24.0f }, { "duck", 48.0f }, { "mix", 78.0f }, { "autolevel", 49.0f }, { "safety", 74.0f }, { "output", -2.4f } }),
            make("Extreme Machine Gun", {{ "amount", 92.0f }, { "rateHz", 16.0f }, { "sliceMs", 12.0f }, { "repeats", 12.0f }, { "reverse", 24.0f }, { "timingJitter", 20.0f }, { "duck", 64.0f }, { "mix", 100.0f }, { "autolevel", 45.0f }, { "safety", 80.0f }, { "output", -3.2f } }),
            make("Extreme Reverse Shred", {{ "amount", 96.0f }, { "rateHz", 14.0f }, { "sliceMs", 14.0f }, { "repeats", 14.0f }, { "reverse", 86.0f }, { "timingJitter", 28.0f }, { "duck", 72.0f }, { "mix", 100.0f }, { "autolevel", 43.0f }, { "safety", 82.0f }, { "output", -3.8f } }),
            make("Rhythmic 16th Chop", {{ "amount", 78.0f }, { "rateHz", 8.0f }, { "sliceMs", 22.0f }, { "repeats", 8.0f }, { "reverse", 14.0f }, { "timingJitter", 8.0f }, { "duck", 52.0f }, { "mix", 84.0f }, { "autolevel", 48.0f }, { "safety", 76.0f }, { "output", -2.6f } }),
            make("Rhythmic Triplet Jam", {{ "amount", 82.0f }, { "rateHz", 6.0f }, { "sliceMs", 28.0f }, { "repeats", 9.0f }, { "reverse", 22.0f }, { "timingJitter", 12.0f }, { "duck", 56.0f }, { "mix", 86.0f }, { "autolevel", 47.0f }, { "safety", 77.0f }, { "output", -2.8f } })
        };
    }

    if (kPluginIndex == 10)
    {
        return {
            make("Init", {{ "skip", 80.0f }, { "jumpRate", 3.8f }, { "segMs", 220.0f }, { "melody", 60.0f }, { "spread", 72.0f }, { "reverse", 34.0f }, { "flutter", 46.0f }, { "blur", 14.0f }, { "mix", 100.0f }, { "autolevel", 47.0f }, { "safety", 68.0f }, { "output", -10.7f } }),
            make("Safe Mix", {{ "skip", 42.0f }, { "jumpRate", 4.8f }, { "segMs", 46.0f }, { "melody", 46.0f }, { "spread", 44.0f }, { "reverse", 14.0f }, { "flutter", 12.0f }, { "blur", 24.0f }, { "mix", 30.0f }, { "autolevel", 52.0f }, { "safety", 71.0f }, { "output", -1.8f } }),
            make("Subtle Disk Fray", {{ "skip", 48.0f }, { "jumpRate", 5.6f }, { "segMs", 34.0f }, { "melody", 52.0f }, { "spread", 42.0f }, { "reverse", 16.0f }, { "flutter", 16.0f }, { "blur", 26.0f }, { "mix", 46.0f }, { "autolevel", 50.0f }, { "safety", 72.0f }, { "output", -1.7f } }),
            make("Subtle Pitch Skips", {{ "skip", 54.0f }, { "jumpRate", 6.2f }, { "segMs", 30.0f }, { "melody", 62.0f }, { "spread", 58.0f }, { "reverse", 18.0f }, { "flutter", 20.0f }, { "blur", 28.0f }, { "mix", 52.0f }, { "autolevel", 49.0f }, { "safety", 72.0f }, { "output", -1.9f } }),
            make("Medium Oval Cutups", {{ "skip", 68.0f }, { "jumpRate", 7.8f }, { "segMs", 24.0f }, { "melody", 72.0f }, { "spread", 68.0f }, { "reverse", 22.0f }, { "flutter", 24.0f }, { "blur", 34.0f }, { "mix", 74.0f }, { "autolevel", 47.0f }, { "safety", 74.0f }, { "output", -2.4f } }),
            make("Medium Overcomes Drift", {{ "skip", 74.0f }, { "jumpRate", 8.5f }, { "segMs", 22.0f }, { "melody", 80.0f }, { "spread", 76.0f }, { "reverse", 26.0f }, { "flutter", 30.0f }, { "blur", 36.0f }, { "mix", 78.0f }, { "autolevel", 46.0f }, { "safety", 75.0f }, { "output", -2.6f } }),
            make("Extreme CD Collapse", {{ "skip", 94.0f }, { "jumpRate", 12.0f }, { "segMs", 16.0f }, { "melody", 92.0f }, { "spread", 88.0f }, { "reverse", 34.0f }, { "flutter", 42.0f }, { "blur", 44.0f }, { "mix", 100.0f }, { "autolevel", 42.0f }, { "safety", 80.0f }, { "output", -3.4f } }),
            make("Extreme Melodic Shatter", {{ "skip", 98.0f }, { "jumpRate", 14.5f }, { "segMs", 12.0f }, { "melody", 100.0f }, { "spread", 96.0f }, { "reverse", 52.0f }, { "flutter", 52.0f }, { "blur", 48.0f }, { "mix", 100.0f }, { "autolevel", 40.0f }, { "safety", 83.0f }, { "output", -3.9f } }),
            make("Rhythmic Quarter Skip", {{ "skip", 76.0f }, { "jumpRate", 4.0f }, { "segMs", 36.0f }, { "melody", 70.0f }, { "spread", 64.0f }, { "reverse", 16.0f }, { "flutter", 20.0f }, { "blur", 30.0f }, { "mix", 82.0f }, { "autolevel", 45.0f }, { "safety", 77.0f }, { "output", -2.8f } }),
            make("Rhythmic Trip Skipline", {{ "skip", 82.0f }, { "jumpRate", 6.0f }, { "segMs", 26.0f }, { "melody", 84.0f }, { "spread", 78.0f }, { "reverse", 24.0f }, { "flutter", 28.0f }, { "blur", 34.0f }, { "mix", 86.0f }, { "autolevel", 44.0f }, { "safety", 78.0f }, { "output", -3.0f } })
        };
    }

    return { make("Init", {{ "digital", 0.0f }, { "mix", 100.0f }, { "autolevel", defaultAutoLevelPercent() }, { "safety", defaultSafetyPercent() }, { "output", defaultOutputTrimDb() } }) };
}

void DigitalisAudioProcessor::applyFactoryPreset(size_t index)
{
    if (factoryPresets.empty())
        return;

    const auto clamped = juce::jmin(index, factoryPresets.size() - 1);
    const auto& preset = factoryPresets[clamped];

    for (const auto& pv : preset.values)
    {
        if (auto* p = parameters.getParameter(pv.id))
            p->setValueNotifyingHost(p->convertTo0to1(pv.value));
    }

    currentProgramIndex = static_cast<int>(clamped);
}

void DigitalisAudioProcessor::applyPostSafety(juce::AudioBuffer<float>& buffer)
{
    const auto autoLevel = parameters.getRawParameterValue("autolevel")->load() * 0.01f;
    const auto safety = parameters.getRawParameterValue("safety")->load() * 0.01f;
    const auto outputTrimDb = parameters.getRawParameterValue("output")->load();
    const auto outputGain = juce::Decibels::decibelsToGain(outputTrimDb);

    const auto channels = getTotalNumInputChannels();
    const auto numSamples = buffer.getNumSamples();

    auto sumSq = 0.0f;
    auto count = 0;
    for (int ch = 0; ch < channels; ++ch)
    {
        const auto* read = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
        {
            sumSq += read[i] * read[i];
            ++count;
        }
    }

    const auto rms = std::sqrt(sumSq / static_cast<float>(juce::jmax(1, count)));
    const auto targetRms = targetRmsForPlugin();
    const auto compensation = juce::jlimit(0.25f, 4.0f, targetRms / (rms + 1.0e-6f));
    postAutoLevelGain += (compensation - postAutoLevelGain) * 0.02f;
    const auto gain = outputGain * juce::jmap(autoLevel, 1.0f, postAutoLevelGain);

    const auto dcR = 0.995f;
    const auto drive = juce::jmap(safety, 1.0f, 4.2f);
    const auto softNorm = 1.0f / std::tanh(drive);
    const auto hardLimit = juce::jmap(safety, 0.995f, 0.8f);

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto c = static_cast<size_t>(ch);
        auto* write = buffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            auto x = write[i];
            const auto dc = x - postDcPrevInput[c] + dcR * postDcPrevOutput[c];
            postDcPrevInput[c] = x;
            postDcPrevOutput[c] = dc;

            auto y = dc * gain;
            y = std::tanh(y * drive) * softNorm;
            y = juce::jlimit(-hardLimit, hardLimit, y);
            write[i] = y;
        }
    }
}

void DigitalisAudioProcessor::processFloatingPointCollapse(juce::AudioBuffer<float>& buffer)
{
    const auto collapse = parameters.getRawParameterValue("collapse")->load() * 0.01f;
    const auto mantissaBits = static_cast<int>(std::round(parameters.getRawParameterValue("mantissaBits")->load()));
    const auto exponentStep = static_cast<int>(std::round(parameters.getRawParameterValue("exponentStep")->load()));
    const auto temporalChoice = static_cast<int>(parameters.getRawParameterValue("temporalHold")->load());
    const auto blockChoice = static_cast<int>(parameters.getRawParameterValue("blockSize")->load());
    const auto quantCurve = static_cast<int>(parameters.getRawParameterValue("quantCurve")->load());
    const auto roundingAmount = parameters.getRawParameterValue("rounding")->load() * 0.01f;
    const auto denormalAmount = parameters.getRawParameterValue("denormal")->load() * 0.01f;

    constexpr std::array<int, 8> holdSteps { 1, 2, 4, 8, 16, 32, 64, 128 };
    constexpr std::array<int, 8> blockSizes { 8, 16, 32, 64, 128, 256, 512, 1024 };
    const auto temporalHoldSamples = holdSteps[(size_t) juce::jlimit(0, 7, temporalChoice)];
    const auto blockSize = blockSizes[(size_t) juce::jlimit(0, 7, blockChoice)];

    const auto driveGain = juce::Decibels::decibelsToGain(juce::jmap(collapse, 0.0f, 1.0f, 0.0f, 18.0f));
    const auto blockMantissaSteps = std::pow(2.0f, juce::jmap(collapse, 0.0f, 1.0f, 5.0f, 12.0f));

    const auto numInputChannels = getTotalNumInputChannels();
    const auto numSamples = buffer.getNumSamples();

    for (int start = 0; start < numSamples; start += blockSize)
    {
        const auto chunkSize = juce::jmin(blockSize, numSamples - start);

        auto peak = 0.0f;
        for (int ch = 0; ch < numInputChannels; ++ch)
        {
            const auto* read = buffer.getReadPointer(ch, start);
            for (int i = 0; i < chunkSize; ++i)
                peak = juce::jmax(peak, std::abs(read[i] * driveGain));
        }

        int sharedExponent = 0;
        std::frexp(peak + 1.0e-20f, &sharedExponent);

        for (int ch = 0; ch < numInputChannels; ++ch)
        {
            auto* write = buffer.getWritePointer(ch, start);
            for (int i = 0; i < chunkSize; ++i)
            {
                auto x = write[i] * driveGain;

                if (--fpcTemporalCountdown[(size_t) ch] <= 0)
                {
                    fpcTemporalHeld[(size_t) ch] = x;
                    fpcTemporalCountdown[(size_t) ch] = temporalHoldSamples;
                }
                x = fpcTemporalHeld[(size_t) ch];

                x = std::ldexp(quantise(std::ldexp(x, -sharedExponent), blockMantissaSteps), sharedExponent);
                x = applyFloatDamage(x, mantissaBits, exponentStep, roundingAmount + collapse * 0.35f);
                x = applyNonlinearQuantiser(x, quantCurve, collapse);

                if (denormalBurstRemaining > 0)
                {
                    x += (random.nextFloat() * 2.0f - 1.0f) * (1.0e-4f * denormalAmount);
                    --denormalBurstRemaining;
                }
                else if (std::abs(x) < juce::jmap(denormalAmount, 0.0f, 1.0f, 1.0e-12f, 1.0e-5f)
                         && random.nextFloat() < denormalAmount * 0.015f)
                {
                    denormalBurstRemaining = 8 + random.nextInt(64);
                }

                write[i] = juce::jlimit(-1.0f, 1.0f, x);
            }
        }
    }
}

float DigitalisAudioProcessor::applyFloatDamage(float x, int mantissaBits, int exponentStep, float roundingAmount)
{
    if (std::abs(x) < 1.0e-35f)
        return 0.0f;

    const auto rounding = juce::jlimit(0.0f, 1.0f, roundingAmount);
    if (rounding > 0.0f && random.nextFloat() < rounding)
    {
        const auto direction = random.nextBool() ? std::numeric_limits<float>::infinity()
                                                 : -std::numeric_limits<float>::infinity();
        x = std::nextafterf(x, direction);
        if (random.nextFloat() < rounding * 0.5f)
            x = std::nextafterf(x, direction);
    }

    x = truncateMantissa(x, mantissaBits);
    x = quantiseExponent(x, exponentStep);
    return x;
}

float DigitalisAudioProcessor::applyNonlinearQuantiser(float x, int mode, float amount)
{
    const auto sign = x < 0.0f ? -1.0f : 1.0f;
    auto mag = std::abs(x);
    const auto levels = juce::jmax(8.0f, 2048.0f - amount * 1850.0f);
    mag = juce::jlimit(0.0f, 1.0f, mag);

    switch (juce::jlimit(0, 3, mode))
    {
        case 1:
        {
            const auto curve = 1.0f + amount * 18.0f;
            const auto encoded = std::log1pf(curve * mag) / std::log1pf(curve);
            const auto crushed = quantise(encoded, levels);
            mag = std::expm1f(crushed * std::log1pf(curve)) / curve;
            break;
        }
        case 2:
        {
            constexpr float mu = 255.0f;
            const auto encoded = std::log1pf(mu * mag) / std::log1pf(mu);
            const auto crushed = quantise(encoded, levels);
            mag = std::expm1f(crushed * std::log1pf(mu)) / mu;
            break;
        }
        case 3:
        {
            fpcChaoticState = juce::jlimit(0.0001f, 0.9999f, 3.99f * fpcChaoticState * (1.0f - fpcChaoticState));
            const auto steps = static_cast<int>(levels);
            auto idx = static_cast<int>(std::round(mag * static_cast<float>(steps - 1)));
            idx = (idx + static_cast<int>(fpcChaoticState * static_cast<float>(steps - 1))) % steps;
            mag = static_cast<float>(idx) / static_cast<float>(steps - 1);
            break;
        }
        default:
            mag = quantise(mag, levels);
            break;
    }

    return juce::jlimit(-1.0f, 1.0f, sign * mag);
}

void DigitalisAudioProcessor::processNyquistDestroyer(juce::AudioBuffer<float>& buffer)
{
    const auto destroy = parameters.getRawParameterValue("destroy")->load() * 0.01f;
    auto minSR = parameters.getRawParameterValue("minSR")->load();
    auto maxSR = parameters.getRawParameterValue("maxSR")->load();
    if (minSR > maxSR)
        std::swap(minSR, maxSR);

    const auto modRate = parameters.getRawParameterValue("modRate")->load();
    const auto modDepth = parameters.getRawParameterValue("modDepth")->load() * 0.01f;
    const auto interpErr = parameters.getRawParameterValue("interpErr")->load() * 0.01f;
    const auto transient = parameters.getRawParameterValue("transient")->load() * 0.01f;
    const auto feedback = parameters.getRawParameterValue("feedback")->load() * 0.01f;
    const auto feedbackTone = parameters.getRawParameterValue("fbTone")->load() * 0.01f;

    const auto phaseInc = juce::MathConstants<float>::twoPi * modRate / static_cast<float>(currentSampleRate);
    const auto inputChannels = getTotalNumInputChannels();
    const auto samples = buffer.getNumSamples();
    const auto minHold = 1;
    const auto maxHold = juce::jmax(2, static_cast<int>(currentSampleRate / 600.0));

    for (int i = 0; i < samples; ++i)
    {
        auto energy = 0.0f;
        for (int ch = 0; ch < inputChannels; ++ch)
            energy += std::abs(buffer.getSample(ch, i));
        energy /= static_cast<float>(juce::jmax(1, inputChannels));

        const auto attack = 0.65f;
        const auto release = 0.9965f;
        nyqTransientEnv = energy > nyqTransientEnv ? (attack * energy + (1.0f - attack) * nyqTransientEnv)
                                                   : (release * nyqTransientEnv + (1.0f - release) * energy);

        const auto lfo = 0.5f + 0.5f * std::sin(nyqPhase);
        nyqPhase += phaseInc;
        if (nyqPhase > juce::MathConstants<float>::twoPi)
            nyqPhase -= juce::MathConstants<float>::twoPi;

        const auto sweep = juce::jmap(modDepth * lfo, maxSR, minSR);
        const auto transientDrop = juce::jmap(transient * nyqTransientEnv, 1.0f, 0.08f);
        const auto effectiveSR = juce::jlimit(750.0f, maxSR, sweep * transientDrop);
        const auto holdSamples = juce::jlimit(minHold, maxHold, static_cast<int>(std::round(currentSampleRate / effectiveSR)));

        for (int ch = 0; ch < inputChannels; ++ch)
        {
            const auto c = static_cast<size_t>(ch);
            auto in = buffer.getSample(ch, i);
            in += nyqFeedbackState[c] * feedback;

            if (--nyqHoldCounter[c] <= 0)
            {
                nyqHeldPrevious[c] = nyqHeldCurrent[c];
                nyqHeldCurrent[c] = in;
                nyqHoldCounter[c] = holdSamples;
            }

            const auto held = nyqHeldCurrent[c];
            const auto frac = 1.0f - (static_cast<float>(nyqHoldCounter[c]) / static_cast<float>(juce::jmax(1, holdSamples)));
            const auto warpedFrac = juce::jlimit(0.0f, 1.0f, frac + (random.nextFloat() * 2.0f - 1.0f) * interpErr * 0.9f);
            const auto wrongLinear = juce::jmap(warpedFrac, nyqHeldPrevious[c], nyqHeldCurrent[c]);
            auto out = juce::jmap(interpErr, held, wrongLinear);

            out = std::tanh(out * juce::jmap(destroy, 1.0f, 2.6f));
            nyqFeedbackToneState[c] += (out - nyqFeedbackToneState[c]) * juce::jmap(feedbackTone, 0.015f, 0.65f);
            nyqFeedbackState[c] = std::tanh(nyqFeedbackToneState[c] * juce::jmap(destroy, 1.0f, 1.8f));

            buffer.setSample(ch, i, out);
        }
    }
}

void DigitalisAudioProcessor::processBufferGlitchEngine(juce::AudioBuffer<float>& buffer)
{
    const auto stress = parameters.getRawParameterValue("stress")->load() * 0.01f;
    const auto blockChoice = static_cast<int>(parameters.getRawParameterValue("baseBlock")->load());
    const auto blockJitter = parameters.getRawParameterValue("blockJitter")->load() * 0.01f;
    const auto seamAmount = parameters.getRawParameterValue("seam")->load() * 0.01f;
    const auto tailDrop = parameters.getRawParameterValue("tailDrop")->load() * 0.01f;
    const auto reorder = parameters.getRawParameterValue("reorder")->load() * 0.01f;
    const auto lookFail = parameters.getRawParameterValue("lookFail")->load() * 0.01f;

    constexpr std::array<int, 6> blockSizes { 16, 32, 64, 128, 256, 512 };
    const auto baseBlock = blockSizes[(size_t) juce::jlimit(0, 5, blockChoice)];
    const auto channels = getTotalNumInputChannels();
    const auto numSamples = buffer.getNumSamples();

    for (int start = 0; start < numSamples;)
    {
        auto block = baseBlock;
        if (blockJitter > 0.0f)
        {
            const auto jitterScale = juce::jmap(blockJitter, 0.0f, 1.0f, 0.0f, 0.9f);
            const auto mul = 1.0f + ((random.nextFloat() * 2.0f - 1.0f) * jitterScale);
            block = juce::jlimit(8, 1024, static_cast<int>(std::round(static_cast<float>(baseBlock) * mul)));
        }

        const auto chunkSize = juce::jmin(block, numSamples - start);
        auto reorderMode = 0;
        if (random.nextFloat() < reorder * 0.65f)
            reorderMode = random.nextInt(4); // 0 none, 1 swap, 2 reverse, 3 duplicate

        auto dropCount = static_cast<int>(std::round(static_cast<float>(chunkSize) * tailDrop * juce::jmap(stress, 0.15f, 0.95f)));
        dropCount = juce::jlimit(0, juce::jmax(0, chunkSize - 1), dropCount);

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto c = static_cast<size_t>(ch);
            auto* write = buffer.getWritePointer(ch, start);
            std::vector<float> chunk((size_t) chunkSize, 0.0f);
            for (int i = 0; i < chunkSize; ++i)
                chunk[(size_t) i] = write[i];

            if (reorderMode == 2)
                std::reverse(chunk.begin(), chunk.end());

            if (dropCount > 0)
            {
                for (int i = chunkSize - dropCount; i < chunkSize; ++i)
                    chunk[(size_t) i] = 0.0f;
            }

            if (bgeHasPrevChunk && seamAmount > 0.0f)
            {
                const auto seamSpan = juce::jmin(chunkSize, juce::jmax(1, static_cast<int>(std::round(1.0f + seamAmount * 10.0f))));
                for (int i = 0; i < seamSpan; ++i)
                {
                    const auto w = static_cast<float>(i) / static_cast<float>(juce::jmax(1, seamSpan - 1));
                    const auto prev = bgePrevChunk[c][(size_t) juce::jlimit(0, juce::jmax(0, bgePrevChunkSize - 1), bgePrevChunkSize - seamSpan + i)];
                    chunk[(size_t) i] = juce::jmap(w + seamAmount * 0.25f, prev, chunk[(size_t) i]);
                }
            }

            if (bgeHasPrevChunk && reorderMode == 1)
            {
                const auto minSize = juce::jmin(chunkSize, bgePrevChunkSize);
                for (int i = 0; i < minSize; ++i)
                    std::swap(chunk[(size_t) i], bgePrevChunk[c][(size_t) i]);
            }
            else if (bgeHasPrevChunk && reorderMode == 3)
            {
                const auto minSize = juce::jmin(chunkSize, bgePrevChunkSize);
                for (int i = 0; i < minSize; ++i)
                    chunk[(size_t) i] = bgePrevChunk[c][(size_t) i];
            }

            for (int i = 0; i < chunkSize; ++i)
                write[i] = chunk[(size_t) i];

            if ((int) bgePrevChunk[c].size() < chunkSize)
                bgePrevChunk[c].resize((size_t) chunkSize, 0.0f);
            for (int i = 0; i < chunkSize; ++i)
                bgePrevChunk[c][(size_t) i] = chunk[(size_t) i];
        }

        bgePrevChunkSize = chunkSize;
        bgeHasPrevChunk = true;
        start += chunkSize;
    }

    const auto threshold = juce::jmap(stress, 0.0f, 1.0f, 0.95f, 0.18f);
    const auto attack = juce::jmap(stress, 0.01f, 0.45f);
    const auto release = juce::jmap(stress, 0.9985f, 0.94f);

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto c = static_cast<size_t>(ch);
        auto* write = buffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto x = write[i];
            const auto mag = std::abs(x);
            bgeEnvelope[c] = mag > bgeEnvelope[c] ? (attack * mag + (1.0f - attack) * bgeEnvelope[c])
                                                  : (release * bgeEnvelope[c] + (1.0f - release) * mag);

            auto envForGain = bgeEnvelope[c];
            if (random.nextFloat() < lookFail * 0.08f)
            {
                if (random.nextBool())
                    envForGain *= 0.35f;
                else
                    envForGain *= 1.8f;
            }

            const auto gain = envForGain > threshold ? (threshold / (envForGain + 1.0e-6f)) : 1.0f;
            auto y = x * juce::jlimit(0.05f, 1.0f, gain);
            if (random.nextFloat() < lookFail * stress * 0.015f)
                y = 0.0f;
            write[i] = std::tanh(y * juce::jmap(stress, 1.0f, 1.6f));
        }
    }
}

void DigitalisAudioProcessor::processAutomationQuantiser(juce::AudioBuffer<float>& buffer)
{
    const auto brutal = parameters.getRawParameterValue("brutal")->load() * 0.01f;
    const auto gridMode = static_cast<int>(parameters.getRawParameterValue("gridMode")->load());
    const auto stepDivChoice = static_cast<int>(parameters.getRawParameterValue("stepDiv")->load());
    const auto zipper = parameters.getRawParameterValue("zipper")->load() * 0.01f;
    const auto levels = static_cast<int>(std::round(parameters.getRawParameterValue("levels")->load()));
    const auto phaseLock = parameters.getRawParameterValue("phaseLock")->load() * 0.01f;
    const auto jitter = parameters.getRawParameterValue("jitter")->load() * 0.01f;

    constexpr std::array<int, 8> stepDivs { 1, 2, 4, 8, 16, 32, 64, 128 };
    const auto stepDiv = stepDivs[(size_t) juce::jlimit(0, 7, stepDivChoice)];

    auto holdSamples = 1;
    if (gridMode == 0)
    {
        holdSamples = juce::jmax(1, maxBlockSize / juce::jmax(1, stepDiv / 2));
    }
    else if (gridMode == 1)
    {
        holdSamples = stepDiv;
    }
    else
    {
        // Beat-like lock fallback without host BPM: derive a stable musical clock from samplerate.
        const auto pseudoBeatHz = 2.0f; // 120 BPM quarter-note
        holdSamples = juce::jmax(1, static_cast<int>(std::round(currentSampleRate / (pseudoBeatHz * static_cast<float>(stepDiv)))));
    }

    const auto channels = getTotalNumInputChannels();
    const auto samples = buffer.getNumSamples();
    const auto quantLevels = juce::jmax(2, static_cast<int>(std::round(juce::jmap(brutal, static_cast<float>(levels), juce::jmax(2.0f, static_cast<float>(levels) * 0.2f)))));
    const auto lfoRate = juce::jmap(brutal, 1.0f, 42.0f);

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto c = static_cast<size_t>(ch);
        auto* write = buffer.getWritePointer(ch);
        auto prevAmp = aqHeldAmp[c];

        for (int i = 0; i < samples; ++i)
        {
            auto stepped = false;
            if (--aqHeldCounter[c] <= 0)
            {
                const auto phaseStep = juce::MathConstants<float>::twoPi * lfoRate / static_cast<float>(currentSampleRate);
                aqLfoPhase[c] += phaseStep;
                if (aqLfoPhase[c] > juce::MathConstants<float>::twoPi)
                    aqLfoPhase[c] -= juce::MathConstants<float>::twoPi;

                if (phaseLock > 0.0f)
                {
                    const auto lockStep = juce::MathConstants<float>::twoPi / static_cast<float>(juce::jmax(1, stepDiv));
                    const auto snapped = std::round(aqLfoPhase[c] / lockStep) * lockStep;
                    aqLfoPhase[c] = juce::jmap(phaseLock, aqLfoPhase[c], snapped);
                }

                auto amp = 0.5f + 0.5f * std::sin(aqLfoPhase[c]);
                amp = std::round(amp * static_cast<float>(quantLevels - 1)) / static_cast<float>(quantLevels - 1);

                if (jitter > 0.0f)
                    amp = juce::jlimit(0.0f, 1.0f, amp + (random.nextFloat() * 2.0f - 1.0f) * jitter * 0.06f);

                prevAmp = aqHeldAmp[c];
                aqHeldAmp[c] = amp;
                stepped = true;
                auto jitterHold = holdSamples;
                if (jitter > 0.0f)
                {
                    const auto jitterOffset = static_cast<int>(std::round((random.nextFloat() * 2.0f - 1.0f) * jitter * 0.35f * holdSamples));
                    jitterHold = juce::jmax(1, holdSamples + jitterOffset);
                }
                aqHeldCounter[c] = jitterHold;
            }

            const auto modulationDepth = juce::jmap(brutal, 0.2f, 1.0f);
            const auto modulation = juce::jmap(modulationDepth, 1.0f, aqHeldAmp[c]);
            auto y = write[i] * modulation;

            if (stepped)
            {
                const auto zipperDelta = aqHeldAmp[c] - prevAmp;
                y += zipperDelta * zipper * juce::jmap(brutal, 0.3f, 0.9f) * std::copysign(1.0f, y == 0.0f ? 1.0f : y);
            }

            write[i] = std::tanh(y * juce::jmap(brutal, 1.2f, 3.2f));
        }
    }
}

void DigitalisAudioProcessor::processStreamingArtifactGenerator(juce::AudioBuffer<float>& buffer)
{
    const auto artifact = parameters.getRawParameterValue("artifact")->load() * 0.01f;
    const auto bitrate = parameters.getRawParameterValue("bitrate")->load();
    const auto masking = parameters.getRawParameterValue("masking")->load() * 0.01f;
    const auto smear = parameters.getRawParameterValue("smear")->load() * 0.01f;
    const auto codecMode = static_cast<int>(parameters.getRawParameterValue("codecMode")->load());
    const auto switchMs = parameters.getRawParameterValue("switchMs")->load();
    const auto packetLoss = parameters.getRawParameterValue("packetLoss")->load() * 0.01f;
    const auto burst = parameters.getRawParameterValue("burst")->load() * 0.01f;
    const auto preEcho = parameters.getRawParameterValue("preecho")->load() * 0.01f;

    const auto channels = getTotalNumInputChannels();
    const auto numSamples = buffer.getNumSamples();
    const auto frameSize = juce::jlimit(16, 1024, static_cast<int>(std::round(64.0f + artifact * 384.0f)));
    const auto switchSamples = juce::jmax(1, static_cast<int>(std::round((switchMs * 0.001f) * static_cast<float>(currentSampleRate))));

    if (codecMode == 0)
    {
        sagCodec = 0;
        sagCodecCounter = switchSamples;
    }
    else if (--sagCodecCounter <= 0)
    {
        if (codecMode == 1)
            sagCodec = (sagCodec + 1) % 4;
        else
            sagCodec = random.nextInt(4);
        sagCodecCounter = switchSamples;
    }

    const auto bitrateCrush = juce::jlimit(8.0f, 4096.0f, std::pow(2.0f, juce::jmap(bitrate, 8.0f, 320.0f, 3.0f, 12.0f)));
    const auto concealNoise = juce::jmap(masking, 0.0f, 1.0f, 0.0f, 0.04f);

    for (int start = 0; start < numSamples; start += frameSize)
    {
        const auto chunkSize = juce::jmin(frameSize, numSamples - start);
        auto lost = false;

        if (sagLossBurstRemaining > 0)
        {
            lost = true;
            --sagLossBurstRemaining;
        }
        else if (random.nextFloat() < packetLoss)
        {
            lost = true;
            if (random.nextFloat() < burst)
                sagLossBurstRemaining = 1 + random.nextInt(juce::jmax(2, static_cast<int>(2 + burst * 12.0f)));
        }

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto c = static_cast<size_t>(ch);
            auto* write = buffer.getWritePointer(ch, start);

            for (int i = 0; i < chunkSize; ++i)
            {
                auto x = write[i];

                if (lost)
                {
                    if (random.nextFloat() < 0.5f)
                        x = sagLastFrameSample[c];
                    else
                        x = (random.nextFloat() * 2.0f - 1.0f) * concealNoise;
                }

                const auto mag = std::abs(x);
                sagTransientEnv[c] = mag > sagTransientEnv[c] ? (0.6f * mag + 0.4f * sagTransientEnv[c])
                                                              : (0.995f * sagTransientEnv[c] + 0.005f * mag);

                // Coarse stand-in for codec flavors.
                switch (sagCodec)
                {
                    case 0: // MP3-ish: stronger masking + low-passed texture.
                        x = quantise(x, bitrateCrush * juce::jmap(masking, 1.0f, 0.12f));
                        sagToneState[c] += (x - sagToneState[c]) * juce::jmap(masking, 0.08f, 0.02f);
                        x = sagToneState[c];
                        break;
                    case 1: // AAC-ish: cleaner highs but smearing.
                        x = quantise(x, bitrateCrush * juce::jmap(masking, 1.0f, 0.35f));
                        sagSmearState[c] = juce::jmap(0.35f + smear * 0.5f, x, sagSmearState[c]);
                        x = juce::jmap(0.35f, x, sagSmearState[c]);
                        break;
                    case 2: // Opus-ish: smoother core with level-dependent wobble.
                        x = quantise(x, bitrateCrush * 0.75f);
                        x += (random.nextFloat() * 2.0f - 1.0f) * (0.008f + 0.018f * masking) * (0.4f + 0.6f * sagTransientEnv[c]);
                        break;
                    case 3: // GSM-ish: narrow + coarse.
                    default:
                        sagToneState[c] += (x - sagToneState[c]) * 0.05f;
                        x = quantise(sagToneState[c], juce::jmax(12.0f, bitrateCrush * 0.2f));
                        break;
                }

                // Smear across time (tonal blurring).
                sagSmearState[c] += (x - sagSmearState[c]) * juce::jmap(smear, 0.45f, 0.03f);
                x = juce::jmap(smear * 0.75f, x, sagSmearState[c]);

                // Pre-echo style writeback into recent samples on transient bursts.
                if (preEcho > 0.0f && i > 4 && sagTransientEnv[c] > 0.25f && random.nextFloat() < preEcho * 0.04f)
                {
                    const auto taps = juce::jmin(i, 8);
                    for (int t = 1; t <= taps; ++t)
                    {
                        const auto leak = preEcho * 0.09f / static_cast<float>(t);
                        write[i - t] = juce::jlimit(-1.0f, 1.0f, write[i - t] + (x * leak));
                    }
                }

                x = std::tanh(x * juce::jmap(artifact, 1.0f, 2.1f));
                write[i] = x;
                sagLastFrameSample[c] = x;
            }
        }
    }
}

void DigitalisAudioProcessor::processFFTBrutalist(juce::AudioBuffer<float>& buffer)
{
    const auto brutalism = parameters.getRawParameterValue("brutalism")->load() * 0.01f;
    const auto binDensity = parameters.getRawParameterValue("binDensity")->load() * 0.01f;
    const auto clusterChoice = static_cast<int>(parameters.getRawParameterValue("cluster")->load());
    const auto freezeRate = parameters.getRawParameterValue("freezeRate")->load() * 0.01f;
    const auto freezeLenMs = parameters.getRawParameterValue("freezeLen")->load();
    const auto phaseScramble = parameters.getRawParameterValue("phaseScramble")->load() * 0.01f;
    const auto phaseSteps = juce::jmax(2, static_cast<int>(std::round(parameters.getRawParameterValue("phaseSteps")->load())));
    const auto sortAmount = parameters.getRawParameterValue("sortAmount")->load() * 0.01f;
    const auto spectralJitter = parameters.getRawParameterValue("jitter")->load() * 0.01f;

    constexpr std::array<int, 6> clusterSizes { 1, 2, 4, 8, 16, 32 };
    const auto clusterSize = clusterSizes[(size_t) juce::jlimit(0, 5, clusterChoice)];
    const auto channels = getTotalNumInputChannels();
    const auto samples = buffer.getNumSamples();
    const auto freezeSamples = juce::jmax(1, static_cast<int>(std::round((freezeLenMs * 0.001) * currentSampleRate)));
    const auto holdBase = juce::jmax(1, clusterSize * juce::jmax(1, static_cast<int>(1 + binDensity * 10.0f)));
    const auto phaseStep = juce::MathConstants<float>::twoPi / static_cast<float>(phaseSteps);

    for (int ch = 0; ch < channels; ++ch)
    {
        auto* write = buffer.getWritePointer(ch);
        auto held = 0.0f;
        auto holdCounter = 0;
        auto phase = 0.0f;
        auto freezeValue = 0.0f;
        auto freezeCounter = 0;

        for (int i = 0; i < samples; ++i)
        {
            auto x = write[i];

            if (freezeCounter > 0)
            {
                x = freezeValue;
                --freezeCounter;
            }
            else if (random.nextFloat() < freezeRate * 0.02f)
            {
                freezeValue = x;
                freezeCounter = freezeSamples;
            }

            if (--holdCounter <= 0)
            {
                held = x;
                holdCounter = juce::jmax(1, holdBase + random.nextInt(juce::jmax(2, holdBase)));
            }
            x = juce::jmap(binDensity, x, held);

            phase += juce::jmap(brutalism, 0.01f, 0.25f) + spectralJitter * 0.04f;
            if (phase > juce::MathConstants<float>::twoPi)
                phase -= juce::MathConstants<float>::twoPi;

            const auto snappedPhase = std::round(phase / phaseStep) * phaseStep;
            const auto randomPhase = (random.nextFloat() * juce::MathConstants<float>::twoPi) - juce::MathConstants<float>::pi;
            const auto warpedPhase = juce::jmap(phaseScramble, snappedPhase, randomPhase);
            const auto carrier = std::sin(warpedPhase);
            x = juce::jmap(phaseScramble, x, x * carrier);

            const auto mag = std::abs(x);
            const auto sortedProxy = std::pow(mag, juce::jmap(sortAmount, 1.0f, 0.28f));
            x = std::copysign(sortedProxy, x);

            x = std::tanh(x * juce::jmap(brutalism, 1.0f, 2.4f));
            write[i] = juce::jlimit(-1.0f, 1.0f, x);
        }
    }
}

void DigitalisAudioProcessor::processOverclockFailure(juce::AudioBuffer<float>& buffer)
{
    const auto overclock = parameters.getRawParameterValue("overclock")->load() * 0.01f;
    const auto sensitivity = parameters.getRawParameterValue("sensitivity")->load() * 0.01f;
    const auto failureRate = parameters.getRawParameterValue("failureRate")->load() * 0.01f;
    const auto latencySpike = parameters.getRawParameterValue("latencySpike")->load() * 0.01f;
    const auto desync = parameters.getRawParameterValue("desync")->load() * 0.01f;
    const auto thermal = parameters.getRawParameterValue("thermal")->load() * 0.01f;
    const auto recovery = parameters.getRawParameterValue("recovery")->load() * 0.01f;

    const auto channels = getTotalNumInputChannels();
    const auto numSamples = buffer.getNumSamples();

    // Compute audio-reactive stress and thermal integration.
    auto blockEnergy = 0.0f;
    for (int ch = 0; ch < channels; ++ch)
    {
        const auto* read = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            blockEnergy += std::abs(read[i]);
    }
    blockEnergy /= static_cast<float>(juce::jmax(1, channels * numSamples));

    const auto stressTarget = juce::jlimit(0.0f, 1.0f, blockEnergy * (0.8f + 3.2f * sensitivity) + overclock * 0.35f);
    ocfStressEnv += (stressTarget - ocfStressEnv) * juce::jmap(recovery, 0.25f, 0.01f);
    const auto thermalRise = (overclock * 0.0012f + ocfStressEnv * 0.0018f) * (0.35f + thermal);
    const auto thermalFall = 0.0003f + recovery * 0.0012f;
    ocfThermalState = juce::jlimit(0.0f, 1.0f, ocfThermalState + thermalRise - thermalFall);

    const auto failChance = juce::jlimit(0.0f, 0.85f, failureRate * (0.25f + 0.75f * ocfStressEnv) + ocfThermalState * 0.28f);
    const auto spikeMax = juce::jmax(1, static_cast<int>(2 + latencySpike * 180.0f + thermal * 80.0f));
    const auto baseDesync = static_cast<int>(desync * 120.0f);

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto c = static_cast<size_t>(ch);
        auto* write = buffer.getWritePointer(ch);
        auto& delayLine = ocfDelayLine[c];
        const auto delaySize = static_cast<int>(delayLine.size());

        // Desync right channel harder to emulate thread drift.
        const auto channelDesync = (ch % 2 == 1) ? baseDesync : static_cast<int>(baseDesync * 0.35f);

        for (int i = 0; i < numSamples; ++i)
        {
            auto x = write[i];

            // Random processing skips and hold glitches under stress.
            if (ocfHoldRemaining[c] > 0)
            {
                x = ocfHoldValue[c];
                --ocfHoldRemaining[c];
            }
            else if (random.nextFloat() < failChance * 0.05f)
            {
                ocfHoldValue[c] = x;
                ocfHoldRemaining[c] = 1 + random.nextInt(juce::jmax(2, static_cast<int>(2 + failChance * 24.0f)));
                x = ocfHoldValue[c];
            }

            if (random.nextFloat() < failChance * 0.03f)
                x = 0.0f; // dropped sample burst

            // Latency spikes: jump read offset unpredictably.
            if (random.nextFloat() < latencySpike * (0.01f + failChance * 0.02f))
            {
                ocfDelayReadOffset[c] = 1 + random.nextInt(spikeMax + juce::jmax(1, channelDesync));
            }
            else
            {
                const auto nominal = 1 + channelDesync;
                const auto pull = juce::jmax(1, nominal);
                ocfDelayReadOffset[c] += (pull - ocfDelayReadOffset[c]) > 0 ? 1 : -1;
                ocfDelayReadOffset[c] = juce::jlimit(1, spikeMax + juce::jmax(1, channelDesync), ocfDelayReadOffset[c]);
            }

            const auto writePos = ocfDelayWritePos[c];
            delayLine[(size_t) writePos] = x;
            auto readPos = writePos - ocfDelayReadOffset[c];
            while (readPos < 0)
                readPos += delaySize;
            auto y = delayLine[(size_t) (readPos % delaySize)];

            ocfDelayWritePos[c] = (writePos + 1) % delaySize;

            // Thermal drift detunes timing/amplitude subtly over long sessions.
            const auto drift = 1.0f + std::sin((processedSamples + i + ch * 31) * (0.00007f + ocfThermalState * 0.00025f)) * (0.01f + ocfThermalState * 0.06f);
            y *= drift;
            y = std::tanh(y * juce::jmap(overclock, 1.0f, 1.9f));

            write[i] = juce::jlimit(-1.0f, 1.0f, y);
        }
    }
}

void DigitalisAudioProcessor::processDeterministicMachine(juce::AudioBuffer<float>& buffer)
{
    const auto determinism = parameters.getRawParameterValue("determinism")->load() * 0.01f;
    const auto stateCount = juce::jmax(2, static_cast<int>(std::round(parameters.getRawParameterValue("stateCount")->load())));
    const auto stateDwellMs = parameters.getRawParameterValue("stateDwell")->load();
    const auto loopMs = parameters.getRawParameterValue("loopMs")->load();
    const auto hashWindow = juce::jmax(8, static_cast<int>(std::round(parameters.getRawParameterValue("hashWindow")->load())));
    const auto jumpRule = static_cast<int>(parameters.getRawParameterValue("jumpRule")->load());
    const auto memory = parameters.getRawParameterValue("memory")->load() * 0.01f;

    const auto channels = getTotalNumInputChannels();
    const auto samples = buffer.getNumSamples();
    const auto loopLength = juce::jlimit(16, juce::jmax(16, static_cast<int>(0.08 * currentSampleRate)),
                                         static_cast<int>(std::round(loopMs * 0.001 * currentSampleRate)));
    const auto dwellSamples = juce::jmax(1, static_cast<int>(std::round(stateDwellMs * 0.001 * currentSampleRate)));

    for (auto& loop : dmLoopBuffer)
    {
        if ((int) loop.size() != loopLength)
            loop.assign((size_t) loopLength, 0.0f);
    }

    for (int i = 0; i < samples; ++i)
    {
        auto probe = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            probe += buffer.getSample(ch, i);
        probe /= static_cast<float>(juce::jmax(1, channels));

        dmHashState = hashStep(dmHashState, probe);
        ++dmHashCounter;

        if (--dmSamplesToNextState <= 0 || dmHashCounter >= hashWindow)
        {
            const auto hashBased = static_cast<int>(dmHashState % static_cast<std::uint32_t>(stateCount));
            auto nextState = dmStateIndex;

            if (jumpRule == 0)
                nextState = (dmStateIndex + 1) % stateCount;
            else if (jumpRule == 1)
                nextState = hashBased;
            else
                nextState = std::abs(probe) > (0.15f + determinism * 0.35f) ? hashBased : dmStateIndex;

            if (random.nextFloat() < memory)
                nextState = static_cast<int>(std::round(juce::jmap(memory, static_cast<float>(nextState), static_cast<float>(dmStateIndex))));

            dmStateIndex = juce::jlimit(0, stateCount - 1, nextState);
            dmSamplesToNextState = dwellSamples;
            dmHashCounter = 0;
            dmHashState ^= (static_cast<std::uint32_t>(dmStateIndex) * 2654435761u);
        }

        const auto stateNorm = static_cast<float>(dmStateIndex) / static_cast<float>(juce::jmax(1, stateCount - 1));
        const auto gainTarget = juce::jmap(stateNorm, 0.45f, 1.65f);
        dmStateSmoother += (gainTarget - dmStateSmoother) * 0.015f;
        const auto crushSteps = juce::jmax(8.0f, 1024.0f - (determinism * 700.0f + stateNorm * 240.0f));
        const auto loopBlend = juce::jlimit(0.0f, 1.0f, determinism * (0.25f + 0.75f * (stateNorm > 0.45f ? 1.0f : 0.0f)));
        const auto fold = juce::jmap(stateNorm, 0.8f, 2.5f);

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto c = static_cast<size_t>(ch);
            auto* write = buffer.getWritePointer(ch);
            auto& loop = dmLoopBuffer[c];
            const auto in = write[i];

            loop[(size_t) dmLoopWritePos[c]] = in;
            dmLoopWritePos[c] = (dmLoopWritePos[c] + 1) % loopLength;

            const auto loopSample = loop[(size_t) dmLoopReadPos[c]];
            dmLoopReadPos[c] = (dmLoopReadPos[c] + 1) % loopLength;

            auto y = juce::jmap(loopBlend, in, loopSample);
            y = quantise(y * dmStateSmoother, crushSteps);
            y = std::sin(y * juce::MathConstants<float>::pi * fold);

            // State quantisation gate: only a finite set of outputs per state.
            const auto stateLevels = juce::jmax(2, 2 + (dmStateIndex % 24));
            y = quantise(y, static_cast<float>(stateLevels));

            write[i] = juce::jlimit(-1.0f, 1.0f, std::tanh(y * juce::jmap(determinism, 1.0f, 2.2f)));
        }
    }
}

void DigitalisAudioProcessor::processClassicBufferStutter(juce::AudioBuffer<float>& buffer)
{
    const auto amount = parameters.getRawParameterValue("amount")->load() * 0.01f;
    const auto rateHz = parameters.getRawParameterValue("rateHz")->load();
    const auto sliceMs = parameters.getRawParameterValue("sliceMs")->load();
    const auto repeats = juce::jmax(1, static_cast<int>(std::round(parameters.getRawParameterValue("repeats")->load())));
    const auto reverseChance = parameters.getRawParameterValue("reverse")->load() * 0.01f;
    const auto timingJitter = parameters.getRawParameterValue("timingJitter")->load() * 0.01f;
    const auto duck = parameters.getRawParameterValue("duck")->load() * 0.01f;

    const auto channels = getTotalNumInputChannels();
    const auto samples = buffer.getNumSamples();
    const auto maxSliceLength = juce::jmax(64, static_cast<int>(0.5 * currentSampleRate));
    const auto sliceLength = juce::jlimit(16, maxSliceLength,
                                          static_cast<int>(std::round(sliceMs * 0.001 * currentSampleRate)));
    const auto baseInterval = juce::jmax(sliceLength + 1, static_cast<int>(std::round(currentSampleRate / juce::jmax(0.25f, rateHz))));
    const auto triggerProb = juce::jmap(amount, 0.04f, 1.0f);
    const auto inputDuck = juce::jmap(duck, 1.0f, 0.22f);

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto c = static_cast<size_t>(ch);
        auto* write = buffer.getWritePointer(ch);
        auto& slice = stutterSliceBuffer[c];

        if ((int) slice.size() < maxSliceLength)
            slice.assign((size_t) maxSliceLength, 0.0f);

        for (int i = 0; i < samples; ++i)
        {
            const auto in = write[i];
            auto out = in;

            if (!stutterIsCapturing[c] && !stutterIsPlaying[c])
            {
                if (--stutterIntervalCounter[c] <= 0)
                {
                    if (random.nextFloat() < triggerProb)
                    {
                        stutterIsCapturing[c] = true;
                        stutterCapturePos[c] = 0;
                        stutterIsReverse[c] = random.nextFloat() < reverseChance;
                    }

                    auto jitteredInterval = baseInterval;
                    if (timingJitter > 0.0f)
                    {
                        const auto offset = static_cast<int>(std::round((random.nextFloat() * 2.0f - 1.0f) * timingJitter * 0.4f * static_cast<float>(baseInterval)));
                        jitteredInterval = juce::jmax(sliceLength + 1, baseInterval + offset);
                    }
                    stutterIntervalCounter[c] = jitteredInterval;
                }
            }

            if (stutterIsCapturing[c])
            {
                slice[(size_t) stutterCapturePos[c]] = in;
                ++stutterCapturePos[c];
                out = in * inputDuck;

                if (stutterCapturePos[c] >= sliceLength)
                {
                    stutterIsCapturing[c] = false;
                    stutterIsPlaying[c] = true;
                    stutterRepeatsRemaining[c] = repeats;
                    stutterPlayPos[c] = stutterIsReverse[c] ? (sliceLength - 1) : 0;
                }
            }
            else if (stutterIsPlaying[c])
            {
                out = slice[(size_t) juce::jlimit(0, sliceLength - 1, stutterPlayPos[c])];
                if (stutterIsReverse[c])
                    --stutterPlayPos[c];
                else
                    ++stutterPlayPos[c];

                const auto wrapped = stutterIsReverse[c] ? (stutterPlayPos[c] < 0) : (stutterPlayPos[c] >= sliceLength);
                if (wrapped)
                {
                    --stutterRepeatsRemaining[c];
                    stutterPlayPos[c] = stutterIsReverse[c] ? (sliceLength - 1) : 0;
                    if (stutterRepeatsRemaining[c] <= 0)
                        stutterIsPlaying[c] = false;
                }
            }

            write[i] = juce::jlimit(-1.0f, 1.0f, std::tanh(out * juce::jmap(amount, 1.0f, 1.5f)));
        }
    }
}

void DigitalisAudioProcessor::processMelodicSkippingEngine(juce::AudioBuffer<float>& buffer)
{
    const auto skip = parameters.getRawParameterValue("skip")->load() * 0.01f;
    const auto jumpRate = parameters.getRawParameterValue("jumpRate")->load();
    const auto segMs = parameters.getRawParameterValue("segMs")->load();
    const auto melody = parameters.getRawParameterValue("melody")->load() * 0.01f;
    const auto spread = parameters.getRawParameterValue("spread")->load() * 0.01f;
    const auto reverseChance = parameters.getRawParameterValue("reverse")->load() * 0.01f;
    const auto flutter = parameters.getRawParameterValue("flutter")->load() * 0.01f;
    const auto blur = parameters.getRawParameterValue("blur")->load() * 0.01f;

    const auto channels = getTotalNumInputChannels();
    const auto samples = buffer.getNumSamples();
    const auto segLength = juce::jlimit(16, juce::jmax(128, static_cast<int>(1.2 * currentSampleRate)),
                                        static_cast<int>(std::round(segMs * 0.001 * currentSampleRate)));
    const auto triggerProbPerSample = juce::jlimit(0.0f, 1.0f, (jumpRate / static_cast<float>(juce::jmax(1.0, currentSampleRate))) * (0.2f + 0.8f * skip));
    const auto skipDepth = std::pow(skip, 0.65f);
    constexpr std::array<int, 15> semitones { -24, -19, -12, -9, -7, -5, -3, 0, 3, 5, 7, 9, 12, 19, 24 };

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto c = static_cast<size_t>(ch);
        auto* write = buffer.getWritePointer(ch);
        auto& mem = mskBuffer[c];
        const auto memSize = static_cast<int>(mem.size());
        if (memSize <= 32)
            continue;

        for (int i = 0; i < samples; ++i)
        {
            const auto in = write[i];
            mem[(size_t) mskWritePos[c]] = in;
            mskWritePos[c] = (mskWritePos[c] + 1) % memSize;

            if (mskRemaining[c] <= 0 && random.nextFloat() < triggerProbPerSample)
            {
                const auto melodicSpan = juce::jlimit(1, static_cast<int>(semitones.size()) - 1,
                                                      2 + static_cast<int>(std::round(melody * (0.5f + spread) * 12.0f)));
                const auto center = static_cast<int>(semitones.size() / 2);
                const auto minIndex = juce::jmax(0, center - melodicSpan);
                const auto maxIndex = juce::jmin(static_cast<int>(semitones.size()) - 1, center + melodicSpan);
                const auto semitone = semitones[(size_t) juce::jlimit(minIndex, maxIndex, minIndex + random.nextInt(juce::jmax(1, maxIndex - minIndex + 1)))];
                mskRate[c] = std::pow(2.0f, static_cast<float>(semitone) / 12.0f);
                mskDirection[c] = (random.nextFloat() < reverseChance) ? -1 : 1;
                const auto lengthMul = juce::jmap(melody, 1.0f, 2.6f);
                const auto baseLength = static_cast<int>(std::round(static_cast<float>(segLength) * lengthMul));
                mskRemaining[c] = juce::jmax(16, baseLength + random.nextInt(juce::jmax(1, baseLength)));

                const auto backMin = juce::jmax(segLength, static_cast<int>(0.03 * currentSampleRate));
                const auto backMax = juce::jmin(memSize - 2, juce::jmax(backMin + 1, static_cast<int>(0.9 * currentSampleRate)));
                const auto back = juce::jlimit(backMin, backMax, backMin + random.nextInt(juce::jmax(1, backMax - backMin + 1)));
                auto start = mskWritePos[c] - back;
                while (start < 0)
                    start += memSize;
                mskPlayPos[c] = static_cast<float>(start);

                // Scratch-start tick accent.
                write[i] = juce::jlimit(-1.0f, 1.0f, in + (random.nextFloat() * 2.0f - 1.0f) * (0.06f + 0.18f * skipDepth));
            }

            auto y = in;
            if (mskRemaining[c] > 0)
            {
                auto pos = mskPlayPos[c];
                while (pos < 0.0f)
                    pos += static_cast<float>(memSize);
                while (pos >= static_cast<float>(memSize))
                    pos -= static_cast<float>(memSize);

                const auto p0 = static_cast<int>(pos);
                const auto p1 = (p0 + 1) % memSize;
                const auto frac = pos - static_cast<float>(p0);
                const auto a = mem[(size_t) p0];
                const auto b = mem[(size_t) p1];
                auto seg = a + (b - a) * frac;

                const auto lpf = juce::jmap(blur, 0.92f, 0.28f);
                mskBlurState[c] = mskBlurState[c] * lpf + seg * (1.0f - lpf);
                seg = juce::jmap(blur, seg, mskBlurState[c]);

                if (random.nextFloat() < flutter * 0.018f)
                    mskDirection[c] = -mskDirection[c];

                if (random.nextFloat() < flutter * 0.01f)
                    mskRate[c] = juce::jlimit(0.35f, 2.6f, mskRate[c] * (0.6f + random.nextFloat() * 1.2f));

                const auto flutterMod = 1.0f + std::sin((processedSamples + i + ch * 59) * 0.0024f) * flutter * 0.24f;
                auto advance = static_cast<float>(mskDirection[c]) * mskRate[c] * flutterMod;
                advance += std::sin((processedSamples + i + ch * 13) * 0.019f) * flutter * 0.42f; // scratch rub
                mskPlayPos[c] += advance;
                --mskRemaining[c];

                y = juce::jmap(skipDepth, in, seg);

                if (random.nextFloat() < skip * flutter * 0.01f)
                    y *= 0.2f; // scratch dropout notch
            }

            write[i] = juce::jlimit(-1.0f, 1.0f, std::tanh(y * juce::jmap(skipDepth, 1.0f, 2.1f)));
        }
    }
}

float DigitalisAudioProcessor::crushSample(float x) const
{
    auto* digitalAmount = parameters.getRawParameterValue("digital");
    const auto steps = juce::jmax(8.0f, 1024.0f - *digitalAmount * 8.0f);
    return quantise(x, steps);
}

float DigitalisAudioProcessor::aliasSample(float x, int channel, int)
{
    auto* digitalAmount = parameters.getRawParameterValue("digital");
    const auto hold = juce::jmax(1, 64 - static_cast<int>(*digitalAmount * 0.5f));
    if (--heldCountdown[(size_t) channel] <= 0)
    {
        heldSamples[(size_t) channel] = x;
        heldCountdown[(size_t) channel] = hold;
    }
    return heldSamples[(size_t) channel];
}

float DigitalisAudioProcessor::gridSample(float x, int sampleInBlock)
{
    auto* digitalAmount = parameters.getRawParameterValue("digital");
    const auto grid = juce::jmax(1, 128 - static_cast<int>(*digitalAmount));
    if (sampleInBlock % grid == 0)
        heldSamples[0] = x;
    return heldSamples[0];
}

float DigitalisAudioProcessor::dropoutSample(float x)
{
    auto* digitalAmount = parameters.getRawParameterValue("digital");
    const auto probability = juce::jlimit(0.0f, 0.75f, *digitalAmount * 0.0075f);
    return random.nextFloat() < probability ? 0.0f : x;
}

float DigitalisAudioProcessor::deterministicSample(float x, int channel)
{
    auto* digitalAmount = parameters.getRawParameterValue("digital");

    auto& buffer = microLoopBuffers[(size_t) channel];
    auto* writePtr = buffer.getWritePointer(0);
    const auto size = buffer.getNumSamples();

    const auto window = juce::jmax(8, static_cast<int>(size - (*digitalAmount * 0.2f * size)));

    writePtr[microLoopWritePos[(size_t) channel]] = x;
    microLoopWritePos[(size_t) channel] = (microLoopWritePos[(size_t) channel] + 1) % window;

    const auto y = writePtr[microLoopReadPos[(size_t) channel]];
    microLoopReadPos[(size_t) channel] = (microLoopReadPos[(size_t) channel] + 1) % window;

    return y;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DigitalisAudioProcessor();
}
