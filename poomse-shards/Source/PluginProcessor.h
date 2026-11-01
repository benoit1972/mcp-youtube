#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Dsp/Engine.h"

class PoomseShardsProcessor : public juce::AudioProcessor
{
public:
    PoomseShardsProcessor();
    ~PoomseShardsProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override { return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Poomse Shards"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 1.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- message thread ----
    bool loadBreak (const juce::File& file);
    void useBuiltInBreak();
    juce::String getBreakName() const;

    struct Mark { float x, y, w, level; };               // one piece of the break, on the map
    struct View { std::vector<float> peaks; std::vector<Mark> marks; };
    View getView() const;
    int getViewVersion() const { return viewVersion.load(); }

    void randomise();
    void newEdits();                                      // another seed: the same bar breaks in another way
    void resetToDefaults();
    static int getNumPresets();
    static juce::String getPresetName (int i);
    void applyPreset (int index);
    int getCurrentPreset() const { return currentPreset.load(); }

    // ---- display ----
    int getHitCount() const { return engine.hitCount(); }
    int getHit (int i) const { return engine.hitAt (i); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void publish (std::shared_ptr<const sh::Kit> k);

    sh::Engine engine;
    juce::SmoothedValue<float> levelS;
    double currentSampleRate = 48000.0, internalBeat = 0.0;
    std::atomic<int> currentPreset { 0 };

    juce::SpinLock kitLock;
    std::shared_ptr<const sh::Kit> kit, audioKit;
    std::vector<std::shared_ptr<const sh::Kit>> retired;
    mutable juce::SpinLock viewLock;
    View view;
    std::atomic<int> viewVersion { 0 };

    std::atomic<float>* raw[10] {};   // place shatter grid ramp scatter fall snap tune level seed

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseShardsProcessor)
};
