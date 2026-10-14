#pragma once

#include "EqBand.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace harmeq
{
// The full band chain. Processes in chunks of at most maxChunkSize samples so that filter
// coefficients follow the pitch closely; callers may pass smaller chunks (e.g. split at MIDI events).
class EqEngine
{
public:
    static constexpr int maxChunkSize = 32;

    void prepare (double sampleRate);
    void reset();

    void setBandSettings (int band, const BandSettings& s) noexcept { bands[(size_t) band].setSettings (s); }
    const EqBand& getBand (int band) const noexcept { return bands[(size_t) band]; }

    // Processes [startSample, startSample + numSamples) with a single pitch value, internally
    // re-splitting into chunks of at most maxChunkSize.
    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, const PitchState& pitch) noexcept;

private:
    std::array<EqBand, numBands> bands;
};
} // namespace harmeq
