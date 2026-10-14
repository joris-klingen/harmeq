#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace harmeq
{
constexpr int numBands = 8;
constexpr int maxVoices = 6; // notes tracked at once in Poly mode (a Juno-106 has six voices)

namespace ParamIDs
{
    inline constexpr auto source         = "source";
    inline constexpr auto midiMode       = "midiMode";
    inline constexpr auto minFreq        = "minFreq";
    inline constexpr auto maxFreq        = "maxFreq";
    inline constexpr auto confidence     = "confidence";
    inline constexpr auto glide          = "glide";
    inline constexpr auto noPitchMode    = "noPitchMode";
    inline constexpr auto fallbackFreq   = "fallbackFreq";
    inline constexpr auto lookahead      = "lookahead";
    inline constexpr auto pitchBendRange = "pitchBendRange";
    inline constexpr auto transpose      = "transpose";
    inline constexpr auto outputGain     = "outputGain";

    // Per-band IDs are "band<N>_<suffix>", N starting at 1.
    juce::String band (int bandIndex, const char* suffix);
}

enum class PitchSource { mainInput, sidechain, midi };
enum class NoPitchMode { hold, fallback, bypassBands };
enum class MidiMode    { lastNote, lowestNote, poly };
enum class FilterType  { bell, lowShelf, highShelf, highPass, lowPass, notch };
enum class TrackMode   { tracked, fixed };

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
} // namespace harmeq
