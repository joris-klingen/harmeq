#include "EqBand.h"

#include <algorithm>

namespace harmeq
{
namespace
{
    bool sameCoefficients (const BandDesign& a, const BandDesign& b)
    {
        for (int s = 0; s < a.numStages; ++s)
        {
            const auto& x = a.coeffs[(size_t) s];
            const auto& y = b.coeffs[(size_t) s];
            if (! juce::exactlyEqual (x.g, y.g) || ! juce::exactlyEqual (x.k, y.k) || ! juce::exactlyEqual (x.m0, y.m0)
                || ! juce::exactlyEqual (x.m1, y.m1) || ! juce::exactlyEqual (x.m2, y.m2))
                return false;
        }
        return juce::exactlyEqual (a.wet, b.wet);
    }

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
    wasAudible = interpolate = false;
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

    if (isPassFilter (s.type))
    {
        // Pass filters fade by sliding their cutoff out of the way (high pass down to 10 Hz, low pass up
        // to just under Nyquist), so the response is always a proper filter. Crossfading against the dry
        // signal would cancel near the cutoff, where the filter's phase approaches 180°.
        if (amount < 1.0f)
        {
            const double away = s.type == FilterType::highPass ? 10.0 : 0.49 * sampleRate;
            f = std::exp (std::log (f) * amount + std::log (away) * (1.0 - amount));
        }
    }
    else
    {
        // A notch crossfaded with its input is exactly a notch of reduced depth (1 − amount·bandpass):
        // smooth, never boosting, never cancelling.
        d.wet = amount;
    }

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

    const auto next = BandDesign::make (settings, std::exp2 ((double) logFreq), std::exp2 ((double) logQ), gainDb,
                                        amount, sampleRate);

    // Interpolate across the chunk when the coefficients moved, unless the band was silent (it starts
    // fresh) or its structure changed (number of stages).
    previous = design;
    design = next;
    interpolate = wasAudible && previous.numStages == design.numStages && ! sameCoefficients (previous, design);
    wasAudible = isActive();
}

void EqBand::process (float* const* channels, int numChannels, int startSample, int numSamples) noexcept
{
    if (! isActive())
        return;

    numChannels = std::min (numChannels, maxChannels);
    const bool twoStages = design.numStages > 1;

    // Steady band: one set of coefficients for the whole chunk.
    if (! interpolate)
    {
        const bool crossfade = design.wet < 0.9999f;
        const float wet = design.wet;
        const auto& c0 = design.coeffs[0];
        const auto& c1 = design.coeffs[1];

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
        return;
    }

    // Moving band: glide from the previous chunk's coefficients to this chunk's, sample by sample.
    // Interpolating g and k and re-deriving a1..a3 tracks a per-sample redesign closely for the cost of
    // one division, instead of stepping every chunk.
    struct Ramp
    {
        double g, k, m0, m1, m2, dg, dk, dm0, dm1, dm2;
    };
    Ramp ramps[2];
    const double step = 1.0 / numSamples;
    for (int s = 0; s < design.numStages; ++s)
    {
        const auto& from = previous.coeffs[(size_t) s];
        const auto& to = design.coeffs[(size_t) s];
        ramps[s] = { from.g, from.k, from.m0, from.m1, from.m2,
                     (to.g - from.g) * step, (to.k - from.k) * step,
                     ((double) to.m0 - from.m0) * step, ((double) to.m1 - from.m1) * step, ((double) to.m2 - from.m2) * step };
    }
    double wet = previous.wet;
    const double dWet = ((double) design.wet - previous.wet) * step;

    for (int i = 0; i < numSamples; ++i)
    {
        SvfCoefficients c[2];
        for (int s = 0; s < design.numStages; ++s)
        {
            auto& r = ramps[s];
            r.g += r.dg; r.k += r.dk; r.m0 += r.dm0; r.m1 += r.dm1; r.m2 += r.dm2;
            const double a1 = 1.0 / (1.0 + r.g * (r.g + r.k));
            c[s].a1 = (float) a1;
            c[s].a2 = (float) (r.g * a1);
            c[s].a3 = (float) (r.g * r.g * a1);
            c[s].m0 = (float) r.m0;
            c[s].m1 = (float) r.m1;
            c[s].m2 = (float) r.m2;
        }
        wet += dWet;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float& sample = channels[ch][startSample + i];
            auto& st = state[(size_t) ch];
            const float dry = sample;
            float y = st[0].process (dry, c[0]);
            if (twoStages)
                y = st[1].process (y, c[1]);
            sample = wet < 0.9999 ? dry + (float) wet * (y - dry) : y;
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
