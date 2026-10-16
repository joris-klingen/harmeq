#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace harmeq
{
// ---- Plugin state ----
//
// Saved state = the APVTS tree (PARAM children + a "UI" child for window/display options), tagged with
// stateVersion. Loading always goes through withParameters(), so parameters missing from older states
// or presets get their defaults instead of keeping whatever value the instance had.
namespace state
{
    // 1: first release (no version attribute). 2: adds MIDI Notes (midiMode).
    constexpr int currentVersion = 2;
    inline const juce::Identifier versionID { "stateVersion" }, presetNameID { "presetName" }, uiID { "UI" };

    // A full state for `apvts`: parameter values from `params` (defaults where absent) plus every
    // non-parameter child and property of `base` (UI settings, preset name).
    juce::ValueTree withParameters (const juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& params,
                                    const juce::ValueTree& base);

    // Brings a tree saved by an older version up to date.
    void migrate (juce::ValueTree& tree);
} // namespace state

// ---- Presets ----
//
// Factory presets live in code; user presets are XML files (*.harmeqpreset) in userFolder(). A preset
// holds parameter values only — never window size or display options.
class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState&);

    static juce::StringArray factoryNames();
    void loadFactory (int index);

    juce::File getUserFolder() const { return userFolder; }
    void setUserFolder (const juce::File& f) { userFolder = f; } // for tests
    juce::Array<juce::File> userPresets() const;
    bool loadFile (const juce::File&);
    juce::Result save (const juce::String& name);

    juce::String getCurrentName() const;

    static constexpr const char* fileExtension = ".harmeqpreset";

private:
    void apply (const juce::ValueTree& params, const juce::String& name);

    juce::AudioProcessorValueTreeState& apvts;
    juce::File userFolder;
};
} // namespace harmeq
