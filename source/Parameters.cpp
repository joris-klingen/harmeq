#include "Parameters.h"

namespace harmeq
{
juce::String ParamIDs::band (int bandIndex, const char* suffix)
{
    return "band" + juce::String (bandIndex + 1) + "_" + suffix;
}

namespace
{
    constexpr int parameterVersion = 1;

    juce::ParameterID pid (const juce::String& id) { return { id, parameterVersion }; }

    // Log-skewed frequency range centred so that the midpoint of the knob sits at `centre`.
    juce::NormalisableRange<float> freqRange (float min, float max, float centre)
    {
        juce::NormalisableRange<float> r { min, max };
        r.setSkewForCentre (centre);
        return r;
    }

    juce::AudioParameterFloatAttributes hzAttributes()
    {
        return juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (
            [] (float v, int) { return v < 1000.0f ? juce::String (v, 1) : juce::String (v / 1000.0f, 2) + "k"; });
    }

    juce::AudioParameterFloatAttributes withLabel (const juce::String& label)
    {
        return juce::AudioParameterFloatAttributes().withLabel (label);
    }

    // Default band setup: first few harmonics as gentle bells, the rest switched off.
    constexpr float defaultHarmonics[numBands] { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto global = std::make_unique<juce::AudioProcessorParameterGroup> ("global", "Global", "|");

    global->addChild (
        std::make_unique<juce::AudioParameterChoice> (pid (ParamIDs::source), "Pitch Source",
                                                      juce::StringArray { "Main Input", "Sidechain", "MIDI" }, 0),
        std::make_unique<juce::AudioParameterFloat> (pid (ParamIDs::minFreq), "Min Pitch",
                                                     freqRange (30.0f, 2000.0f, 200.0f), 50.0f, hzAttributes()),
        std::make_unique<juce::AudioParameterFloat> (pid (ParamIDs::maxFreq), "Max Pitch",
                                                     freqRange (30.0f, 2000.0f, 200.0f), 1000.0f, hzAttributes()),
        std::make_unique<juce::AudioParameterFloat> (pid (ParamIDs::confidence), "Confidence",
                                                     juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.5f),
        std::make_unique<juce::AudioParameterFloat> (pid (ParamIDs::glide), "Glide",
                                                     juce::NormalisableRange<float> { 0.0f, 500.0f, 0.0f, 0.5f }, 20.0f,
                                                     withLabel ("ms")),
        std::make_unique<juce::AudioParameterChoice> (pid (ParamIDs::noPitchMode), "No-Pitch Mode",
                                                      juce::StringArray { "Hold", "Fallback", "Bypass Bands" }, 0),
        std::make_unique<juce::AudioParameterFloat> (pid (ParamIDs::fallbackFreq), "Fallback Pitch",
                                                     freqRange (20.0f, 2000.0f, 200.0f), 110.0f, hzAttributes()),
        std::make_unique<juce::AudioParameterBool> (pid (ParamIDs::lookahead), "Lookahead", false),
        std::make_unique<juce::AudioParameterInt> (pid (ParamIDs::pitchBendRange), "Pitch Bend Range", 0, 24, 2),
        std::make_unique<juce::AudioParameterInt> (pid (ParamIDs::transpose), "Transpose", -24, 24, 0),
        std::make_unique<juce::AudioParameterFloat> (pid (ParamIDs::outputGain), "Output Gain",
                                                     juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f,
                                                     withLabel ("dB")));

    layout.add (std::move (global));

    for (int b = 0; b < numBands; ++b)
    {
        const auto name = "Band " + juce::String (b + 1);
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("band" + juce::String (b + 1), name, "|");

        group->addChild (
            std::make_unique<juce::AudioParameterBool> (pid (ParamIDs::band (b, "on")), name + " On", b < 3),
            std::make_unique<juce::AudioParameterChoice> (
                pid (ParamIDs::band (b, "type")), name + " Type",
                juce::StringArray { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Notch" }, 0),
            std::make_unique<juce::AudioParameterChoice> (pid (ParamIDs::band (b, "track")), name + " Mode",
                                                          juce::StringArray { "Tracked", "Fixed" }, 0),
            std::make_unique<juce::AudioParameterFloat> (pid (ParamIDs::band (b, "harmonic")), name + " Harmonic",
                                                         juce::NormalisableRange<float> { 0.25f, 32.0f, 0.25f, 0.5f },
                                                         defaultHarmonics[b]),
            std::make_unique<juce::AudioParameterFloat> (pid (ParamIDs::band (b, "detune")), name + " Detune",
                                                         juce::NormalisableRange<float> { -100.0f, 100.0f, 0.1f },
                                                         0.0f, withLabel ("ct")),
            std::make_unique<juce::AudioParameterFloat> (pid (ParamIDs::band (b, "freq")), name + " Freq",
                                                         freqRange (20.0f, 20000.0f, 1000.0f),
                                                         110.0f * defaultHarmonics[b], hzAttributes()),
            std::make_unique<juce::AudioParameterFloat> (pid (ParamIDs::band (b, "gain")), name + " Gain",
                                                         juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f },
                                                         0.0f, withLabel ("dB")),
            std::make_unique<juce::AudioParameterFloat> (pid (ParamIDs::band (b, "q")), name + " Q",
                                                         juce::NormalisableRange<float> { 0.1f, 30.0f, 0.0f, 0.3f },
                                                         4.0f),
            std::make_unique<juce::AudioParameterChoice> (pid (ParamIDs::band (b, "slope")), name + " Slope",
                                                          juce::StringArray { "12 dB/oct", "24 dB/oct" }, 0));

        layout.add (std::move (group));
    }

    return layout;
}
} // namespace harmeq
