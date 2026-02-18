#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <iostream>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
float dbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

float toDb(float x)
{
    return 20.0f * std::log10(juce::jmax(1.0e-9f, x));
}
}

int main(int argc, char** argv)
{
    const bool dryRun = (argc > 1 && juce::String(argv[1]) == "--dry");
    const bool noProgram = (argc > 1 && juce::String(argv[1]) == "--no-program");
    const bool prepareOnly = (argc > 1 && juce::String(argv[1]) == "--prepare-only");

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    constexpr int channels = 2;
    constexpr float durationSec = 10.0f;
    const int totalSamples = static_cast<int>(durationSec * static_cast<float>(sampleRate));

    std::unique_ptr<juce::AudioProcessor> proc(createPluginFilter());
    if (dryRun)
    {
        std::cout << proc->getName() << " (dry)\n";
        return 0;
    }
    proc->prepareToPlay(sampleRate, blockSize);
    if (prepareOnly)
    {
        std::cout << proc->getName() << " (prepare-only)\n";
        return 0;
    }
    if (!noProgram)
        proc->setCurrentProgram(0); // Init default

    juce::AudioBuffer<float> block(channels, blockSize);
    juce::MidiBuffer midi;

    double phaseA = 0.0;
    double phaseB = 0.0;
    const double incA = juce::MathConstants<double>::twoPi * 97.0 / sampleRate;
    const double incB = juce::MathConstants<double>::twoPi * 1880.0 / sampleRate;

    double inSq = 0.0;
    double outSq = 0.0;
    int n = 0;

    int processed = 0;
    while (processed < totalSamples)
    {
        const int ns = juce::jmin(blockSize, totalSamples - processed);
        block.clear();

        for (int i = 0; i < ns; ++i)
        {
            const float t = static_cast<float>(processed + i) / static_cast<float>(sampleRate);
            const float burstEnv = (std::fmod(t, 1.3f) < 0.08f) ? 1.0f : 0.0f;

            const float sineLow = static_cast<float>(std::sin(phaseA)) * dbToGain(-14.0f);
            const float sineHigh = static_cast<float>(std::sin(phaseB)) * dbToGain(-21.0f);
            const float noise = (juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f) * dbToGain(-31.0f);
            const float burst = burstEnv * (juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f) * dbToGain(-9.0f);

            phaseA += incA;
            phaseB += incB;
            if (phaseA > juce::MathConstants<double>::twoPi) phaseA -= juce::MathConstants<double>::twoPi;
            if (phaseB > juce::MathConstants<double>::twoPi) phaseB -= juce::MathConstants<double>::twoPi;

            const float l = juce::jlimit(-1.0f, 1.0f, sineLow + sineHigh + noise + burst);
            const float r = juce::jlimit(-1.0f, 1.0f, sineLow * 0.92f + sineHigh * 1.06f + noise * 1.04f + burst * 0.95f);
            block.setSample(0, i, l);
            block.setSample(1, i, r);
        }

        juce::AudioBuffer<float> dryBuffer(block);
        proc->processBlock(block, midi);

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* in = dryBuffer.getReadPointer(ch);
            const auto* out = block.getReadPointer(ch);
            for (int i = 0; i < ns; ++i)
            {
                inSq += in[i] * in[i];
                outSq += out[i] * out[i];
                ++n;
            }
        }

        processed += ns;
    }

    const float inRms = static_cast<float>(std::sqrt(inSq / static_cast<double>(juce::jmax(1, n))));
    const float outRms = static_cast<float>(std::sqrt(outSq / static_cast<double>(juce::jmax(1, n))));
    const float delta = toDb(outRms) - toDb(inRms);

    std::cout << proc->getName() << "\n";
    std::cout << "InputRMS_dB=" << toDb(inRms) << "\n";
    std::cout << "OutputRMS_dB=" << toDb(outRms) << "\n";
    std::cout << "Delta_dB=" << delta << "\n";

    return 0;
}
