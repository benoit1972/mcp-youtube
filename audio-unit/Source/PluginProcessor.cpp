#include "PluginProcessor.h"

SimpleGainProcessor::SimpleGainProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout SimpleGainProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gain", 1 }, "Gain (dB)",
        juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "invert", 1 }, "Invert Phase", false));
    return layout;
}

void SimpleGainProcessor::prepareToPlay (double sampleRate, int)
{
    smoothedGain.reset (sampleRate, 0.02);
    smoothedGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("gain")->load()));
}

bool SimpleGainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == layouts.getMainInputChannelSet();
}

void SimpleGainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    const float db  = apvts.getRawParameterValue ("gain")->load();
    const bool inv  = apvts.getRawParameterValue ("invert")->load() > 0.5f;
    smoothedGain.setTargetValue (juce::Decibels::decibelsToGain (db) * (inv ? -1.0f : 1.0f));

    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();

    for (int s = 0; s < n; ++s)
    {
        const float g = smoothedGain.getNextValue();
        for (int c = 0; c < channels; ++c)
            buffer.getWritePointer (c)[s] *= g;
    }
}

juce::AudioProcessorEditor* SimpleGainProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void SimpleGainProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void SimpleGainProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SimpleGainProcessor();
}
