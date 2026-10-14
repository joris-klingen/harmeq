#include "BandStrip.h"
#include "PluginProcessor.h"
#include "ResponseDisplay.h"

namespace harmeq
{
namespace
{
    bool usesGain (FilterType t) { return t == FilterType::bell || t == FilterType::lowShelf || t == FilterType::highShelf; }
    bool isPass (FilterType t) { return t == FilterType::highPass || t == FilterType::lowPass; }

    juce::String signedCents (double c)
    {
        const int v = juce::roundToInt (c);
        return (v > 0 ? "+" : v < 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String())
               + juce::String (std::abs (v)) + " ct";
    }
} // namespace

BandColumn::BandColumn (HarmEQAudioProcessor& p, int bandIndex)
    : processor (p),
      index (bandIndex),
      colour (theme::band (bandIndex)),
      trackAttachment (*p.getValueTreeState().getParameter (ParamIDs::band (bandIndex, "track")),
                       [this] (float v) { trackButton.setToggleState (v < 0.5f, juce::dontSendNotification); })
{
    auto& apvts = processor.getValueTreeState();
    auto id = [&] (const char* suffix) { return ParamIDs::band (index, suffix); };

    harmonicBox.snap = [] (double v) { return ResponseDisplay::snapHarmonic (v); };
    harmonicAttachment = attach (apvts, id ("harmonic"), harmonicBox, [] (double v) { return theme::formatHarmonic (v); });
    freqAttachment = attach (apvts, id ("freq"), freqBox, [] (double v) { return theme::formatHz (v); });
    harmonicBox.setTooltip ("Harmonic of the tracked pitch");
    freqBox.setTooltip ("Frequency (Hz)");
    addAndMakeVisible (harmonicBox);
    addChildComponent (freqBox);

    onButton.setColour (juce::ToggleButton::tickColourId, colour);
    onButton.setTooltip ("Band on/off");
    onAttachment = std::make_unique<ButtonAttachment> (apvts, id ("on"), onButton);
    addAndMakeVisible (onButton);

    typeBox.addItemList ({ "Bell", "Lo Shelf", "Hi Shelf", "Hi Pass", "Lo Pass", "Notch" }, 1);
    typeAttachment = std::make_unique<ComboAttachment> (apvts, id ("type"), typeBox);
    typeBox.setTooltip ("Curve");
    addAndMakeVisible (typeBox);

    trackButton.setColour (juce::TextButton::buttonOnColourId, theme::well.interpolatedWith (colour, 0.25f));
    trackButton.setTooltip ("Follow the pitch (off: fixed frequency)");
    trackButton.onClick = [this] { toggleTracking(); };
    addAndMakeVisible (trackButton);
    trackAttachment.sendInitialUpdate();

    gain.setAccent (colour);
    q.setAccent (colour);
    detune.setAccent (colour);
    gain.format = [] (double v) { return theme::formatDb (v, false); };
    q.format = [] (double v) { return juce::String (v, 2); };
    detune.format = signedCents;
    gainAttachment = attach (apvts, id ("gain"), gain.slider);
    qAttachment = attach (apvts, id ("q"), q.slider);
    detuneAttachment = attach (apvts, id ("detune"), detune.slider);
    for (auto* k : { &gain, &q, &detune })
        addAndMakeVisible (k);

    slopeBox.addItemList ({ "12", "24" }, 1);
    slopeAttachment = std::make_unique<ComboAttachment> (apvts, id ("slope"), slopeBox);
    slopeBox.setTooltip ("Slope (dB/oct), high and low pass only");
    addAndMakeVisible (slopeBox);

    for (auto* c : std::initializer_list<juce::Component*> { &harmonicBox, &freqBox, &onButton, &typeBox, &trackButton, &gain, &q, &detune, &slopeBox })
        c->addMouseListener (this, true);
}

void BandColumn::toggleTracking()
{
    const auto s = processor.getBandSettings (index);

    if (s.track == TrackMode::tracked)
    {
        // Keep the band where it currently sits when it stops tracking.
        const auto f = EqBand::resolveTarget (s, processor.getDisplayPitch(), processor.getDisplaySampleRate()).freqHz;
        auto* freq = processor.getValueTreeState().getParameter (ParamIDs::band (index, "freq"));
        freq->beginChangeGesture();
        freq->setValueNotifyingHost (freq->convertTo0to1 ((float) juce::jlimit (20.0, 20000.0, f)));
        freq->endChangeGesture();
    }

    trackAttachment.setValueAsCompleteGesture (s.track == TrackMode::tracked ? 1.0f : 0.0f);
}

void BandColumn::mouseDown (const juce::MouseEvent&)
{
    if (onSelect)
        onSelect();
}

void BandColumn::refresh (const BandSettings& s, bool isSelected)
{
    const bool layoutChanged = firstRefresh || s.track != shown.track;
    const bool look = firstRefresh || s.on != shown.on || s.type != shown.type || isSelected != selected || layoutChanged;
    shown = s;
    selected = isSelected;
    firstRefresh = false;

    if (layoutChanged)
    {
        harmonicBox.setVisible (s.track == TrackMode::tracked);
        freqBox.setVisible (s.track == TrackMode::fixed);
    }

    if (look)
    {
        gain.setEnabled (usesGain (s.type));
        detune.setEnabled (s.track == TrackMode::tracked);
        slopeBox.setEnabled (isPass (s.type));

        const float alpha = s.on ? 1.0f : 0.38f;
        for (auto* c : std::initializer_list<juce::Component*> { &harmonicBox, &freqBox, &typeBox, &trackButton, &gain, &q, &detune, &slopeBox })
            c->setAlpha (alpha);

        repaint();
    }
}

void BandColumn::paint (juce::Graphics& g)
{
    if (selected)
        g.fillAll (colour.withAlpha (0.06f));

    g.setColour (colour.withAlpha (selected ? 1.0f : 0.25f));
    g.fillRect (getLocalBounds().removeFromTop (2));

    g.setColour (theme::lineSoft);
    g.fillRect (getLocalBounds().removeFromRight (1));

    g.setColour (colour);
    g.setFont (theme::numbers (13.0f, true));
    g.drawText (juce::String (index + 1), juce::Rectangle<int> (8, 8, 14, 22), juce::Justification::centredLeft, false);

    drawCaption (g, "dB/oct", slopeBox.getBounds().withSizeKeepingCentre (64, 12).withY (slopeBox.getBottom() + 1),
                 juce::Justification::centred, slopeBox.isEnabled() ? theme::muted : theme::faint);
}

void BandColumn::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    r.removeFromTop (8);

    auto row1 = r.removeFromTop (22);
    row1.removeFromLeft (16);
    onButton.setBounds (row1.removeFromRight (32));
    row1.removeFromRight (4);
    harmonicBox.setBounds (row1);
    freqBox.setBounds (row1);

    r.removeFromTop (6);
    auto row2 = r.removeFromTop (22);
    trackButton.setBounds (row2.removeFromRight (48));
    row2.removeFromRight (4);
    typeBox.setBounds (row2);

    r.removeFromTop (6);
    auto row3 = r.removeFromTop (58);
    gain.setBounds (row3.removeFromLeft (row3.getWidth() / 2));
    q.setBounds (row3);

    r.removeFromTop (2);
    auto row4 = r.removeFromTop (58);
    detune.setBounds (row4.removeFromLeft (row4.getWidth() / 2));
    slopeBox.setBounds (row4.withSizeKeepingCentre (36, 20).withY (row4.getY() + 8));
}

// ---------------------------------------------------------------------------------------------- strip

BandStrip::BandStrip (HarmEQAudioProcessor& p) : processor (p)
{
    for (int b = 0; b < numBands; ++b)
    {
        auto* c = columns.add (new BandColumn (processor, b));
        c->onSelect = [this, b] {
            if (onBandSelected)
                onBandSelected (b);
        };
        addAndMakeVisible (c);
    }
}

void BandStrip::refresh (int selectedBand)
{
    for (int b = 0; b < numBands; ++b)
        columns[b]->refresh (processor.getBandSettings (b), b == selectedBand);
}

void BandStrip::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void BandStrip::resized()
{
    auto r = getLocalBounds();
    const int w = r.getWidth();
    for (int b = 0; b < numBands; ++b)
    {
        const int x0 = w * b / numBands, x1 = w * (b + 1) / numBands;
        columns[b]->setBounds (x0, 0, x1 - x0, r.getHeight());
    }
}
} // namespace harmeq
