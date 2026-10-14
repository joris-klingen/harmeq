#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <array>

namespace harmeq
{
// Lock-free single-producer / single-consumer hand-off of mono audio from the audio thread to the
// spectrum analyzer. The audio thread drops samples rather than block when the UI falls behind.
class AnalyzerTap
{
public:
    static constexpr int capacity = 1 << 15;

    void push (const juce::AudioBuffer<float>& buffer) noexcept
    {
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numChannels == 0 || numSamples == 0)
            return;

        const float scale = 1.0f / (float) numChannels;
        const auto scope = fifo.write (std::min (numSamples, fifo.getFreeSpace()));

        auto copy = [&] (int dest, int count, int srcOffset) {
            for (int i = 0; i < count; ++i)
            {
                float sum = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                    sum += buffer.getReadPointer (ch)[srcOffset + i];
                data[(size_t) (dest + i)] = sum * scale;
            }
        };

        copy (scope.startIndex1, scope.blockSize1, 0);
        copy (scope.startIndex2, scope.blockSize2, scope.blockSize1);
    }

    // Message thread: copies up to maxSamples into dest and returns how many were read.
    int pull (float* dest, int maxSamples) noexcept
    {
        const auto scope = fifo.read (std::min (maxSamples, fifo.getNumReady()));
        std::copy_n (data.data() + scope.startIndex1, scope.blockSize1, dest);
        std::copy_n (data.data() + scope.startIndex2, scope.blockSize2, dest + scope.blockSize1);
        return scope.blockSize1 + scope.blockSize2;
    }

private:
    juce::AbstractFifo fifo { capacity };
    std::array<float, capacity> data {};
};
} // namespace harmeq
