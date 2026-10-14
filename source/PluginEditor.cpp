#include "PluginEditor.h"
#include "PluginProcessor.h"

HarmEQAudioProcessorEditor::HarmEQAudioProcessorEditor (HarmEQAudioProcessor& p)
    : AudioProcessorEditor (p), genericEditor (p)
{
    addAndMakeVisible (genericEditor);
    setResizable (true, true);
    setResizeLimits (400, 300, 1600, 1200);
    setSize (600, 600);
}

void HarmEQAudioProcessorEditor::resized()
{
    genericEditor.setBounds (getLocalBounds());
}
