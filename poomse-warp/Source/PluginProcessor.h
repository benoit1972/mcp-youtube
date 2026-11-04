#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Dsp/Engine.h"
#include "Dsp/Fx.h"
#include "Dsp/Reverb.h"

class PoomseWarpProcessor : public juce::AudioProcessor
{
public:
    PoomseWarpProcessor();
    ~PoomseWarpProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Poomse Warp"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    void uiNote (int note, bool on);
    void randomise();
    void reroll();
    int getSeed() const { return (int) apvts.getRawParameterValue ("seed")->load(); }
    void resetToDefaults();
    static int getNumPresets();
    static juce::String getPresetName (int i);
    void applyPreset (int index);
    int getCurrentPreset() const { return currentPreset.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    wp::Params readParams() const;

    wp::Engine engine;
    wp::Echo echo;
    wp::Reverb reverb;
    juce::SmoothedValue<float> levelS;
    double currentSampleRate = 48000.0;
    std::atomic<int> currentPreset { 0 };
    std::atomic<bool> clearRequest { false };

    struct UiNote { int note; bool on; };
    juce::AbstractFifo uiFifo { 64 };
    std::array<UiNote, 64> uiNotes {};
    std::atomic<float>* raw[24] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseWarpProcessor)
};
