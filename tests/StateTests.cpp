#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"

using namespace harmeq;
using Catch::Matchers::WithinAbs;

namespace
{
void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    auto* p = apvts.getParameter (id);
    REQUIRE (p != nullptr);
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}

float value (HarmEQAudioProcessor& p, const juce::String& id)
{
    return p.getValueTreeState().getRawParameterValue (id)->load();
}

bool isChoice (HarmEQAudioProcessor& p, const juce::String& id, int index)
{
    return juce::roundToInt (value (p, id)) == index;
}

juce::MemoryBlock saveState (HarmEQAudioProcessor& p)
{
    juce::MemoryBlock block;
    p.getStateInformation (block);
    return block;
}

struct TempFolder
{
    juce::File dir = juce::File::createTempFile ("harmeq-presets");
    TempFolder() { dir.createDirectory(); }
    ~TempFolder() { dir.deleteRecursively(); }
};
} // namespace

TEST_CASE ("State records its version and round-trips")
{
    juce::ScopedJuceInitialiser_GUI juce;
    HarmEQAudioProcessor a, b;
    setParam (a.getValueTreeState(), ParamIDs::band (4, "gain"), -7.5f);
    setParam (a.getValueTreeState(), ParamIDs::midiMode, (float) MidiMode::poly);

    const auto block = saveState (a);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
    REQUIRE (xml != nullptr);
    CHECK (xml->getIntAttribute ("stateVersion") == state::currentVersion);

    b.setStateInformation (block.getData(), (int) block.getSize());
    CHECK_THAT (value (b, ParamIDs::band (4, "gain")), WithinAbs (-7.5, 1e-4));
    CHECK (isChoice (b, ParamIDs::midiMode, (int) MidiMode::poly));
}

TEST_CASE ("Loading an older state resets parameters it didn't have to their defaults")
{
    juce::ScopedJuceInitialiser_GUI juce;

    // A version-1 project: no stateVersion, no midiMode parameter.
    HarmEQAudioProcessor source;
    setParam (source.getValueTreeState(), ParamIDs::band (0, "gain"), 4.0f);
    auto tree = source.getValueTreeState().copyState();
    tree.removeProperty (state::versionID, nullptr);
    for (int i = tree.getNumChildren(); --i >= 0;)
        if (tree.getChild (i)["id"].toString() == ParamIDs::midiMode)
            tree.removeChild (i, nullptr);
    juce::MemoryBlock oldProject;
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), oldProject);

    // Loaded into an instance that had Poly selected, MIDI Notes must come back as the default.
    HarmEQAudioProcessor target;
    setParam (target.getValueTreeState(), ParamIDs::midiMode, (float) MidiMode::poly);
    target.setStateInformation (oldProject.getData(), (int) oldProject.getSize());

    CHECK (isChoice (target, ParamIDs::midiMode, (int) MidiMode::lastNote));
    CHECK_THAT (value (target, ParamIDs::band (0, "gain")), WithinAbs (4.0, 1e-4));
}

TEST_CASE ("Window and display settings survive a state load")
{
    juce::ScopedJuceInitialiser_GUI juce;
    HarmEQAudioProcessor a, b;
    a.getValueTreeState().state.getOrCreateChildWithName (state::uiID, nullptr).setProperty ("width", 1234, nullptr);

    const auto block = saveState (a);
    b.setStateInformation (block.getData(), (int) block.getSize());
    CHECK ((int) b.getValueTreeState().state.getChildWithName (state::uiID).getProperty ("width") == 1234);
}

TEST_CASE ("Factory presets load completely")
{
    juce::ScopedJuceInitialiser_GUI juce;
    HarmEQAudioProcessor proc;
    auto& pm = proc.getPresetManager();
    const auto names = PresetManager::factoryNames();
    REQUIRE (names.size() >= 6);

    for (int i = 0; i < names.size(); ++i)
    {
        // Dirty every band first: a preset must set (or default) everything, not layer on top.
        for (int b = 0; b < numBands; ++b)
        {
            setParam (proc.getValueTreeState(), ParamIDs::band (b, "on"), 1.0f);
            setParam (proc.getValueTreeState(), ParamIDs::band (b, "gain"), 9.0f);
        }

        pm.loadFactory (i);
        INFO ("preset " << names[i]);
        CHECK (pm.getCurrentName() == names[i]);

        int bandsOn = 0;
        for (int b = 0; b < numBands; ++b)
        {
            bandsOn += value (proc, ParamIDs::band (b, "on")) > 0.5f ? 1 : 0;
            CHECK (value (proc, ParamIDs::band (b, "gain")) < 9.0f);
        }
        CHECK (bandsOn > 0);
        CHECK (bandsOn < numBands);
    }

    // Spot-check one preset's content.
    pm.loadFactory (PresetManager::factoryNames().indexOf ("Pads: Tame the Boom (Poly)"));
    CHECK (isChoice (proc, ParamIDs::midiMode, (int) MidiMode::poly));
    CHECK (isChoice (proc, ParamIDs::source, (int) PitchSource::midi));
    CHECK_THAT (value (proc, ParamIDs::band (0, "gain")), WithinAbs (-6.0, 1e-4));
}

TEST_CASE ("User presets save and load")
{
    juce::ScopedJuceInitialiser_GUI juce;
    TempFolder folder;

    HarmEQAudioProcessor a, b;
    a.getPresetManager().setUserFolder (folder.dir);
    b.getPresetManager().setUserFolder (folder.dir);

    setParam (a.getValueTreeState(), ParamIDs::band (2, "harmonic"), 5.0f);
    setParam (a.getValueTreeState(), ParamIDs::band (2, "gain"), -3.0f);
    setParam (a.getValueTreeState(), ParamIDs::glide, 120.0f);
    a.getValueTreeState().state.getOrCreateChildWithName (state::uiID, nullptr).setProperty ("width", 1500, nullptr);

    REQUIRE (a.getPresetManager().save ("My Bass / Thing").wasOk());
    CHECK (a.getPresetManager().getCurrentName() == "My Bass / Thing");

    const auto files = b.getPresetManager().userPresets();
    REQUIRE (files.size() == 1);

    b.getValueTreeState().state.getOrCreateChildWithName (state::uiID, nullptr).setProperty ("width", 1000, nullptr);
    REQUIRE (b.getPresetManager().loadFile (files[0]));
    CHECK_THAT (value (b, ParamIDs::band (2, "harmonic")), WithinAbs (5.0, 1e-4));
    CHECK_THAT (value (b, ParamIDs::band (2, "gain")), WithinAbs (-3.0, 1e-4));
    CHECK_THAT (value (b, ParamIDs::glide), WithinAbs (120.0, 1e-3));
    CHECK (b.getPresetManager().getCurrentName() == "My Bass / Thing");

    // A preset carries sound settings only, not the window size.
    CHECK ((int) b.getValueTreeState().state.getChildWithName (state::uiID).getProperty ("width") == 1000);

    CHECK (a.getPresetManager().save ("   ").failed());
}

TEST_CASE ("Host bypass keeps the lookahead delay")
{
    juce::ScopedJuceInitialiser_GUI juce;
    constexpr double fs = 48000.0;
    constexpr int blockSize = 256;

    HarmEQAudioProcessor proc;
    setParam (proc.getValueTreeState(), ParamIDs::source, (float) PitchSource::mainInput);
    setParam (proc.getValueTreeState(), ParamIDs::lookahead, 1.0f);
    proc.setPlayConfigDetails (2, 2, fs, blockSize);
    proc.prepareToPlay (fs, blockSize);
    const int latency = proc.getLatencySamples();
    REQUIRE (latency > 0);

    juce::Random rng (9);
    std::vector<float> in, out;
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    for (int b = 0; b < 40; ++b)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const float x = rng.nextFloat() - 0.5f;
            in.push_back (x);
            buffer.setSample (0, i, x);
            buffer.setSample (1, i, x);
        }
        proc.processBlockBypassed (buffer, midi);
        for (int i = 0; i < blockSize; ++i)
            out.push_back (buffer.getSample (0, i));
    }

    for (size_t n = (size_t) latency; n < out.size(); n += 31)
        REQUIRE (juce::exactlyEqual (out[n], in[n - (size_t) latency]));
}
