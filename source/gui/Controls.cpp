#include "Controls.h"

namespace harmeq
{
DragSlider::DragSlider()
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setScrollWheelEnabled (false);
}

void DragSlider::updateSensitivity (const juce::ModifierKeys& mods)
{
    setMouseDragSensitivity (mods.isShiftDown() ? 900 : 180);
}

void DragSlider::mouseDown (const juce::MouseEvent& e)
{
    updateSensitivity (e.mods);
    Slider::mouseDown (e);
}

void DragSlider::mouseDrag (const juce::MouseEvent& e)
{
    updateSensitivity (e.mods);
    Slider::mouseDrag (e);
}

std::unique_ptr<SliderAttachment> attach (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
                                          juce::Slider& slider, std::function<juce::String (double)> format)
{
    auto a = std::make_unique<SliderAttachment> (apvts, paramID, slider);

    // The attachment installs the parameter's own text; ours is more compact.
    if (format)
        slider.textFromValueFunction = std::move (format);

    if (auto* p = apvts.getParameter (paramID))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));

    return a;
}

// ---- Knob ----
Knob::Knob (const juce::String& captionText, bool bipolar) : caption (captionText)
{
    slider.getProperties().set ("bipolar", bipolar);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    const int size = juce::jmin (r.getWidth(), r.getHeight() - 26);
    slider.setBounds (r.removeFromTop (size).withSizeKeepingCentre (size, size));
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    r.removeFromTop (slider.getBottom());

    const auto value = format ? format (slider.getValue()) : slider.getTextFromValue (slider.getValue());
    g.setColour (isEnabled() ? theme::text : theme::faint);
    g.setFont (theme::numbers (11.0f));
    g.drawFittedText (isEnabled() ? value : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")),
                      r.removeFromTop (13), juce::Justification::centred, 1, 0.8f);
    drawCaption (g, caption, r.removeFromTop (12), juce::Justification::centred,
                 isEnabled() ? theme::muted : theme::faint);
}

// ---- ValueBox ----
ValueBox::ValueBox()
{
    setSliderStyle (juce::Slider::LinearBarVertical);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

// ---- SegmentedControl ----
SegmentedControl::SegmentedControl (const juce::StringArray& labels)
{
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (labels[i]));
        b->getProperties().set ("segment", labels.size() == 1 ? "" : i == 0 ? "left" : i == labels.size() - 1 ? "right" : "mid");
        b->setClickingTogglesState (false);
        b->onClick = [this, i] { setSelected (i, juce::sendNotification); };
        addAndMakeVisible (b);
    }
}

void SegmentedControl::setSelected (int index, juce::NotificationType notification)
{
    const bool changed = index != selected;
    selected = index;

    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (i == index, juce::dontSendNotification);

    if (changed && notification != juce::dontSendNotification && onChange)
        onChange (index);
}

void SegmentedControl::resized()
{
    if (buttons.isEmpty())
        return;

    // Share the width out by label length so short labels ("±6") don't waste space.
    juce::Array<int> weights;
    int total = 0;
    for (auto* b : buttons)
    {
        weights.add (b->getButtonText().length() + 3);
        total += weights.getLast();
    }

    const auto r = getLocalBounds();
    int cumulative = 0;
    for (int i = 0; i < buttons.size(); ++i)
    {
        const int x0 = r.getX() + r.getWidth() * cumulative / total;
        cumulative += weights[i];
        const int x1 = r.getX() + r.getWidth() * cumulative / total;
        // Adjacent segments overlap by one pixel so their borders merge.
        buttons[i]->setBounds (x0, r.getY(), x1 - x0 + (i < buttons.size() - 1 ? 1 : 0), r.getHeight());
    }
}

ParameterSegmented::ParameterSegmented (juce::RangedAudioParameter& param, const juce::StringArray& labels)
    : SegmentedControl (labels),
      attachment (param, [this] (float value) { setSelected (juce::roundToInt (value)); })
{
    onChange = [this] (int index) { attachment.setValueAsCompleteGesture ((float) index); };
    attachment.sendInitialUpdate();
}

void drawCaption (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                  juce::Justification just, juce::Colour colour)
{
    g.setColour (colour);
    g.setFont (theme::caps (10.0f));
    g.drawText (text.toUpperCase(), area, just, false);
}
} // namespace harmeq
