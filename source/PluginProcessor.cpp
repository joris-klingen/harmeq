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
    outputGainParam = apvts.getRawParameterValue (harmeq::ParamIDs::outputGain);
}

void HarmEQAudioProcessor::prepareToPlay (double sampleRate, int)
{
    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGainParam->load()));
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
    juce::ignoreUnused (midi);

    auto mainBuffer = getBusBuffer (buffer, false, 0);

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
