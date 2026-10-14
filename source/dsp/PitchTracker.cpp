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
}

bool PitchTracker::sourcePitch (float& log2Hz) const noexcept
{
    if (settings.source != PitchSource::midi || ! midi.hasNote())
        return false;

    const float note = (float) midi.currentNote() + midi.pitchBend() * settings.pitchBendRange + settings.transpose;
    log2Hz = std::log2 (440.0f) + (note - 69.0f) / 12.0f;
    return true;
}

PitchState PitchTracker::advance (int numSamples) noexcept
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
} // namespace harmeq
