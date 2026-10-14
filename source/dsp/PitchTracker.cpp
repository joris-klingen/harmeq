#include "PitchTracker.h"

namespace harmeq
{
void PitchTracker::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    reset();
}

void PitchTracker::reset()
{
    midi.reset();
    hasCurrent = hasHistory = wasBypassed = false;
    slotNote.fill (noNote);
    slotFreedAt.fill (0);
    slotLog2.fill (std::log2 (110.0f));
    releaseCounter = 0;
    chordSize = 0;
}

float PitchTracker::noteLog2 (int note) const noexcept
{
    if (note == fallbackNote)
        return std::log2 (settings.fallbackHz);

    const float n = (float) note + midi.pitchBend() * settings.pitchBendRange + settings.transpose;
    return std::log2 (440.0f) + (n - 69.0f) / 12.0f;
}

bool PitchTracker::sourcePitch (float& log2Hz) const noexcept
{
    if (settings.source != PitchSource::midi || ! midi.hasNote())
        return false;

    log2Hz = noteLog2 (settings.midiMode == MidiMode::lowestNote ? midi.lowestNote() : midi.currentNote());
    return true;
}

PitchSet PitchTracker::advance (int numSamples) noexcept
{
    if (isPoly())
        return advancePoly();

    // Leaving Poly: forget its chord so coming back starts clean.
    chordSize = 0;
    slotNote.fill (noNote);

    return monoPitch (advanceMono (numSamples));
}

PitchState PitchTracker::advanceMono (int numSamples) noexcept
{
    float target = 0.0f;
    bool voiced = sourcePitch (target);
    bool snap = ! hasCurrent;

    if (voiced)
    {
        // Coming back from Bypass Bands, the bands are fading in from silence, so start on the new note
        // rather than gliding from a stale pitch.
        snap = snap || wasBypassed;
        lastVoicedLog2 = target;
        hasHistory = true;
    }
    else
    {
        switch (settings.noPitchMode)
        {
            case NoPitchMode::hold:
                target = hasHistory ? lastVoicedLog2 : std::log2 (settings.fallbackHz);
                voiced = true;
                break;

            case NoPitchMode::fallback:
                target = std::log2 (settings.fallbackHz);
                voiced = true;
                break;

            case NoPitchMode::bypassBands:
                // Keep the current frequency while the tracked bands fade out.
                target = hasCurrent ? currentLog2 : std::log2 (settings.fallbackHz);
                break;
        }
    }

    wasBypassed = ! voiced;

    if (snap || settings.glideMs <= 0.0f)
    {
        currentLog2 = target;
    }
    else
    {
        const auto alpha = (float) (1.0 - std::exp (-numSamples / (settings.glideMs * 0.001 * sampleRate)));
        currentLog2 += alpha * (target - currentLog2);
    }

    hasCurrent = true;
    return { std::exp2 (currentLog2), voiced };
}

PitchSet PitchTracker::advancePoly() noexcept
{
    // 1. Which notes should sound: the held notes (newest maxVoices of them), else the no-pitch rule.
    std::array<int, maxVoices> wanted {};
    int numWanted = 0;

    if (midi.hasNote())
    {
        const int held = midi.numHeld();
        for (int i = std::max (0, held - maxVoices); i < held; ++i)
            wanted[(size_t) numWanted++] = midi.heldNote (i);

        chord = wanted;
        chordSize = numWanted;
    }
    else
    {
        switch (settings.noPitchMode)
        {
            case NoPitchMode::hold:
                if (chordSize > 0)
                {
                    wanted = chord;
                    numWanted = chordSize;
                }
                else
                {
                    wanted[0] = fallbackNote;
                    numWanted = 1;
                }
                break;

            case NoPitchMode::fallback:
                wanted[0] = fallbackNote;
                numWanted = 1;
                break;

            case NoPitchMode::bypassBands:
                break;
        }
    }

    auto isWanted = [&] (int note) {
        for (int i = 0; i < numWanted; ++i)
            if (wanted[(size_t) i] == note)
                return true;
        return false;
    };

    // 2. Free slots whose note is gone (they fade out at their last pitch).
    for (size_t s = 0; s < slotNote.size(); ++s)
    {
        if (slotNote[s] != noNote && ! isWanted (slotNote[s]))
        {
            slotNote[s] = noNote;
            slotFreedAt[s] = ++releaseCounter;
        }
    }

    // 3. Give new notes the slot that has been free the longest, so a slot still fading out is reused last.
    for (int i = 0; i < numWanted; ++i)
    {
        const int note = wanted[(size_t) i];
        if (std::find (slotNote.begin(), slotNote.end(), note) != slotNote.end())
            continue;

        int best = -1;
        for (int s = 0; s < maxVoices; ++s)
            if (slotNote[(size_t) s] == noNote && (best < 0 || slotFreedAt[(size_t) s] < slotFreedAt[(size_t) best]))
                best = s;

        if (best >= 0)
            slotNote[(size_t) best] = note;
    }

    // 4. Output: held slots voiced at their (bent, transposed) pitch; freed slots unvoiced at their last pitch.
    PitchSet set;
    for (size_t s = 0; s < slotNote.size(); ++s)
    {
        const bool active = slotNote[s] != noNote;
        if (active)
            slotLog2[s] = noteLog2 (slotNote[s]);
        set[s] = { std::exp2 (slotLog2[s]), active };
    }

    return set;
}
} // namespace harmeq
