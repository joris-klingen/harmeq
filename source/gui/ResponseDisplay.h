#pragma once

#include "SpectrumAnalyzer.h"
#include "Theme.h"
#include "dsp/EqBand.h"

#include <juce_audio_processors/juce_audio_processors.h>

class HarmEQAudioProcessor;

namespace harmeq
{
enum class AnalyzerMode { off, pre, post, both };

// The EQ curve display: frequency/dB grid, spectrum analyzer, harmonic lines that follow the pitch,
// per-band and combined response curves, and draggable band nodes.
class ResponseDisplay final : public juce::Component
{
public:
    explicit ResponseDisplay (HarmEQAudioProcessor&);

    // Called from the editor's timer: pulls pitch, settings and analyzer data; repaints if anything moved.
    void refresh();

    void setRangeDb (float db);
    void setAnalyzerMode (AnalyzerMode);
    void setSelectedBand (int band);
    std::function<void (int)> onBandSelected;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Snap points for tracked bands: ¼, ½, then whole harmonics up to 32.
    static double snapHarmonic (double h);

private:
    // ---- geometry ----
    float xOf (double hz) const;
    double hzOf (float x) const;
    float yOf (double db) const;
    double dbOf (float y) const;
    float analyzerY (double db) const;
    juce::Point<float> nodePosition (int band) const;
    int nodeAt (juce::Point<float>) const;

    // ---- data ----
    void recomputeCurves();
    juce::RangedAudioParameter* param (int band, const char* suffix) const;
    void beginGesture (juce::RangedAudioParameter*);
    void endGestures();
    static void setPlain (juce::RangedAudioParameter*, float value);
    void setComplete (juce::RangedAudioParameter*, float value);
    void selectBand (int band);
    void showBandMenu (int band);
    void addBandAt (juce::Point<float>);

    void drawGrid (juce::Graphics&);
    void drawAnalyzer (juce::Graphics&);
    void drawHarmonics (juce::Graphics&);
    void drawCurves (juce::Graphics&);
    void drawNodes (juce::Graphics&);
    void drawTooltip (juce::Graphics&);

    HarmEQAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;

    juce::Rectangle<float> plot;
    float rangeDb = 24.0f;
    AnalyzerMode analyzerMode = AnalyzerMode::both;

    std::array<BandSettings, numBands> settings {};
    std::array<float, numBands> shownAmount {};
    PitchState pitch;
    double sampleRate = 48000.0;
    float outputGainDb = 0.0f;
    bool curvesDirty = true;

    std::vector<double> columnHz;
    std::array<std::vector<float>, numBands> bandDb;
    std::vector<float> sumDb;

    SpectrumAnalyzer preAnalyzer, postAnalyzer;

    int hovered = -1, selected = 0;
    juce::Point<float> mousePos;

    struct DragState
    {
        int band = -1;
        juce::Point<float> start, nodeStart;
        float detuneStart = 0.0f;
        bool fine = false;
    } drag;

    juce::Array<juce::RangedAudioParameter*> gestures;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResponseDisplay)
};
} // namespace harmeq
