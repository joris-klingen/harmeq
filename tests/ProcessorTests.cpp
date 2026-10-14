#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"

using Catch::Matchers::WithinAbs;

namespace
{
juce::AudioProcessor::BusesLayout layout (juce::AudioChannelSet main, juce::AudioChannelSet sidechain)
{
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses = { main, sidechain };
    l.outputBuses = { main };
    return l;
}
} // namespace

TEST_CASE ("Processor reports MIDI input and sidechain bus")
{
    juce::ScopedJuceInitialiser_GUI juce;
    HarmEQAudioProcessor p;

    CHECK (p.acceptsMidi());
    CHECK_FALSE (p.producesMidi());
    REQUIRE (p.getBusCount (true) == 2);
    CHECK_FALSE (p.getBus (true, 1)->isEnabledByDefault());
}

TEST_CASE ("Bus layouts")
{
    juce::ScopedJuceInitialiser_GUI juce;
    HarmEQAudioProcessor p;

    using CS = juce::AudioChannelSet;

    CHECK (p.checkBusesLayoutSupported (layout (CS::stereo(), CS::disabled())));
    CHECK (p.checkBusesLayoutSupported (layout (CS::mono(), CS::disabled())));
    CHECK (p.checkBusesLayoutSupported (layout (CS::stereo(), CS::mono())));
    CHECK (p.checkBusesLayoutSupported (layout (CS::stereo(), CS::stereo())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (CS::create5point1(), CS::disabled())));
    CHECK_FALSE (p.checkBusesLayoutSupported (layout (CS::stereo(), CS::create5point1())));

    auto mismatched = layout (CS::mono(), CS::disabled());
    mismatched.outputBuses = { CS::stereo() };
    CHECK_FALSE (p.checkBusesLayoutSupported (mismatched));
}

TEST_CASE ("Parameter layout contains global and per-band parameters")
{
    juce::ScopedJuceInitialiser_GUI juce;
    HarmEQAudioProcessor p;
    auto& apvts = p.getValueTreeState();

    CHECK (apvts.getParameter (harmeq::ParamIDs::source) != nullptr);
    CHECK (apvts.getParameter (harmeq::ParamIDs::outputGain) != nullptr);

    for (int b = 0; b < harmeq::numBands; ++b)
        for (auto* suffix : { "on", "type", "track", "harmonic", "detune", "freq", "gain", "q", "slope" })
            CHECK (apvts.getParameter (harmeq::ParamIDs::band (b, suffix)) != nullptr);
}

TEST_CASE ("Passes audio through at unity and applies output gain")
{
    juce::ScopedJuceInitialiser_GUI juce;
    HarmEQAudioProcessor p;

    constexpr int blockSize = 256;
    p.setPlayConfigDetails (2, 2, 48000.0, blockSize);
    p.prepareToPlay (48000.0, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    auto fill = [&] {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (ch, i, 0.5f);
    };

    fill();
    p.processBlock (buffer, midi);
    CHECK_THAT (buffer.getSample (0, blockSize - 1), WithinAbs (0.5f, 1e-6f));

    auto* gain = p.getValueTreeState().getParameter (harmeq::ParamIDs::outputGain);
    gain->setValueNotifyingHost (gain->convertTo0to1 (-6.0f));

    // Run past the 20 ms smoothing time, then check the settled level.
    for (int n = 0; n < 10; ++n)
    {
        fill();
        p.processBlock (buffer, midi);
    }

    CHECK_THAT (buffer.getSample (1, blockSize - 1), WithinAbs (0.5f * juce::Decibels::decibelsToGain (-6.0f), 1e-4f));
}

TEST_CASE ("State round-trips")
{
    juce::ScopedJuceInitialiser_GUI juce;
    HarmEQAudioProcessor a, b;

    auto* q = a.getValueTreeState().getParameter (harmeq::ParamIDs::band (2, "q"));
    q->setValueNotifyingHost (0.8f);

    juce::MemoryBlock state;
    a.getStateInformation (state);
    b.setStateInformation (state.getData(), (int) state.getSize());

    CHECK_THAT (b.getValueTreeState().getParameter (harmeq::ParamIDs::band (2, "q"))->getValue(),
                WithinAbs (0.8f, 1e-6f));
}
