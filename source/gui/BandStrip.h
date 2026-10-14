#pragma once

#include "Controls.h"
#include "dsp/EqBand.h"

class HarmEQAudioProcessor;

namespace harmeq
{
// One band's controls:
//   [n] [harmonic | Hz] [on]
//   [curve      ] [Track]
//   (gain)  (Q)
//   (detune) [slope]
class BandColumn final : public juce::Component
{
public:
    BandColumn (HarmEQAudioProcessor&, int bandIndex);

    void refresh (const BandSettings&, bool isSelected);
    std::function<void()> onSelect;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void toggleTracking();

    HarmEQAudioProcessor& processor;
    const int index;
    const juce::Colour colour;

    ValueBox harmonicBox, freqBox;
    juce::ToggleButton onButton;
    juce::ComboBox typeBox, slopeBox;
    juce::TextButton trackButton { "Track" };
    Knob gain { "Gain", true }, q { "Q", false }, detune { "Detune", true };

    std::unique_ptr<SliderAttachment> harmonicAttachment, freqAttachment, gainAttachment, qAttachment, detuneAttachment;
    std::unique_ptr<ButtonAttachment> onAttachment;
    std::unique_ptr<ComboAttachment> typeAttachment, slopeAttachment;
    juce::ParameterAttachment trackAttachment;

    BandSettings shown;
    bool selected = false, firstRefresh = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandColumn)
};

class BandStrip final : public juce::Component
{
public:
    explicit BandStrip (HarmEQAudioProcessor&);

    void refresh (int selectedBand);
    std::function<void (int)> onBandSelected;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    HarmEQAudioProcessor& processor;
    juce::OwnedArray<BandColumn> columns;
};
} // namespace harmeq
