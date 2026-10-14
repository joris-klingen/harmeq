#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/EqEngine.h"
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
    CHECK_THAT (pt.advance (32)[0].f0Hz, WithinRel (440.0f, 1e-5f));

    pt.handleMidi (juce::MidiMessage::pitchWheel (1, 16383));
    CHECK_THAT (pt.advance (32)[0].f0Hz, WithinRel (noteHz (71.0f), 1e-5f));

    s.transpose = -12.0f;
    pt.setSettings (s);
    CHECK_THAT (pt.advance (32)[0].f0Hz, WithinRel (noteHz (59.0f), 1e-5f));
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
    CHECK_THAT (pt.advance (32)[0].f0Hz, WithinRel (noteHz (57.0f), 1e-5f)); // first note: no glide

    pt.handleMidi (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100));

    // After one time constant, a one-pole glide has covered 1 - 1/e of the 12 semitones.
    PitchState p;
    for (int i = 0; i < (int) (0.1 * fs) / 32; ++i)
        p = pt.advance (32)[0];

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

        auto p = pt.advance (32)[0];
        CHECK (p.voiced);
        CHECK_THAT (p.f0Hz, WithinRel (100.0f, 1e-5f));

        pt.handleMidi (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100));
        pt.advance (32);
        pt.handleMidi (juce::MidiMessage::noteOff (1, 69));
        p = pt.advance (32)[0];
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
        CHECK_THAT (pt.advance (32)[0].f0Hz, WithinRel (100.0f, 1e-5f));
    }

    SECTION ("Bypass bands reports unvoiced and snaps to the next note")
    {
        s.noPitchMode = NoPitchMode::bypassBands;
        s.glideMs = 200.0f;
        pt.setSettings (s);

        CHECK_FALSE (pt.advance (32)[0].voiced);

        pt.handleMidi (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100));
        pt.advance (32);
        pt.handleMidi (juce::MidiMessage::noteOff (1, 69));
        CHECK_FALSE (pt.advance (32)[0].voiced);

        pt.handleMidi (juce::MidiMessage::noteOn (1, 45, (juce::uint8) 100));
        const auto p = pt.advance (32)[0];
        CHECK (p.voiced);
        CHECK_THAT (p.f0Hz, WithinRel (110.0f, 1e-5f));
    }

    SECTION ("Audio sources report no pitch until detection exists")
    {
        s.source = PitchSource::sidechain;
        s.noPitchMode = NoPitchMode::bypassBands;
        pt.setSettings (s);
        pt.handleMidi (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100));
        CHECK_FALSE (pt.advance (32)[0].voiced);
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

// ---------------------------------------------------------------------------------------------- lowest / poly

namespace
{
PitchTracker::Settings midiSettings (MidiMode mode, NoPitchMode noPitch = NoPitchMode::hold)
{
    PitchTracker::Settings s;
    s.source = PitchSource::midi;
    s.midiMode = mode;
    s.noPitchMode = noPitch;
    return s;
}

std::vector<float> voicedHz (const PitchSet& set)
{
    std::vector<float> hz;
    for (const auto& p : set)
        if (p.voiced)
            hz.push_back (p.f0Hz);
    std::sort (hz.begin(), hz.end());
    return hz;
}

juce::MidiMessage on (int note) { return juce::MidiMessage::noteOn (1, note, (juce::uint8) 100); }
juce::MidiMessage off (int note) { return juce::MidiMessage::noteOff (1, note); }
} // namespace

TEST_CASE ("Lowest Note follows the lowest held note")
{
    PitchTracker pt;
    pt.prepare (fs);
    pt.setSettings (midiSettings (MidiMode::lowestNote));

    pt.handleMidi (on (52));
    pt.handleMidi (on (48));
    pt.handleMidi (on (55)); // newest, but higher
    CHECK_THAT (pt.advance (32)[0].f0Hz, WithinRel (noteHz (48.0f), 1e-5f));

    pt.handleMidi (off (48));
    CHECK_THAT (pt.advance (32)[0].f0Hz, WithinRel (noteHz (52.0f), 1e-5f));
}

TEST_CASE ("Poly gives each held note a slot")
{
    PitchTracker pt;
    pt.prepare (fs);
    pt.setSettings (midiSettings (MidiMode::poly));

    for (int n : { 48, 52, 55 })
        pt.handleMidi (on (n));

    auto set = pt.advance (32);
    auto hz = voicedHz (set);
    REQUIRE (hz.size() == 3);
    CHECK_THAT (hz[0], WithinRel (noteHz (48.0f), 1e-5f));
    CHECK_THAT (hz[1], WithinRel (noteHz (52.0f), 1e-5f));
    CHECK_THAT (hz[2], WithinRel (noteHz (55.0f), 1e-5f));

    SECTION ("Adding a note leaves the existing notes in their slots")
    {
        std::array<float, maxVoices> before {};
        for (size_t v = 0; v < set.size(); ++v)
            before[v] = set[v].voiced ? set[v].f0Hz : 0.0f;

        pt.handleMidi (on (59));
        const auto after = pt.advance (32);
        for (size_t v = 0; v < set.size(); ++v)
            if (before[v] > 0.0f)
                CHECK_THAT (after[v].f0Hz, WithinRel (before[v], 1e-6f));
        CHECK (voicedHz (after).size() == 4);
    }

    SECTION ("Releasing one note silences only its slot, at its last pitch")
    {
        pt.handleMidi (off (52));
        const auto after = pt.advance (32);
        CHECK (voicedHz (after).size() == 2);

        bool found = false;
        for (const auto& p : after)
            found |= ! p.voiced && juce::approximatelyEqual (p.f0Hz, noteHz (52.0f));
        CHECK (found);
    }

    SECTION ("Hold keeps the last chord after every key is released, until the next note")
    {
        for (int n : { 48, 52, 55 })
            pt.handleMidi (off (n));
        CHECK (voicedHz (pt.advance (32)).size() == 3);

        pt.handleMidi (on (45));
        const auto next = voicedHz (pt.advance (32));
        REQUIRE (next.size() == 1);
        CHECK_THAT (next[0], WithinRel (noteHz (45.0f), 1e-5f));
    }

    SECTION ("Bypass Bands silences every slot on release")
    {
        pt.setSettings (midiSettings (MidiMode::poly, NoPitchMode::bypassBands));
        for (int n : { 48, 52, 55 })
            pt.handleMidi (off (n));
        CHECK (voicedHz (pt.advance (32)).empty());
    }

    SECTION ("Fallback uses a single fallback voice on release")
    {
        auto s = midiSettings (MidiMode::poly, NoPitchMode::fallback);
        s.fallbackHz = 100.0f;
        pt.setSettings (s);
        for (int n : { 48, 52, 55 })
            pt.handleMidi (off (n));
        const auto fallbackHz = voicedHz (pt.advance (32));
        REQUIRE (fallbackHz.size() == 1);
        CHECK_THAT (fallbackHz[0], WithinRel (100.0f, 1e-5f));
    }

    SECTION ("More than six notes keeps the newest six")
    {
        for (int n : { 59, 60, 62, 64 })
            pt.handleMidi (on (n));
        const auto newest = voicedHz (pt.advance (32));
        REQUIRE (newest.size() == (size_t) maxVoices);
        CHECK_THAT (newest.front(), WithinRel (noteHz (52.0f), 1e-5f)); // 48 was the oldest
    }
}

TEST_CASE ("Shared harmonics from different notes are merged")
{
    std::array<BandSettings, numBands> bands {};
    bands[0].on = true; // notch on H2
    bands[0].type = FilterType::notch;
    bands[0].harmonic = 2.0f;
    bands[1].on = true; // notch on H3
    bands[1].type = FilterType::notch;
    bands[1].harmonic = 3.0f;

    // C3 E3 G3: G's 2nd harmonic (392.0 Hz) and C's 3rd (392.4 Hz) are 2 cents apart.
    PitchSet chord = monoPitch ({ noteHz (48.0f), true });
    chord[1] = { noteHz (52.0f), true };
    chord[2] = { noteHz (55.0f), true };

    auto count = [] (const InstanceMask& m) {
        int n = 0;
        for (auto& band : m)
            for (bool b : band)
                n += b ? 1 : 0;
        return n;
    };

    CHECK (count (EqEngine::activeInstances (bands, chord, fs)) == 5);
    CHECK_FALSE (EqEngine::activeInstances (bands, chord, fs)[1][0]); // C's H3 gives way to G's H2 (band 1 first)

    SECTION ("Different filter types are not merged")
    {
        bands[1].type = FilterType::bell;
        CHECK (count (EqEngine::activeInstances (bands, chord, fs)) == 6);
    }

    SECTION ("Two bands on the same harmonic of one note both run")
    {
        bands[1].harmonic = 2.0f;
        CHECK (count (EqEngine::activeInstances (bands, monoPitch ({ 110.0f, true }), fs)) == 2);
    }
}

TEST_CASE ("Processor: Poly applies a tracked band to every note of a chord")
{
    juce::ScopedJuceInitialiser_GUI juce;
    HarmEQAudioProcessor proc;
    auto& apvts = proc.getValueTreeState();

    setParam (apvts, ParamIDs::source, (float) PitchSource::midi);
    setParam (apvts, ParamIDs::band (0, "harmonic"), 1.0f);
    setParam (apvts, ParamIDs::band (0, "gain"), 12.0f);
    setParam (apvts, ParamIDs::band (0, "q"), 8.0f);

    constexpr int blockSize = 512;
    proc.setPlayConfigDetails (2, 2, fs, blockSize);
    proc.prepareToPlay (fs, blockSize);

    auto gainAt = [&] (double probeHz, juce::MidiBuffer midi) {
        juce::AudioBuffer<float> buffer (2, blockSize);
        double inSq = 0.0, outSq = 0.0;
        for (int b = 0; b < 60; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto x = (float) std::sin (2.0 * juce::MathConstants<double>::pi * probeHz * (b * blockSize + i) / fs);
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, x);
                if (b >= 30)
                    inSq += (double) x * x;
            }
            proc.processBlock (buffer, midi);
            midi.clear();
            if (b >= 30)
                for (int i = 0; i < blockSize; ++i)
                    outSq += (double) buffer.getSample (0, i) * buffer.getSample (0, i);
        }
        return 10.0 * std::log10 (outSq / inSq);
    };

    juce::MidiBuffer chord;
    for (int n : { 48, 52, 55 })
        chord.addEvent (on (n), 0);

    // The three bells overlap a little, so the expected gain is the sum of all three responses.
    auto chordResponseDb = [&] (double probeHz) {
        BandSettings s;
        s.on = true;
        s.gainDb = 12.0f;
        s.q = 8.0f;
        double db = 0.0;
        for (int n : { 48, 52, 55 })
            db += juce::Decibels::gainToDecibels (
                std::abs (EqBand::settledDesign (s, { noteHz ((float) n), true }, fs).response (probeHz, fs)));
        return db;
    };

    SECTION ("Poly: every chord note is boosted")
    {
        setParam (apvts, ParamIDs::midiMode, (float) MidiMode::poly);
        CHECK_THAT (gainAt (noteHz (52.0f), chord), WithinAbs (chordResponseDb (noteHz (52.0f)), 0.2));
        CHECK_THAT (gainAt (noteHz (48.0f), {}), WithinAbs (chordResponseDb (noteHz (48.0f)), 0.2));
        CHECK (gainAt (noteHz (52.0f), {}) > 12.0);
        CHECK (gainAt (noteHz (50.0f), {}) < gainAt (noteHz (52.0f), {}) - 5.0); // D3 is not in the chord
    }

    SECTION ("Last Note: only the newest note is boosted")
    {
        setParam (apvts, ParamIDs::midiMode, (float) MidiMode::lastNote);
        CHECK_THAT (gainAt (noteHz (55.0f), chord), WithinAbs (12.0, 0.3));
        CHECK (gainAt (noteHz (52.0f), {}) < 3.0);
    }
}
