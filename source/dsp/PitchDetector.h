#pragma once

#include "SvfFilter.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <vector>

namespace harmeq
{
// Monophonic pitch detector (YIN: de Cheveigné & Kawahara, 2002) for the audio thread.
//
// The input is high-passed, low-passed and decimated to ~11–12 kHz (plenty for fundamentals up to 2 kHz
// and ~4× cheaper), then analysed every ~5 ms over a window of two periods of the lowest allowed pitch.
// Raw estimates are conditioned before use:
//   - confidence gate: 1 − (YIN aperiodicity) must reach the threshold;
//   - octave guard: a median of the last three estimates, so a one-hop octave error never gets through;
//   - hold-over: a couple of failed hops (a consonant, a pick scrape) don't drop the pitch.
// All memory is allocated in prepare(); push() is real-time safe.
class PitchDetector
{
public:
    struct Result
    {
        float f0Hz = 0.0f;
        float confidence = 0.0f; // of the latest analysis frame, 0–1
        bool voiced = false;
    };

    static constexpr float lowestSupportedHz = 30.0f;

    void prepare (double sampleRate);
    void reset();

    void setRange (float minHz, float maxHz) noexcept;
    void setThreshold (float minConfidence) noexcept { threshold = minConfidence; }

    // Feeds mono audio at the host rate; runs an analysis whenever a hop's worth has arrived.
    void push (const float* samples, int numSamples) noexcept;

    const Result& getResult() const noexcept { return result; }

    // How far (host samples) a detected pitch change lags the audio, i.e. the lookahead delay that lines
    // bands up with it. Measured: a new note is accepted once it fills the whole analysis span
    // (two periods of the lowest Detect pitch); mixed frames before that fail the confidence gate.
    int getLatencySamples() const noexcept;

    // The largest getLatencySamples() can get at this sample rate (Detect low at its minimum).
    int getMaxLatencySamples() const noexcept;

    double getAnalysisRate() const noexcept { return analysisRate; }

private:
    void analyse() noexcept;
    void condition (float f0Hz, float confidence, bool rawVoiced) noexcept;

    double sampleRate = 48000.0, analysisRate = 12000.0;
    int decimation = 4, decimationPhase = 0;

    SvfCoefficients highPass, antiAlias1, antiAlias2;
    SvfState highPassState, antiAliasState1, antiAliasState2;

    std::vector<float> ring, frame, diff, rawDiff, fftA, fftB;
    std::unique_ptr<juce::dsp::FFT> fft;
    int fftSize = 1024;
    float fftScale = 1.0f;
    int ringPos = 0, hopSize = 60, samplesSinceHop = 0, filled = 0;
    int tauMin = 6, tauMax = 240, largestTau = 500;
    float threshold = 0.5f;

    std::array<float, 3> recentLog2 {};
    int recentCount = 0, failedHops = 0, goodHops = 0;
    Result result;
};
} // namespace harmeq
