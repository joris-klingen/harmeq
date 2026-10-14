#pragma once

#include "EqBand.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace harmeq
{
// Which band instances run: [band][voice slot].
using InstanceMask = std::array<std::array<bool, maxVoices>, numBands>;

// The full band chain. Each band has one instance per voice slot: tracked bands run an instance per
// sounding note, fixed bands only use slot 0. Processes in chunks of at most maxChunkSize samples so
// filter coefficients follow the pitch closely; callers may pass smaller chunks (e.g. split at MIDI events).
class EqEngine
{
public:
    static constexpr int maxChunkSize = 32;

    // Two instances from different notes, of the same filter type, closer than this are merged:
    // only the first (lowest band number, then lowest note) runs. Stops shared harmonics in a chord
    // (e.g. C's 3rd and G's 2nd) getting the cut or boost twice.
    static constexpr double mergeCents = 20.0;

    void prepare (double sampleRate);
    void reset();

    void setBandSettings (int band, const BandSettings& s) noexcept { settings[(size_t) band] = s; }
    const EqBand& getInstance (int band, int voice) const noexcept { return bands[(size_t) band][(size_t) voice]; }
    const EqBand& getBand (int band) const noexcept { return getInstance (band, 0); }

    // Processes [startSample, startSample + numSamples) with one set of pitches, internally
    // re-splitting into chunks of at most maxChunkSize.
    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, const PitchSet& pitches) noexcept;

    // Shared with the display, so it draws the same instances that run.
    static InstanceMask activeInstances (const std::array<BandSettings, numBands>&, const PitchSet&, double sampleRate);

private:
    double sampleRate = 44100.0;
    std::array<BandSettings, numBands> settings {};
    std::array<std::array<EqBand, maxVoices>, numBands> bands;
};
} // namespace harmeq
