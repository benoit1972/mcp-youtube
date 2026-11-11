#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Dsp/Murmur.h"
#include "Dsp/Reverb.h"
#include "Dsp/PitchDetect.h"
#include "Dsp/Aiff.h"

class PoomseMurmurProcessor : public juce::AudioProcessor
{
public:
    PoomseMurmurProcessor();
    ~PoomseMurmurProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Poomse Murmur"; }
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

    // one dropped sound replaces the built-in one
    struct Loaded { std::shared_ptr<mur::Zone> zone; juce::String info; };
    static juce::Result readSound (const juce::File& file, Loaded& out, int attempts);
    juce::Result installSound (const Loaded& loaded, const juce::File& file);
    juce::Result loadSound (const juce::File& file);
    void restoreOriginal();
    bool isCustom() const { return userZone != nullptr; }
    juce::String customName() const;

    void reroll();
    void freeVersion();
    void randomise();
    void resetToDefaults();

    // what the interface shows
    bool isBuilding() const { return building.load() > 0; }
    bool pieceRunning() const { return murmur.pieceRunning(); }
    float voicePitch (int v) const { return murmur.voicePitch (v); }
    float voiceLevel (int v) const { return murmur.voiceLevel (v); }
    int rootNote() const { return rootShown.load(); }
    float progress() const { return progressShown.load(); }
    int versionInUse() const { return seedInUse.load(); }
    int getSeed() const { return (int) apvts.getRawParameterValue ("seed")->load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void buildBuiltIns();

    mur::Murmur murmur;
    mur::Reverb reverb;
    juce::SmoothedValue<float> levelS, reverbS;
    double currentSampleRate = 48000.0, internalBeat = 0.0;
    std::atomic<bool> clearRequest { false };
    std::atomic<int> building { 0 }, seedInUse { 0 }, rootShown { 60 };
    std::atomic<float> progressShown { -1.f };

    juce::SpinLock zoneLock;
    std::shared_ptr<const mur::Zone> builtIn[2], userZone, audioZone;
    std::vector<std::shared_ptr<const mur::Zone>> retired;
    std::vector<float> dryL, dryR, sendL, sendR;
    gg::Rng seedRng;
    std::atomic<float>* raw[24] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseMurmurProcessor)
};
