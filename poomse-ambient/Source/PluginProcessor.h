#pragma once
#include <map>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Dsp/Sampler.h"
#include "Dsp/Fx.h"
#include "Dsp/Reverb.h"
#include "Dsp/PitchDetect.h"
#include "Dsp/Aiff.h"

class PoomseAmbientProcessor : public juce::AudioProcessor, private juce::AsyncUpdater, private juce::AudioProcessorValueTreeState::Listener
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
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // a dropped sound: read and cut in the background (with new attempts: a file arriving from iCloud is not readable at once), then put in place
    struct Loaded { std::shared_ptr<amb::Bank> bank; juce::String info; };
    // a dropped sound goes into one of the four places; what the place is decides how it is played (a short note, a held note, a chord, a place)
    static juce::Result readSound (const juce::File& file, int slot, Loaded& out, int attempts);
    juce::Result installSound (int slot, const Loaded& loaded, const juce::File& file);
    juce::Result loadSound (int slot, const juce::File& file);
    void restoreOriginal (int slot);
    bool isCustom (int slot) const { return slot >= 0 && slot < amb::numFactory && userBank[slot] != nullptr; }
    juce::String customName (int slot) const;

    void uiNote (int note, bool on);
    void resetToDefaults();

    struct View { std::vector<float> peaks; bool loops = false; float loopStart = 0.f; };
    View getView() const;
    int getViewVersion() const { return viewVersion.load(); }
    bool isBuilding() const { return building.load() > 0; }
    float voicePosition (int i) const { return sampler.voicePosition (i); }
    int getSoundIndex() const { return (int) apvts.getRawParameterValue ("sound")->load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void handleAsyncUpdate() override;
    void parameterChanged (const juce::String& id, float) override { if (id == "sound") triggerAsyncUpdate(); }
    void publish (std::shared_ptr<const amb::Bank> b);
    void buildSound (int index);
    amb::SamplerParams readParams() const;

    amb::Sampler sampler;
    amb::Echo echo;
    amb::Reverb reverb;
    juce::SmoothedValue<float> levelS;
    double currentSampleRate = 48000.0, internalBeat = 0.0;
    std::atomic<bool> clearRequest { false };
    std::atomic<int> building { 0 }, viewVersion { 0 }, wanted { 0 };

    juce::SpinLock kitLock;
    std::shared_ptr<const amb::Bank> bank, audioBank;
    std::vector<std::shared_ptr<const amb::Bank>> retired;
    std::shared_ptr<const amb::Bank> userBank[amb::numFactory];
    juce::CriticalSection cacheLock;
    std::map<int, std::shared_ptr<const amb::Bank>> cache;
    mutable juce::SpinLock viewLock;
    View view;

    struct UiNote { int note; bool on; };
    juce::AbstractFifo uiFifo { 64 };
    std::array<UiNote, 64> uiNotes {};
    std::atomic<float>* raw[16] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseAmbientProcessor)
};
