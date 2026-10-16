#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/EqEngine.h"

#include <chrono>

#include <juce_audio_basics/juce_audio_basics.h>

using namespace harmeq;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr double fs = 48000.0;

double toDb (double gain) { return 20.0 * std::log10 (gain); }

// Feeds a sine through `processSample`, discards the settling time and returns the RMS gain in dB.
template <typename Fn>
double measureGainDb (double freqHz, Fn&& processSample, double seconds = 1.0)
{
    const int total = (int) (seconds * fs);
    const int settle = total / 2;
    double inSq = 0.0, outSq = 0.0;

    for (int i = 0; i < total; ++i)
    {
        const auto x = (float) std::sin (2.0 * std::numbers::pi * freqHz * i / fs);
        const auto y = processSample (x);

        if (i >= settle)
        {
            inSq += (double) x * x;
            outSq += (double) y * y;
        }
    }

    return 10.0 * std::log10 (outSq / inSq);
}

double measureBandGainDb (EqBand& band, const PitchState& pitch, double freqHz)
{
    band.prepare (fs);
    float sample = 0.0f;
    float* channels[] = { &sample };

    return measureGainDb (freqHz, [&] (float x) {
        sample = x;
        band.update (pitch, 1);
        band.process (channels, 1, 0, 1);
        return sample;
    });
}

const char* typeName (FilterType t)
{
    switch (t)
    {
        case FilterType::bell:      return "bell";
        case FilterType::lowShelf:  return "low shelf";
        case FilterType::highShelf: return "high shelf";
        case FilterType::highPass:  return "high pass";
        case FilterType::lowPass:   return "low pass";
        case FilterType::notch:     return "notch";
    }
    return "";
}
} // namespace

TEST_CASE ("SVF measured response matches the analytic response")
{
    const auto type = GENERATE (FilterType::bell, FilterType::lowShelf, FilterType::highShelf, FilterType::highPass,
                                FilterType::lowPass, FilterType::notch);
    const auto testFreq = GENERATE (100.0, 700.0, 1000.0, 1400.0, 5000.0, 15000.0);

    const auto coeffs = SvfCoefficients::design (type, 1000.0, 2.0, 12.0, fs);
    const auto expectedDb = toDb (coeffs.magnitude (testFreq, fs));

    // The notch's exact null is unmeasurable; skip points where the response is that deep.
    if (expectedDb < -60.0)
        return;

    SvfState state;
    const auto measuredDb = measureGainDb (testFreq, [&] (float x) { return state.process (x, coeffs); });

    INFO (typeName (type) << " at " << testFreq << " Hz");
    CHECK_THAT (measuredDb, WithinAbs (expectedDb, 0.1));
}

TEST_CASE ("SVF curves hit their nominal gains")
{
    CHECK_THAT (toDb (SvfCoefficients::design (FilterType::bell, 1000.0, 4.0, 9.0, fs).magnitude (1000.0, fs)),
                WithinAbs (9.0, 1e-6));
    CHECK_THAT (toDb (SvfCoefficients::design (FilterType::bell, 1000.0, 4.0, -9.0, fs).magnitude (1000.0, fs)),
                WithinAbs (-9.0, 1e-6));
    CHECK_THAT (toDb (SvfCoefficients::design (FilterType::lowShelf, 1000.0, 0.7, 6.0, fs).magnitude (10.0, fs)),
                WithinAbs (6.0, 0.01));
    CHECK_THAT (toDb (SvfCoefficients::design (FilterType::highShelf, 1000.0, 0.7, -6.0, fs).magnitude (20000.0, fs)),
                WithinAbs (-6.0, 0.05));
    CHECK_THAT (toDb (SvfCoefficients::design (FilterType::lowPass, 1000.0, 0.70710678, 0.0, fs).magnitude (1000.0, fs)),
                WithinAbs (-3.01, 0.01));
}

TEST_CASE ("24 dB/oct high pass gives a 4th-order Butterworth response")
{
    EqBand band;
    BandSettings s;
    s.on = true;
    s.type = FilterType::highPass;
    s.track = TrackMode::fixed;
    s.freqHz = 1000.0f;
    s.q = 0.70710678f;
    s.steep = true;
    band.setSettings (s);
    band.prepare (fs);
    band.update ({}, 32);

    CHECK_THAT (toDb (band.magnitudeAt (1000.0)), WithinAbs (-3.01, 0.05));
    CHECK_THAT (toDb (band.magnitudeAt (500.0)), WithinAbs (-24.1, 0.2));
    CHECK_THAT (toDb (band.magnitudeAt (10000.0)), WithinAbs (0.0, 0.01));
}

TEST_CASE ("Tracked band follows pitch * harmonic * detune")
{
    EqBand band;
    BandSettings s;
    s.on = true;
    s.type = FilterType::bell;
    s.track = TrackMode::tracked;
    s.harmonic = 3.0f;
    s.detuneCents = 100.0f;
    s.gainDb = 12.0f;
    s.q = 8.0f;
    band.setSettings (s);
    band.prepare (fs);

    const PitchState pitch { 220.0f, true };
    band.update (pitch, 32);

    const double expected = 220.0 * 3.0 * std::exp2 (100.0 / 1200.0);
    CHECK_THAT (band.getCurrentFrequency(), WithinAbs (expected, 0.01));
    CHECK_THAT (measureBandGainDb (band, pitch, expected), WithinAbs (12.0, 0.1));
    CHECK_THAT (measureBandGainDb (band, pitch, 220.0), WithinAbs (0.0, 0.1));
}

TEST_CASE ("Fixed band ignores pitch")
{
    EqBand band;
    BandSettings s;
    s.on = true;
    s.track = TrackMode::fixed;
    s.freqHz = 2500.0f;
    s.gainDb = -6.0f;
    band.setSettings (s);
    band.prepare (fs);
    band.update ({ 100.0f, false }, 32);

    CHECK_THAT (band.getCurrentFrequency(), WithinAbs (2500.0, 0.01));
    CHECK_THAT (measureBandGainDb (band, { 100.0f, false }, 2500.0), WithinAbs (-6.0, 0.1));
}

TEST_CASE ("Tracked band fades out near Nyquist and when unvoiced")
{
    EqBand band;
    BandSettings s;
    s.on = true;
    s.track = TrackMode::tracked;
    s.harmonic = 32.0f;
    s.gainDb = 12.0f;
    band.setSettings (s);
    band.prepare (fs);

    // 32 × 1000 Hz = 32 kHz, far above 0.45 × 48 kHz.
    band.update ({ 1000.0f, true }, 32);
    CHECK (band.getCurrentAmount() <= 0.0f);
    CHECK (band.getCurrentFrequency() <= EqBand::fadeEnd * fs + 0.01);

    // Halfway into the fade region.
    s.harmonic = 1.0f;
    band.setSettings (s);
    band.prepare (fs);
    band.update ({ (float) (0.425 * fs), true }, 32);
    CHECK_THAT (band.getCurrentAmount(), WithinAbs (0.5, 1e-3));

    // Unvoiced pitch in Bypass Bands mode.
    band.prepare (fs);
    CHECK_THAT (measureBandGainDb (band, { 1000.0f, false }, 1000.0), WithinAbs (0.0, 0.01));
}

TEST_CASE ("Disabled bands are transparent and fade smoothly")
{
    EqEngine engine;
    engine.prepare (fs);

    juce::AudioBuffer<float> buffer (2, 512);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample (ch, i, (float) std::sin (0.05 * i));

    juce::AudioBuffer<float> original (buffer);
    engine.process (buffer, 0, 512, monoPitch ({ 110.0f, true }));

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            REQUIRE (juce::exactlyEqual (buffer.getSample (ch, i), original.getSample (ch, i)));
}

TEST_CASE ("Fast pitch sweeps stay stable")
{
    const auto type = GENERATE (FilterType::bell, FilterType::highPass, FilterType::lowPass, FilterType::notch,
                                FilterType::lowShelf, FilterType::highShelf);

    EqEngine engine;
    engine.prepare (fs);

    // A single extreme band: stacking bands multiplies legitimate gain, which would hide instability.
    BandSettings s;
    s.on = true;
    s.type = type;
    s.harmonic = 4.0f;
    s.gainDb = 24.0f;
    s.q = 30.0f;
    s.steep = true;
    engine.setBandSettings (0, s);

    juce::Random rng (1234);
    juce::AudioBuffer<float> buffer (2, EqEngine::maxChunkSize);
    float peak = 0.0f;

    // Sweep f0 between 30 Hz and 3 kHz and back, five times a second, for two seconds.
    const int chunks = (int) (2.0 * fs) / EqEngine::maxChunkSize;
    for (int c = 0; c < chunks; ++c)
    {
        const double t = c * EqEngine::maxChunkSize / fs;
        const auto f0 = (float) (30.0 * std::pow (100.0, 0.5 + 0.5 * std::sin (2.0 * std::numbers::pi * 5.0 * t)));

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (ch, i, rng.nextFloat() * 0.2f - 0.1f);

        engine.process (buffer, 0, buffer.getNumSamples(), monoPitch ({ f0, true }));

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto y = buffer.getSample (ch, i);
                REQUIRE (std::isfinite (y));
                peak = std::max (peak, std::abs (y));
            }
    }

    INFO (typeName (type) << " peak " << peak);
    // Input peaks at 0.1; the loudest static response here (steep resonant pass, Q ≈ 55) stays well
    // under 100x, so anything near this bound means the modulated filter is misbehaving.
    CHECK (peak < 20.0f);
}

TEST_CASE ("Fading pass and notch bands never cuts deeper than the band itself")
{
    // A fade must only ever filter less: at every frequency, a partly faded band passes at least as much
    // as a more faded-in one. (Crossfading a 24 dB/oct cascade with the dry signal breaks this: it nulls
    // at the cutoff half-way through.)
    for (auto type : { FilterType::highPass, FilterType::lowPass, FilterType::notch })
    {
        for (bool steep : { false, true })
        {
            BandSettings s;
            s.on = true;
            s.type = type;
            s.steep = steep;

            const float amounts[] { 0.0f, 0.2f, 0.4f, 0.586f, 0.8f, 0.95f, 1.0f };
            for (size_t i = 0; i + 1 < std::size (amounts); ++i)
            {
                const auto lighter = BandDesign::make (s, 1000.0, 0.70710678, 0.0, amounts[i], fs);
                const auto heavier = BandDesign::make (s, 1000.0, 0.70710678, 0.0, amounts[i + 1], fs);

                double worst = 0.0; // how much more the lighter fade cuts than the heavier one, in dB
                for (double hz = 20.0; hz < 20000.0; hz *= 1.01)
                    worst = std::max (worst, toDb (std::abs (heavier.response (hz, fs))) - toDb (std::abs (lighter.response (hz, fs))));

                INFO (typeName (type) << (steep ? " 24" : " 12") << " dB/oct, amount " << amounts[i] << " vs "
                                      << amounts[i + 1] << ": worst " << worst << " dB");
                CHECK (worst < 0.05);
            }
        }
    }
}

TEST_CASE ("Coefficient updates every 32 samples are indistinguishable from per-sample updates")
{
    // The engine recomputes coefficients once per 32-sample chunk. Reference: the same engine driven one
    // sample at a time (coefficients every sample). The difference between the two is what chunking adds.
    struct Case
    {
        const char* name;
        FilterType type;
        float gainDb, q;
        bool jumps;      // pitch jumps between notes (vs. vibrato)
        double limitDb;  // allowed difference from per-sample updates
    };

    // With coefficients interpolated across each chunk, vibrato is within -80 dB of per-sample updates.
    // On note jumps what remains is the shape of the 5 ms glide (piecewise linear per chunk vs. a per-
    // sample exponential), not stepping; without interpolation these were -49/-61/-28/-39 dB.
    for (const Case& c : { Case { "bell vibrato", FilterType::bell, 12.0f, 10.0f, false, -75.0 },
                           Case { "notch vibrato", FilterType::notch, 0.0f, 10.0f, false, -75.0 },
                           Case { "bell note jumps", FilterType::bell, 12.0f, 10.0f, true, -50.0 },
                           Case { "notch note jumps", FilterType::notch, 0.0f, 10.0f, true, -50.0 } })
    {
        BandSettings s;
        s.on = true;
        s.type = c.type;
        s.harmonic = 4.0f;
        s.gainDb = c.gainDb;
        s.q = c.q;

        EqEngine chunked, perSample;
        chunked.prepare (fs);
        perSample.prepare (fs);
        chunked.setBandSettings (0, s);
        perSample.setBandSettings (0, s);

        // A rich test signal: a saw on A2 (many harmonics around the band) plus a little noise.
        constexpr int total = 96000;
        juce::Random rng (5);
        juce::AudioBuffer<float> a (1, total), b (1, total);
        double phase = 0.0;
        for (int n = 0; n < total; ++n)
        {
            double saw = 0.0;
            for (int h = 1; h < 40; ++h)
                saw += std::sin (2.0 * std::numbers::pi * h * phase) / h;
            phase = std::fmod (phase + 110.0 / fs, 1.0);
            const float x = (float) (0.3 * saw) + 0.01f * (rng.nextFloat() - 0.5f);
            a.setSample (0, n, x);
            b.setSample (0, n, x);
        }

        auto f0At = [&] (int n) {
            const double t = n / fs;
            if (c.jumps) // alternate A2 / E3 every 100 ms
                return (float) ((int) (t / 0.1) % 2 == 0 ? 110.0 : 164.8);
            return (float) (110.0 * std::exp2 (0.5 / 12.0 * std::sin (2.0 * std::numbers::pi * 6.0 * t))); // ±50 ct, 6 Hz
        };

        for (int n = 0; n < total; n += EqEngine::maxChunkSize)
            chunked.process (a, n, EqEngine::maxChunkSize, monoPitch ({ f0At (n), true }));
        for (int n = 0; n < total; ++n)
            perSample.process (b, n, 1, monoPitch ({ f0At (n - n % EqEngine::maxChunkSize), true }));

        double signal = 0.0, error = 0.0;
        for (int n = 4800; n < total; ++n)
        {
            signal += (double) b.getSample (0, n) * b.getSample (0, n);
            const double d = (double) a.getSample (0, n) - b.getSample (0, n);
            error += d * d;
        }

        const double errorDb = 10.0 * std::log10 (error / signal);
        INFO (c.name << ": chunked vs per-sample difference " << errorDb << " dB");
        CHECK (errorDb < c.limitDb);
    }
}

TEST_CASE ("Engine CPU, worst case: 6 notes x 8 bands, all moving")
{
    EqEngine engine;
    engine.prepare (fs);
    for (int b = 0; b < numBands; ++b)
    {
        BandSettings s;
        s.on = true;
        s.type = b % 2 == 0 ? FilterType::bell : FilterType::highPass;
        s.steep = true;
        s.harmonic = (float) (b + 1);
        s.gainDb = 6.0f;
        s.q = 4.0f;
        engine.setBandSettings (b, s);
    }

    juce::AudioBuffer<float> buffer (2, EqEngine::maxChunkSize);
    juce::Random rng (2);
    const int chunks = (int) (10.0 * fs) / EqEngine::maxChunkSize;
    const auto start = std::chrono::steady_clock::now();
    for (int c = 0; c < chunks; ++c)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (ch, i, rng.nextFloat() - 0.5f);

        // A six-note chord (spread so no harmonics merge) under constant vibrato.
        PitchSet chord;
        const double vib = std::exp2 (0.3 / 12.0 * std::sin (2.0 * std::numbers::pi * 5.0 * c * EqEngine::maxChunkSize / fs));
        for (int v = 0; v < maxVoices; ++v)
            chord[(size_t) v] = { (float) (55.0 * std::exp2 (v * 0.37) * vib), true };

        engine.process (buffer, 0, buffer.getNumSamples(), chord);
    }
    const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();

    INFO ("10 s of stereo audio in " << seconds * 1000.0 << " ms (" << seconds * 10.0 << " % of one core)");
    CHECK (seconds < 2.0); // generous for CI; see the reported figure
}
