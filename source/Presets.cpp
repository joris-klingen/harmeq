#include "Presets.h"
#include "Parameters.h"

namespace harmeq
{
namespace
{
    const juce::Identifier paramType { "PARAM" }, idID { "id" }, valueID { "value" }, presetType { "HarmEQPreset" },
        nameID { "name" };

    juce::ValueTree findParam (const juce::ValueTree& params, const juce::String& id)
    {
        for (const auto& child : params)
            if (child.hasType (paramType) && child[idID].toString() == id)
                return child;
        return {};
    }

    // ---- Factory presets: values on top of an all-default state with every band switched off ----
    struct FactoryPreset
    {
        const char* name;
        std::vector<std::pair<juce::String, float>> values;
    };

    std::vector<std::pair<juce::String, float>> band (int b, std::initializer_list<std::pair<const char*, float>> values)
    {
        std::vector<std::pair<juce::String, float>> out { { ParamIDs::band (b, "on"), 1.0f } };
        for (const auto& [suffix, v] : values)
            out.emplace_back (ParamIDs::band (b, suffix), v);
        return out;
    }

    FactoryPreset make (const char* name, std::initializer_list<std::vector<std::pair<juce::String, float>>> groups)
    {
        FactoryPreset p { name, {} };
        for (const auto& g : groups)
            p.values.insert (p.values.end(), g.begin(), g.end());
        return p;
    }

    const std::vector<FactoryPreset>& factory()
    {
        constexpr auto bell = (float) FilterType::bell, notch = (float) FilterType::notch,
                       highPass = (float) FilterType::highPass;
        constexpr auto fixed = (float) TrackMode::fixed;
        constexpr auto mainIn = (float) PitchSource::mainInput, sidechain = (float) PitchSource::sidechain,
                       midi = (float) PitchSource::midi;

        static const std::vector<FactoryPreset> presets {
            make ("Init", { band (0, {}), band (1, { { "harmonic", 2.0f } }), band (2, { { "harmonic", 3.0f } }) }),

            make ("Bass: Tame the Fundamental",
                  { { { ParamIDs::source, mainIn }, { ParamIDs::lookahead, 1.0f }, { ParamIDs::minFreq, 30.0f }, { ParamIDs::maxFreq, 300.0f } },
                    band (0, { { "type", bell }, { "harmonic", 1.0f }, { "gain", -4.0f }, { "q", 3.0f } }),
                    band (1, { { "type", bell }, { "harmonic", 2.0f }, { "gain", 2.0f }, { "q", 4.0f } }),
                    band (3, { { "type", highPass }, { "track", fixed }, { "freq", 25.0f }, { "q", 0.71f }, { "slope", 1.0f } }) }),

            make ("Bass: Add 2nd Harmonic",
                  { { { ParamIDs::source, mainIn }, { ParamIDs::lookahead, 1.0f }, { ParamIDs::minFreq, 30.0f }, { ParamIDs::maxFreq, 300.0f } },
                    band (0, { { "type", bell }, { "harmonic", 2.0f }, { "gain", 5.0f }, { "q", 5.0f } }),
                    band (1, { { "type", bell }, { "harmonic", 3.0f }, { "gain", 2.0f }, { "q", 6.0f } }) }),

            make ("Notch the Fundamental",
                  { { { ParamIDs::source, mainIn }, { ParamIDs::lookahead, 1.0f }, { ParamIDs::minFreq, 40.0f } },
                    band (0, { { "type", notch }, { "harmonic", 1.0f }, { "q", 8.0f } }) }),

            make ("Vocal: Soften Upper Harmonics",
                  { { { ParamIDs::source, mainIn }, { ParamIDs::lookahead, 1.0f }, { ParamIDs::minFreq, 80.0f }, { ParamIDs::glide, 30.0f } },
                    band (0, { { "type", bell }, { "harmonic", 5.0f }, { "gain", -2.0f }, { "q", 6.0f } }),
                    band (1, { { "type", bell }, { "harmonic", 6.0f }, { "gain", -3.0f }, { "q", 6.0f } }),
                    band (2, { { "type", bell }, { "harmonic", 7.0f }, { "gain", -3.0f }, { "q", 6.0f } }),
                    band (3, { { "type", bell }, { "harmonic", 8.0f }, { "gain", -2.0f }, { "q", 6.0f } }) }),

            make ("Synth Lead: Odd Harmonics",
                  { { { ParamIDs::source, midi }, { ParamIDs::midiMode, (float) MidiMode::lastNote } },
                    band (0, { { "type", bell }, { "harmonic", 3.0f }, { "gain", 3.0f }, { "q", 6.0f } }),
                    band (1, { { "type", bell }, { "harmonic", 5.0f }, { "gain", 3.0f }, { "q", 6.0f } }),
                    band (2, { { "type", bell }, { "harmonic", 7.0f }, { "gain", 2.0f }, { "q", 6.0f } }) }),

            make ("Pads: Tame the Boom (Poly)",
                  { { { ParamIDs::source, midi }, { ParamIDs::midiMode, (float) MidiMode::poly },
                      { ParamIDs::noPitchMode, (float) NoPitchMode::hold } },
                    band (0, { { "type", bell }, { "harmonic", 1.0f }, { "gain", -6.0f }, { "q", 3.0f } }),
                    band (1, { { "type", bell }, { "harmonic", 2.0f }, { "gain", -3.0f }, { "q", 4.0f } }),
                    band (3, { { "type", highPass }, { "track", fixed }, { "freq", 30.0f }, { "q", 0.71f }, { "slope", 1.0f } }) }),

            make ("Sidechain: Carve Its Notes",
                  { { { ParamIDs::source, sidechain }, { ParamIDs::lookahead, 1.0f }, { ParamIDs::minFreq, 40.0f }, { ParamIDs::maxFreq, 600.0f } },
                    band (0, { { "type", notch }, { "harmonic", 1.0f }, { "q", 10.0f } }),
                    band (1, { { "type", bell }, { "harmonic", 2.0f }, { "gain", -4.0f }, { "q", 8.0f } }) }),
        };
        return presets;
    }

    juce::File defaultUserFolder()
    {
#if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Presets/HarmEQ");
#else
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("HarmEQ/Presets");
#endif
    }
} // namespace

// ---------------------------------------------------------------------------------------------- state

juce::ValueTree state::withParameters (const juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& params,
                                       const juce::ValueTree& base)
{
    juce::ValueTree result (apvts.state.getType());

    // Everything that isn't a parameter (UI settings, preset name, ...) comes from base.
    if (base.isValid())
    {
        for (int i = 0; i < base.getNumProperties(); ++i)
            result.setProperty (base.getPropertyName (i), base.getProperty (base.getPropertyName (i)), nullptr);
        for (const auto& child : base)
            if (! child.hasType (paramType))
                result.appendChild (child.createCopy(), nullptr);
    }

    for (auto* p : apvts.processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (ranged == nullptr)
            continue;

        const auto saved = findParam (params, ranged->paramID);
        const float value = saved.isValid() ? (float) saved[valueID]
                                            : ranged->convertFrom0to1 (ranged->getDefaultValue());

        juce::ValueTree child (paramType);
        child.setProperty (idID, ranged->paramID, nullptr);
        child.setProperty (valueID, value, nullptr);
        result.appendChild (child, nullptr);
    }

    result.setProperty (versionID, currentVersion, nullptr);
    return result;
}

void state::migrate (juce::ValueTree& tree)
{
    const int version = tree.getProperty (versionID, 1);
    juce::ignoreUnused (version);
    // Nothing to convert yet: version 2 only added parameters, which withParameters() defaults.
    tree.setProperty (versionID, currentVersion, nullptr);
}

// ---------------------------------------------------------------------------------------------- presets

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s), userFolder (defaultUserFolder()) {}

juce::StringArray PresetManager::factoryNames()
{
    juce::StringArray names;
    for (const auto& p : factory())
        names.add (p.name);
    return names;
}

void PresetManager::loadFactory (int index)
{
    const auto& presets = factory();
    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;

    // Start from defaults with every band off, then apply the preset's values.
    juce::ValueTree params (presetType);
    auto set = [&] (const juce::String& id, float value) {
        auto existing = findParam (params, id);
        if (! existing.isValid())
        {
            existing = juce::ValueTree (paramType);
            existing.setProperty (idID, id, nullptr);
            params.appendChild (existing, nullptr);
        }
        existing.setProperty (valueID, value, nullptr);
    };

    for (int b = 0; b < numBands; ++b)
        set (ParamIDs::band (b, "on"), 0.0f);
    for (const auto& [id, value] : presets[(size_t) index].values)
        set (id, value);

    apply (params, presets[(size_t) index].name);
}

juce::Array<juce::File> PresetManager::userPresets() const
{
    auto files = userFolder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension);
    files.sort();
    return files;
}

bool PresetManager::loadFile (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (presetType.toString()))
        return false;

    auto params = juce::ValueTree::fromXml (*xml);
    state::migrate (params);
    apply (params, params.getProperty (nameID, file.getFileNameWithoutExtension()));
    return true;
}

juce::Result PresetManager::save (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return juce::Result::fail ("Enter a name for the preset.");

    if (! userFolder.createDirectory())
        return juce::Result::fail ("Couldn't create the presets folder " + userFolder.getFullPathName());

    juce::ValueTree preset (presetType);
    preset.setProperty (nameID, name.trim(), nullptr);
    preset.setProperty (state::versionID, state::currentVersion, nullptr);
    for (const auto& child : apvts.copyState())
        if (child.hasType (paramType))
            preset.appendChild (child.createCopy(), nullptr);

    const auto file = userFolder.getChildFile (clean + fileExtension);
    if (auto xml = preset.createXml(); xml == nullptr || ! xml->writeTo (file))
        return juce::Result::fail ("Couldn't write " + file.getFullPathName());

    apvts.state.setProperty (state::presetNameID, name.trim(), nullptr);
    return juce::Result::ok();
}

juce::String PresetManager::getCurrentName() const
{
    return apvts.state.getProperty (state::presetNameID, "Init").toString();
}

void PresetManager::apply (const juce::ValueTree& params, const juce::String& name)
{
    auto next = state::withParameters (apvts, params, apvts.copyState());
    next.setProperty (state::presetNameID, name, nullptr);
    apvts.replaceState (next);
}
} // namespace harmeq
