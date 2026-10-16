#pragma once

#include "Controls.h"
#include "ResponseDisplay.h"

class HarmEQAudioProcessor;

namespace harmeq
{
// Top bar: name, pitch source, then either the MIDI note mode (MIDI) or the detection settings (audio
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
    ParameterSegmented source, notes;
    ValueBox outputGain, detectLow, detectHigh, detectConfidence;
    std::unique_ptr<SliderAttachment> outputAttachment, lowAttachment, highAttachment, confidenceAttachment;

    juce::Rectangle<int> logoArea, sourceCaption, notesCaption, detectCaption, rangeDash, confidenceCaption,
        readoutArea, outCaption;
    float threshold = 0.5f;

    PitchState pitch;
    PitchSet pitches = monoPitch ({});
    bool poly = false;
    float confidence = 0.0f;
    PitchSource sourceMode = PitchSource::mainInput;
    bool sidechainConnected = true;
};

// Second row: how pitch is followed, plus display options (range, analyzer).
class TrackingBar final : public juce::Component
{
public:
    explicit TrackingBar (HarmEQAudioProcessor&);

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

    ValueBox glide, fallback, transpose, bend;
    juce::ComboBox noPitch;
    std::vector<std::unique_ptr<SliderAttachment>> attachments;
    std::unique_ptr<ComboAttachment> noPitchAttachment;
    std::vector<Field> fields;
};
} // namespace harmeq
