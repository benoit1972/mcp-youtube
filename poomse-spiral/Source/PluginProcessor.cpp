#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using Range = juce::NormalisableRange<float>;
enum { pTime, pPitchL, pPitchR, pFeedback, pTone, pDrive, pDrift, pSmear, pMix, pLevel, pHold, numRaw };
const char* const kIds[numRaw] = { "time", "pitchL", "pitchR", "feedback", "tone", "drive", "drift", "smear", "mix", "level", "hold" };

struct PresetParam { const char* id; float value; };
struct Preset { const char* name; std::vector<PresetParam> params; };
const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list = {
        { "Init", {} },
        { "Rising Spiral", { { "time", 2 }, { "pitchL", 7.f }, { "pitchR", 7.f }, { "feedback", 0.7f }, { "drift", 0.2f } } },
        { "Falling Away",  { { "time", 3 }, { "pitchL", -12.f }, { "pitchR", -12.f }, { "feedback", 0.65f }, { "tone", 0.4f }, { "drive", 0.5f } } },
        { "Octave Shimmer",{ { "time", 1 }, { "pitchL", 12.f }, { "pitchR", 12.f }, { "feedback", 0.6f }, { "mix", 0.4f }, { "smear", 0.8f }, { "drive", 0.1f } } },
        { "Robot Echo",    { { "time", 0 }, { "pitchL", 3.f }, { "pitchR", -3.f }, { "feedback", 0.7f }, { "smear", 0.f }, { "drive", 0.6f }, { "drift", 0.f } } },
        { "Tape Wash",     { { "time", 5 }, { "pitchL", 2.f }, { "pitchR", -2.f }, { "feedback", 0.85f }, { "tone", 0.35f }, { "mix", 0.6f }, { "smear", 1.f }, { "drift", 0.8f }, { "drive", 0.4f } } },
    };
    return list;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout PoomseSpiralProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* n) { return juce::ParameterID { n, 1 }; };
    auto attr = [] (auto fn) { return juce::AudioParameterFloatAttributes().withStringFromValueFunction (fn); };
    auto pct = [] (float x, int) { return juce::String (juce::roundToInt (x * 100.f)) + " %"; };

    l.add (std::make_unique<juce::AudioParameterChoice> (id ("time"), "Time", juce::StringArray { "1/16", "1/8", "1/8.", "1/4", "1/2", "1 bar" }, 2));
    auto st = [] (float x, int) { return juce::String (x, 1) + " st"; };
    l.add (std::make_unique<F> (id ("pitchL"), "Pitch L", Range (-24.f, 24.f, 0.01f), 7.f, attr (st)));
    l.add (std::make_unique<F> (id ("pitchR"), "Pitch R", Range (-24.f, 24.f, 0.01f), -5.f, attr (st)));
    l.add (std::make_unique<F> (id ("feedback"), "Feedback", Range (0.f, 1.f, 0.001f), 0.6f, attr (pct)));
    l.add (std::make_unique<F> (id ("tone"), "Tone", Range (0.f, 1.f, 0.001f), 0.7f, attr (pct)));
    l.add (std::make_unique<F> (id ("drive"), "Drive", Range (0.f, 1.f, 0.001f), 0.3f, attr (pct)));
    l.add (std::make_unique<F> (id ("drift"), "Drift", Range (0.f, 1.f, 0.001f), 0.35f, attr (pct)));
    l.add (std::make_unique<F> (id ("smear"), "Smear", Range (0.f, 1.f, 0.001f), 0.3f, attr (pct)));
    l.add (std::make_unique<F> (id ("mix"), "Mix", Range (0.f, 1.f, 0.001f), 0.5f, attr (pct)));
    l.add (std::make_unique<F> (id ("level"), "Level", Range (-48.f, 6.f, 0.1f), 0.f, attr ([] (float x, int) { return juce::String (x, 1) + " dB"; })));
    l.add (std::make_unique<juce::AudioParameterBool> (id ("hold"), "Hold", false));
    return l;
}

PoomseSpiralProcessor::PoomseSpiralProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int i = 0; i < numRaw; ++i) raw[i] = apvts.getRawParameterValue (kIds[i]);
}

int PoomseSpiralProcessor::getNumPresets() { return (int) presets().size(); }
juce::String PoomseSpiralProcessor::getPresetName (int i) { return presets()[(size_t) i].name; }

sp::Params PoomseSpiralProcessor::readParams() const
{
    sp::Params p;
    p.time = (int) raw[pTime]->load(); p.pitchL = raw[pPitchL]->load(); p.pitchR = raw[pPitchR]->load(); p.feedback = raw[pFeedback]->load();
    p.tone = raw[pTone]->load(); p.drive = raw[pDrive]->load(); p.drift = raw[pDrift]->load(); p.smear = raw[pSmear]->load(); p.mix = raw[pMix]->load(); p.hold = raw[pHold]->load() > 0.5f;
    return p;
}

void PoomseSpiralProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    inL.assign ((size_t) std::max (samplesPerBlock, 8192), 0.f);
    inR = inL;
    internalBeat = 0.0;
    levelS.reset (sampleRate, 0.05);
}

void PoomseSpiralProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2) return;
    if ((int) inL.size() < n) { inL.assign ((size_t) n, 0.f); inR = inL; }
    std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + n, inL.begin());
    std::copy (buffer.getReadPointer (1), buffer.getReadPointer (1) + n, inR.begin());

    if (clearRequest.exchange (false)) engine.clear();
    double bpm = 120.0;
    if (auto* head = getPlayHead()) if (auto pos = head->getPosition()) if (auto b = pos->getBpm()) bpm = *b;
    engine.process (n, readParams(), bpm, inL.data(), inR.data(), buffer.getWritePointer (0), buffer.getWritePointer (1));

    levelS.setTargetValue (juce::Decibels::decibelsToGain (raw[pLevel]->load()));
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    for (int i = 0; i < n; ++i) { const float g = levelS.getNextValue(); L[i] *= g; R[i] *= g; }
}

void PoomseSpiralProcessor::randomise()
{
    juce::Random rng;
    auto set = [this] (const char* id, float v) { if (auto* q = apvts.getParameter (id)) { q->beginChangeGesture(); q->setValueNotifyingHost (q->convertTo0to1 (v)); q->endChangeGesture(); } };
    static const float steps[] = { -12.f, -7.f, -5.f, -2.f, 0.f, 2.f, 5.f, 7.f, 12.f };
    set ("time", (float) rng.nextInt (6)); set ("pitchL", steps[rng.nextInt (9)]); set ("pitchR", steps[rng.nextInt (9)]);
    set ("feedback", 0.3f + 0.55f * rng.nextFloat()); set ("tone", 0.3f + 0.6f * rng.nextFloat());
    set ("drive", rng.nextFloat()); set ("drift", rng.nextFloat()); set ("smear", rng.nextFloat()); set ("mix", 0.3f + 0.5f * rng.nextFloat());
}

void PoomseSpiralProcessor::resetToDefaults()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->getDefaultValue());
            rp->endChangeGesture();
        }
    clearRequest.store (true);
    currentPreset.store (0);
}

void PoomseSpiralProcessor::applyPreset (int index)
{
    if (index < 0 || index >= getNumPresets()) return;
    resetToDefaults();
    for (const auto& pp : presets()[(size_t) index].params)
        if (auto* rp = apvts.getParameter (pp.id))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->convertTo0to1 (pp.value));
            rp->endChangeGesture();
        }
    currentPreset.store (index);
}

juce::AudioProcessorEditor* PoomseSpiralProcessor::createEditor() { return new PoomseSpiralEditor (*this); }

void PoomseSpiralProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    apvts.state.setProperty ("preset", currentPreset.load(), nullptr);
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void PoomseSpiralProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset.store (juce::jlimit (0, getNumPresets() - 1, (int) apvts.state.getProperty ("preset", 0)));
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PoomseSpiralProcessor(); }
