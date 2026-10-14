#include "EqEngine.h"

namespace harmeq
{
void EqEngine::prepare (double sampleRate)
{
    for (auto& b : bands)
        b.prepare (sampleRate);
}

void EqEngine::reset()
{
    for (auto& b : bands)
        b.reset();
}

void EqEngine::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                        const PitchState& pitch) noexcept
{
    auto* const* channels = buffer.getArrayOfWritePointers();
    const int numChannels = buffer.getNumChannels();

    while (numSamples > 0)
    {
        const int n = std::min (numSamples, maxChunkSize);

        for (auto& b : bands)
        {
            b.update (pitch, n);
            b.process (channels, numChannels, startSample, n);
        }

        startSample += n;
        numSamples -= n;
    }
}
} // namespace harmeq
