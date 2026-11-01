#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Dsp/Engine.h"

class PoomseLoopsProcessor : public juce::AudioProcessor
{
public:
    PoomseLoopsProcessor();
    ~PoomseLoopsProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override { return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Poomse Loops"; }
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

    // ---- message thread ----
    bool loadRecord (const juce::File& file);
    void useBuiltInRecord();
    juce::String getRecordName() const;

    struct Mark { float x, y, w, score, level; };        // one found loop, on the map (x = place, y = size, w = length as a fraction of the record)
    struct View { std::vector<float> peaks; std::vector<Mark> marks; int version = 0; };
    View getView() const;                                 // copy for the interface
    int getViewVersion() const { return viewVersion.load(); }

    void randomise();
    void resetToDefaults();
    static int getNumPresets();
    static juce::String getPresetName (int i);
    void applyPreset (int index);
    int getCurrentPreset() const { return currentPreset.load(); }

    // ---- display ----
    int getVoiceLoop (int i) const { return voiceLoop[(size_t) i].load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void publish (std::shared_ptr<const lp::Record> r);

    lp::Engine engine;
    juce::SmoothedValue<float> levelS;
    double currentSampleRate = 48000.0;
    std::atomic<int> currentPreset { 0 };

    juce::SpinLock recordLock;
    std::shared_ptr<const lp::Record> record, audioRecord;
    std::vector<std::shared_ptr<const lp::Record>> retired;
    mutable juce::SpinLock viewLock;
    View view;
    std::atomic<int> viewVersion { 0 };

    std::atomic<float>* raw[9] {};   // position size seam moire scan pulse tail level places
    std::array<std::atomic<int>, lp::Engine::maxVoices> voiceLoop {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseLoopsProcessor)
};
