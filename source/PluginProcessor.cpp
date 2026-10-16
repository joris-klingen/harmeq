#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

HarmEQAudioProcessor::BusesProperties HarmEQAudioProcessor::createBusesProperties()
{
    return BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
        // Disabled by default so hosts that handle extra buses poorly still load the plugin.
        .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false);
}

HarmEQAudioProcessor::HarmEQAudioProcessor()
    : AudioProcessor (createBusesProperties()),
      apvts (*this, nullptr, "HarmEQ", harmeq::createParameterLayout())
{
    using namespace harmeq;

    sourceParam = apvts.getRawParameterValue (ParamIDs::source);
    midiModeParam = apvts.getRawParameterValue (ParamIDs::midiMode);

    for (int v = 0; v < maxVoices; ++v)
    {
        displayPitchHz[(size_t) v].store (110.0f);
        displayVoiced[(size_t) v].store (v == 0);
    }
    glideParam = apvts.getRawParameterValue (ParamIDs::glide);
    noPitchModeParam = apvts.getRawParameterValue (ParamIDs::noPitchMode);
    fallbackFreqParam = apvts.getRawParameterValue (ParamIDs::fallbackFreq);
    pitchBendRangeParam = apvts.getRawParameterValue (ParamIDs::pitchBendRange);
    transposeParam = apvts.getRawParameterValue (ParamIDs::transpose);
    outputGainParam = apvts.getRawParameterValue (ParamIDs::outputGain);
    minFreqParam = apvts.getRawParameterValue (ParamIDs::minFreq);
    maxFreqParam = apvts.getRawParameterValue (ParamIDs::maxFreq);
    confidenceParam = apvts.getRawParameterValue (ParamIDs::confidence);
    lookaheadParam = apvts.getRawParameterValue (ParamIDs::lookahead);

    for (int b = 0; b < numBands; ++b)
    {
        auto& p = bandParams[(size_t) b];
        p.on = apvts.getRawParameterValue (ParamIDs::band (b, "on"));
        p.type = apvts.getRawParameterValue (ParamIDs::band (b, "type"));
        p.track = apvts.getRawParameterValue (ParamIDs::band (b, "track"));
        p.harmonic = apvts.getRawParameterValue (ParamIDs::band (b, "harmonic"));
        p.detune = apvts.getRawParameterValue (ParamIDs::band (b, "detune"));
        p.freq = apvts.getRawParameterValue (ParamIDs::band (b, "freq"));
        p.gain = apvts.getRawParameterValue (ParamIDs::band (b, "gain"));
        p.q = apvts.getRawParameterValue (ParamIDs::band (b, "q"));
        p.slope = apvts.getRawParameterValue (ParamIDs::band (b, "slope"));
    }
}

void HarmEQAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    pitchTracker.prepare (sampleRate);
    detector.prepare (sampleRate);

    // Settle the lookahead immediately here; while playing, changes are debounced (updateLatency).
    updateSettings();
    lookaheadDelay.prepare (juce::jmax (1, getMainBusNumOutputChannels()), detector.getMaxLatencySamples());
    updateLatency (0, true);

    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGainParam->load()));
}

void HarmEQAudioProcessor::updateSettings()
{
    using namespace harmeq;

    PitchTracker::Settings ps;
    ps.source = static_cast<PitchSource> ((int) sourceParam->load());
    ps.midiMode = static_cast<MidiMode> ((int) midiModeParam->load());
    ps.noPitchMode = static_cast<NoPitchMode> ((int) noPitchModeParam->load());
    ps.glideMs = glideParam->load();
    ps.fallbackHz = fallbackFreqParam->load();
    ps.pitchBendRange = pitchBendRangeParam->load();
    ps.transpose = transposeParam->load();
    pitchTracker.setSettings (ps);

    detector.setRange (minFreqParam->load(), maxFreqParam->load());
    detector.setThreshold (confidenceParam->load());

    // A different input means old analysis history no longer applies.
    if (ps.source != lastSource)
    {
        detector.reset();
        lastSource = ps.source;
    }

    for (int b = 0; b < numBands; ++b)
        engine.setBandSettings (b, getBandSettings (b));
}

// Lookahead: with an audio source, delay the EQ'd audio by the detector's lag so detected pitches line
// up with the notes they came from, and report it as latency. A new value only takes effect once it has
// been stable for a quarter second, so dragging Detect low doesn't make the host re-align on every step.
void HarmEQAudioProcessor::updateLatency (int blockSamples, bool immediately) noexcept
{
    const bool wanted = lookaheadParam->load() > 0.5f && lastSource != harmeq::PitchSource::midi;
    const int target = wanted ? detector.getLatencySamples() : 0;

    if (! immediately)
    {
        if (target != pendingLatency)
        {
            pendingLatency = target;
            pendingLatencyAge = 0;
            return;
        }

        pendingLatencyAge += blockSamples;
        if (target == appliedLatency.load (std::memory_order_relaxed) || pendingLatencyAge < (int) (0.25 * currentSampleRate))
            return;
    }

    pendingLatency = target;

    lookaheadDelay.reset();
    lookaheadDelay.setDelay (target);
    appliedLatency.store (target, std::memory_order_relaxed);
    setLatencySamples (target);
}

// Runs the detector on this chunk of the selected audio source (before the EQ touches it) and hands
// the result to the tracker.
void HarmEQAudioProcessor::detectPitch (juce::AudioBuffer<float>& buffer, int start, int numSamples) noexcept
{
    using namespace harmeq;

    if (lastSource == PitchSource::midi)
    {
        detectedConfidence.store (0.0f, std::memory_order_relaxed);
        return;
    }

    const int busIndex = lastSource == PitchSource::sidechain ? 1 : 0;
    const auto input = busIndex < getBusCount (true) ? getBusBuffer (buffer, true, busIndex) : juce::AudioBuffer<float>();
    const int channels = input.getNumChannels();

    if (channels == 0)
    {
        pitchTracker.setDetectedPitch (0.0f, false);
        detectedConfidence.store (0.0f, std::memory_order_relaxed);
        return;
    }

    // Mono mix, in pieces that fit a small stack buffer (chunks are at most EqEngine::maxChunkSize).
    float mono[EqEngine::maxChunkSize];
    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (numSamples - done, EqEngine::maxChunkSize);
        for (int i = 0; i < n; ++i)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                sum += input.getReadPointer (ch)[start + done + i];
            mono[i] = sum / (float) channels;
        }
        detector.push (mono, n);
        done += n;
    }

    const auto& r = detector.getResult();
    pitchTracker.setDetectedPitch (r.f0Hz, r.voiced);
    detectedConfidence.store (r.confidence, std::memory_order_relaxed);
}

harmeq::PitchSet HarmEQAudioProcessor::getDisplayPitches() const noexcept
{
    harmeq::PitchSet set;
    for (size_t v = 0; v < set.size(); ++v)
        set[v] = { displayPitchHz[v].load (std::memory_order_relaxed), displayVoiced[v].load (std::memory_order_relaxed) };
    return set;
}

harmeq::PitchState HarmEQAudioProcessor::getDisplayPitch() const noexcept
{
    const auto set = getDisplayPitches();
    const harmeq::PitchState* lowest = nullptr;

    for (const auto& p : set)
        if (p.voiced && (lowest == nullptr || p.f0Hz < lowest->f0Hz))
            lowest = &p;

    return lowest != nullptr ? *lowest : harmeq::PitchState { set[0].f0Hz, false };
}

harmeq::BandSettings HarmEQAudioProcessor::getBandSettings (int band) const noexcept
{
    using namespace harmeq;
    const auto& p = bandParams[(size_t) band];

    BandSettings s;
    s.on = p.on->load() > 0.5f;
    s.type = static_cast<FilterType> ((int) p.type->load());
    s.track = static_cast<TrackMode> ((int) p.track->load());
    s.harmonic = p.harmonic->load();
    s.detuneCents = p.detune->load();
    s.freqHz = p.freq->load();
    s.gainDb = p.gain->load();
    s.q = p.q->load();
    s.steep = p.slope->load() > 0.5f;
    return s;
}

bool HarmEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    if (mainIn != mainOut)
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sidechain = layouts.getChannelSet (true, 1);

        if (! sidechain.isDisabled() && sidechain != juce::AudioChannelSet::mono()
            && sidechain != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void HarmEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    auto mainBuffer = getBusBuffer (buffer, false, 0);
    const int numSamples = mainBuffer.getNumSamples();

    updateSettings();
    updateLatency (numSamples);

    sidechainConnected.store (getBusCount (true) > 1 && getBusBuffer (buffer, true, 1).getNumChannels() > 0,
                              std::memory_order_relaxed);

    const bool feedAnalyzer = analyzerActive.load (std::memory_order_relaxed);
    if (feedAnalyzer)
        preTap.push (mainBuffer);

    // Split the block at MIDI events (and into engine-sized chunks) so note changes are sample-accurate.
    auto event = midi.cbegin();
    harmeq::PitchSet pitches;

    for (int pos = 0; pos < numSamples;)
    {
        for (; event != midi.cend() && (*event).samplePosition <= pos; ++event)
            pitchTracker.handleMidi ((*event).getMessage());

        int end = std::min (numSamples, pos + harmeq::EqEngine::maxChunkSize);
        if (event != midi.cend())
            end = std::min (end, (*event).samplePosition);

        detectPitch (buffer, pos, end - pos);
        lookaheadDelay.process (mainBuffer.getArrayOfWritePointers(), mainBuffer.getNumChannels(), pos, end - pos);
        pitches = pitchTracker.advance (end - pos);
        engine.process (mainBuffer, pos, end - pos, pitches);
        pos = end;
    }

    // Events stamped at or past the block end still belong to this block.
    for (; event != midi.cend(); ++event)
        pitchTracker.handleMidi ((*event).getMessage());

    if (numSamples > 0)
    {
        for (size_t v = 0; v < pitches.size(); ++v)
        {
            displayPitchHz[v].store (pitches[v].f0Hz, std::memory_order_relaxed);
            displayVoiced[v].store (pitches[v].voiced, std::memory_order_relaxed);
        }
        polyActive.store (pitchTracker.isPoly(), std::memory_order_relaxed);
    }

    applyOutputGain (mainBuffer);

    if (feedAnalyzer)
        postTap.push (mainBuffer);
}

void HarmEQAudioProcessor::applyOutputGain (juce::AudioBuffer<float>& mainBuffer)
{
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputGainParam->load()));

    if (outputGain.isSmoothing())
    {
        for (int i = 0; i < mainBuffer.getNumSamples(); ++i)
        {
            const auto g = outputGain.getNextValue();

            for (int ch = 0; ch < mainBuffer.getNumChannels(); ++ch)
                mainBuffer.getWritePointer (ch)[i] *= g;
        }
    }
    else
    {
        mainBuffer.applyGain (outputGain.getTargetValue());
    }
}

juce::AudioProcessorEditor* HarmEQAudioProcessor::createEditor()
{
    return new HarmEQAudioProcessorEditor (*this);
}

void HarmEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = apvts.copyState();
    tree.setProperty (harmeq::state::versionID, harmeq::state::currentVersion, nullptr);
    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void HarmEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto loaded = juce::ValueTree::fromXml (*xml);
    harmeq::state::migrate (loaded);

    // Parameters missing from an older project get their defaults rather than keeping whatever this
    // instance had (replaceState alone would keep them).
    apvts.replaceState (harmeq::state::withParameters (apvts, loaded, loaded));
}

// Host bypass: pass the audio through unprocessed, but keep the lookahead delay so the track stays
// aligned with the latency the host is compensating for.
void HarmEQAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    auto mainBuffer = getBusBuffer (buffer, false, 0);
    lookaheadDelay.process (mainBuffer.getArrayOfWritePointers(), mainBuffer.getNumChannels(), 0,
                            mainBuffer.getNumSamples());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmEQAudioProcessor();
}
