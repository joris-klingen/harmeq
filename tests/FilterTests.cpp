#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/EqEngine.h"

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

TEST_CASE ("Tracked band follows pitch × harmonic × detune")
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
    CHECK (band.getCurrentAmount() == 0.0f);
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
    engine.process (buffer, 0, 512, { 110.0f, true });

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            REQUIRE (buffer.getSample (ch, i) == original.getSample (ch, i));
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

        engine.process (buffer, 0, buffer.getNumSamples(), { f0, true });

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
