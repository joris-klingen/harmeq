#pragma once

#include "Theme.h"

namespace harmeq
{
// Flat dark look. Per-component tweaks are passed through component properties:
//   Slider   "bipolar"  – value arc starts at the centre (gain, detune)
//   ComboBox "arrow"    – draw a drop-down arrow (off by default)
//   Button   "segment"  – "left" | "mid" | "right" for segmented controls
// Accent colours come from the usual colour IDs (rotarySliderFillColourId, buttonOnColourId,
// ToggleButton::tickColourId).
class HarmLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    HarmLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minPos,
                           float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    juce::Font getPopupMenuFont() override { return theme::label (13.0f); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmLookAndFeel)
};
} // namespace harmeq
