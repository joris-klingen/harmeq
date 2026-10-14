#pragma once

#include "EqBand.h"
#include "MidiNoteTracker.h"

namespace harmeq
{
// Turns the selected pitch source into the f0 the bands follow: applies transpose, glide (in the
// log-frequency domain, so glides are uniform in semitones) and the no-pitch behaviour.
// Audio pitch detection (main/sidechain) is not implemented yet; those sources currently report no pitch.
class PitchTracker
{
public:
    struct Settings
    {
        PitchSource source = PitchSource::midi;
        NoPitchMode noPitchMode = NoPitchMode::hold;
        float glideMs = 0.0f;
        float fallbackHz = 110.0f;
        float pitchBendRange = 2.0f; // semitones
        float transpose = 0.0f;      // semitones
    };

    void prepare (double sampleRate);
    void reset();

    void setSettings (const Settings& s) noexcept { settings = s; }
    void handleMidi (const juce::MidiMessage& m) noexcept { midi.handle (m); }

    // Advances the glide by numSamples and returns the pitch to use for that span.
    PitchState advance (int numSamples) noexcept;

    const MidiNoteTracker& getMidi() const noexcept { return midi; }

private:
    bool sourcePitch (float& log2Hz) const noexcept;

    Settings settings;
    MidiNoteTracker midi;
    double sampleRate = 44100.0;

    float currentLog2 = 0.0f, lastVoicedLog2 = 0.0f;
    bool hasCurrent = false, hasHistory = false, wasBypassed = false;
};
} // namespace harmeq
