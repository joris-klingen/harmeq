// Renders the editor to a PNG with example settings and live audio/MIDI, for reviewing the GUI
// without a host:  HarmEQScreenshot out.png [scale] [poly]
#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdio>

namespace
{
void set (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void exampleSettings (juce::AudioProcessorValueTreeState& apvts)
{
    using namespace harmeq;
    set (apvts, ParamIDs::source, (float) PitchSource::midi);

    auto band = [&] (int b, std::initializer_list<std::pair<const char*, float>> values) {
        for (auto& [suffix, v] : values)
            set (apvts, ParamIDs::band (b, suffix), v);
    };

    band (0, { { "on", 1 }, { "harmonic", 1 }, { "gain", 3 }, { "q", 4 } });
    band (1, { { "on", 1 }, { "harmonic", 2 }, { "gain", 6 }, { "q", 8 } });
    band (2, { { "on", 1 }, { "type", (float) FilterType::notch }, { "harmonic", 3 }, { "q", 10 } });
    band (3, { { "on", 1 }, { "type", (float) FilterType::highPass }, { "track", 1 }, { "freq", 40 }, { "q", 0.71f }, { "slope", 1 } });
    band (4, { { "on", 1 }, { "type", (float) FilterType::highShelf }, { "track", 1 }, { "freq", 8000 }, { "gain", -3 }, { "q", 0.7f } });
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: %s out.png [scale]\n", argv[0]);
        return 1;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    constexpr double fs = 48000.0;
    constexpr int blockSize = 512;

    HarmEQAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, fs, blockSize);
    proc.prepareToPlay (fs, blockSize);
    exampleSettings (proc.getValueTreeState());

    const bool poly = argc > 3 && juce::String (argv[3]) == "poly";
    if (poly)
        set (proc.getValueTreeState(), harmeq::ParamIDs::midiMode, (float) harmeq::MidiMode::poly);

    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    auto* editor = dynamic_cast<HarmEQAudioProcessorEditor*> (base.get());
    editor->setSize (HarmEQAudioProcessorEditor::defaultWidth, HarmEQAudioProcessorEditor::defaultHeight);

    // Two seconds of band-limited saws held via MIDI: G1 (MIDI 43), or a C2–E2–G2 chord in poly.
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    const std::vector<int> notes = poly ? std::vector<int> { 48, 52, 55 } : std::vector<int> { 43 };
    std::vector<double> phases (notes.size(), 0.0);

    for (int block = 0; block < (int) (2.0 * fs) / blockSize; ++block)
    {
        midi.clear();
        if (block == 0)
            for (int note : notes)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);

        for (int i = 0; i < blockSize; ++i)
        {
            float s = 0.0f;
            for (size_t k = 0; k < notes.size(); ++k)
            {
                const double f0 = 440.0 * std::pow (2.0, (notes[k] - 69) / 12.0);
                for (int h = 1; h * f0 < 20000.0; ++h)
                    s += (float) (std::sin (2.0 * juce::MathConstants<double>::pi * h * phases[k]) / h);
                phases[k] = std::fmod (phases[k] + f0 / fs, 1.0);
            }
            s *= 0.25f / (float) notes.size();
            buffer.setSample (0, i, s);
            buffer.setSample (1, i, s);
        }

        proc.processBlock (buffer, midi);
        if (block % 4 == 0)
            editor->refreshNow();
    }

    for (int i = 0; i < 10; ++i)
        editor->refreshNow();

    const float scale = argc > 2 ? (float) std::atof (argv[2]) : 1.0f;
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);

    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
    {
        std::fprintf (stderr, "could not write %s\n", argv[1]);
        return 1;
    }

    std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
    base.reset();
    return 0;
}
