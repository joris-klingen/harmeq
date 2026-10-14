#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Parameters.h"

namespace harmeq::theme
{
// Neutral darks with a slight blue bias; one accent per band.
inline const juce::Colour background { 0xff15191e };
inline const juce::Colour panel { 0xff1b2027 };
inline const juce::Colour well { 0xff10141a };
inline const juce::Colour line { 0xff29303a };
inline const juce::Colour lineSoft { 0xff1f252d };
inline const juce::Colour text { 0xffdce2ea };
inline const juce::Colour muted { 0xff808b98 };
inline const juce::Colour faint { 0xff4a535f };
inline const juce::Colour sum { 0xfff2f5f8 };
inline const juce::Colour warn { 0xffe5a54b };
inline const juce::Colour good { 0xff8fc46a };
inline const juce::Colour harmonic { 0xffe8b04a };

inline juce::Colour band (int index)
{
    static const juce::Colour colours[numBands] { juce::Colour (0xffe8b04a), juce::Colour (0xff5fb3d9),
                                                  juce::Colour (0xffe07893), juce::Colour (0xff8fc46a),
                                                  juce::Colour (0xffb08be0), juce::Colour (0xffe48a5c),
                                                  juce::Colour (0xff4fc2b0), juce::Colour (0xffcfc86c) };
    return colours[juce::jlimit (0, numBands - 1, index)];
}

// System fonts, as in the hitdmx plugins: the platform UI sans for labels, a monospace for numbers.
inline juce::Font label (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font numbers (float size, bool bold = false)
{
#if JUCE_MAC
    const juce::String name = "Menlo";
#else
    const juce::String name = juce::Font::getDefaultMonospacedFontName();
#endif
    return juce::Font (juce::FontOptions (name, size, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font caps (float size)
{
    auto f = label (size, true);
    f.setExtraKerningFactor (0.08f);
    return f;
}

// ---- Shared formatting ----
inline juce::String formatHz (double hz)
{
    if (hz < 100.0)
        return juce::String (hz, 1);
    if (hz < 1000.0)
        return juce::String (juce::roundToInt (hz));
    return juce::String (hz / 1000.0, hz < 10000.0 ? 2 : 1) + "k";
}

inline juce::String formatDb (double db, bool withUnit = true)
{
    const auto v = std::abs (db) < 0.05 ? 0.0 : db;
    return (v > 0 ? "+" : v < 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String())
           + juce::String (std::abs (v), 1) + (withUnit ? " dB" : "");
}

inline juce::String formatHarmonic (double h)
{
    if (std::abs (h - 0.25) < 1e-3) return juce::CharPointer_UTF8 ("H\xc2\xbc");
    if (std::abs (h - 0.5) < 1e-3)  return juce::CharPointer_UTF8 ("H\xc2\xbd");
    if (std::abs (h - 0.75) < 1e-3) return juce::CharPointer_UTF8 ("H\xc2\xbe");
    if (std::abs (h - std::round (h)) < 1e-3) return "H" + juce::String (juce::roundToInt (h));
    return "H" + juce::String (h, 2);
}

inline juce::String noteName (double hz, int* centsOut = nullptr)
{
    const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int nearest = juce::roundToInt (midi);
    if (centsOut != nullptr)
        *centsOut = juce::roundToInt ((midi - nearest) * 100.0);
    return juce::MidiMessage::getMidiNoteName (nearest, true, true, 3); // MIDI 60 = C3, as in Ableton Live
}
} // namespace harmeq::theme
