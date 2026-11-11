#pragma once
#include <map>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Dsp/Orchestra.h"
#include "Dsp/Fx.h"
#include "Dsp/Reverb.h"
#include "Dsp/PitchDetect.h"
#include "Dsp/Aiff.h"

class PoomseAmbientProcessor : public juce::AudioProcessor
{
public:
    PoomseAmbientProcessor();
    ~PoomseAmbientProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Poomse Ambient"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // a dropped sound goes into one of the four places; what the place is decides how it is played (a short note, a held note, a chord, a place)
    struct Loaded { std::shared_ptr<amb::Bank> bank; juce::String info; };
    static juce::Result readSound (const juce::File& file, int slot, Loaded& out, int attempts);
    juce::Result installSound (int slot, const Loaded& loaded, const juce::File& file);
    juce::Result loadSound (int slot, const juce::File& file);
    void restoreOriginal (int slot);
    bool isCustom (int slot) const { return slot >= 0 && slot < 4 && userBank[slot] != nullptr; }
    juce::String customName (int slot) const;

    void preview (int slot) { previewMask.fetch_or (1 << slot); }
    void reroll();
    void freeVersion();
    void resetToDefaults();

    // what the interface shows
    bool isBuilding() const { return building.load() > 0; }
    bool pieceRunning() const { return running.load(); }
    float progress() const { return progressShown.load(); }
    int chordRoot() const { return chordRootShown.load(); }
    int chordQuality() const { return chordQualityShown.load(); }      // 0 major, 1 minor, 2 diminished
    int mood() const { return moodShown.load(); }
    float slotActivity (int s) const { return activityShown[(size_t) s].load(); }
    int versionInUse() const { return seedInUse.load(); }
    int getSeed() const { return (int) apvts.getRawParameterValue ("seed")->load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void buildFactory();
    void setCurrent (int slot, std::shared_ptr<const amb::Bank> b);

    amb::Orchestra orchestra;
    amb::Reverb reverb;
    juce::SmoothedValue<float> levelS, reverbS;
    double currentSampleRate = 48000.0, internalBeat = 0.0;
    std::atomic<bool> clearRequest { false };
    std::atomic<int> building { 0 }, previewMask { 0 }, seedInUse { 0 };
    std::atomic<bool> running { false };
    std::atomic<float> progressShown { -1.f };
    std::atomic<int> chordRootShown { 0 }, chordQualityShown { 0 }, moodShown { 0 };
    std::array<std::atomic<float>, 4> activityShown {};

    juce::SpinLock bankLock;
    std::shared_ptr<const amb::Bank> factory[4], userBank[4], current[4], audioBank[4];
    std::vector<std::shared_ptr<const amb::Bank>> retired;
    std::vector<float> dryL, dryR, sendL, sendR;
    gg::Rng seedRng;

    std::atomic<float>* raw[24] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseAmbientProcessor)
};
