#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace harmeq
{
// Fixed delay for the lookahead path. Capacity is allocated in prepare(); setDelay() never allocates.
// Changing the delay while running jumps the read position (the host re-aligns tracks on a latency
// change anyway).
class LatencyDelay
{
public:
    void prepare (int numChannels, int maxDelaySamples)
    {
        capacity = maxDelaySamples + 1;
        lines.assign ((size_t) numChannels, std::vector<float> ((size_t) capacity, 0.0f));
        writePos = 0;
    }

    void reset()
    {
        for (auto& l : lines)
            std::fill (l.begin(), l.end(), 0.0f);
        writePos = 0;
    }

    void setDelay (int samples) noexcept { delay = juce::jlimit (0, capacity - 1, samples); }
    int getDelay() const noexcept { return delay; }

    // Delays [start, start + numSamples) of each channel in place.
    void process (float* const* channels, int numChannels, int start, int numSamples) noexcept
    {
        if (delay == 0 || lines.empty())
            return;

        numChannels = std::min (numChannels, (int) lines.size());
        int pos = writePos;

        for (int i = 0; i < numSamples; ++i)
        {
            const int readPos = (pos - delay + capacity) % capacity;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& line = lines[(size_t) ch];
                float& sample = channels[ch][start + i];
                line[(size_t) pos] = sample;
                sample = line[(size_t) readPos];
            }
            pos = (pos + 1) % capacity;
        }

        writePos = pos;
    }

private:
    std::vector<std::vector<float>> lines;
    int capacity = 1, writePos = 0, delay = 0;
};
} // namespace harmeq
