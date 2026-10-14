#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "gui/ResponseDisplay.h"
#include "gui/SpectrumAnalyzer.h"

using namespace harmeq;
using Catch::Matchers::WithinAbs;

TEST_CASE ("Editor opens, refreshes, renders and closes")
{
    juce::ScopedJuceInitialiser_GUI juce;
    HarmEQAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 256);
    proc.prepareToPlay (48000.0, 256);

    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        auto* e = dynamic_cast<HarmEQAudioProcessorEditor*> (editor.get());
        REQUIRE (e != nullptr);

        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
        for (int i = 0; i < 64; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < 256; ++s)
                    buffer.setSample (ch, s, (float) std::sin (0.05 * (i * 256 + s)));
            proc.processBlock (buffer, midi);
            midi.clear();
            e->refreshNow();
        }

        const auto image = e->createComponentSnapshot (e->getLocalBounds());
        CHECK (image.getWidth() == e->getWidth());

        // Resizing to the limits must not break the layout code.
        e->setSize (940, 560);
        e->createComponentSnapshot (e->getLocalBounds());
        e->setSize (2000, 1400);
        e->createComponentSnapshot (e->getLocalBounds());
    }
}

TEST_CASE ("View settings are stored in the plugin state")
{
    juce::ScopedJuceInitialiser_GUI juce;
    HarmEQAudioProcessor a, b;

    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (a.createEditor());
        editor->setSize (1200, 700);
    }

    juce::MemoryBlock state;
    a.getStateInformation (state);
    b.setStateInformation (state.getData(), (int) state.getSize());

    const auto ui = b.getValueTreeState().state.getChildWithName ("UI");
    REQUIRE (ui.isValid());
    CHECK ((int) ui.getProperty ("width") == 1200);
    CHECK ((int) ui.getProperty ("height") == 700);
}

TEST_CASE ("Displayed curve equals the settled audio response")
{
    constexpr double fs = 48000.0;
    BandSettings s;
    s.on = true;
    s.track = TrackMode::tracked;
    s.harmonic = 3.0f;
    s.detuneCents = 25.0f;
    s.q = 6.0f;
    const PitchState pitch { 196.0f, true };

    for (auto type : { FilterType::bell, FilterType::lowShelf, FilterType::highShelf, FilterType::highPass,
                       FilterType::lowPass, FilterType::notch })
    {
        for (bool steep : { false, true })
        {
            s.type = type;
            s.steep = steep;
            s.gainDb = -7.5f;

            EqBand band;
            band.setSettings (s);
            band.prepare (fs);
            for (int i = 0; i < 2000; ++i)
                band.update (pitch, 32);

            const auto shown = EqBand::settledDesign (s, pitch, fs);
            for (double hz : { 50.0, 300.0, 588.0, 600.0, 1200.0, 8000.0 })
                CHECK_THAT (std::abs (shown.response (hz, fs)), WithinAbs (band.magnitudeAt (hz), 1e-4));
        }
    }
}

TEST_CASE ("Node dragging snaps to whole harmonics, with quarter and half below one")
{
    CHECK_THAT (ResponseDisplay::snapHarmonic (0.2), WithinAbs (0.25, 1e-9));
    CHECK_THAT (ResponseDisplay::snapHarmonic (0.45), WithinAbs (0.5, 1e-9));
    CHECK_THAT (ResponseDisplay::snapHarmonic (0.8), WithinAbs (1.0, 1e-9));
    CHECK_THAT (ResponseDisplay::snapHarmonic (2.4), WithinAbs (2.0, 1e-9));
    CHECK_THAT (ResponseDisplay::snapHarmonic (2.6), WithinAbs (3.0, 1e-9));
    CHECK_THAT (ResponseDisplay::snapHarmonic (50.0), WithinAbs (32.0, 1e-9));
}

TEST_CASE ("Analyzer reads a full-scale-relative sine level")
{
    constexpr double fs = 48000.0;
    AnalyzerTap tap;
    SpectrumAnalyzer analyzer;

    juce::AudioBuffer<float> buffer (1, 1024);
    int n = 0;
    for (int block = 0; block < 16; ++block)
    {
        for (int i = 0; i < 1024; ++i, ++n)
            buffer.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * n / fs));
        tap.push (buffer);
        analyzer.update (tap, fs);
    }

    // 0.5 amplitude = -6 dBFS; Hann scalloping can cost up to ~1.4 dB between bins.
    CHECK_THAT (analyzer.levelDb (990.0, 1010.0), WithinAbs (-6.0, 1.5));
    CHECK (analyzer.levelDb (4000.0, 4100.0) < -60.0f);
}

namespace
{
juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos,
                          juce::ModifierKeys mods = {}, int clicks = 1)
{
    auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    return { source, pos, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, downPos, now, clicks, pos != downPos };
}

float paramValue (HarmEQAudioProcessor& p, int band, const char* suffix)
{
    return p.getValueTreeState().getRawParameterValue (ParamIDs::band (band, suffix))->load();
}

// Screen x of a frequency on the display, using the same mapping and plot margins as ResponseDisplay.
float xOfHz (const juce::Component& d, double hz)
{
    const float left = 34.0f, width = (float) d.getWidth() - 44.0f;
    return left + (float) (std::log (hz / 20.0) / std::log (1000.0)) * width;
}
} // namespace

TEST_CASE ("Mouse editing on the response display")
{
    juce::ScopedJuceInitialiser_GUI juce;
    HarmEQAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    auto& display = dynamic_cast<HarmEQAudioProcessorEditor&> (*editor).getDisplay();

    // Before any audio the display follows the 110 Hz fallback; band 2 sits on H2 = 220 Hz at 0 dB.
    const float zeroDbY = 18.0f + ((float) display.getHeight() - 38.0f) * 0.5f;
    const juce::Point<float> node { xOfHz (display, 220.0), zeroDbY };

    SECTION ("Dragging right moves a tracked band up the harmonics and dragging up raises its gain")
    {
        const juce::Point<float> to { xOfHz (display, 440.0), zeroDbY - 40.0f };
        display.mouseDown (mouseAt (display, node, node));
        display.mouseDrag (mouseAt (display, to, node));
        display.mouseUp (mouseAt (display, to, node));

        CHECK_THAT (paramValue (proc, 1, "harmonic"), WithinAbs (4.0, 1e-6));
        CHECK (paramValue (proc, 1, "gain") > 3.0f);
    }

    SECTION ("Shift-drag detunes instead of changing harmonic")
    {
        const juce::Point<float> to = node + juce::Point<float> (60.0f, 0.0f);
        display.mouseDown (mouseAt (display, node, node, juce::ModifierKeys::shiftModifier));
        display.mouseDrag (mouseAt (display, to, node, juce::ModifierKeys::shiftModifier));
        display.mouseUp (mouseAt (display, to, node));

        CHECK_THAT (paramValue (proc, 1, "harmonic"), WithinAbs (2.0, 1e-6));
        CHECK_THAT (paramValue (proc, 1, "detune"), WithinAbs (30.0, 0.5));
    }

    SECTION ("Wheel changes Q, double-click toggles the band")
    {
        const float q0 = paramValue (proc, 1, "q");
        juce::MouseWheelDetails wheel { 0.0f, 0.5f, false, false, false };
        display.mouseWheelMove (mouseAt (display, node, node), wheel);
        CHECK (paramValue (proc, 1, "q") > q0);

        CHECK (paramValue (proc, 1, "on") > 0.5f);
        display.mouseDoubleClick (mouseAt (display, node, node, {}, 2));
        CHECK (paramValue (proc, 1, "on") < 0.5f);
    }
}
