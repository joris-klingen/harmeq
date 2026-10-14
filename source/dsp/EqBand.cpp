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

BandTarget EqBand::resolveTarget (const BandSettings& s, const PitchState& pitch, double sampleRate)
{
    BandTarget t;

    if (s.track == TrackMode::tracked)
    {
        const double freq = (double) pitch.f0Hz * s.harmonic * std::exp2 (s.detuneCents / 1200.0);
        const double fadePos = (freq / sampleRate - fadeStart) / (fadeEnd - fadeStart);
        t.amount = pitch.voiced ? (float) std::clamp (1.0 - fadePos, 0.0, 1.0) : 0.0f;
        t.freqHz = std::clamp (freq, 10.0, fadeEnd * sampleRate);
    }
    else
    {
        t.amount = 1.0f;
        t.freqHz = std::clamp ((double) s.freqHz, 10.0, 0.49 * sampleRate);
    }

    if (! s.on)
        t.amount = 0.0f;

    return t;
}

BandDesign EqBand::settledDesign (const BandSettings& s, const PitchState& pitch, double sampleRate)
{
    const auto t = resolveTarget (s, pitch, sampleRate);
    return BandDesign::make (s, t.freqHz, std::max (s.q, 0.01f), s.gainDb, t.amount, sampleRate);
}

BandDesign BandDesign::make (const BandSettings& s, double f, double q, double gainDb, float amount,
                             double sampleRate)
{
    BandDesign d;

    if (usesGain (s.type))
    {
        // Fading a gain-type band = scaling its gain in dB, which never introduces phase cancellation.
        d.coeffs[0] = SvfCoefficients::design (s.type, f, q, gainDb * amount, sampleRate);
        return d;
    }

    d.wet = amount;

    if (isPassFilter (s.type) && s.steep)
    {
        d.numStages = 2;
        d.coeffs[0] = SvfCoefficients::design (s.type, f, butterworthQ1, 0.0, sampleRate);
        d.coeffs[1] = SvfCoefficients::design (s.type, f, q * butterworthQ2 / butterworthQ, 0.0, sampleRate);
    }
    else
    {
        d.coeffs[0] = SvfCoefficients::design (s.type, f, q, 0.0, sampleRate);
    }

    return d;
}

std::complex<double> BandDesign::response (double freqHz, double sampleRate) const
{
    auto h = coeffs[0].response (freqHz, sampleRate);

    if (numStages > 1)
        h *= coeffs[1].response (freqHz, sampleRate);

    return (1.0 - wet) + (double) wet * h;
}

void EqBand::update (const PitchState& pitch, int numSamples)
{
    const auto target = resolveTarget (settings, pitch, sampleRate);
    const auto targetLogFreq = (float) std::log2 (target.freqHz);
    const auto targetLogQ = std::log2 (std::max (settings.q, 0.01f));
    amountTarget = target.amount;

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
        // A band coming back from silence starts at its new frequency and fades in, rather than
        // sweeping there (e.g. a voice slot taking over a new note).
        if (amount <= 0.0f && amountTarget > 0.0f)
        {
            logFreq = targetLogFreq;
            gainDb = settings.gainDb;
            logQ = targetLogQ;
        }

        const auto fa = smoothingAlpha (numSamples, freqSmoothingSeconds, sampleRate);
        const auto pa = smoothingAlpha (numSamples, paramSmoothingSeconds, sampleRate);
        logFreq += fa * (targetLogFreq - logFreq);
        gainDb += pa * (settings.gainDb - gainDb);
        logQ += pa * (targetLogQ - logQ);
        amount += pa * (amountTarget - amount);

        if (amountTarget <= 0.0f && amount > 0.0f && amount < 1.0e-4f)
        {
            // Fully faded out: clear filter memory so the band restarts cleanly when it comes back.
            amount = 0.0f;
            for (auto& ch : state)
                for (auto& s : ch)
                    s.reset();
        }
    }

    design = BandDesign::make (settings, std::exp2 ((double) logFreq), std::exp2 ((double) logQ), gainDb, amount,
                               sampleRate);
}

void EqBand::process (float* const* channels, int numChannels, int startSample, int numSamples) noexcept
{
    if (! isActive())
        return;

    const bool crossfade = design.wet < 0.9999f;
    const float wet = design.wet;
    const auto& c0 = design.coeffs[0];
    const auto& c1 = design.coeffs[1];
    const bool twoStages = design.numStages > 1;
    numChannels = std::min (numChannels, maxChannels);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = channels[ch] + startSample;
        auto& st = state[(size_t) ch];

        for (int i = 0; i < numSamples; ++i)
        {
            const float dry = data[i];
            float y = st[0].process (dry, c0);

            if (twoStages)
                y = st[1].process (y, c1);

            data[i] = crossfade ? dry + wet * (y - dry) : y;
        }
    }
}

double EqBand::magnitudeAt (double freqHz) const
{
    if (! isActive())
        return 1.0;

    return std::abs (design.response (freqHz, sampleRate));
}
} // namespace harmeq
