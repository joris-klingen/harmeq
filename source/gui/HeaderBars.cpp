#include "HeaderBars.h"
#include "PluginProcessor.h"

namespace harmeq
{
namespace
{
    juce::String signedSemis (double v)
    {
        const int s = juce::roundToInt (v);
        return (s > 0 ? "+" : s < 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String())
               + juce::String (std::abs (s)) + " st";
    }
} // namespace

// ---------------------------------------------------------------------------------------------- top bar

TopBar::TopBar (HarmEQAudioProcessor& p)
    : processor (p),
      source (*p.getValueTreeState().getParameter (ParamIDs::source), { "Main", "Sidechain", "MIDI" }),
      notes (*p.getValueTreeState().getParameter (ParamIDs::midiMode), { "Last", "Lowest", "Poly" })
{
    addAndMakeVisible (source);
    addAndMakeVisible (notes);
    notes.setTooltip ("MIDI notes: follow the last or lowest held note, or every note of a chord (Poly)");
    outputAttachment = attach (p.getValueTreeState(), ParamIDs::outputGain, outputGain,
                               [] (double v) { return theme::formatDb (v); });
    outputGain.setTooltip ("Output gain");
    addAndMakeVisible (outputGain);
    refresh();
}

void TopBar::refresh()
{
    const auto newPitch = processor.getDisplayPitch();
    const auto newConfidence = processor.getPitchConfidence();
    const auto newSource = static_cast<PitchSource> (
        (int) processor.getValueTreeState().getRawParameterValue (ParamIDs::source)->load());
    const bool newSidechain = processor.isSidechainConnected();
    const auto newPitches = processor.getDisplayPitches();
    const bool newPoly = processor.isPolyActive();

    bool pitchesChanged = newPoly != poly;
    for (size_t v = 0; v < newPitches.size() && ! pitchesChanged; ++v)
        pitchesChanged = newPitches[v].voiced != pitches[v].voiced
                         || (newPitches[v].voiced && ! juce::approximatelyEqual (newPitches[v].f0Hz, pitches[v].f0Hz));

    notes.setEnabled (newSource == PitchSource::midi);

    if (juce::approximatelyEqual (newPitch.f0Hz, pitch.f0Hz) && newPitch.voiced == pitch.voiced && ! pitchesChanged
        && juce::approximatelyEqual (newConfidence, confidence) && newSource == sourceMode
        && newSidechain == sidechainConnected)
        return;

    pitch = newPitch;
    pitches = newPitches;
    poly = newPoly;
    confidence = newConfidence;
    sourceMode = newSource;
    sidechainConnected = newSidechain;
    repaint (readoutArea.getUnion (sourceCaption).getUnion (source.getBounds().expanded (200, 0)));
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (14, 0);

    logoArea = r.removeFromLeft (76);
    r.removeFromLeft (10);
    sourceCaption = r.removeFromLeft (50);
    source.setBounds (r.removeFromLeft (196).withSizeKeepingCentre (196, 26));
    r.removeFromLeft (16);
    notesCaption = r.removeFromLeft (46);
    notes.setBounds (r.removeFromLeft (156).withSizeKeepingCentre (156, 26));

    auto right = r.removeFromRight (120);
    outCaption = right.removeFromLeft (32);
    outputGain.setBounds (right.withSizeKeepingCentre (right.getWidth(), 26));

    readoutArea = r.reduced (12, 0);
}

void TopBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::line);
    g.fillRect (getLocalBounds().removeFromBottom (1));

    // Name
    {
        juce::Font f (juce::FontOptions ("Helvetica Neue", 19.0f, juce::Font::plain));
        f.setExtraKerningFactor (0.06f);
        g.setFont (f);
        juce::AttributedString name;
        name.append ("Harm", f, theme::text);
        name.append ("EQ", f, theme::harmonic);
        name.setJustification (juce::Justification::centredLeft);
        name.draw (g, logoArea.toFloat());
    }

    drawCaption (g, "Source", sourceCaption);
    drawCaption (g, "Notes", notesCaption, juce::Justification::centredLeft,
                 sourceMode == PitchSource::midi ? theme::muted : theme::faint);
    drawCaption (g, "Out", outCaption);

    // Readout, right-aligned in the space between source and output: [warning] note cents Hz [meter|MIDI]
    auto r = readoutArea;
    const bool midi = sourceMode == PitchSource::midi;

    auto meter = r.removeFromRight (midi ? 40 : 56).withSizeKeepingCentre (midi ? 40 : 54, midi ? 18 : 6);
    if (midi)
    {
        g.setColour (theme::line);
        g.drawRoundedRectangle (meter.toFloat().reduced (0.5f), 3.0f, 1.0f);
        g.setColour (theme::muted);
        g.setFont (theme::caps (10.0f));
        g.drawText ("MIDI", meter, juce::Justification::centred, false);
    }
    else
    {
        g.setColour (theme::well);
        g.fillRoundedRectangle (meter.toFloat(), 3.0f);
        g.setColour (confidence > 0.5f ? theme::good : theme::faint);
        g.fillRoundedRectangle (meter.toFloat().withWidth (meter.getWidth() * juce::jlimit (0.0f, 1.0f, confidence)), 3.0f);
    }

    r.removeFromRight (12);
    g.setFont (theme::numbers (12.0f));

    if (poly && pitch.voiced)
    {
        // Poly: the chord, lowest note first.
        juce::Array<float> hz;
        for (const auto& p : pitches)
            if (p.voiced)
                hz.add (p.f0Hz);
        hz.sort();

        juce::StringArray names;
        for (float f : hz)
            names.add (theme::noteName (f));

        g.setColour (theme::text);
        g.setFont (theme::numbers (16.0f));
        g.drawFittedText (names.joinIntoString (" "), r, juce::Justification::centredRight, 1, 0.7f);
    }
    else if (pitch.voiced)
    {
        int cents = 0;
        const auto note = theme::noteName (pitch.f0Hz, &cents);
        g.setColour (theme::muted);
        g.drawText (theme::formatHz (pitch.f0Hz) + " Hz", r.removeFromRight (70), juce::Justification::centredRight, false);
        const juce::String minus (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
        g.drawText ((cents >= 0 ? "+" : minus) + juce::String (std::abs (cents)) + " ct", r.removeFromRight (56),
                    juce::Justification::centredRight, false);
        g.setColour (theme::text);
        g.setFont (theme::numbers (19.0f));
        g.drawText (note, r.removeFromRight (56), juce::Justification::centredRight, false);
    }
    else
    {
        g.setColour (theme::muted);
        g.drawText ("no pitch", r.removeFromRight (90), juce::Justification::centredRight, false);
    }

    if (sourceMode == PitchSource::sidechain && ! sidechainConnected)
    {
        r.removeFromRight (12);
        auto chip = r.removeFromRight (128).withSizeKeepingCentre (128, 20);
        if (chip.getX() >= readoutArea.getX())
        {
            g.setColour (theme::warn.withAlpha (0.5f));
            g.drawRoundedRectangle (chip.toFloat().reduced (0.5f), 4.0f, 1.0f);
            g.setColour (theme::warn);
            g.setFont (theme::caps (10.0f));
            g.drawText ("No sidechain signal", chip, juce::Justification::centred, false);
        }
    }
}

// ---------------------------------------------------------------------------------------------- tracking bar

TrackingBar::TrackingBar (HarmEQAudioProcessor& p)
{
    auto& apvts = p.getValueTreeState();

    attachments.push_back (attach (apvts, ParamIDs::glide, glide, [] (double v) { return juce::String (juce::roundToInt (v)) + " ms"; }));
    attachments.push_back (attach (apvts, ParamIDs::fallbackFreq, fallback, [] (double v) { return theme::formatHz (v) + " Hz"; }));
    attachments.push_back (attach (apvts, ParamIDs::transpose, transpose, signedSemis));
    attachments.push_back (attach (apvts, ParamIDs::pitchBendRange, bend, [] (double v) { return juce::String (juce::roundToInt (v)) + " st"; }));

    noPitch.addItemList ({ "Hold", "Fallback", "Bypass bands" }, 1);
    noPitch.getProperties().set ("arrow", true);
    noPitchAttachment = std::make_unique<ComboAttachment> (apvts, ParamIDs::noPitchMode, noPitch);

    glide.setTooltip ("Time to glide between pitches");
    noPitch.setTooltip ("What tracked bands do when there is no pitch");
    fallback.setTooltip ("Pitch used before the first note, and in Fallback mode");
    transpose.setTooltip ("Shift the tracked pitch");
    bend.setTooltip ("MIDI pitch-bend range");
    range.setTooltip ("Display range");
    analyzer.setTooltip ("Spectrum analyzer: input (Pre), output (Post) or both");

    fields = { { "Glide", &glide, 56, {} },        { "No pitch", &noPitch, 100, {} },
               { "Fallback", &fallback, 70, {} },  { "Transpose", &transpose, 52, {} },
               { "Bend", &bend, 46, {} },          { "", &range, 96, {} },
               { "Analyzer", &analyzer, 150, {} } };

    for (auto& f : fields)
        addAndMakeVisible (f.component);
}

void TrackingBar::resized()
{
    auto r = getLocalBounds().reduced (14, 0);
    const int gap = 12;

    auto place = [&] (Field& f, juce::Rectangle<int>& area, bool fromRight) {
        const int captionW = f.caption.isEmpty() ? 0
                                                 : juce::GlyphArrangement::getStringWidthInt (theme::caps (10.0f), f.caption.toUpperCase()) + 8;
        const int total = captionW + f.width;
        auto slot = fromRight ? area.removeFromRight (total) : area.removeFromLeft (total);
        f.captionArea = slot.removeFromLeft (captionW);
        f.component->setBounds (slot.withSizeKeepingCentre (slot.getWidth(), 24));
        fromRight ? area.removeFromRight (gap) : area.removeFromLeft (gap);
    };

    // Display options sit at the right; pitch-follow settings fill from the left.
    place (fields[6], r, true);
    place (fields[5], r, true);
    for (size_t i = 0; i < 5; ++i)
        place (fields[i], r, false);
}

void TrackingBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::line);
    g.fillRect (getLocalBounds().removeFromTop (1));
    g.fillRect (getLocalBounds().removeFromBottom (1));

    for (auto& f : fields)
        drawCaption (g, f.caption, f.captionArea);
}
} // namespace harmeq
