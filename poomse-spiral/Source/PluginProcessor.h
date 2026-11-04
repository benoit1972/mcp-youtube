#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Dsp/Engine.h"

class PoomseSpiralProcessor : public juce::AudioProcessor
{
public:
    PoomseSpiralProcessor();
    ~PoomseSpiralProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && l.getMainInputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Poomse Spiral"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    void randomise();
    void reroll();
    int getSeed() const { return (int) apvts.getRawParameterValue ("seed")->load(); }
    void resetToDefaults();
    static int getNumPresets();
    static juce::String getPresetName (int i);
    void applyPreset (int index);
    int getCurrentPreset() const { return currentPreset.load(); }

    const sp::Engine& getEngine() const { return engine; }
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    sp::Params readParams() const;

    sp::Engine engine;
    juce::SmoothedValue<float> levelS;
    double currentSampleRate = 48000.0, internalBeat = 0.0;
    std::atomic<int> currentPreset { 0 };
    std::vector<float> inL, inR;
    std::atomic<bool> clearRequest { false };
    std::atomic<float>* raw[16] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseSpiralProcessor)
};
