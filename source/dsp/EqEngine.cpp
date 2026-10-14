#include "EqEngine.h"

#include <algorithm>

namespace harmeq
{
void EqEngine::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    for (auto& band : bands)
        for (auto& instance : band)
            instance.prepare (sampleRate);
}

void EqEngine::reset()
{
    for (auto& band : bands)
        for (auto& instance : band)
            instance.reset();
}

InstanceMask EqEngine::activeInstances (const std::array<BandSettings, numBands>& settings, const PitchSet& pitches,
                                        double sampleRate)
{
    InstanceMask mask {};

    // Voices from lowest to highest pitch, so merges keep the lower note's instance.
    std::array<int, maxVoices> order {};
    for (int v = 0; v < maxVoices; ++v)
        order[(size_t) v] = v;
    std::stable_sort (order.begin(), order.end(),
                      [&] (int a, int b) { return pitches[(size_t) a].f0Hz < pitches[(size_t) b].f0Hz; });

    struct Kept
    {
        double log2Hz;
        FilterType type;
        int voice;
    };
    std::array<Kept, (size_t) numBands * maxVoices> kept {};
    int numKept = 0;
    const double mergeOctaves = mergeCents / 1200.0;

    for (int b = 0; b < numBands; ++b)
    {
        const auto& s = settings[(size_t) b];
        if (! s.on)
            continue;

        if (s.track == TrackMode::fixed)
        {
            mask[(size_t) b][0] = true;
            continue;
        }

        for (int v : order)
        {
            const auto& pitch = pitches[(size_t) v];
            if (! pitch.voiced)
                continue;

            const auto target = EqBand::resolveTarget (s, pitch, sampleRate);
            if (target.amount <= 0.0f)
                continue; // faded out near Nyquist; doesn't claim the spot

            const double log2Hz = std::log2 (target.freqHz);
            const bool duplicate = std::any_of (kept.begin(), kept.begin() + numKept, [&] (const Kept& k) {
                return k.voice != v && k.type == s.type && std::abs (k.log2Hz - log2Hz) < mergeOctaves;
            });

            if (duplicate)
                continue;

            mask[(size_t) b][(size_t) v] = true;
            kept[(size_t) numKept++] = { log2Hz, s.type, v };
        }
    }

    return mask;
}

void EqEngine::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                        const PitchSet& pitches) noexcept
{
    auto* const* channels = buffer.getArrayOfWritePointers();
    const int numChannels = buffer.getNumChannels();
    const auto mask = activeInstances (settings, pitches, sampleRate);

    while (numSamples > 0)
    {
        const int n = std::min (numSamples, maxChunkSize);

        for (int b = 0; b < numBands; ++b)
        {
            for (int v = 0; v < maxVoices; ++v)
            {
                auto& instance = bands[(size_t) b][(size_t) v];
                const bool active = mask[(size_t) b][(size_t) v];

                // Silent instances that aren't wanted cost nothing.
                if (! active && instance.isIdle() && v > 0)
                    continue;

                // Inactive instances fade out: tracked ones by going unvoiced, fixed ones by switching off.
                auto s = settings[(size_t) b];
                if (! active && s.track == TrackMode::fixed)
                    s.on = false;

                instance.setSettings (s);
                instance.update ({ pitches[(size_t) v].f0Hz, active }, n);
                instance.process (channels, numChannels, startSample, n);
            }
        }

        startSample += n;
        numSamples -= n;
    }
}
} // namespace harmeq
