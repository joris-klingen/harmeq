#pragma once

#include "dsp/AnalyzerTap.h"

#include <juce_dsp/juce_dsp.h>

#include <vector>

namespace harmeq
{
// Message-thread FFT analyzer fed from an AnalyzerTap. Keeps a smoothed magnitude spectrum in dBFS
// (a full-scale sine reads 0 dB).
class SpectrumAnalyzer
{
public:
    static constexpr int fftOrder = 13; // 8192 points: ~6 Hz bins at 48 kHz, enough to separate low harmonics
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr float floorDb = -100.0f;

    SpectrumAnalyzer();

    // Pulls new audio from the tap; recomputes the spectrum when enough has arrived. Returns true if
    // the spectrum changed.
    bool update (AnalyzerTap& tap, double sampleRate);

    void clear();

    // Level at a frequency band [f0, f1] (the peak bin inside it, or interpolated when narrower than a bin).
    float levelDb (double f0, double f1) const;

private:
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };

    std::vector<float> ring, pullBuffer, fftData, smoothedDb;
    int writePos = 0, newSamples = 0;
    double sampleRate = 48000.0;
};
} // namespace harmeq
