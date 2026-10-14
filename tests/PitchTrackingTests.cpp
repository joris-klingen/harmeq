#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/PitchTracker.h"

using namespace harmeq;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
constexpr double fs = 48000.0;

float noteHz (float note) { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }

void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    auto* p = apvts.getParameter (id);
    REQUIRE (p != nullptr);
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}
} // namespace

TEST_CASE ("MIDI note stack uses last-note priority")
{
    MidiNoteTracker t;
    CHECK_FALSE (t.hasNote());

    t.noteOn (60);
    t.noteOn (64);
    t.noteOn (67);
    CHECK (t.currentNote() == 67);

    t.noteOff (64); // releasing a non-current note changes nothing
    CHECK (t.currentNote() == 67);

    t.noteOff (67); // falls back to the newest note still held
    CHECK (t.currentNote() == 60);

    t.noteOn (60); // re-pressing a held note moves it to the top without duplicating it
    t.noteOff (60);
    CHECK_FALSE (t.hasNote());

    t.noteOn (50);
    t.noteOn (52);
    t.handle (juce::MidiMessage::allNotesOff (1));
    CHECK_FALSE (t.hasNote());
}

TEST_CASE ("MIDI messages: velocity-0 note-on and pitch wheel")
{
    MidiNoteTracker t;
    t.handle (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
    t.handle (juce::MidiMessage::noteOn (3, 62, (juce::uint8) 100)); // omni: any channel counts
    CHECK (t.currentNote() == 62);

    t.handle (juce::MidiMessage::noteOn (3, 62, (juce::uint8) 0));
    CHECK (t.currentNote() == 60);

    t.handle (juce::MidiMessage::pitchWheel (1, 16383));
    CHECK_THAT (t.pitchBend(), WithinAbs (1.0, 1e-6));
    t.handle (juce::MidiMessage::pitchWheel (1, 0));
    CHECK_THAT (t.pitchBend(), WithinAbs (-1.0, 1e-6));
    t.handle (juce::MidiMessage::pitchWheel (1, 8192));
    CHECK_THAT (t.pitchBend(), WithinAbs (0.0, 1e-9));
}

TEST_CASE ("Pitch tracker converts MIDI to frequency with bend and transpose")
{
    PitchTracker pt;
    pt.prepare (fs);

    PitchTracker::Settings s;
    s.source = PitchSource::midi;
    s.pitchBendRange = 2.0f;
    pt.setSettings (s);

    pt.handleMidi (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100));
    CHECK_THAT (pt.advance (32).f0Hz, WithinRel (440.0f, 1e-5f));

    pt.handleMidi (juce::MidiMessage::pitchWheel (1, 16383));
    CHECK_THAT (pt.advance (32).f0Hz, WithinRel (noteHz (71.0f), 1e-5f));

    s.transpose = -12.0f;
    pt.setSettings (s);
    CHECK_THAT (pt.advance (32).f0Hz, WithinRel (noteHz (59.0f), 1e-5f));
}

TEST_CASE ("Glide moves in log-frequency with the set time constant")
{
    PitchTracker pt;
    pt.prepare (fs);

    PitchTracker::Settings s;
    s.source = PitchSource::midi;
    s.glideMs = 100.0f;
    pt.setSettings (s);

    pt.handleMidi (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100));
    CHECK_THAT (pt.advance (32).f0Hz, WithinRel (noteHz (57.0f), 1e-5f)); // first note: no glide

    pt.handleMidi (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100));

    // After one time constant, a one-pole glide has covered 1 - 1/e of the 12 semitones.
    PitchState p;
    for (int i = 0; i < (int) (0.1 * fs) / 32; ++i)
        p = pt.advance (32);

    const float semis = 12.0f * std::log2 (p.f0Hz / noteHz (57.0f));
    CHECK_THAT (semis, WithinAbs (12.0 * (1.0 - std::exp (-1.0)), 0.05));
}

TEST_CASE ("No-pitch modes")
{
    PitchTracker pt;
    pt.prepare (fs);

    PitchTracker::Settings s;
    s.source = PitchSource::midi;
    s.fallbackHz = 100.0f;

    SECTION ("Hold uses the fallback before any note, then keeps the last note")
    {
        s.noPitchMode = NoPitchMode::hold;
        pt.setSettings (s);

        auto p = pt.advance (32);
        CHECK (p.voiced);
        CHECK_THAT (p.f0Hz, WithinRel (100.0f, 1e-5f));

        pt.handleMidi (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100));
        pt.advance (32);
        pt.handleMidi (juce::MidiMessage::noteOff (1, 69));
        p = pt.advance (32);
        CHECK (p.voiced);
        CHECK_THAT (p.f0Hz, WithinRel (440.0f, 1e-5f));
    }

    SECTION ("Fallback returns to the fallback pitch on release")
    {
        s.noPitchMode = NoPitchMode::fallback;
        pt.setSettings (s);

        pt.handleMidi (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100));
        pt.advance (32);
        pt.handleMidi (juce::MidiMessage::noteOff (1, 69));
        CHECK_THAT (pt.advance (32).f0Hz, WithinRel (100.0f, 1e-5f));
    }

    SECTION ("Bypass bands reports unvoiced and snaps to the next note")
    {
        s.noPitchMode = NoPitchMode::bypassBands;
        s.glideMs = 200.0f;
        pt.setSettings (s);

        CHECK_FALSE (pt.advance (32).voiced);

        pt.handleMidi (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100));
        pt.advance (32);
        pt.handleMidi (juce::MidiMessage::noteOff (1, 69));
        CHECK_FALSE (pt.advance (32).voiced);

        pt.handleMidi (juce::MidiMessage::noteOn (1, 45, (juce::uint8) 100));
        const auto p = pt.advance (32);
        CHECK (p.voiced);
        CHECK_THAT (p.f0Hz, WithinRel (110.0f, 1e-5f));
    }

    SECTION ("Audio sources report no pitch until detection exists")
    {
        s.source = PitchSource::sidechain;
        s.noPitchMode = NoPitchMode::bypassBands;
        pt.setSettings (s);
        pt.handleMidi (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100));
        CHECK_FALSE (pt.advance (32).voiced);
    }
}

TEST_CASE ("Processor: MIDI note moves a tracked band onto its harmonic")
{
    juce::ScopedJuceInitialiser_GUI juce;
    HarmEQAudioProcessor proc;
    auto& apvts = proc.getValueTreeState();

    setParam (apvts, ParamIDs::source, (float) PitchSource::midi);
    setParam (apvts, ParamIDs::glide, 0.0f);
    setParam (apvts, ParamIDs::noPitchMode, (float) NoPitchMode::bypassBands);
    setParam (apvts, ParamIDs::band (0, "harmonic"), 2.0f);
    setParam (apvts, ParamIDs::band (0, "gain"), 12.0f);
    setParam (apvts, ParamIDs::band (0, "q"), 8.0f);

    constexpr int blockSize = 512;
    proc.setPlayConfigDetails (2, 2, fs, blockSize);
    proc.prepareToPlay (fs, blockSize);

    // Probe at 880 Hz: the 2nd harmonic of A4.
    constexpr double probeHz = 880.0;
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    int sampleIndex = 0;

    auto runBlocks = [&] (int numBlocks) {
        double inSq = 0.0, outSq = 0.0;

        for (int b = 0; b < numBlocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto x = (float) std::sin (2.0 * std::numbers::pi * probeHz * (sampleIndex + i) / fs);
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, x);
                inSq += (double) x * x;
            }

            sampleIndex += blockSize;
            proc.processBlock (buffer, midi);
            midi.clear();

            for (int i = 0; i < blockSize; ++i)
                outSq += (double) buffer.getSample (0, i) * buffer.getSample (0, i);
        }

        return 10.0 * std::log10 (outSq / inSq);
    };

    // No note held and Bypass Bands: transparent.
    CHECK_THAT (runBlocks (20), WithinAbs (0.0, 0.01));

    // Note-on mid-block; let the band settle, then measure.
    midi.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 200);
    runBlocks (20);
    CHECK_THAT (runBlocks (20), WithinAbs (12.0, 0.1));
    CHECK_THAT (proc.getCurrentPitchHz(), WithinRel (440.0f, 1e-5f));

    // A different note moves the band away from the probe.
    midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
    runBlocks (20);
    CHECK (runBlocks (20) < 1.0);
}
