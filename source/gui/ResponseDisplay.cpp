#include "ResponseDisplay.h"
#include "PluginProcessor.h"

namespace harmeq
{
namespace
{
    constexpr double minHz = 20.0, maxHz = 20000.0;
    constexpr float analyzerRangeDb = 90.0f;
    constexpr float nodeRadius = 9.0f, hitRadius = 14.0f;
    constexpr int maxHarmonicLines = 8;

    bool usesGain (FilterType t) { return t == FilterType::bell || t == FilterType::lowShelf || t == FilterType::highShelf; }

    bool sameSettings (const BandSettings& a, const BandSettings& b)
    {
        return a.on == b.on && a.type == b.type && a.track == b.track && juce::exactlyEqual (a.harmonic, b.harmonic)
               && juce::exactlyEqual (a.detuneCents, b.detuneCents) && juce::exactlyEqual (a.freqHz, b.freqHz)
               && juce::exactlyEqual (a.gainDb, b.gainDb) && juce::exactlyEqual (a.q, b.q) && a.steep == b.steep;
    }

    const char* typeName (FilterType t)
    {
        static const char* names[] { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch" };
        return names[(int) t];
    }
} // namespace

ResponseDisplay::ResponseDisplay (HarmEQAudioProcessor& p)
    : processor (p), apvts (p.getValueTreeState())
{
    setOpaque (true);
    setRepaintsOnMouseActivity (false);
    refresh();
}

double ResponseDisplay::snapHarmonic (double h)
{
    if (h < 0.375) return 0.25;
    if (h < 0.75) return 0.5;
    return juce::jlimit (1.0, 32.0, std::round (h));
}

// ---------------------------------------------------------------------------------------------- geometry

void ResponseDisplay::resized()
{
    plot = getLocalBounds().toFloat().withTrimmedLeft (34.0f).withTrimmedRight (10.0f).withTrimmedTop (18.0f)
               .withTrimmedBottom (20.0f);
    curvesDirty = true;
}

float ResponseDisplay::xOf (double hz) const
{
    return plot.getX() + (float) (std::log (hz / minHz) / std::log (maxHz / minHz)) * plot.getWidth();
}

double ResponseDisplay::hzOf (float x) const
{
    return minHz * std::pow (maxHz / minHz, (double) ((x - plot.getX()) / plot.getWidth()));
}

float ResponseDisplay::yOf (double db) const
{
    return plot.getY() + (float) (1.0 - (db + rangeDb) / (2.0 * rangeDb)) * plot.getHeight();
}

double ResponseDisplay::dbOf (float y) const
{
    return (1.0 - (y - plot.getY()) / plot.getHeight()) * 2.0 * rangeDb - rangeDb;
}

float ResponseDisplay::analyzerY (double db) const
{
    return plot.getY() + (float) (-db / analyzerRangeDb) * plot.getHeight();
}

juce::Point<float> ResponseDisplay::nodePosition (int band) const
{
    const auto& s = settings[(size_t) band];
    const auto target = EqBand::resolveTarget (s, pitch, sampleRate);
    const double gain = usesGain (s.type) ? juce::jlimit ((double) -rangeDb, (double) rangeDb, (double) s.gainDb) : 0.0;
    return { xOf (juce::jlimit (minHz, maxHz, target.freqHz)), yOf (gain) };
}

int ResponseDisplay::nodeAt (juce::Point<float> p) const
{
    int best = -1;
    float bestDistance = hitRadius;

    for (int b = 0; b < numBands; ++b)
    {
        const float d = nodePosition (b).getDistanceFrom (p);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = b;
        }
    }

    return best;
}

// ---------------------------------------------------------------------------------------------- data

void ResponseDisplay::setRangeDb (float db)
{
    rangeDb = db;
    curvesDirty = true;
    repaint();
}

void ResponseDisplay::setAnalyzerMode (AnalyzerMode m)
{
    analyzerMode = m;
    processor.setAnalyzerActive (m != AnalyzerMode::off);
    if (m == AnalyzerMode::off)
    {
        preAnalyzer.clear();
        postAnalyzer.clear();
    }
    repaint();
}

void ResponseDisplay::setSelectedBand (int band)
{
    if (band != selected)
    {
        selected = band;
        repaint();
    }
}

void ResponseDisplay::selectBand (int band)
{
    setSelectedBand (band);
    if (onBandSelected)
        onBandSelected (band);
}

void ResponseDisplay::refresh()
{
    bool changed = false;

    for (int b = 0; b < numBands; ++b)
    {
        const auto s = processor.getBandSettings (b);
        if (! sameSettings (s, settings[(size_t) b]))
        {
            settings[(size_t) b] = s;
            changed = true;
        }
    }

    const auto newPitch = processor.getDisplayPitch();
    if (! juce::approximatelyEqual (newPitch.f0Hz, pitch.f0Hz) || newPitch.voiced != pitch.voiced)
    {
        pitch = newPitch;
        changed = true;
    }

    const auto newRate = processor.getDisplaySampleRate();
    const auto newOut = apvts.getRawParameterValue (ParamIDs::outputGain)->load();
    if (! juce::approximatelyEqual (newRate, sampleRate) || ! juce::approximatelyEqual (newOut, outputGainDb))
    {
        sampleRate = newRate;
        outputGainDb = newOut;
        changed = true;
    }

    // Ease bands in and out the way the audio does, instead of popping.
    for (int b = 0; b < numBands; ++b)
    {
        const float target = EqBand::resolveTarget (settings[(size_t) b], pitch, sampleRate).amount;
        auto& a = shownAmount[(size_t) b];
        if (! juce::approximatelyEqual (a, target))
        {
            a += 0.5f * (target - a);
            if (std::abs (a - target) < 0.01f)
                a = target;
            changed = true;
        }
    }

    if (changed)
        curvesDirty = true;

    bool analyzerChanged = false;
    if (analyzerMode != AnalyzerMode::off)
    {
        analyzerChanged |= preAnalyzer.update (processor.getPreTap(), sampleRate);
        analyzerChanged |= postAnalyzer.update (processor.getPostTap(), sampleRate);
    }

    if (changed || analyzerChanged)
        repaint();
}

void ResponseDisplay::recomputeCurves()
{
    const int columns = juce::jmax (2, (int) plot.getWidth());
    columnHz.resize ((size_t) columns);
    sumDb.assign ((size_t) columns, outputGainDb);

    for (int c = 0; c < columns; ++c)
        columnHz[(size_t) c] = hzOf (plot.getX() + (float) c);

    for (int b = 0; b < numBands; ++b)
    {
        auto& curve = bandDb[(size_t) b];
        curve.assign ((size_t) columns, 0.0f);

        const auto& s = settings[(size_t) b];
        const float amount = shownAmount[(size_t) b];
        if (amount <= 0.0f)
            continue;

        const auto target = EqBand::resolveTarget (s, pitch, sampleRate);
        const auto design = BandDesign::make (s, target.freqHz, juce::jmax (s.q, 0.01f), s.gainDb, amount, sampleRate);

        for (int c = 0; c < columns; ++c)
        {
            const double hz = columnHz[(size_t) c];
            const float db = hz < 0.5 * sampleRate
                                 ? (float) juce::Decibels::gainToDecibels (std::abs (design.response (hz, sampleRate)), -200.0)
                                 : 0.0f;
            curve[(size_t) c] = db;
            sumDb[(size_t) c] += db;
        }
    }

    curvesDirty = false;
}

juce::RangedAudioParameter* ResponseDisplay::param (int band, const char* suffix) const
{
    return apvts.getParameter (ParamIDs::band (band, suffix));
}

void ResponseDisplay::beginGesture (juce::RangedAudioParameter* p)
{
    if (! gestures.contains (p))
    {
        p->beginChangeGesture();
        gestures.add (p);
    }
}

void ResponseDisplay::endGestures()
{
    for (auto* p : gestures)
        p->endChangeGesture();
    gestures.clear();
}

void ResponseDisplay::setPlain (juce::RangedAudioParameter* p, float value)
{
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void ResponseDisplay::setComplete (juce::RangedAudioParameter* p, float value)
{
    p->beginChangeGesture();
    setPlain (p, value);
    p->endChangeGesture();
}

// ---------------------------------------------------------------------------------------------- mouse

void ResponseDisplay::mouseMove (const juce::MouseEvent& e)
{
    mousePos = e.position;
    const int h = nodeAt (e.position);
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
    if (h != hovered || h >= 0)
    {
        hovered = h;
        repaint();
    }
}

void ResponseDisplay::mouseExit (const juce::MouseEvent&)
{
    if (drag.band < 0 && hovered >= 0)
    {
        hovered = -1;
        repaint();
    }
}

void ResponseDisplay::mouseDown (const juce::MouseEvent& e)
{
    mousePos = e.position;
    const int band = nodeAt (e.position);
    if (band < 0)
        return;

    selectBand (band);

    if (e.mods.isPopupMenu())
    {
        showBandMenu (band);
        return;
    }

    drag = { band, e.position, nodePosition (band), settings[(size_t) band].detuneCents, e.mods.isShiftDown() };
    hovered = band;
}

void ResponseDisplay::mouseDrag (const juce::MouseEvent& e)
{
    mousePos = e.position;
    if (drag.band < 0)
        return;

    const int b = drag.band;
    const auto& s = settings[(size_t) b];
    const bool fine = e.mods.isShiftDown();

    // Switching between coarse and fine mid-drag re-anchors, so the node doesn't jump.
    if (fine != drag.fine)
    {
        drag.fine = fine;
        drag.start = e.position;
        drag.nodeStart = nodePosition (b);
        drag.detuneStart = s.detuneCents;
    }

    const auto delta = e.position - drag.start;
    const auto target = drag.nodeStart + delta;

    if (s.track == TrackMode::tracked)
    {
        if (fine)
        {
            auto* p = param (b, "detune");
            beginGesture (p);
            setPlain (p, juce::jlimit (-100.0f, 100.0f, drag.detuneStart + delta.x * 0.5f));
        }
        else
        {
            const double hz = hzOf (juce::jlimit (plot.getX(), plot.getRight(), target.x));
            const double unDetuned = hz / std::exp2 (s.detuneCents / 1200.0);
            auto* p = param (b, "harmonic");
            beginGesture (p);
            setPlain (p, (float) snapHarmonic (unDetuned / juce::jmax (1.0f, pitch.f0Hz)));
        }
    }
    else
    {
        auto* p = param (b, "freq");
        beginGesture (p);
        const double scale = fine ? 0.2 : 1.0;
        const float x = drag.nodeStart.x + delta.x * (float) scale;
        setPlain (p, (float) juce::jlimit (minHz, maxHz, hzOf (x)));
    }

    if (usesGain (s.type))
    {
        auto* p = param (b, "gain");
        beginGesture (p);
        const float y = drag.nodeStart.y + delta.y * (fine ? 0.2f : 1.0f);
        setPlain (p, (float) juce::jlimit (-24.0, 24.0, std::round (dbOf (y) * 10.0) / 10.0));
    }

    refresh();
}

void ResponseDisplay::mouseUp (const juce::MouseEvent&)
{
    endGestures();
    drag.band = -1;
    repaint();
}

void ResponseDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int band = nodeAt (e.position);

    if (band >= 0)
    {
        selectBand (band);
        setComplete (param (band, "on"), settings[(size_t) band].on ? 0.0f : 1.0f);
    }
    else if (plot.contains (e.position))
    {
        addBandAt (e.position);
    }

    refresh();
}

void ResponseDisplay::addBandAt (juce::Point<float> p)
{
    int free = -1;
    for (int b = 0; b < numBands && free < 0; ++b)
        if (! settings[(size_t) b].on)
            free = b;

    if (free < 0)
        return;

    const auto values = std::initializer_list<std::pair<const char*, float>> {
        { "type", (float) FilterType::bell },
        { "track", (float) TrackMode::tracked },
        { "harmonic", (float) snapHarmonic (hzOf (p.x) / juce::jmax (1.0f, pitch.f0Hz)) },
        { "detune", 0.0f },
        { "gain", (float) juce::jlimit (-24.0, 24.0, std::round (dbOf (p.y) * 10.0) / 10.0) },
        { "q", 6.0f },
        { "slope", 0.0f },
        { "on", 1.0f },
    };

    for (const auto& [suffix, value] : values)
        setComplete (param (free, suffix), value);

    shownAmount[(size_t) free] = 0.0f;
    selectBand (free);
}

void ResponseDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    int band = nodeAt (e.position);
    if (band < 0)
        band = hovered;
    if (band < 0)
        return;

    auto* p = param (band, "q");
    const float q = settings[(size_t) band].q * std::exp ((wheel.isReversed ? -wheel.deltaY : wheel.deltaY) * 0.6f);
    setComplete (p, juce::jlimit (0.1f, 30.0f, q));
    selectBand (band);
    hovered = band;
    mousePos = e.position;
    refresh();
}

void ResponseDisplay::showBandMenu (int band)
{
    const auto s = settings[(size_t) band];
    juce::PopupMenu menu;

    for (int t = 0; t < 6; ++t)
        menu.addItem (1 + t, typeName ((FilterType) t), true, (int) s.type == t);

    menu.addSeparator();
    menu.addItem (10, "Track pitch", true, s.track == TrackMode::tracked);
    menu.addSeparator();
    const bool pass = s.type == FilterType::highPass || s.type == FilterType::lowPass;
    menu.addItem (20, "12 dB/oct", pass, ! s.steep);
    menu.addItem (21, "24 dB/oct", pass, s.steep);
    menu.addSeparator();
    menu.addItem (30, s.on ? "Turn off" : "Turn on");
    menu.addItem (31, "Reset band");

    juce::Component::SafePointer<ResponseDisplay> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (
                            juce::Rectangle<int> (1, 1).withPosition (localPointToGlobal (mousePos.toInt()))),
                        [safe, band, s] (int result) {
                            if (safe == nullptr || result == 0)
                                return;

                            auto& d = *safe;
                            if (result >= 1 && result <= 6)
                                d.setComplete (d.param (band, "type"), (float) (result - 1));
                            else if (result == 10)
                            {
                                if (s.track == TrackMode::tracked)
                                {
                                    // Keep the band where it is when it stops tracking.
                                    const auto f = EqBand::resolveTarget (s, d.pitch, d.sampleRate).freqHz;
                                    d.setComplete (d.param (band, "freq"), (float) juce::jlimit (minHz, maxHz, f));
                                }
                                d.setComplete (d.param (band, "track"), s.track == TrackMode::tracked ? 1.0f : 0.0f);
                            }
                            else if (result == 20 || result == 21)
                                d.setComplete (d.param (band, "slope"), result == 21 ? 1.0f : 0.0f);
                            else if (result == 30)
                                d.setComplete (d.param (band, "on"), s.on ? 0.0f : 1.0f);
                            else if (result == 31)
                            {
                                for (auto* suffix : { "type", "track", "harmonic", "detune", "freq", "gain", "q", "slope" })
                                {
                                    auto* p = d.param (band, suffix);
                                    p->beginChangeGesture();
                                    p->setValueNotifyingHost (p->getDefaultValue());
                                    p->endChangeGesture();
                                }
                            }
                            d.refresh();
                        });
}

// ---------------------------------------------------------------------------------------------- painting

void ResponseDisplay::paint (juce::Graphics& g)
{
    if (curvesDirty)
        recomputeCurves();

    g.fillAll (theme::well);
    drawGrid (g);
    drawAnalyzer (g);
    drawHarmonics (g);
    drawCurves (g);
    drawNodes (g);
    drawTooltip (g);
}

void ResponseDisplay::drawGrid (juce::Graphics& g)
{
    g.setFont (theme::numbers (10.0f));

    for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = std::round (xOf (f)) + 0.5f;
        g.setColour (theme::lineSoft);
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());

        const auto label = f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f);
        const auto just = f <= 20.0 ? juce::Justification::centredLeft
                                    : f >= 20000.0 ? juce::Justification::centredRight : juce::Justification::centred;
        const float w = 40.0f;
        const float left = f <= 20.0 ? x : f >= 20000.0 ? x - w : x - w * 0.5f;
        g.setColour (theme::faint);
        g.drawText (label, juce::Rectangle<float> (left, plot.getBottom() + 2.0f, w, 16.0f), just, false);
    }

    const float step = rangeDb >= 24.0f ? 6.0f : rangeDb >= 12.0f ? 3.0f : 2.0f;
    for (float db = -rangeDb; db <= rangeDb + 0.01f; db += step)
    {
        const float y = std::round (yOf (db)) + 0.5f;
        g.setColour (std::abs (db) < 0.01f ? theme::line : theme::lineSoft);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());

        if (std::abs (std::abs (db) - rangeDb) > 0.01f)
        {
            g.setColour (theme::faint);
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (0.0f, y - 7.0f, plot.getX() - 6.0f, 14.0f),
                        juce::Justification::centredRight, false);
        }
    }
}

void ResponseDisplay::drawAnalyzer (juce::Graphics& g)
{
    if (analyzerMode == AnalyzerMode::off || columnHz.empty())
        return;

    const int columns = (int) columnHz.size();
    auto edge = [this] (int c) { return hzOf (plot.getX() + (float) c - 0.5f); };

    if (analyzerMode == AnalyzerMode::pre || analyzerMode == AnalyzerMode::both)
    {
        juce::Path fill;
        fill.startNewSubPath (plot.getX(), plot.getBottom());
        for (int c = 0; c < columns; ++c)
            fill.lineTo (plot.getX() + (float) c,
                         juce::jmin (plot.getBottom(), analyzerY (preAnalyzer.levelDb (edge (c), edge (c + 1)))));
        fill.lineTo (plot.getX() + (float) columns, plot.getBottom());
        fill.closeSubPath();
        g.setColour (theme::muted.withAlpha (0.16f));
        g.fillPath (fill);
    }

    if (analyzerMode == AnalyzerMode::post || analyzerMode == AnalyzerMode::both)
    {
        juce::Path line;
        for (int c = 0; c < columns; ++c)
        {
            const auto p = juce::Point<float> (plot.getX() + (float) c,
                                               juce::jmin (plot.getBottom(), analyzerY (postAnalyzer.levelDb (edge (c), edge (c + 1)))));
            c == 0 ? line.startNewSubPath (p) : line.lineTo (p);
        }
        g.setColour (juce::Colour (0xffa0bed7).withAlpha (0.4f));
        g.strokePath (line, juce::PathStrokeType (1.0f));
    }
}

void ResponseDisplay::drawHarmonics (juce::Graphics& g)
{
    g.setFont (theme::numbers (10.0f));
    float lastLabelX = -1.0e9f;

    for (int n = 1; n <= maxHarmonicLines; ++n)
    {
        const double hz = pitch.f0Hz * n;
        if (hz > maxHz)
            break;
        if (hz < minHz)
            continue;

        const float x = std::round (xOf (hz)) + 0.5f;
        const float alpha = (pitch.voiced ? 1.0f : 0.4f) * (n == 1 ? 0.6f : 0.42f - (float) n * 0.02f);
        g.setColour (theme::harmonic.withAlpha (alpha));

        juce::Line<float> line (x, plot.getY(), x, plot.getBottom());
        if (pitch.voiced)
        {
            g.drawLine (line, n == 1 ? 1.5f : 1.0f);
        }
        else
        {
            const float dashes[] { 3.0f, 4.0f };
            g.drawDashedLine (line, dashes, 2, 1.0f);
        }

        if (x - lastLabelX > 16.0f)
        {
            g.setColour (theme::harmonic.withAlpha (juce::jmin (1.0f, alpha * 2.0f)));
            g.drawText (n == 1 ? "f0" : juce::String (n), juce::Rectangle<float> (x - 12.0f, 1.0f, 24.0f, plot.getY() - 2.0f),
                        juce::Justification::centred, false);
            lastLabelX = x;
        }
    }
}

void ResponseDisplay::drawCurves (juce::Graphics& g)
{
    const int columns = (int) columnHz.size();
    const float y0 = yOf (0.0);
    const float top = plot.getY() - 4.0f, bottom = plot.getBottom() + 4.0f;
    auto clampY = [&] (float y) { return juce::jlimit (top, bottom, y); };

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (plot.expanded (0.0f, 4.0f).toNearestInt());

    for (int b = 0; b < numBands; ++b)
    {
        if (shownAmount[(size_t) b] <= 0.001f)
            continue;

        const auto& curve = bandDb[(size_t) b];
        juce::Path stroke, fill;
        fill.startNewSubPath (plot.getX(), y0);

        // Outline only where the band actually does something, so flat stretches don't paint
        // coloured lines along 0 dB.
        bool penDown = false;
        for (int c = 0; c < columns; ++c)
        {
            const juce::Point<float> p (plot.getX() + (float) c, clampY (yOf (curve[(size_t) c])));
            fill.lineTo (p);

            if (std::abs (curve[(size_t) c]) > 0.05f)
            {
                penDown ? stroke.lineTo (p) : stroke.startNewSubPath (p);
                penDown = true;
            }
            else
            {
                penDown = false;
            }
        }

        fill.lineTo (plot.getX() + (float) columns, y0);
        fill.closeSubPath();

        const auto colour = theme::band (b);
        const bool isSelected = b == selected;
        g.setColour (colour.withAlpha (isSelected ? 0.20f : 0.10f));
        g.fillPath (fill);
        g.setColour (colour.withAlpha (isSelected ? 0.8f : 0.45f));
        g.strokePath (stroke, juce::PathStrokeType (1.0f));
    }

    juce::Path sum;
    for (int c = 0; c < columns; ++c)
    {
        const juce::Point<float> p (plot.getX() + (float) c, clampY (yOf (sumDb[(size_t) c])));
        c == 0 ? sum.startNewSubPath (p) : sum.lineTo (p);
    }
    g.setColour (theme::sum);
    g.strokePath (sum, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void ResponseDisplay::drawNodes (juce::Graphics& g)
{
    g.setFont (theme::numbers (10.0f, true));

    // Draw in reverse so band 1 ends up on top where nodes overlap.
    for (int b = numBands - 1; b >= 0; --b)
    {
        const auto& s = settings[(size_t) b];
        const auto p = nodePosition (b);
        const auto colour = theme::band (b);
        const bool live = s.on && shownAmount[(size_t) b] > 0.01f;
        const auto circle = juce::Rectangle<float> (nodeRadius * 2.0f, nodeRadius * 2.0f).withCentre (p);

        if (live)
        {
            g.setColour (colour);
            g.fillEllipse (circle);
        }
        else
        {
            g.setColour (theme::well.withAlpha (0.85f));
            g.fillEllipse (circle);
            g.setColour (colour.withAlpha (s.on ? 0.8f : 0.4f));
            g.drawEllipse (circle.reduced (0.75f), 1.5f);
        }

        if (b == selected)
        {
            g.setColour (colour.withAlpha (0.9f));
            g.drawEllipse (circle.expanded (4.0f), 1.5f);
        }

        g.setColour (live ? juce::Colour (0xff101318) : colour.withAlpha (s.on ? 0.9f : 0.5f));
        g.drawText (juce::String (b + 1), circle, juce::Justification::centred, false);
    }
}

void ResponseDisplay::drawTooltip (juce::Graphics& g)
{
    const int b = drag.band >= 0 ? drag.band : hovered;
    if (b < 0)
        return;

    const auto& s = settings[(size_t) b];
    const auto target = EqBand::resolveTarget (s, pitch, sampleRate);
    const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));

    juce::String text = juce::String (b + 1) + " " + typeName (s.type);
    if (s.track == TrackMode::tracked)
    {
        text << dot << theme::formatHarmonic (s.harmonic);
        if (std::abs (s.detuneCents) >= 0.5f)
            text << " " << (s.detuneCents > 0 ? "+" : "") << juce::roundToInt (s.detuneCents) << "ct";
    }
    text << dot << theme::formatHz (target.freqHz) << " Hz";
    if (usesGain (s.type))
        text << dot << theme::formatDb (s.gainDb);
    text << dot << "Q " << juce::String (s.q, 2);
    if (! s.on)
        text << dot << "off";

    const auto font = theme::numbers (11.0f);
    g.setFont (font);
    const float w = juce::GlyphArrangement::getStringWidth (font, text) + 14.0f;
    const float h = 20.0f;
    const float x = juce::jlimit (4.0f, (float) getWidth() - w - 4.0f, mousePos.x + 14.0f);
    const float y = mousePos.y < 40.0f ? mousePos.y + 16.0f : mousePos.y - 30.0f;
    const juce::Rectangle<float> box (x, y, w, h);

    g.setColour (juce::Colour (0xeb0a0c0f));
    g.fillRoundedRectangle (box, 4.0f);
    g.setColour (theme::line);
    g.drawRoundedRectangle (box, 4.0f, 1.0f);
    g.setColour (theme::text);
    g.drawText (text, box, juce::Justification::centred, false);
}
} // namespace harmeq
