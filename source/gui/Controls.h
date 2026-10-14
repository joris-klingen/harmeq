#pragma once

#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace harmeq
{
using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

// Slider that drags vertically, goes fine with Shift held, and double-clicks back to its default.
class DragSlider : public juce::Slider
{
public:
    DragSlider();

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    // Optional quantiser applied to every value the user sets (e.g. whole harmonics).
    std::function<double (double)> snap;

protected:
    double snapValue (double attemptedValue, DragMode) override { return snap ? snap (attemptedValue) : attemptedValue; }

private:
    void updateSensitivity (const juce::ModifierKeys&);
};

// Attaches a slider to a parameter, keeps our own text formatting, and sets double-click to default.
std::unique_ptr<SliderAttachment> attach (juce::AudioProcessorValueTreeState&, const juce::String& paramID,
                                          juce::Slider&, std::function<juce::String (double)> format = {});

// Knob with its value printed underneath and a caption below that.
class Knob : public juce::Component
{
public:
    Knob (const juce::String& caption, bool bipolar);

    DragSlider slider;
    std::function<juce::String (double)> format;

    void setAccent (juce::Colour c) { slider.setColour (juce::Slider::rotarySliderFillColourId, c); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void enablementChanged() override { repaint(); }

private:
    juce::String caption;
};

// Small boxed value dragged up/down (Glide, Fallback, harmonic, …).
class ValueBox : public DragSlider
{
public:
    ValueBox();
};

// Row of mutually exclusive buttons.
class SegmentedControl : public juce::Component
{
public:
    explicit SegmentedControl (const juce::StringArray& labels);

    void setSelected (int index, juce::NotificationType = juce::dontSendNotification);
    int getSelected() const noexcept { return selected; }
    void setTooltip (const juce::String& tip)
    {
        for (auto* b : buttons)
            b->setTooltip (tip);
    }
    std::function<void (int)> onChange;

    void resized() override;

private:
    juce::OwnedArray<juce::TextButton> buttons;
    int selected = -1;
};

// SegmentedControl bound to a choice parameter.
class ParameterSegmented : public SegmentedControl
{
public:
    ParameterSegmented (juce::RangedAudioParameter&, const juce::StringArray& labels);

private:
    juce::ParameterAttachment attachment;
};

// Caption text in the small spaced capitals used for labels.
void drawCaption (juce::Graphics&, const juce::String& text, juce::Rectangle<int> area,
                  juce::Justification = juce::Justification::centredLeft, juce::Colour = theme::muted);
} // namespace harmeq
