#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "gui/BandStrip.h"
#include "gui/HeaderBars.h"
#include "gui/LookAndFeel.h"
#include "gui/ResponseDisplay.h"

class HarmEQAudioProcessor;

class HarmEQAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HarmEQAudioProcessorEditor (HarmEQAudioProcessor&);
    ~HarmEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Runs one UI update immediately (the timer does this 30 times a second).
    void refreshNow() { timerCallback(); }

    static constexpr int defaultWidth = 1000, defaultHeight = 620;

    harmeq::ResponseDisplay& getDisplay() noexcept { return display; }

private:
    void timerCallback() override;
    void selectBand (int band);
    juce::ValueTree uiState();

    HarmEQAudioProcessor& processor;
    harmeq::HarmLookAndFeel lookAndFeel;

    harmeq::TopBar topBar;
    harmeq::ResponseDisplay display;
    harmeq::TrackingBar trackingBar;
    harmeq::BandStrip strip;
    juce::TooltipWindow tooltips { this, 600 };

    int selectedBand = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmEQAudioProcessorEditor)
};
