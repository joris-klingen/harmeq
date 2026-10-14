#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace harmeq
{
// Monophonic, last-note-priority note stack (omni). Releasing the newest note falls back to the most
// recent note that is still held.
class MidiNoteTracker
{
public:
    void handle (const juce::MidiMessage& m) noexcept
    {
        if (m.isNoteOn())
            noteOn (m.getNoteNumber());
        else if (m.isNoteOff())
            noteOff (m.getNoteNumber());
        else if (m.isPitchWheel())
            setPitchWheel (m.getPitchWheelValue());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            allNotesOff();
        else if (m.isResetAllControllers())
            bend = 0.0f;
    }

    void noteOn (int note) noexcept
    {
        remove (note);
        if (size < (int) stack.size())
            stack[(size_t) size++] = note;
    }

    void noteOff (int note) noexcept { remove (note); }
    void allNotesOff() noexcept { size = 0; }

    void setPitchWheel (int value14Bit) noexcept
    {
        const int centred = value14Bit - 8192;
        bend = (float) centred / (centred < 0 ? 8192.0f : 8191.0f);
    }

    void reset() noexcept
    {
        size = 0;
        bend = 0.0f;
    }

    bool hasNote() const noexcept { return size > 0; }
    int currentNote() const noexcept { return size > 0 ? stack[(size_t) size - 1] : -1; }
    float pitchBend() const noexcept { return bend; } // -1 … +1

private:
    void remove (int note) noexcept
    {
        for (int i = 0; i < size; ++i)
        {
            if (stack[(size_t) i] == note)
            {
                for (int j = i; j < size - 1; ++j)
                    stack[(size_t) j] = stack[(size_t) j + 1];
                --size;
                return;
            }
        }
    }

    std::array<int, 128> stack {};
    int size = 0;
    float bend = 0.0f;
};
} // namespace harmeq
