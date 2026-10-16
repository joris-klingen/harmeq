#pragma once

#include "Controls.h"
#include "ResponseDisplay.h"

class HarmEQAudioProcessor;

namespace harmeq
{
// The plugin name with the current preset under it; click for the preset menu (factory and user presets,
// save, open folder).
class PresetSelector final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PresetSelector (HarmEQAudioProcessor&);

    void refresh();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    void showMenu();
    void askNameAndSave();

    HarmEQAudioProcessor& processor;
    juce::String shownName;
};

// Top bar: name and preset, pitch source, then either the MIDI note mode (MIDI) or the detection settings (audio
// sources), pitch readout, output gain.
class TopBar final : public juce::Component
{
public:
    explicit TopBar (HarmEQAudioProcessor&);

    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    HarmEQAudioProcessor& processor;
    PresetSelector presetSelector;
    ParameterSegmented source, notes;
    ValueBox outputGain, detectLow, detectHigh, detectConfidence;
    std::unique_ptr<SliderAttachment> outputAttachment, lowAttachment, highAttachment, confidenceAttachment;

    juce::Rectangle<int> logoArea, notesCaption, detectCaption, rangeDash, confidenceCaption,
        readoutArea, outCaption;
    float threshold = 0.5f;

    PitchState pitch;
    PitchSet pitches = monoPitch ({});
    bool poly = false;
    float confidence = 0.0f;
    PitchSource sourceMode = PitchSource::mainInput;
    bool sidechainConnected = true;
};

// Second row: how pitch is followed, plus display options (range, analyzer). The MIDI-only Bend field
// becomes the Lookahead switch when the source is audio.
class TrackingBar final : public juce::Component
{
public:
    explicit TrackingBar (HarmEQAudioProcessor&);

    void refresh();

    SegmentedControl range { { juce::String (juce::CharPointer_UTF8 ("\xc2\xb1" "6")),
                               juce::String (juce::CharPointer_UTF8 ("\xc2\xb1" "12")),
                               juce::String (juce::CharPointer_UTF8 ("\xc2\xb1" "24")) } };
    SegmentedControl analyzer { { "Off", "Pre", "Post", "Both" } };

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Field
    {
        juce::String caption;
        juce::Component* component;
        int width;
        juce::Rectangle<int> captionArea;
    };

    HarmEQAudioProcessor& processor;
    ValueBox glide, fallback, transpose, bend;
    juce::ComboBox noPitch;
    juce::TextButton lookahead { "Lookahead" };
    std::unique_ptr<ButtonAttachment> lookaheadAttachment;
    bool audioSource = false;
    int shownLatency = -1;
    std::vector<std::unique_ptr<SliderAttachment>> attachments;
    std::unique_ptr<ComboAttachment> noPitchAttachment;
    std::vector<Field> fields;
};
} // namespace harmeq
