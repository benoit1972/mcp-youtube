#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Dsp/AmbientFx.h"
#include "Dsp/GlitchFx.h"
#include "Dsp/GranularEngine.h"
#include "Dsp/ModMatrix.h"
#include "Dsp/NoteGen.h"

class GlitchGrainProcessor : public juce::AudioProcessor
{
public:
    GlitchGrainProcessor();
    ~GlitchGrainProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Poomse Glitch"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Message thread only. Returns false if the file cannot be read.
    bool loadSample (const juce::File& file);
    juce::String getSampleName() const;

    // Message thread: randomise one section within musically useful ranges.
    //   Grain / Play / Fx: that section's knobs (never Level, never Drift)
    //   Macros: assigns the 24 macro slots musically (macro values are left alone)
    //   AllSound: Grain + Play + Fx
    enum class RandomScope { Grain, Play, Fx, Macros, AllSound };
    void randomise (RandomScope scope);
    // Peak profile (0..1, 320 values) of the current sample, for the waveform display.
    std::vector<float> getPeaks() const;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    // Parameter value with the macro modulation applied (when the id is a modulation destination).
    float param (const char* id) const;
    void publishSource (std::shared_ptr<const gg::SourceBuffer> s);

    gg::GranularEngine engine;
    gg::GlitchFx fx;
    gg::AmbientFx ambient;
    gg::NoteGen noteGen;
    std::array<juce::int64, 12> lastHeard {}; // sample clock when each pitch class was last played (MIDI input only)
    juce::int64 sampleClock = 0;

    struct Ev { int offset; int kind; int note; float vel; }; // kind: 0 on, 1 off, 2 all off
    std::vector<Ev> events;
    // macro modulation: pointers cached once, offsets recomputed every block (audio thread)
    std::array<std::atomic<float>*, gg::kNumMacros> macroRaw {};
    std::atomic<float>* slotDestRaw[gg::kNumMacros][gg::kSlotsPerMacro] {};
    std::atomic<float>* slotAmtRaw[gg::kNumMacros][gg::kSlotsPerMacro] {};
    std::array<juce::RangedAudioParameter*, gg::kNumDests> destParam {};
    std::array<std::atomic<float>*, gg::kNumDests> destRaw {};
    mutable std::array<float, gg::kNumDests> modOffsets {};

    juce::SmoothedValue<float> level;
    double currentSampleRate = 48000.0;

    // Source hand-over: message thread publishes, audio thread picks it up with a try-lock.
    // Retired buffers are kept alive until the message thread purges them, so the audio
    // thread never frees memory.
    mutable juce::SpinLock peakLock;
    std::vector<float> peaks;

    juce::SpinLock sourceLock;
    std::shared_ptr<const gg::SourceBuffer> source;      // guarded by sourceLock
    std::shared_ptr<const gg::SourceBuffer> audioSource; // audio thread only
    std::vector<std::shared_ptr<const gg::SourceBuffer>> retired; // message thread only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlitchGrainProcessor)
};
