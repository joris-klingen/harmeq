#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
const juce::Identifier uiID { "UI" }, widthID { "width" }, heightID { "height" }, rangeID { "rangeDb" },
    analyzerID { "analyzer" };

constexpr float ranges[] { 6.0f, 12.0f, 24.0f };
} // namespace

HarmEQAudioProcessorEditor::HarmEQAudioProcessorEditor (HarmEQAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p), topBar (p), display (p), trackingBar (p), strip (p)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (topBar);
    addAndMakeVisible (display);
    addAndMakeVisible (trackingBar);
    addAndMakeVisible (strip);

    display.onBandSelected = [this] (int b) { selectBand (b); };
    strip.onBandSelected = [this] (int b) { selectBand (b); };

    // View options live in the plugin state so they come back with the project.
    auto ui = uiState();
    const int rangeIndex = juce::jlimit (0, 2, (int) ui.getProperty (rangeID, 2));
    const int analyzerIndex = juce::jlimit (0, 3, (int) ui.getProperty (analyzerID, 3));

    trackingBar.range.onChange = [this] (int i) {
        display.setRangeDb (ranges[i]);
        uiState().setProperty (rangeID, i, nullptr);
    };
    trackingBar.analyzer.onChange = [this] (int i) {
        display.setAnalyzerMode (static_cast<harmeq::AnalyzerMode> (i));
        uiState().setProperty (analyzerID, i, nullptr);
    };
    trackingBar.range.setSelected (rangeIndex);
    trackingBar.analyzer.setSelected (analyzerIndex);
    display.setRangeDb (ranges[rangeIndex]);
    display.setAnalyzerMode (static_cast<harmeq::AnalyzerMode> (analyzerIndex));

    setResizable (true, true);
    setResizeLimits (940, 560, 2000, 1400);
    setSize (juce::jlimit (940, 2000, (int) ui.getProperty (widthID, defaultWidth)),
             juce::jlimit (560, 1400, (int) ui.getProperty (heightID, defaultHeight)));

    timerCallback();
    startTimerHz (30);
}

HarmEQAudioProcessorEditor::~HarmEQAudioProcessorEditor()
{
    processor.setAnalyzerActive (false);
    setLookAndFeel (nullptr);
}

juce::ValueTree HarmEQAudioProcessorEditor::uiState()
{
    return processor.getValueTreeState().state.getOrCreateChildWithName (uiID, nullptr);
}

void HarmEQAudioProcessorEditor::selectBand (int band)
{
    selectedBand = band;
    display.setSelectedBand (band);
    strip.refresh (selectedBand);
}

void HarmEQAudioProcessorEditor::timerCallback()
{
    topBar.refresh();
    display.refresh();
    strip.refresh (selectedBand);
}

void HarmEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (harmeq::theme::background);
}

void HarmEQAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    topBar.setBounds (r.removeFromTop (48));
    strip.setBounds (r.removeFromBottom (186));
    trackingBar.setBounds (r.removeFromBottom (42));
    display.setBounds (r);

    auto ui = uiState();
    ui.setProperty (widthID, getWidth(), nullptr);
    ui.setProperty (heightID, getHeight(), nullptr);
}
