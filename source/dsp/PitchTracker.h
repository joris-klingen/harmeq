#pragma once

#include "EqBand.h"
#include "MidiNoteTracker.h"

namespace harmeq
{
// Turns the selected pitch source into the pitches the bands follow.
//
// Mono (Last Note / Lowest Note, and the audio sources): one pitch in slot 0, with transpose, glide
// (in the log-frequency domain, so glides are uniform in semitones) and the no-pitch behaviour.
//
// Poly (MIDI only): one slot per held note, up to maxVoices. A note keeps its slot while held, so its
// bands never jump to another note; released slots fade out at their last pitch. With No-Pitch Mode
// = Hold, releasing every key keeps the last chord until the next note.
//
// Main input / Sidechain: the caller runs a PitchDetector and hands its result to setDetectedPitch()
// before each advance(); it is treated like a single note (transpose and glide apply).
class PitchTracker
{
public:
    struct Settings
    {
        PitchSource source = PitchSource::midi;
        MidiMode midiMode = MidiMode::lastNote;
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

    // Latest detector output for the audio sources (ignored when the source is MIDI).
    void setDetectedPitch (float f0Hz, bool voiced) noexcept
    {
        detectedHz = f0Hz;
        detectedVoiced = voiced && f0Hz > 0.0f;
    }

    // Advances by numSamples and returns the pitches to use for that span.
    PitchSet advance (int numSamples) noexcept;

    bool isPoly() const noexcept { return settings.source == PitchSource::midi && settings.midiMode == MidiMode::poly; }

    const MidiNoteTracker& getMidi() const noexcept { return midi; }

private:
    PitchState advanceMono (int numSamples) noexcept;
    PitchSet advancePoly() noexcept;
    bool sourcePitch (float& log2Hz) const noexcept;
    float noteLog2 (int note) const noexcept;

    Settings settings;
    MidiNoteTracker midi;
    double sampleRate = 44100.0;

    float detectedHz = 0.0f;
    bool detectedVoiced = false;

    // Mono state
    float currentLog2 = 0.0f, lastVoicedLog2 = 0.0f;
    bool hasCurrent = false, hasHistory = false, wasBypassed = false;

    // Poly state. Slot "notes" are MIDI notes, or fallbackNote for the fallback pitch.
    static constexpr int noNote = -1, fallbackNote = 1000;
    std::array<int, maxVoices> slotNote {}, chord {};
    std::array<float, maxVoices> slotLog2 {};
    std::array<juce::uint32, maxVoices> slotFreedAt {};
    juce::uint32 releaseCounter = 0;
    int chordSize = 0;
};
} // namespace harmeq
