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

// ---------------------------------------------------------------------------------------------- presets

PresetSelector::PresetSelector (HarmEQAudioProcessor& p) : processor (p)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("Presets");
    refresh();
}

void PresetSelector::refresh()
{
    const auto name = processor.getPresetManager().getCurrentName();
    if (name != shownName)
    {
        shownName = name;
        repaint();
    }
}

void PresetSelector::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (0, 5);

    juce::Font f (juce::FontOptions ("Helvetica Neue", 18.0f, juce::Font::plain));
    f.setExtraKerningFactor (0.06f);
    juce::AttributedString title;
    title.append ("Harm", f, theme::text);
    title.append ("EQ", f, theme::harmonic);
    title.setJustification (juce::Justification::bottomLeft);
    title.draw (g, r.removeFromTop (r.getHeight() / 2 + 2).toFloat());

    g.setColour (isMouseOver() ? theme::text : theme::muted);
    g.setFont (theme::label (11.0f));
    g.drawFittedText (shownName + juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xbe")), r, juce::Justification::topLeft, 1, 0.8f);
}

void PresetSelector::mouseDown (const juce::MouseEvent&)
{
    showMenu();
}

void PresetSelector::showMenu()
{
    auto& presets = processor.getPresetManager();
    const auto factory = PresetManager::factoryNames();
    const auto user = presets.userPresets();
    const auto current = presets.getCurrentName();

    juce::PopupMenu menu;
    menu.addSectionHeader ("Factory");
    for (int i = 0; i < factory.size(); ++i)
        menu.addItem (1 + i, factory[i], true, factory[i] == current);

    menu.addSectionHeader ("Yours");
    if (user.isEmpty())
        menu.addItem (-1, "(none saved yet)", false);
    for (int i = 0; i < user.size(); ++i)
        menu.addItem (1000 + i, user[i].getFileNameWithoutExtension(), true, user[i].getFileNameWithoutExtension() == current);

    menu.addSeparator();
    menu.addItem (2000, "Save Preset...");
    menu.addItem (2001, "Show Presets Folder");

    juce::Component::SafePointer<PresetSelector> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, user] (int result) {
        if (safe == nullptr || result <= 0)
            return;

        auto& pm = safe->processor.getPresetManager();
        if (result < 1000)
            pm.loadFactory (result - 1);
        else if (result < 2000)
            pm.loadFile (user[result - 1000]);
        else if (result == 2000)
            safe->askNameAndSave();
        else if (result == 2001)
        {
            pm.getUserFolder().createDirectory();
            pm.getUserFolder().revealToUser();
        }
        safe->refresh();
    });
}

void PresetSelector::askNameAndSave()
{
    auto* window = new juce::AlertWindow ("Save Preset", "Name for the new preset:", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", processor.getPresetManager().getCurrentName());
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<PresetSelector> safe (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window] (int result) {
        if (safe == nullptr || result != 1)
            return;

        const auto outcome = safe->processor.getPresetManager().save (window->getTextEditorContents ("name"));
        if (outcome.failed())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't save the preset",
                                                    outcome.getErrorMessage());
        safe->refresh();
    }), true);
}

// ---------------------------------------------------------------------------------------------- top bar

TopBar::TopBar (HarmEQAudioProcessor& p)
    : processor (p),
      presetSelector (p),
      source (*p.getValueTreeState().getParameter (ParamIDs::source), { "Main", "Sidechain", "MIDI" }),
      notes (*p.getValueTreeState().getParameter (ParamIDs::midiMode), { "Last", "Lowest", "Poly" })
{
    addAndMakeVisible (presetSelector);
    addAndMakeVisible (source);
    addAndMakeVisible (notes);
    source.setTooltip ("Where the pitch comes from: this track's audio, the sidechain input, or MIDI");
    notes.setTooltip ("MIDI notes: follow the last or lowest held note, or every note of a chord (Poly)");

    auto& apvts = p.getValueTreeState();
    auto hz = [] (double v) { return theme::formatHz (v); };
    lowAttachment = attach (apvts, ParamIDs::minFreq, detectLow, hz);
    highAttachment = attach (apvts, ParamIDs::maxFreq, detectHigh, hz);
    confidenceAttachment = attach (apvts, ParamIDs::confidence, detectConfidence, [] (double v) { return juce::String (v, 2); });
    detectLow.setTooltip ("Lowest pitch to detect (Hz). Raising it makes detection faster and cheaper");
    detectHigh.setTooltip ("Highest pitch to detect (Hz)");
    detectConfidence.setTooltip ("Minimum detection confidence. Raise it if the pitch jumps around on noisy material");
    for (auto* c : { &detectLow, &detectHigh, &detectConfidence })
        addChildComponent (c);
    outputAttachment = attach (p.getValueTreeState(), ParamIDs::outputGain, outputGain,
                               [] (double v) { return theme::formatDb (v); });
    outputGain.setTooltip ("Output gain");
    addAndMakeVisible (outputGain);
    refresh();
}

void TopBar::refresh()
{
    presetSelector.refresh();

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

    const bool midiSource = newSource == PitchSource::midi;
    notes.setVisible (midiSource);
    for (auto* c : { &detectLow, &detectHigh, &detectConfidence })
        c->setVisible (! midiSource);

    const float newThreshold = processor.getValueTreeState().getRawParameterValue (ParamIDs::confidence)->load();
    if (! juce::approximatelyEqual (newThreshold, threshold))
    {
        threshold = newThreshold;
        pitchesChanged = true;
    }

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
    repaint();
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (14, 0);

    logoArea = r.removeFromLeft (140);
    presetSelector.setBounds (logoArea);
    r.removeFromLeft (8);
    source.setBounds (r.removeFromLeft (196).withSizeKeepingCentre (196, 26));
    r.removeFromLeft (16);
    // The MIDI note mode and the detection settings share this space; only one shows at a time.
    auto modeArea = r.removeFromLeft (238);
    notesCaption = modeArea.withWidth (46);
    notes.setBounds (modeArea.withTrimmedLeft (46).withWidth (156).withSizeKeepingCentre (156, 26));

    auto d = modeArea;
    detectCaption = d.removeFromLeft (50);
    detectLow.setBounds (d.removeFromLeft (48).withSizeKeepingCentre (48, 26));
    rangeDash = d.removeFromLeft (12);
    detectHigh.setBounds (d.removeFromLeft (50).withSizeKeepingCentre (50, 26));
    d.removeFromLeft (10);
    confidenceCaption = d.removeFromLeft (34);
    detectConfidence.setBounds (d.removeFromLeft (44).withSizeKeepingCentre (44, 26));

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

    if (sourceMode == PitchSource::midi)
    {
        drawCaption (g, "Notes", notesCaption);
    }
    else
    {
        drawCaption (g, "Detect", detectCaption);
        drawCaption (g, "Conf", confidenceCaption);
        g.setColour (theme::muted);
        g.setFont (theme::numbers (12.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")), rangeDash, juce::Justification::centred, false);
    }
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
        // Confidence bar, green when it passes the threshold (the tick).
        g.setColour (theme::well);
        g.fillRoundedRectangle (meter.toFloat(), 3.0f);
        g.setColour (confidence >= threshold ? theme::good : theme::faint);
        g.fillRoundedRectangle (meter.toFloat().withWidth (meter.getWidth() * juce::jlimit (0.0f, 1.0f, confidence)), 3.0f);
        g.setColour (theme::muted);
        const float tickX = (float) meter.getX() + (float) meter.getWidth() * juce::jlimit (0.0f, 1.0f, threshold);
        g.drawLine (tickX, (float) meter.getY() - 3.0f, tickX, (float) meter.getBottom() + 3.0f, 1.0f);
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
        g.drawText (theme::formatHz (pitch.f0Hz) + " Hz", r.removeFromRight (64), juce::Justification::centredRight, false);
        const juce::String minus (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
        g.drawText ((cents >= 0 ? "+" : minus) + juce::String (std::abs (cents)) + " ct", r.removeFromRight (50),
                    juce::Justification::centredRight, false);
        g.setColour (theme::text);
        g.setFont (theme::numbers (19.0f));
        g.drawText (note, r.removeFromRight (50), juce::Justification::centredRight, false);
    }
    else if (sourceMode == PitchSource::sidechain && ! sidechainConnected)
    {
        // Without a sidechain there can be no pitch; say why instead.
        auto chip = r.removeFromRight (128).withSizeKeepingCentre (128, 20);
        g.setColour (theme::warn.withAlpha (0.5f));
        g.drawRoundedRectangle (chip.toFloat().reduced (0.5f), 4.0f, 1.0f);
        g.setColour (theme::warn);
        g.setFont (theme::caps (10.0f));
        g.drawText ("No sidechain signal", chip, juce::Justification::centred, false);
    }
    else
    {
        g.setColour (theme::muted);
        g.drawText ("no pitch", r.removeFromRight (90), juce::Justification::centredRight, false);
    }
}

// ---------------------------------------------------------------------------------------------- tracking bar

TrackingBar::TrackingBar (HarmEQAudioProcessor& p) : processor (p)
{
    auto& apvts = p.getValueTreeState();

    lookahead.setClickingTogglesState (true);
    lookahead.setColour (juce::TextButton::buttonOnColourId, theme::well.interpolatedWith (theme::harmonic, 0.25f));
    lookaheadAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::lookahead, lookahead);
    addChildComponent (lookahead);

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

    refresh();
}

void TrackingBar::refresh()
{
    const bool audio = static_cast<PitchSource> ((int) processor.getValueTreeState().getRawParameterValue (ParamIDs::source)->load())
                       != PitchSource::midi;
    const int latency = processor.getLookaheadSamples();

    if (audio != audioSource)
    {
        audioSource = audio;
        bend.setVisible (! audio);
        lookahead.setVisible (audio);
        repaint();
    }

    if (latency != shownLatency)
    {
        shownLatency = latency;
        const auto ms = juce::String (1000.0 * latency / processor.getDisplaySampleRate(), 0);
        lookahead.setTooltip (latency > 0 ? "Lookahead on: the audio is delayed by " + ms
                                                + " ms (reported to the host) so bands land on note changes"
                                          : "Delay the audio by the detector's lag (about two periods of the "
                                            "Detect low pitch) so bands land exactly on note changes");
    }
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

    // Lookahead takes the whole Bend slot (caption included).
    lookahead.setBounds (fields[4].captionArea.getUnion (bend.getBounds()).withSizeKeepingCentre (
        fields[4].captionArea.getUnion (bend.getBounds()).getWidth(), 24));
}

void TrackingBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::line);
    g.fillRect (getLocalBounds().removeFromTop (1));
    g.fillRect (getLocalBounds().removeFromBottom (1));

    for (auto& f : fields)
        if (! (audioSource && f.component == &bend))
            drawCaption (g, f.caption, f.captionArea);
}
} // namespace harmeq
