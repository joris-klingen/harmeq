#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/PitchDetector.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <functional>

using namespace harmeq;

namespace
{
constexpr double twoPi = 2.0 * 3.14159265358979323846;

enum class Wave { sine, saw, square, pulse };

const char* waveName (Wave w)
{
    switch (w)
    {
        case Wave::sine:   return "sine";
        case Wave::saw:    return "saw";
        case Wave::square: return "square";
        case Wave::pulse:  return "pulse";
    }
    return "";
}

// Band-limited test waveforms by additive synthesis (no aliasing to confuse the detector).
float waveAt (Wave w, double phase, double f0, double fs)
{
    if (w == Wave::sine)
        return (float) std::sin (twoPi * phase);

    double s = 0.0;
    for (int h = 1; h * f0 < 0.45 * fs && h < 200; ++h)
    {
        double a = 0.0;
        switch (w)
        {
            case Wave::saw:    a = 1.0 / h; break;
            case Wave::square: a = (h % 2 == 1) ? 1.0 / h : 0.0; break;
            case Wave::pulse:  a = std::sin (3.14159265358979323846 * h * 0.2) / h; break; // 20 % duty
            case Wave::sine:   break;
        }
        s += a * std::sin (twoPi * h * phase);
    }
    return (float) (0.5 * s);
}

// Runs `seconds` of a signal through the detector; returns the result at the end.
PitchDetector::Result run (PitchDetector& d, double fs, double seconds, const std::function<float (int)>& signal)
{
    std::vector<float> block (256);
    const int total = (int) (seconds * fs);
    for (int n = 0; n < total; n += (int) block.size())
    {
        const int len = std::min ((int) block.size(), total - n);
        for (int i = 0; i < len; ++i)
            block[(size_t) i] = signal (n + i);
        d.push (block.data(), len);
    }
    return d.getResult();
}

double centsBetween (double a, double b) { return 1200.0 * std::log2 (a / b); }
} // namespace

TEST_CASE ("Detects clean tones across range, waveform and sample rate")
{
    const double fs = GENERATE (44100.0, 48000.0, 96000.0);
    const auto wave = GENERATE (Wave::sine, Wave::saw, Wave::square, Wave::pulse);
    const double f0 = GENERATE (41.2, 55.0, 98.0, 146.8, 220.0, 329.6, 523.3, 880.0, 1318.5);

    PitchDetector d;
    d.prepare (fs);
    d.setRange (35.0f, 2000.0f);
    d.setThreshold (0.5f);

    double phase = 0.0;
    const auto r = run (d, fs, 0.5, [&] (int) {
        const float s = 0.5f * waveAt (wave, phase, f0, fs);
        phase = std::fmod (phase + f0 / fs, 1.0);
        return s;
    });

    INFO (waveName (wave) << " " << f0 << " Hz @ " << fs << " -> " << r.f0Hz << " Hz, confidence " << r.confidence);
    REQUIRE (r.voiced);
    CHECK (std::abs (centsBetween (r.f0Hz, f0)) < 5.0);
}

TEST_CASE ("Tracks a tone with background noise")
{
    constexpr double fs = 48000.0;
    const double f0 = GENERATE (82.4, 196.0, 440.0);

    PitchDetector d;
    d.prepare (fs);
    d.setRange (40.0f, 1000.0f);
    d.setThreshold (0.5f);

    juce::Random rng (7);
    double phase = 0.0;
    const auto r = run (d, fs, 0.5, [&] (int) {
        const float s = 0.4f * waveAt (Wave::saw, phase, f0, fs) + 0.04f * (rng.nextFloat() * 2.0f - 1.0f); // ~20 dB SNR
        phase = std::fmod (phase + f0 / fs, 1.0);
        return s;
    });

    INFO (f0 << " Hz -> " << r.f0Hz << " Hz, confidence " << r.confidence);
    REQUIRE (r.voiced);
    CHECK (std::abs (centsBetween (r.f0Hz, f0)) < 10.0);
}

TEST_CASE ("Follows vibrato and a melody without octave errors")
{
    constexpr double fs = 48000.0;
    PitchDetector d;
    d.prepare (fs);
    d.setRange (50.0f, 1000.0f);
    d.setThreshold (0.5f);

    // Notes of a bass line, each 250 ms, with ±30 cent vibrato at 5 Hz.
    const double notes[] { 110.0, 146.8, 98.0, 130.8, 82.4, 164.8 };
    const int noteLen = (int) (0.25 * fs);
    double phase = 0.0;
    int checked = 0, octaveErrors = 0, offPitch = 0;

    std::vector<float> block (128);
    for (int n = 0; n < noteLen * 6; n += 128)
    {
        double f = 0.0;
        for (int i = 0; i < 128; ++i)
        {
            const int k = n + i;
            f = notes[k / noteLen] * std::exp2 (0.3 / 12.0 * std::sin (twoPi * 5.0 * k / fs));
            block[(size_t) i] = 0.5f * waveAt (Wave::saw, phase, f, fs);
            phase = std::fmod (phase + f / fs, 1.0);
        }
        d.push (block.data(), 128);

        // Judge only the settled part of each note (skip the first 60 ms after a change).
        const int pos = (n + 127) % noteLen;
        const auto r = d.getResult();
        if (pos > (int) (0.06 * fs) && r.voiced)
        {
            ++checked;
            const double c = std::abs (centsBetween (r.f0Hz, f));
            if (c > 1100.0)
                ++octaveErrors;
            else if (c > 50.0) // vibrato moves fast; the estimate lags it a little
                ++offPitch;
        }
    }

    INFO ("checked " << checked << ", octave errors " << octaveErrors << ", off pitch " << offPitch);
    CHECK (checked > 300);
    CHECK (octaveErrors == 0);
    CHECK (offPitch < checked / 50);
}

TEST_CASE ("Reports no pitch for silence and noise")
{
    constexpr double fs = 48000.0;
    PitchDetector d;
    d.prepare (fs);
    d.setRange (40.0f, 1000.0f);
    d.setThreshold (0.5f);

    SECTION ("Silence")
    {
        const auto r = run (d, fs, 0.3, [] (int) { return 0.0f; });
        CHECK_FALSE (r.voiced);
        CHECK (r.confidence < 0.01f);
    }

    SECTION ("White noise")
    {
        juce::Random rng (3);
        int voicedHops = 0;
        std::vector<float> block (240);
        for (int b = 0; b < 200; ++b)
        {
            for (auto& s : block)
                s = 0.3f * (rng.nextFloat() * 2.0f - 1.0f);
            d.push (block.data(), (int) block.size());
            voicedHops += d.getResult().voiced ? 1 : 0;
        }
        CHECK (voicedHops < 10);
    }

    SECTION ("A tone that stops")
    {
        double phase = 0.0;
        run (d, fs, 0.3, [&] (int) {
            const float s = 0.5f * waveAt (Wave::saw, phase, 220.0, fs);
            phase = std::fmod (phase + 220.0 / fs, 1.0);
            return s;
        });
        REQUIRE (d.getResult().voiced);
        CHECK_FALSE (run (d, fs, 0.1, [] (int) { return 0.0f; }).voiced);
    }
}

TEST_CASE ("Pitch arrives soon after a note starts")
{
    constexpr double fs = 48000.0;
    const double f0 = GENERATE (55.0, 110.0, 440.0);

    PitchDetector d;
    d.prepare (fs);
    d.setRange (50.0f, 1000.0f);
    d.setThreshold (0.5f);

    double phase = 0.0;
    int firstVoiced = -1;
    std::vector<float> block (32);
    for (int n = 0; n < (int) (0.2 * fs) && firstVoiced < 0; n += 32)
    {
        for (auto& s : block)
        {
            s = 0.5f * waveAt (Wave::saw, phase, f0, fs);
            phase = std::fmod (phase + f0 / fs, 1.0);
        }
        d.push (block.data(), 32);
        if (d.getResult().voiced && std::abs (centsBetween (d.getResult().f0Hz, f0)) < 20.0)
            firstVoiced = n + 32;
    }

    const double ms = 1000.0 * firstVoiced / fs;
    INFO (f0 << " Hz detected after " << ms << " ms");
    REQUIRE (firstVoiced > 0);
    // Two periods of the lowest allowed pitch (50 Hz) to fill the window, plus a couple of hops.
    CHECK (ms < 60.0);
}

TEST_CASE ("Respects the pitch range")
{
    constexpr double fs = 48000.0;
    PitchDetector d;
    d.prepare (fs);
    d.setRange (100.0f, 400.0f);
    d.setThreshold (0.5f);

    double phase = 0.0;
    const auto r = run (d, fs, 0.3, [&] (int) {
        const float s = 0.5f * waveAt (Wave::sine, phase, 60.0, fs);
        phase = std::fmod (phase + 60.0 / fs, 1.0);
        return s;
    });

    // A 60 Hz sine has no period inside 100–400 Hz, so it must not be reported as some other pitch.
    CHECK_FALSE (r.voiced);
}

TEST_CASE ("Detector CPU stays small")
{
    constexpr double fs = 48000.0;
    PitchDetector d;
    d.prepare (fs);
    d.setRange (30.0f, 2000.0f); // worst case: the widest range
    d.setThreshold (0.5f);

    std::vector<float> block (512);
    double phase = 0.0;
    const auto start = std::chrono::steady_clock::now();
    for (int n = 0; n < (int) (10.0 * fs); n += 512)
    {
        for (auto& s : block)
        {
            s = 0.5f * (float) std::sin (twoPi * phase);
            phase = std::fmod (phase + 110.0 / fs, 1.0);
        }
        d.push (block.data(), 512);
    }
    const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();

    INFO ("10 s of audio analysed in " << seconds * 1000.0 << " ms");
    CHECK (seconds < 1.0); // generous for CI machines; typically a few percent of real time
}

// ---------------------------------------------------------------------------------------------- processor

#include "Parameters.h"
#include "PluginProcessor.h"

namespace
{
void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    auto* p = apvts.getParameter (id);
    REQUIRE (p != nullptr);
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}
} // namespace

TEST_CASE ("Processor: pitch detected from the main input")
{
    juce::ScopedJuceInitialiser_GUI juce;
    constexpr double fs = 48000.0;
    constexpr int blockSize = 480;

    HarmEQAudioProcessor proc;
    setParam (proc.getValueTreeState(), ParamIDs::source, (float) PitchSource::mainInput);
    proc.setPlayConfigDetails (2, 2, fs, blockSize);
    proc.prepareToPlay (fs, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    double phase = 0.0;

    for (int b = 0; b < 40; ++b)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const float s = 0.4f * waveAt (Wave::saw, phase, 164.8, fs);
            phase = std::fmod (phase + 164.8 / fs, 1.0);
            buffer.setSample (0, i, s);
            buffer.setSample (1, i, s);
        }
        proc.processBlock (buffer, midi);
    }

    CHECK (std::abs (centsBetween (proc.getCurrentPitchHz(), 164.8)) < 5.0);
    CHECK (proc.getPitchConfidence() > 0.9f);
}

TEST_CASE ("Processor: sidechain pitch steers the EQ on the main input")
{
    juce::ScopedJuceInitialiser_GUI juce;
    constexpr double fs = 48000.0;
    constexpr int blockSize = 480;

    HarmEQAudioProcessor proc;
    auto& apvts = proc.getValueTreeState();
    setParam (apvts, ParamIDs::source, (float) PitchSource::sidechain);
    setParam (apvts, ParamIDs::noPitchMode, (float) NoPitchMode::bypassBands);
    setParam (apvts, ParamIDs::band (0, "harmonic"), 2.0f);
    setParam (apvts, ParamIDs::band (0, "gain"), 12.0f);
    setParam (apvts, ParamIDs::band (0, "q"), 8.0f);

    SECTION ("Connected sidechain: a 110 Hz key boosts 220 Hz in the main signal")
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses = { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo() };
        layout.outputBuses = { juce::AudioChannelSet::stereo() };
        REQUIRE (proc.setBusesLayout (layout));
        proc.prepareToPlay (fs, blockSize);

        juce::AudioBuffer<float> buffer (4, blockSize);
        juce::MidiBuffer midi;
        double keyPhase = 0.0, probePhase = 0.0, inSq = 0.0, outSq = 0.0;

        for (int b = 0; b < 60; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float probe = 0.3f * (float) std::sin (twoPi * probePhase);
                const float key = 0.4f * waveAt (Wave::saw, keyPhase, 110.0, fs);
                probePhase = std::fmod (probePhase + 220.0 / fs, 1.0);
                keyPhase = std::fmod (keyPhase + 110.0 / fs, 1.0);
                buffer.setSample (0, i, probe);
                buffer.setSample (1, i, probe);
                buffer.setSample (2, i, key);
                buffer.setSample (3, i, key);
                if (b >= 30)
                    inSq += (double) probe * probe;
            }
            proc.processBlock (buffer, midi);
            if (b >= 30)
                for (int i = 0; i < blockSize; ++i)
                    outSq += (double) buffer.getSample (0, i) * buffer.getSample (0, i);
        }

        CHECK (proc.isSidechainConnected());
        CHECK (std::abs (centsBetween (proc.getCurrentPitchHz(), 110.0)) < 5.0);
        CHECK_THAT (10.0 * std::log10 (outSq / inSq), Catch::Matchers::WithinAbs (12.0, 0.3));
    }

    SECTION ("No sidechain: no pitch, tracked bands stay out")
    {
        proc.setPlayConfigDetails (2, 2, fs, blockSize);
        proc.prepareToPlay (fs, blockSize);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        for (int b = 0; b < 20; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (ch, i, 0.1f * (float) std::sin (0.03 * (b * blockSize + i)));
            proc.processBlock (buffer, midi);
        }

        CHECK_FALSE (proc.isSidechainConnected());
        CHECK (proc.getCurrentPitchHz() <= 0.0f);
    }
}

// ---------------------------------------------------------------------------------------------- lookahead

namespace
{
struct LookaheadRun
{
    std::vector<float> input, output;
    int latency = 0;
};

// Runs mono audio through a freshly prepared processor (Source = Main) and returns input, output and
// the reported latency.
LookaheadRun runMain (bool lookahead, const std::function<float (int)>& signal, int numSamples,
                      const std::function<void (juce::AudioProcessorValueTreeState&)>& configure = {})
{
    constexpr double fs = 48000.0;
    constexpr int blockSize = 256;

    HarmEQAudioProcessor proc;
    auto& apvts = proc.getValueTreeState();
    setParam (apvts, ParamIDs::source, (float) PitchSource::mainInput);
    setParam (apvts, ParamIDs::lookahead, lookahead ? 1.0f : 0.0f);
    setParam (apvts, ParamIDs::glide, 0.0f);
    if (configure)
        configure (apvts);

    proc.setPlayConfigDetails (2, 2, fs, blockSize);
    proc.prepareToPlay (fs, blockSize);

    LookaheadRun run;
    run.latency = proc.getLatencySamples();
    run.input.resize ((size_t) numSamples);
    run.output.resize ((size_t) numSamples);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    for (int n = 0; n < numSamples; n += blockSize)
    {
        const int len = std::min (blockSize, numSamples - n);
        buffer.setSize (2, len, false, false, true);
        for (int i = 0; i < len; ++i)
        {
            run.input[(size_t) (n + i)] = signal (n + i);
            buffer.setSample (0, i, run.input[(size_t) (n + i)]);
            buffer.setSample (1, i, run.input[(size_t) (n + i)]);
        }
        proc.processBlock (buffer, midi);
        for (int i = 0; i < len; ++i)
            run.output[(size_t) (n + i)] = buffer.getSample (0, i);
    }

    return run;
}
} // namespace

TEST_CASE ("Lookahead reports latency only for audio sources")
{
    juce::ScopedJuceInitialiser_GUI juce;

    auto latencyFor = [] (PitchSource source, bool lookahead) {
        HarmEQAudioProcessor proc;
        setParam (proc.getValueTreeState(), ParamIDs::source, (float) source);
        setParam (proc.getValueTreeState(), ParamIDs::lookahead, lookahead ? 1.0f : 0.0f);
        proc.setPlayConfigDetails (2, 2, 48000.0, 256);
        proc.prepareToPlay (48000.0, 256);
        return proc.getLatencySamples();
    };

    CHECK (latencyFor (PitchSource::mainInput, false) == 0);
    CHECK (latencyFor (PitchSource::midi, true) == 0);
    const int main = latencyFor (PitchSource::mainInput, true);
    CHECK (main > 480);   // > 10 ms
    CHECK (main < 2400);  // < 50 ms at the default 50 Hz Detect low
    CHECK (latencyFor (PitchSource::sidechain, true) == main);
}

TEST_CASE ("Lookahead delays the audio by exactly the reported latency")
{
    juce::ScopedJuceInitialiser_GUI juce;

    // All bands off: the only thing left in the path is the delay.
    auto bandsOff = [] (juce::AudioProcessorValueTreeState& apvts) {
        for (int b = 0; b < numBands; ++b)
            setParam (apvts, ParamIDs::band (b, "on"), 0.0f);
    };

    juce::Random rng (11);
    const auto run = runMain (true, [&] (int) { return rng.nextFloat() - 0.5f; }, 48000, bandsOff);
    REQUIRE (run.latency > 0);

    for (int n = run.latency; n < 48000; n += 97)
        REQUIRE (juce::exactlyEqual (run.output[(size_t) n], run.input[(size_t) (n - run.latency)]));
}

TEST_CASE ("Lookahead lines tracked bands up with note changes")
{
    juce::ScopedJuceInitialiser_GUI juce;
    constexpr double fs = 48000.0;
    constexpr int blockSize = 256;
    const int change = (int) (0.6 * fs), total = (int) (1.2 * fs);

    // A saw jumps from A2 to E3; band 1 boosts the fundamental by 12 dB.
    auto boostFundamental = [] (juce::AudioProcessorValueTreeState& apvts) {
        setParam (apvts, ParamIDs::band (0, "harmonic"), 1.0f);
        setParam (apvts, ParamIDs::band (0, "gain"), 12.0f);
        setParam (apvts, ParamIDs::band (0, "q"), 6.0f);
    };

    auto sawSignal = [&] {
        auto phase = std::make_shared<double> (0.0);
        return [phase, change] (int n) {
            const double f = n < change ? 110.0 : 164.8;
            const float s = 0.4f * waveAt (Wave::saw, *phase, f, fs);
            *phase = std::fmod (*phase + f / fs, 1.0);
            return s;
        };
    };

    // Reference: the same audio with the band driven by MIDI notes timed exactly at the change.
    std::vector<float> ideal ((size_t) total);
    {
        HarmEQAudioProcessor proc;
        auto& apvts = proc.getValueTreeState();
        setParam (apvts, ParamIDs::source, (float) PitchSource::midi);
        setParam (apvts, ParamIDs::glide, 0.0f);
        boostFundamental (apvts);
        proc.setPlayConfigDetails (2, 2, fs, blockSize);
        proc.prepareToPlay (fs, blockSize);

        auto signal = sawSignal();
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        for (int n = 0; n < total; n += blockSize)
        {
            midi.clear();
            if (n == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 45, (juce::uint8) 100), 0);
            if (change >= n && change < n + blockSize)
            {
                midi.addEvent (juce::MidiMessage::noteOff (1, 45), change - n);
                midi.addEvent (juce::MidiMessage::noteOn (1, 52, (juce::uint8) 100), change - n);
            }
            for (int i = 0; i < blockSize; ++i)
            {
                const float s = signal (n + i);
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }
            proc.processBlock (buffer, midi);
            for (int i = 0; i < blockSize; ++i)
                ideal[(size_t) (n + i)] = buffer.getSample (0, i);
        }
    }

    // Counts 5 ms windows around the change where the detected-pitch output (re-aligned by its reported
    // latency) differs from the ideal by more than 1.5 dB.
    auto misalignedWindows = [&] (bool lookahead) {
        const auto run = runMain (lookahead, sawSignal(), total, boostFundamental);
        const int win = (int) (0.005 * fs);
        int bad = 0;
        for (int start = change - (int) (0.1 * fs); start < change + (int) (0.15 * fs); start += win)
        {
            double a = 0.0, b = 0.0;
            for (int i = start; i < start + win; ++i)
            {
                a += (double) run.output[(size_t) (i + run.latency)] * run.output[(size_t) (i + run.latency)];
                b += (double) ideal[(size_t) i] * ideal[(size_t) i];
            }
            if (std::abs (10.0 * std::log10 (a / b)) > 1.5)
                ++bad;
        }
        return bad;
    };

    const int without = misalignedWindows (false);
    const int with = misalignedWindows (true);
    INFO ("misaligned 5 ms windows: " << without << " without lookahead, " << with << " with");
    CHECK (without >= 6);
    CHECK (with <= 2);
}

TEST_CASE ("Lookahead switched on during playback settles before the latency changes")
{
    juce::ScopedJuceInitialiser_GUI juce;
    constexpr double fs = 48000.0;
    constexpr int blockSize = 480; // 10 ms

    HarmEQAudioProcessor proc;
    auto& apvts = proc.getValueTreeState();
    setParam (apvts, ParamIDs::source, (float) PitchSource::mainInput);
    proc.setPlayConfigDetails (2, 2, fs, blockSize);
    proc.prepareToPlay (fs, blockSize);
    REQUIRE (proc.getLatencySamples() == 0);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    auto runMs = [&] (int ms) {
        for (int i = 0; i < ms / 10; ++i)
        {
            buffer.clear();
            proc.processBlock (buffer, midi);
        }
    };

    setParam (apvts, ParamIDs::lookahead, 1.0f);
    runMs (100);
    CHECK (proc.getLatencySamples() == 0); // still settling
    runMs (300);
    CHECK (proc.getLatencySamples() > 0);
    CHECK (proc.getLookaheadSamples() == proc.getLatencySamples());

    setParam (apvts, ParamIDs::source, (float) PitchSource::midi);
    runMs (400);
    CHECK (proc.getLatencySamples() == 0);
}
