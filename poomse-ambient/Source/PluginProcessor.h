#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Dsp/Engine.h"
#include "Dsp/Presets.h"
#include "Dsp/Util.h"
#include "Dsp/Reverb.h"

class PoomseAmbientProcessor : public juce::AudioProcessor,
                               private juce::AudioProcessorValueTreeState::Listener,
                               private juce::AsyncUpdater
{
public:
    PoomseAmbientProcessor();
    ~PoomseAmbientProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Poomse Ambient"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- message thread ----
    bool loadSample (int slot, const juce::File& file);
    juce::String getSampleName (int slot) const;
    std::vector<float> getPeaks (int slot) const;
    bool slotUsesBuiltIn (int slot) const;

    enum class RandomScope { Piece, Layers, Sound, AllSound };
    void randomise (RandomScope scope);
    void resetToDefaults();
    static int getNumPresets() { return (int) amb::presets().size(); }
    static juce::String getPresetName (int i) { return amb::presets()[(size_t) i].name; }
    void applyPreset (int index);
    int getCurrentPreset() const { return currentPreset.load(); }
    void keepSeed(); // copies the seed of the piece being played into the Seed parameter

    // ---- display (UI reads these) ----
    float getProgress() const { return progressDisplay.load(); }            // 0..1, or -1 when no piece is playing
    int getSeedInUse() const { return seedInUse.load(); }
    int getSceneCount() const { return sceneCount.load(); }
    float getSceneStart (int i) const { return sceneStart[(size_t) i].load(); }
    int getSceneMood (int i) const { return sceneMood[(size_t) i].load(); }
    float getActivity (int layer) const { return activity[(size_t) layer].load(); }
    bool isNight() const { return apvts.getRawParameterValue ("night")->load() > 0.5f; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override;
    void publishSource (int slot, std::shared_ptr<const amb::SourceBuffer> s);
    void installBuiltIns();

    amb::Engine engine;
    amb::Cloud cloud;
    amb::Reverb reverb;
    amb::Echo echo;
    amb::Tape tape;
    amb::Dark dark;
    amb::Crackle crackle;
    juce::SmoothedValue<float> darkS, crackleS, tapeS, reverbS, echoS, cloudS, levelS;
    double currentSampleRate = 48000.0, internalBeat = 0.0;
    std::atomic<int> currentPreset { 0 };
    gg::Rng seedRng { (uint32_t) 0x5EED1234u };

    // sample hand-over (see the Pad plugin): message thread publishes, audio thread picks up with a try-lock
    juce::SpinLock sourceLock;
    std::shared_ptr<const amb::SourceBuffer> source[amb::kNumLayers];
    std::shared_ptr<const amb::SourceBuffer> audioSource[amb::kNumLayers];
    std::vector<std::shared_ptr<const amb::SourceBuffer>> retired;
    mutable juce::SpinLock peakLock;
    std::vector<float> peaks[amb::kNumLayers];

    std::vector<float> dryL, dryR, sendL, sendR, cloudInL, cloudInR;
    std::atomic<float>* layerRaw[amb::kNumLayers][4] {};   // level, amount, space, root
    std::atomic<float>* globalRaw[8] {};                   // scale, journey, space, depth, surprise, bars, seed, night
    std::atomic<float>* fxRaw[7] {};                       // dark, crackle, tape, reverb, echo, cloud, level

    std::atomic<float> progressDisplay { -1.f };
    std::atomic<int> seedInUse { 0 }, sceneCount { 0 };
    std::array<std::atomic<float>, 64> sceneStart {};
    std::array<std::atomic<int>, 64> sceneMood {};
    std::array<std::atomic<float>, amb::kNumLayers> activity {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseAmbientProcessor)
};
