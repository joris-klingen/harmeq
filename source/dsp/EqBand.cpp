#include "EqBand.h"

#include <algorithm>

namespace harmeq
{
namespace
{
    constexpr double freqSmoothingSeconds = 0.005;
    constexpr double paramSmoothingSeconds = 0.010;

    // Butterworth stage Qs for a 4th-order response; the user Q scales the resonant stage.
    constexpr double butterworthQ1 = 0.54119610, butterworthQ2 = 1.30656296, butterworthQ = 0.70710678;

    float smoothingAlpha (int numSamples, double seconds, double sampleRate)
    {
        return (float) (1.0 - std::exp (-numSamples / (seconds * sampleRate)));
    }

    bool isPassFilter (FilterType t) { return t == FilterType::highPass || t == FilterType::lowPass; }
    bool usesGain (FilterType t) { return t == FilterType::bell || t == FilterType::lowShelf || t == FilterType::highShelf; }
} // namespace

void EqBand::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    reset();
}

void EqBand::reset()
{
    for (auto& ch : state)
        for (auto& s : ch)
            s.reset();

    snapToTargets = true;
}

void EqBand::update (const PitchState& pitch, int numSamples)
{
    double freq;
    float pitchAmount = 1.0f;

    if (settings.track == TrackMode::tracked)
    {
        freq = (double) pitch.f0Hz * settings.harmonic * std::exp2 (settings.detuneCents / 1200.0);

        const double fadePos = (freq / sampleRate - fadeStart) / (fadeEnd - fadeStart);
        pitchAmount = pitch.voiced ? (float) std::clamp (1.0 - fadePos, 0.0, 1.0) : 0.0f;
        freq = std::clamp (freq, 10.0, fadeEnd * sampleRate);
    }
    else
    {
        freq = std::clamp ((double) settings.freqHz, 10.0, 0.49 * sampleRate);
    }

    const auto targetLogFreq = (float) std::log2 (freq);
    const auto targetLogQ = std::log2 (std::max (settings.q, 0.01f));
    amountTarget = settings.on ? pitchAmount : 0.0f;

    if (snapToTargets)
    {
        logFreq = targetLogFreq;
        gainDb = settings.gainDb;
        logQ = targetLogQ;
        amount = amountTarget;
        snapToTargets = false;
    }
    else
    {
        const auto fa = smoothingAlpha (numSamples, freqSmoothingSeconds, sampleRate);
        const auto pa = smoothingAlpha (numSamples, paramSmoothingSeconds, sampleRate);
        logFreq += fa * (targetLogFreq - logFreq);
        gainDb += pa * (settings.gainDb - gainDb);
        logQ += pa * (targetLogQ - logQ);
        amount += pa * (amountTarget - amount);

        if (amountTarget == 0.0f && amount < 1.0e-4f && amount != 0.0f)
        {
            // Fully faded out: clear filter memory so the band restarts cleanly when it comes back.
            amount = 0.0f;
            for (auto& ch : state)
                for (auto& s : ch)
                    s.reset();
        }
    }

    const double f = std::exp2 ((double) logFreq);
    const double q = std::exp2 ((double) logQ);

    if (usesGain (settings.type))
    {
        // Fading a gain-type band = scaling its gain in dB, which never introduces phase cancellation.
        numStages = 1;
        coeffs[0] = SvfCoefficients::design (settings.type, f, q, gainDb * amount, sampleRate);
    }
    else if (isPassFilter (settings.type) && settings.steep)
    {
        numStages = 2;
        coeffs[0] = SvfCoefficients::design (settings.type, f, butterworthQ1, 0.0, sampleRate);
        coeffs[1] = SvfCoefficients::design (settings.type, f, q * butterworthQ2 / butterworthQ, 0.0, sampleRate);
    }
    else
    {
        numStages = 1;
        coeffs[0] = SvfCoefficients::design (settings.type, f, q, 0.0, sampleRate);
    }
}

void EqBand::process (float* const* channels, int numChannels, int startSample, int numSamples) noexcept
{
    if (! isActive())
        return;

    // Gain-type bands already fold `amount` into their gain; the others crossfade against the dry signal.
    const bool crossfade = ! usesGain (settings.type) && amount < 0.9999f;
    numChannels = std::min (numChannels, maxChannels);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = channels[ch] + startSample;
        auto& st = state[(size_t) ch];

        for (int i = 0; i < numSamples; ++i)
        {
            const float dry = data[i];
            float y = st[0].process (dry, coeffs[0]);

            if (numStages > 1)
                y = st[1].process (y, coeffs[1]);

            data[i] = crossfade ? dry + amount * (y - dry) : y;
        }
    }
}

double EqBand::magnitudeAt (double freqHz) const
{
    if (! isActive())
        return 1.0;

    double m = coeffs[0].magnitude (freqHz, sampleRate);

    if (numStages > 1)
        m *= coeffs[1].magnitude (freqHz, sampleRate);

    if (! usesGain (settings.type) && amount < 0.9999f)
        return std::abs ((1.0 - amount) + amount * m); // approximate: ignores phase of the crossfade

    return m;
}
} // namespace harmeq
