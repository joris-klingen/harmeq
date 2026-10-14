#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class HarmEQAudioProcessor;

// Placeholder editor: exposes every parameter through JUCE's generic UI until the custom GUI (milestone 6).
class HarmEQAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit HarmEQAudioProcessorEditor (HarmEQAudioProcessor&);

    void resized() override;

private:
    juce::GenericAudioProcessorEditor genericEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmEQAudioProcessorEditor)
};
