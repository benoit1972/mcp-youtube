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
    double getTailLengthSeconds() const override { return 1.0; }

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

    struct Mark { float x, y, w, level; };               // one hit of the loop, on the map (x = place in the loop)
    struct View { std::vector<float> peaks; std::vector<Mark> marks; float origin = 0.f; };
    View getView() const;
    int getViewVersion() const { return viewVersion.load(); }

    void uiNote (int note, bool on);                      // the pads of the interface play like keys
    void randomise();
    void newEdits();                                      // another seed: the same bar breaks in other ways
    void resetToDefaults();
    static int getNumPresets();
    static juce::String getPresetName (int i);
    void applyPreset (int index);
    int getCurrentPreset() const { return currentPreset.load(); }

    // ---- display ----
    int getHitCount() const { return engine.hitCount(); }
    int getHit (int i) const { return engine.hitAt (i); }
    float getLoopPhase() const { return engine.loopPhase(); }
    int getActiveFlavours() const { return engine.activeFlavours(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void publish (std::shared_ptr<const sh::Kit> k);
    sh::Params readParams() const;

    sh::Engine engine;
    juce::SmoothedValue<float> levelS;
    double currentSampleRate = 48000.0, internalBeat = 0.0;
    int lastBars = -1;
    std::atomic<int> currentPreset { 0 };

    juce::SpinLock kitLock;
    std::shared_ptr<const sh::Kit> kit, audioKit;
    std::vector<std::shared_ptr<const sh::Kit>> retired;
    mutable juce::SpinLock viewLock;
    View view;
    std::atomic<int> viewVersion { 0 };

    struct UiNote { int note; bool on; };
    juce::AbstractFifo uiFifo { 64 };
    std::array<UiNote, 64> uiNotes {};

    std::atomic<float>* raw[16] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseShardsProcessor)
};
