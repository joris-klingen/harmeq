#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/EqEngine.h"
#include "dsp/AnalyzerTap.h"
#include "dsp/PitchDetector.h"
#include "dsp/PitchTracker.h"

class HarmEQAudioProcessor final : public juce::AudioProcessor
{
public:
    HarmEQAudioProcessor();
    ~HarmEQAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }

    static BusesProperties createBusesProperties();

    // ---- State for the editor (all safe to call from the message thread) ----

    // Pitch the tracked bands are following (the lowest sounding note in Poly; 0 when they are
    // bypassed); updated per block.
    float getCurrentPitchHz() const noexcept
    {
        const auto p = getDisplayPitch();
        return p.voiced ? p.f0Hz : 0.0f;
    }

    // Per voice slot: last pitch and whether it is sounding. Mono modes use slot 0 only. Before audio
    // has run, slot 0 holds the fallback pitch.
    harmeq::PitchSet getDisplayPitches() const noexcept;

    // The pitch the display's nodes and labels follow: the lowest sounding slot, else slot 0.
    harmeq::PitchState getDisplayPitch() const noexcept;

    bool isPolyActive() const noexcept { return polyActive.load (std::memory_order_relaxed); }

    // Detection confidence (0–1) of the latest analysis frame, for the audio sources.
    float getPitchConfidence() const noexcept { return detectedConfidence.load (std::memory_order_relaxed); }

    bool isSidechainConnected() const noexcept { return sidechainConnected.load (std::memory_order_relaxed); }

    harmeq::BandSettings getBandSettings (int band) const noexcept;

    double getDisplaySampleRate() const noexcept { return getSampleRate() > 0.0 ? getSampleRate() : 48000.0; }

    // The analyzer taps are only fed while an editor wants them.
    void setAnalyzerActive (bool shouldBeActive) noexcept { analyzerActive.store (shouldBeActive); }
    harmeq::AnalyzerTap& getPreTap() noexcept { return preTap; }
    harmeq::AnalyzerTap& getPostTap() noexcept { return postTap; }

private:
    struct BandParams
    {
        std::atomic<float>* on = nullptr;
        std::atomic<float>* type = nullptr;
        std::atomic<float>* track = nullptr;
        std::atomic<float>* harmonic = nullptr;
        std::atomic<float>* detune = nullptr;
        std::atomic<float>* freq = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
        std::atomic<float>* slope = nullptr;
    };

    void updateSettings();
    void detectPitch (juce::AudioBuffer<float>& buffer, int start, int numSamples) noexcept;
    void applyOutputGain (juce::AudioBuffer<float>&);

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* sourceParam = nullptr;
    std::atomic<float>* midiModeParam = nullptr;
    std::atomic<float>* glideParam = nullptr;
    std::atomic<float>* noPitchModeParam = nullptr;
    std::atomic<float>* fallbackFreqParam = nullptr;
    std::atomic<float>* pitchBendRangeParam = nullptr;
    std::atomic<float>* transposeParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* minFreqParam = nullptr;
    std::atomic<float>* maxFreqParam = nullptr;
    std::atomic<float>* confidenceParam = nullptr;
    std::array<BandParams, harmeq::numBands> bandParams;

    harmeq::EqEngine engine;
    harmeq::PitchTracker pitchTracker;
    harmeq::PitchDetector detector;
    harmeq::PitchSource lastSource = harmeq::PitchSource::mainInput;
    std::atomic<float> detectedConfidence { 0.0f };
    std::array<std::atomic<float>, harmeq::maxVoices> displayPitchHz;
    std::array<std::atomic<bool>, harmeq::maxVoices> displayVoiced;
    std::atomic<bool> polyActive { false };
    std::atomic<bool> sidechainConnected { false };

    std::atomic<bool> analyzerActive { false };
    harmeq::AnalyzerTap preTap, postTap;
    juce::SmoothedValue<float> outputGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmEQAudioProcessor)
};
