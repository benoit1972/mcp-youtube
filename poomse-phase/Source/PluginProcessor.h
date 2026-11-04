#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Dsp/Engine.h"
#include "Dsp/Sounds.h"
#include "Dsp/Fx.h"
#include "Dsp/Reverb.h"
#include "Dsp/PitchDetect.h"
#include "Dsp/Aiff.h"

class PoomsePhaseProcessor : public juce::AudioProcessor
{
public:
    PoomsePhaseProcessor();
    ~PoomsePhaseProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override { return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Poomse Phase"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // a sound is read and cut in the background (it retries: a file arriving from iCloud is not readable at once), then put in place
    struct Loaded { std::shared_ptr<ph::Kit> kit; int root = 60; juce::String info; };
    static juce::Result readSound (const juce::File& file, Loaded& out, int attempts);
    juce::Result installSound (const Loaded& loaded, const juce::File& file);
    juce::Result loadSound (const juce::File& file);
    void useBuiltInSound();
    juce::String getSoundName() const;
    int getSoundVersion() const { return soundVersion.load(); }

    void uiNote (int note, bool on);
    void reroll();
    int getSeed() const { return (int) apvts.getRawParameterValue ("seed")->load(); }
    void resetToDefaults();
    void clearNotes() { clearRequest.store (true); }          // lets go of every key, held or latched, and silences what rings
    static int getNumPresets();
    static juce::String getPresetName (int i);
    void applyPreset (int index);
    int getCurrentPreset() const { return currentPreset.load(); }
    void randomise();

    // display
    int getHitCount() const { return engine.hitCount(); }
    int getHit (int i) const { return engine.hitAt (i); }
    int layerLength (int i) const { return engine.layerLength (i); }
    int layerPosition (int i) const { return engine.layerPosition (i); }
    uint64_t layerMask (int i) const { return engine.layerMask (i); }
    int getActiveNotes() const { return engine.activeNotes(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void publish (std::shared_ptr<const ph::Kit> k, int root);
    ph::Params readParams() const;

    ph::Engine engine;
    ph::Echo echo;
    ph::Reverb reverb;
    juce::SmoothedValue<float> levelS;
    double currentSampleRate = 48000.0, internalBeat = 0.0;
    std::atomic<int> currentPreset { 0 }, soundVersion { 0 };
    std::atomic<bool> clearRequest { false };

    juce::SpinLock kitLock;
    std::shared_ptr<const ph::Kit> kit, audioKit;
    int kitRoot = 60, audioRoot = 60;
    std::vector<std::shared_ptr<const ph::Kit>> retired;

    struct UiNote { int note; bool on; };
    juce::AbstractFifo uiFifo { 64 };
    std::array<UiNote, 64> uiNotes {};
    std::atomic<float>* raw[16] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomsePhaseProcessor)
};
