#pragma once

#include "SvfFilter.h"

#include <array>

namespace harmeq
{
struct PitchState
{
    float f0Hz = 110.0f;
    bool voiced = true; // false: tracked bands should fade out (No-Pitch Mode = Bypass Bands)
};

struct BandSettings
{
    bool on = false;
    FilterType type = FilterType::bell;
    TrackMode track = TrackMode::tracked;
    float harmonic = 1.0f;
    float detuneCents = 0.0f;
    float freqHz = 1000.0f; // used when track == fixed
    float gainDb = 0.0f;
    float q = 1.0f;
    bool steep = false; // 24 dB/oct for HP/LP
};

// Where a band should sit and how much of it applies (on/off × voiced × Nyquist fade), before smoothing.
struct BandTarget
{
    double freqHz = 1000.0;
    float amount = 0.0f;
};

// The filter a band runs for a given frequency, Q, gain and amount. The audio path and the response
// display both build bands through this, so the drawn curve is exactly what is heard.
struct BandDesign
{
    std::array<SvfCoefficients, 2> coeffs {};
    int numStages = 1;
    float wet = 1.0f; // < 1: crossfade against the dry signal (pass/notch types fading in or out)

    static BandDesign make (const BandSettings& s, double freqHz, double q, double gainDb, float amount,
                            double sampleRate);

    std::complex<double> response (double freqHz, double sampleRate) const;
};

// One EQ band: resolves its target frequency from the pitch, smooths frequency/gain/Q/amount per chunk,
// and runs one or two cascaded SVF stages per channel.
class EqBand
{
public:
    static constexpr int maxChannels = 2;

    static BandTarget resolveTarget (const BandSettings& s, const PitchState& pitch, double sampleRate);

    // Response of a band with these settings once fully settled (what the display draws).
    static BandDesign settledDesign (const BandSettings& s, const PitchState& pitch, double sampleRate);

    void prepare (double sampleRate);
    void reset();

    void setSettings (const BandSettings& s) noexcept { settings = s; }
    const BandSettings& getSettings() const noexcept { return settings; }

    // Call once per chunk before process(); numSamples is the chunk length (used for smoothing).
    void update (const PitchState& pitch, int numSamples);
    void process (float* const* channels, int numChannels, int startSample, int numSamples) noexcept;

    float getCurrentFrequency() const noexcept { return std::exp2 (logFreq); }
    float getCurrentAmount() const noexcept { return amount; }

    // Magnitude of the band's current (smoothed) state.
    double magnitudeAt (double freqHz) const;

    // Tracked bands fade out over this range (as a fraction of the sample rate) instead of
    // piling up against the clamp when the harmonic approaches Nyquist.
    static constexpr double fadeStart = 0.40, fadeEnd = 0.45;

private:
    bool isActive() const noexcept { return amount > 1.0e-4f || amountTarget > 0.0f; }

    BandSettings settings;
    double sampleRate = 44100.0;
    bool snapToTargets = true;

    float logFreq = 10.0f, gainDb = 0.0f, logQ = 0.0f, amount = 0.0f, amountTarget = 0.0f;

    BandDesign design;
    std::array<std::array<SvfState, 2>, maxChannels> state {};
};
} // namespace harmeq
