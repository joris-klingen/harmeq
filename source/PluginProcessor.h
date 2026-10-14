#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/EqEngine.h"
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

    // Pitch the tracked bands are following (0 when they are bypassed). For the UI; updated per block.
    float getCurrentPitchHz() const noexcept { return currentPitchHz.load (std::memory_order_relaxed); }

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
    void applyOutputGain (juce::AudioBuffer<float>&);

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* sourceParam = nullptr;
    std::atomic<float>* glideParam = nullptr;
    std::atomic<float>* noPitchModeParam = nullptr;
    std::atomic<float>* fallbackFreqParam = nullptr;
    std::atomic<float>* pitchBendRangeParam = nullptr;
    std::atomic<float>* transposeParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    std::array<BandParams, harmeq::numBands> bandParams;

    harmeq::EqEngine engine;
    harmeq::PitchTracker pitchTracker;
    std::atomic<float> currentPitchHz { 0.0f };
    juce::SmoothedValue<float> outputGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmEQAudioProcessor)
};
