#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Dsp/ModMatrix.h"
#include "Dsp/Modulators.h"
#include "Dsp/PadReverb.h"
#include "Dsp/Presets.h"
#include "Dsp/SpectralPad.h"
#include "Dsp/Util.h"

class PoomsePadProcessor : public juce::AudioProcessor
{
public:
    PoomsePadProcessor();
    ~PoomsePadProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Poomse Pad"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Message thread. Randomise one page within musically useful ranges.
    //   Pad / Tone: that page's knobs (never Mix, Level or Hold)
    //   Mod: gives the 8 macros random targets in the matrix AND random values
    //   AllSound: Pad + Tone
    enum class RandomScope { Pad, Tone, Mod, AllSound };
    void randomise (RandomScope scope);

    // Message thread: every parameter back to its default and the matrix completely empty.
    void resetToDefaults();

    // Factory presets (message thread). Applying one starts from the neutral defaults, then sets the preset.
    static int getNumPresets() { return (int) pad::presets().size(); }
    static juce::String getPresetName (int i) { return pad::presets()[(size_t) i].name; }
    void applyPreset (int index);
    int getCurrentPreset() const { return currentPreset.load(); }

    // Modulation matrix (source x destination amount, -1..1). Thread-safe, edited from the UI.
    float getMatrix (int src, int dest) const { return matrix[(size_t) (src * gg::kNumDests + dest)].load(); }
    void setMatrix (int src, int dest, float v)
    {
        matrix[(size_t) (src * gg::kNumDests + dest)].store (juce::jlimit (-1.f, 1.f, v));
        ++matrixVersion;
    }
    int getMatrixVersion() const { return matrixVersion.load(); }
    float getSourceValue (int src) const { return sourceDisplay[(size_t) src].load(); }
    int getSeqStep() const { return seq.currentStep(); }
    float getDisplayBand (int i) const { return spectrumDisplay[(size_t) i].load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float param (const char* id) const; // value with the macro modulation applied

    pad::SpectralPad padEngine;
    pad::Reverb reverb;
    juce::SmoothedValue<float> levelSmooth, mixSmooth;
    double currentSampleRate = 48000.0;
    int reverbCounter = 0;
    std::atomic<int> currentPreset { 0 };

    std::array<std::atomic<float>*, gg::kNumMacros> macroRaw {};
    std::atomic<float>* lfoRaw[2][3] {};   // rate, sync, shape
    std::atomic<float>* seqRaw[6] {};      // steps, hits, rot, rate, decay, random
    std::array<std::atomic<float>, (size_t) (gg::kNumSources * gg::kNumDests)> matrix {};
    std::atomic<int> matrixVersion { 0 };
    std::array<std::atomic<float>, gg::kNumSources> sourceDisplay {};
    std::array<std::atomic<float>, pad::kDisplayBands> spectrumDisplay {};
    std::array<juce::RangedAudioParameter*, gg::kNumDests> destParam {};
    std::array<std::atomic<float>*, gg::kNumDests> destRaw {};
    mutable std::array<float, gg::kNumDests> modOffsets {};
    gg::Lfo lfo[2];
    gg::EuclidSeq seq;
    juce::String serialiseMatrix() const;
    void restoreMatrix (const juce::String&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomsePadProcessor)
};
