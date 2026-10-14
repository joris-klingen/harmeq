#include "SpectrumAnalyzer.h"

namespace harmeq
{
namespace
{
    constexpr int hopSize = SpectrumAnalyzer::fftSize / 4;
    constexpr float releaseAmount = 0.35f; // per analysis frame; peaks jump up immediately
} // namespace

SpectrumAnalyzer::SpectrumAnalyzer()
    : ring ((size_t) fftSize, 0.0f),
      pullBuffer (4096, 0.0f),
      fftData ((size_t) fftSize * 2, 0.0f),
      smoothedDb ((size_t) fftSize / 2 + 1, floorDb)
{
}

void SpectrumAnalyzer::clear()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    std::fill (smoothedDb.begin(), smoothedDb.end(), floorDb);
    writePos = newSamples = 0;
}

bool SpectrumAnalyzer::update (AnalyzerTap& tap, double newSampleRate)
{
    sampleRate = newSampleRate;

    for (;;)
    {
        const int n = tap.pull (pullBuffer.data(), (int) pullBuffer.size());
        if (n == 0)
            break;

        for (int i = 0; i < n; ++i)
        {
            ring[(size_t) writePos] = pullBuffer[(size_t) i];
            writePos = (writePos + 1) % fftSize;
        }

        newSamples += n;
    }

    if (newSamples < hopSize)
        return false;

    newSamples = 0;

    // Unroll the ring oldest-first, window, transform.
    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = ring[(size_t) ((writePos + i) % fftSize)];

    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    // A Hann-windowed sine of amplitude A peaks at A·N/4.
    const float norm = 4.0f / (float) fftSize;

    for (size_t b = 0; b < smoothedDb.size(); ++b)
    {
        const float db = juce::jmax (floorDb, juce::Decibels::gainToDecibels (fftData[b] * norm, floorDb));
        auto& s = smoothedDb[b];
        s = db > s ? db : s + releaseAmount * (db - s);
    }

    return true;
}

float SpectrumAnalyzer::levelDb (double f0, double f1) const
{
    const double binHz = sampleRate / fftSize;
    const double b0 = f0 / binHz, b1 = f1 / binHz;
    const int last = (int) smoothedDb.size() - 1;

    if (b1 - b0 < 1.0)
    {
        const double centre = juce::jlimit (0.0, (double) last, 0.5 * (b0 + b1));
        const int i = juce::jmin ((int) centre, last - 1);
        const float t = (float) (centre - i);
        return smoothedDb[(size_t) i] + t * (smoothedDb[(size_t) i + 1] - smoothedDb[(size_t) i]);
    }

    float peak = floorDb;
    for (int i = juce::jmax (0, (int) b0); i <= juce::jmin (last, (int) std::ceil (b1)); ++i)
        peak = juce::jmax (peak, smoothedDb[(size_t) i]);

    return peak;
}
} // namespace harmeq
