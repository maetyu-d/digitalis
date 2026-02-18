#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <cstdint>

class DigitalisAudioProcessor final : public juce::AudioProcessor
{
public:
    DigitalisAudioProcessor();
    ~DigitalisAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return parameters; }
    const juce::AudioProcessorValueTreeState& getValueTreeState() const noexcept { return parameters; }

private:
    struct PresetParam
    {
        const char* id;
        float value;
    };

    struct FactoryPreset
    {
        const char* name;
        std::vector<PresetParam> values;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static std::vector<FactoryPreset> createFactoryPresets();
    void applyFactoryPreset(size_t index);

    void processFloatingPointCollapse(juce::AudioBuffer<float>& buffer);
    void processNyquistDestroyer(juce::AudioBuffer<float>& buffer);
    void processBufferGlitchEngine(juce::AudioBuffer<float>& buffer);
    void processAutomationQuantiser(juce::AudioBuffer<float>& buffer);
    void processStreamingArtifactGenerator(juce::AudioBuffer<float>& buffer);
    void processFFTBrutalist(juce::AudioBuffer<float>& buffer);
    void processOverclockFailure(juce::AudioBuffer<float>& buffer);
    void processDeterministicMachine(juce::AudioBuffer<float>& buffer);
    void processClassicBufferStutter(juce::AudioBuffer<float>& buffer);
    void processMelodicSkippingEngine(juce::AudioBuffer<float>& buffer);
    void applyPostSafety(juce::AudioBuffer<float>& buffer);
    float applyFloatDamage(float x, int mantissaBits, int exponentStep, float roundingAmount);
    float applyNonlinearQuantiser(float x, int mode, float amount);

    float crushSample(float x) const;
    float aliasSample(float x, int channel, int sampleInBlock);
    float gridSample(float x, int sampleInBlock);
    float dropoutSample(float x);
    float deterministicSample(float x, int channel);

    juce::AudioProcessorValueTreeState parameters;

    juce::dsp::DryWetMixer<float> dryWet;
    juce::Random random;

    std::array<float, 2> heldSamples { 0.0f, 0.0f };
    std::array<int, 2> heldCountdown { 0, 0 };
    std::array<float, 2> fpcTemporalHeld { 0.0f, 0.0f };
    std::array<int, 2> fpcTemporalCountdown { 0, 0 };

    std::array<juce::AudioBuffer<float>, 2> microLoopBuffers;
    std::array<int, 2> microLoopWritePos { 0, 0 };
    std::array<int, 2> microLoopReadPos { 0, 0 };
    float fpcChaoticState = 0.371f;
    int denormalBurstRemaining = 0;
    std::array<float, 2> nyqHeldCurrent { 0.0f, 0.0f };
    std::array<float, 2> nyqHeldPrevious { 0.0f, 0.0f };
    std::array<int, 2> nyqHoldCounter { 1, 1 };
    std::array<float, 2> nyqFeedbackState { 0.0f, 0.0f };
    std::array<float, 2> nyqFeedbackToneState { 0.0f, 0.0f };
    float nyqPhase = 0.0f;
    float nyqTransientEnv = 0.0f;
    std::array<std::vector<float>, 2> bgePrevChunk;
    std::array<float, 2> bgeEnvelope { 0.0f, 0.0f };
    int bgePrevChunkSize = 0;
    bool bgeHasPrevChunk = false;
    std::array<float, 2> aqHeldAmp { 1.0f, 1.0f };
    std::array<int, 2> aqHeldCounter { 1, 1 };
    std::array<float, 2> aqLfoPhase { 0.0f, 0.0f };
    std::array<float, 2> sagToneState { 0.0f, 0.0f };
    std::array<float, 2> sagSmearState { 0.0f, 0.0f };
    std::array<float, 2> sagLastFrameSample { 0.0f, 0.0f };
    std::array<float, 2> sagTransientEnv { 0.0f, 0.0f };
    int sagCodec = 0;
    int sagCodecCounter = 0;
    int sagLossBurstRemaining = 0;
    static constexpr int fftBrutalistOrder = 10;
    static constexpr int fftBrutalistSize = 1 << fftBrutalistOrder;
    juce::dsp::FFT fftBrutalistFft { fftBrutalistOrder };
    juce::dsp::WindowingFunction<float> fftBrutalistWindow { fftBrutalistSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::array<std::vector<juce::dsp::Complex<float>>, 2> fftBrutalistFrozenSpectrum;
    std::array<int, 2> fftBrutalistFreezeRemaining { 0, 0 };
    std::array<std::vector<float>, 2> ocfDelayLine;
    std::array<int, 2> ocfDelayWritePos { 0, 0 };
    std::array<int, 2> ocfDelayReadOffset { 1, 1 };
    std::array<float, 2> ocfHoldValue { 0.0f, 0.0f };
    std::array<int, 2> ocfHoldRemaining { 0, 0 };
    float ocfThermalState = 0.0f;
    float ocfStressEnv = 0.0f;
    std::array<std::vector<float>, 2> dmLoopBuffer;
    std::array<int, 2> dmLoopWritePos { 0, 0 };
    std::array<int, 2> dmLoopReadPos { 0, 0 };
    int dmStateIndex = 0;
    int dmSamplesToNextState = 0;
    int dmHashCounter = 0;
    std::uint32_t dmHashState = 2166136261u;
    float dmStateSmoother = 0.0f;
    std::array<std::vector<float>, 2> stutterSliceBuffer;
    std::array<int, 2> stutterCapturePos { 0, 0 };
    std::array<int, 2> stutterPlayPos { 0, 0 };
    std::array<int, 2> stutterRepeatsRemaining { 0, 0 };
    std::array<int, 2> stutterIntervalCounter { 0, 0 };
    std::array<bool, 2> stutterIsCapturing { false, false };
    std::array<bool, 2> stutterIsPlaying { false, false };
    std::array<bool, 2> stutterIsReverse { false, false };
    std::array<std::vector<float>, 2> mskBuffer;
    std::array<int, 2> mskWritePos { 0, 0 };
    std::array<float, 2> mskPlayPos { 0.0f, 0.0f };
    std::array<int, 2> mskRemaining { 0, 0 };
    std::array<float, 2> mskRate { 1.0f, 1.0f };
    std::array<int, 2> mskDirection { 1, 1 };
    std::array<float, 2> mskBlurState { 0.0f, 0.0f };
    std::array<float, 2> postDcPrevInput { 0.0f, 0.0f };
    std::array<float, 2> postDcPrevOutput { 0.0f, 0.0f };
    float postAutoLevelGain = 1.0f;
    std::vector<FactoryPreset> factoryPresets;
    int currentProgramIndex = 0;

    double currentSampleRate = 44100.0;
    int maxBlockSize = 512;
    int processedSamples = 0;
};
