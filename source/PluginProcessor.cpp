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
    glideParam = apvts.getRawParameterValue (ParamIDs::glide);
    noPitchModeParam = apvts.getRawParameterValue (ParamIDs::noPitchMode);
    fallbackFreqParam = apvts.getRawParameterValue (ParamIDs::fallbackFreq);
    pitchBendRangeParam = apvts.getRawParameterValue (ParamIDs::pitchBendRange);
    transposeParam = apvts.getRawParameterValue (ParamIDs::transpose);
    outputGainParam = apvts.getRawParameterValue (ParamIDs::outputGain);

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
    engine.prepare (sampleRate);
    pitchTracker.prepare (sampleRate);

    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGainParam->load()));
}

void HarmEQAudioProcessor::updateSettings()
{
    using namespace harmeq;

    PitchTracker::Settings ps;
    ps.source = static_cast<PitchSource> ((int) sourceParam->load());
    ps.noPitchMode = static_cast<NoPitchMode> ((int) noPitchModeParam->load());
    ps.glideMs = glideParam->load();
    ps.fallbackHz = fallbackFreqParam->load();
    ps.pitchBendRange = pitchBendRangeParam->load();
    ps.transpose = transposeParam->load();
    pitchTracker.setSettings (ps);

    for (int b = 0; b < numBands; ++b)
        engine.setBandSettings (b, getBandSettings (b));
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

    sidechainConnected.store (getBusCount (true) > 1 && getBusBuffer (buffer, true, 1).getNumChannels() > 0,
                              std::memory_order_relaxed);

    const bool feedAnalyzer = analyzerActive.load (std::memory_order_relaxed);
    if (feedAnalyzer)
        preTap.push (mainBuffer);

    // Split the block at MIDI events (and into engine-sized chunks) so note changes are sample-accurate.
    auto event = midi.cbegin();
    harmeq::PitchState pitch;

    for (int pos = 0; pos < numSamples;)
    {
        for (; event != midi.cend() && (*event).samplePosition <= pos; ++event)
            pitchTracker.handleMidi ((*event).getMessage());

        int end = std::min (numSamples, pos + harmeq::EqEngine::maxChunkSize);
        if (event != midi.cend())
            end = std::min (end, (*event).samplePosition);

        pitch = pitchTracker.advance (end - pos);
        engine.process (mainBuffer, pos, end - pos, pitch);
        pos = end;
    }

    // Events stamped at or past the block end still belong to this block.
    for (; event != midi.cend(); ++event)
        pitchTracker.handleMidi ((*event).getMessage());

    if (numSamples > 0)
    {
        displayPitchHz.store (pitch.f0Hz, std::memory_order_relaxed);
        displayVoiced.store (pitch.voiced, std::memory_order_relaxed);
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
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void HarmEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmEQAudioProcessor();
}
