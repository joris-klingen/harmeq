#include "PitchDetector.h"

#include <algorithm>
#include <cmath>

namespace harmeq
{
namespace
{
    constexpr double targetAnalysisRate = 14000.0; // decimate down to at least this rate
    constexpr double hopSeconds = 0.005;
    constexpr float dipThreshold = 0.2f;    // YIN absolute threshold for picking the first dip
    constexpr float silenceRms = 1.0e-3f;   // -60 dBFS: below this the frame counts as silence
    constexpr int holdHops = 3;             // failed hops tolerated before the pitch is dropped
    constexpr int onsetHops = 2;            // good hops needed before a pitch is reported
} // namespace

void PitchDetector::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    decimation = std::max (1, (int) std::floor (sampleRate / targetAnalysisRate));
    analysisRate = sampleRate / decimation;
    hopSize = std::max (1, (int) std::round (hopSeconds * analysisRate));

    highPass = SvfCoefficients::design (FilterType::highPass, 20.0, 0.7071, 0.0, sampleRate);
    const double cutoff = std::min (0.38 * analysisRate, 0.45 * sampleRate);
    antiAlias1 = SvfCoefficients::design (FilterType::lowPass, cutoff, 0.5412, 0.0, sampleRate);
    antiAlias2 = SvfCoefficients::design (FilterType::lowPass, cutoff, 1.3066, 0.0, sampleRate);

    // Sized for the lowest pitch the parameters allow, so range changes never allocate.
    const int largestTau = (int) std::ceil (analysisRate / lowestSupportedHz) + 2;
    ring.assign ((size_t) (2 * largestTau + 2), 0.0f);
    frame.assign (ring.size(), 0.0f);
    diff.assign ((size_t) largestTau + 2, 0.0f);
    rawDiff.assign (diff.size(), 0.0f);

    // The correlation is done with an FFT big enough for the longest analysis span (no wrap-around).
    int order = 1;
    while ((1 << order) < (int) ring.size())
        ++order;
    fft = std::make_unique<juce::dsp::FFT> (order);
    fftSize = 1 << order;
    fftA.assign ((size_t) fftSize * 2, 0.0f);
    fftB.assign ((size_t) fftSize * 2, 0.0f);

    // Calibrate the transform pair's overall scaling (it differs between FFT back ends) with an impulse.
    fftA[0] = 1.0f;
    fftB[0] = 1.0f;
    fft->performRealOnlyForwardTransform (fftA.data());
    fft->performRealOnlyForwardTransform (fftB.data());
    for (int k = 0; k < fftSize; ++k)
    {
        const std::complex<float> a (fftA[(size_t) (2 * k)], fftA[(size_t) (2 * k + 1)]);
        const std::complex<float> b (fftB[(size_t) (2 * k)], fftB[(size_t) (2 * k + 1)]);
        const auto c = std::conj (a) * b;
        fftA[(size_t) (2 * k)] = c.real();
        fftA[(size_t) (2 * k + 1)] = c.imag();
    }
    fft->performRealOnlyInverseTransform (fftA.data());
    fftScale = std::abs (fftA[0]) > 1.0e-12f ? 1.0f / fftA[0] : 1.0f;

    setRange (50.0f, 1000.0f);
    reset();
}

void PitchDetector::reset()
{
    highPassState.reset();
    antiAliasState1.reset();
    antiAliasState2.reset();
    std::fill (ring.begin(), ring.end(), 0.0f);
    ringPos = samplesSinceHop = filled = decimationPhase = 0;
    recentCount = failedHops = goodHops = 0;
    result = {};
}

void PitchDetector::setRange (float minHz, float maxHz) noexcept
{
    float lo = std::max (lowestSupportedHz, std::min (minHz, maxHz));
    float hi = std::max (minHz, maxHz);
    hi = std::min (hi, (float) (0.25 * analysisRate));
    lo = std::min (lo, hi * 0.9f);

    tauMax = std::min ((int) std::ceil (analysisRate / lo) + 1, (int) diff.size() - 2);
    tauMin = std::max (2, (int) std::floor (analysisRate / hi));
}

int PitchDetector::getLatencySamples() const noexcept
{
    // The analysis span is 2·tauMax decimated samples; a pitch describes its middle.
    return (tauMax + hopSize) * decimation;
}

void PitchDetector::push (const float* samples, int numSamples) noexcept
{
    const int capacity = (int) ring.size();

    for (int i = 0; i < numSamples; ++i)
    {
        float x = highPassState.process (samples[i], highPass);
        x = antiAliasState1.process (x, antiAlias1);
        x = antiAliasState2.process (x, antiAlias2);

        if (++decimationPhase < decimation)
            continue;
        decimationPhase = 0;

        ring[(size_t) ringPos] = x;
        ringPos = (ringPos + 1) % capacity;
        filled = std::min (filled + 1, capacity);

        if (++samplesSinceHop >= hopSize)
        {
            samplesSinceHop = 0;
            analyse();
        }
    }
}

void PitchDetector::analyse() noexcept
{
    const int window = tauMax;          // integration window W
    const int span = window + tauMax;   // samples needed: x[0 .. W + tauMax)
    if (filled < span)
        return;

    // Unroll the newest `span` samples, oldest first.
    const int capacity = (int) ring.size();
    const int start = (ringPos - span + capacity) % capacity;
    for (int j = 0; j < span; ++j)
        frame[(size_t) j] = ring[(size_t) ((start + j) % capacity)];

    // Silence: no pitch, and no confidence.
    double energy = 0.0;
    for (int j = 0; j < window; ++j)
        energy += (double) frame[(size_t) j] * frame[(size_t) j];

    if (std::sqrt (energy / window) < silenceRms)
    {
        condition (0.0f, 0.0f, false);
        return;
    }

    // Cross-correlation r(τ) = Σ_{j<W} x_j x_{j+τ} for every τ at once, via FFT:
    // R = conj(FFT(first W samples)) · FFT(whole span).
    std::fill (fftA.begin(), fftA.end(), 0.0f);
    std::fill (fftB.begin(), fftB.end(), 0.0f);
    std::copy_n (frame.begin(), window, fftA.begin());
    std::copy_n (frame.begin(), span, fftB.begin());
    fft->performRealOnlyForwardTransform (fftA.data(), true);
    fft->performRealOnlyForwardTransform (fftB.data(), true);
    for (int k = 0; k <= fftSize / 2; ++k)
    {
        const std::complex<float> a (fftA[(size_t) (2 * k)], fftA[(size_t) (2 * k + 1)]);
        const std::complex<float> b (fftB[(size_t) (2 * k)], fftB[(size_t) (2 * k + 1)]);
        const auto c = std::conj (a) * b;
        fftA[(size_t) (2 * k)] = c.real();
        fftA[(size_t) (2 * k + 1)] = c.imag();
    }
    fft->performRealOnlyInverseTransform (fftA.data());

    // Difference function d(τ) = Σ (x_j − x_{j+τ})² = energy(0) + energy(τ) − 2·r(τ), with the shifted
    // window's energy updated incrementally.
    double shiftedEnergy = energy;
    diff[0] = 0.0f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        shiftedEnergy += (double) frame[(size_t) (window + tau - 1)] * frame[(size_t) (window + tau - 1)]
                         - (double) frame[(size_t) (tau - 1)] * frame[(size_t) (tau - 1)];

        const double r = (double) fftA[(size_t) tau] * fftScale;
        diff[(size_t) tau] = (float) std::max (0.0, energy + shiftedEnergy - 2.0 * r);
        rawDiff[(size_t) tau] = diff[(size_t) tau];
    }

    // Cumulative mean normalised difference d'(τ).
    double running = 0.0;
    diff[0] = 1.0f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        running += diff[(size_t) tau];
        diff[(size_t) tau] = running > 0.0 ? (float) (diff[(size_t) tau] * tau / running) : 1.0f;
    }

    // First dip under the threshold (walked down to its local minimum), else the global minimum.
    int best = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (diff[(size_t) tau] < dipThreshold)
        {
            while (tau + 1 < tauMax && diff[(size_t) tau + 1] < diff[(size_t) tau])
                ++tau;
            best = tau;
            break;
        }
    }

    if (best < 0)
    {
        best = tauMin;
        for (int tau = tauMin + 1; tau < tauMax; ++tau)
            if (diff[(size_t) tau] < diff[(size_t) best])
                best = tau;
    }

    // Parabolic interpolation for sub-sample period accuracy, on the raw difference function: the
    // normalised one is skewed by its running mean, which biases short periods sharp.
    double period = best;
    if (best > 1 && best < tauMax)
    {
        const double y0 = rawDiff[(size_t) best - 1], y1 = rawDiff[(size_t) best], y2 = rawDiff[(size_t) best + 1];
        const double denom = y0 - 2.0 * y1 + y2;
        if (std::abs (denom) > 1.0e-12)
            period += 0.5 * (y0 - y2) / denom;
    }

    const float confidence = std::clamp (1.0f - diff[(size_t) best], 0.0f, 1.0f);
    const float f0 = (float) (analysisRate / period);
    const bool inRange = best > tauMin && best < tauMax - 1;
    condition (f0, confidence, inRange && confidence >= threshold);
}

void PitchDetector::condition (float f0Hz, float confidence, bool rawVoiced) noexcept
{
    result.confidence = confidence;

    if (! rawVoiced)
    {
        goodHops = 0;
        if (++failedHops >= holdHops)
        {
            result.voiced = false;
            recentCount = 0;
        }
        return;
    }

    failedHops = 0;
    ++goodHops;

    // Median of the last three estimates (in log-frequency) rejects single-frame octave errors.
    if (recentCount < 3)
        recentLog2[(size_t) recentCount++] = std::log2 (f0Hz);
    else
    {
        recentLog2[0] = recentLog2[1];
        recentLog2[1] = recentLog2[2];
        recentLog2[2] = std::log2 (f0Hz);
    }

    float estimate = recentLog2[(size_t) recentCount - 1];
    if (recentCount == 3)
    {
        auto sorted = recentLog2;
        std::sort (sorted.begin(), sorted.end());
        estimate = sorted[1];
    }

    if (result.voiced || goodHops >= onsetHops)
    {
        result.voiced = true;
        result.f0Hz = std::exp2 (estimate);
    }
}
} // namespace harmeq
