#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using Range = juce::NormalisableRange<float>;
enum { pPosition, pSize, pPitch, pDensity, pTexture, pSpread, pBlend, pFeedback, pReverb, pLevel, pFreeze, pGrid, numRaw };
const char* const kIds[numRaw] = { "position", "size", "pitch", "density", "texture", "spread", "blend", "feedback", "reverb", "level", "freeze", "grid" };

struct PresetParam { const char* id; float value; };
struct Preset { const char* name; std::vector<PresetParam> params; };
const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list = {
        { "Init", {} },
        { "Mist",        { { "size", 0.7f }, { "density", 0.85f }, { "texture", 0.6f }, { "spread", 1.f }, { "blend", 0.45f }, { "reverb", 0.4f } } },
        { "Shimmer",     { { "position", 0.15f }, { "size", 0.75f }, { "pitch", 12.f }, { "density", 0.8f }, { "feedback", 0.55f }, { "reverb", 0.5f }, { "blend", 0.5f } } },
        { "Frozen Pad",  { { "position", 0.3f }, { "size", 0.85f }, { "density", 0.9f }, { "texture", 0.8f }, { "spread", 1.f }, { "blend", 1.f }, { "freeze", 1.f }, { "reverb", 0.45f } } },
        { "Stutter",     { { "position", 0.05f }, { "size", 0.25f }, { "density", 0.5f }, { "texture", 0.2f }, { "spread", 0.f }, { "blend", 0.8f }, { "grid", 1.f }, { "reverb", 0.1f } } },
        { "Underwater",  { { "position", 0.5f }, { "size", 0.6f }, { "pitch", -7.f }, { "density", 0.3f }, { "spread", 0.8f }, { "feedback", 0.3f }, { "blend", 0.7f }, { "reverb", 0.6f } } },
    };
    return list;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout PoomseCloudsProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* n) { return juce::ParameterID { n, 1 }; };
    auto attr = [] (auto fn) { return juce::AudioParameterFloatAttributes().withStringFromValueFunction (fn); };
    auto pct = [] (float x, int) { return juce::String (juce::roundToInt (x * 100.f)) + " %"; };

    l.add (std::make_unique<F> (id ("position"), "Position", Range (0.f, 1.f, 0.001f), 0.4f, attr ([] (float x, int) { return juce::String (0.02f + x * 4.7f, 2) + " s ago"; })));
    l.add (std::make_unique<F> (id ("size"),     "Size",     Range (0.f, 1.f, 0.001f), 0.5f, attr ([] (float x, int) { return juce::String (juce::roundToInt (20.f * std::pow (75.f, x))) + " ms"; })));
    l.add (std::make_unique<F> (id ("pitch"),    "Pitch",    Range (-24.f, 24.f, 0.01f), 0.f, attr ([] (float x, int) { return juce::String (x, 1) + " st"; })));
    l.add (std::make_unique<F> (id ("density"),  "Density",  Range (0.f, 1.f, 0.001f), 0.6f, attr (pct)));
    l.add (std::make_unique<F> (id ("texture"),  "Texture",  Range (0.f, 1.f, 0.001f), 0.5f, attr (pct)));
    l.add (std::make_unique<F> (id ("spread"),   "Spread",   Range (0.f, 1.f, 0.001f), 0.5f, attr (pct)));
    l.add (std::make_unique<F> (id ("blend"),    "Blend",    Range (0.f, 1.f, 0.001f), 0.5f, attr (pct)));
    l.add (std::make_unique<F> (id ("feedback"), "Feedback", Range (0.f, 1.f, 0.001f), 0.f, attr (pct)));
    l.add (std::make_unique<F> (id ("reverb"),   "Reverb",   Range (0.f, 1.f, 0.001f), 0.2f, attr (pct)));
    l.add (std::make_unique<F> (id ("level"),    "Level",    Range (-48.f, 6.f, 0.1f), 0.f, attr ([] (float x, int) { return juce::String (x, 1) + " dB"; })));
    l.add (std::make_unique<juce::AudioParameterBool> (id ("freeze"), "Freeze", false));
    l.add (std::make_unique<juce::AudioParameterBool> (id ("grid"), "Grid", false));
    return l;
}

PoomseCloudsProcessor::PoomseCloudsProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int i = 0; i < numRaw; ++i) raw[i] = apvts.getRawParameterValue (kIds[i]);
}

int PoomseCloudsProcessor::getNumPresets() { return (int) presets().size(); }
juce::String PoomseCloudsProcessor::getPresetName (int i) { return presets()[(size_t) i].name; }

cl::Params PoomseCloudsProcessor::readParams() const
{
    cl::Params p;
    p.position = raw[pPosition]->load(); p.size = raw[pSize]->load(); p.pitch = raw[pPitch]->load(); p.density = raw[pDensity]->load();
    p.texture = raw[pTexture]->load(); p.spread = raw[pSpread]->load(); p.blend = raw[pBlend]->load(); p.feedback = raw[pFeedback]->load();
    p.reverb = raw[pReverb]->load(); p.freeze = raw[pFreeze]->load() > 0.5f; p.grid = raw[pGrid]->load() > 0.5f;
    return p;
}

void PoomseCloudsProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    inL.assign ((size_t) std::max (samplesPerBlock, 8192), 0.f);
    inR = inL;
    internalBeat = 0.0;
    levelS.reset (sampleRate, 0.05);
}

void PoomseCloudsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2) return;
    if ((int) inL.size() < n) { inL.assign ((size_t) n, 0.f); inR = inL; }
    std::copy (buffer.getReadPointer (0), buffer.getReadPointer (0) + n, inL.begin());
    std::copy (buffer.getReadPointer (1), buffer.getReadPointer (1) + n, inR.begin());

    double bpm = 120.0, hostBeat = -1.0;
    bool playing = false;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto ppq = pos->getPpqPosition()) hostBeat = *ppq;
            playing = pos->getIsPlaying();
        }
    const double spq = currentSampleRate * 60.0 / std::max (20.0, bpm);
    const double beatStart = hostBeat >= 0.0 ? hostBeat : internalBeat;
    internalBeat = beatStart + (double) n / spq;

    engine.process (n, readParams(), beatStart, bpm, playing, inL.data(), inR.data(), buffer.getWritePointer (0), buffer.getWritePointer (1));

    levelS.setTargetValue (juce::Decibels::decibelsToGain (raw[pLevel]->load()));
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    for (int i = 0; i < n; ++i) { const float g = levelS.getNextValue(); L[i] *= g; R[i] *= g; }
}

void PoomseCloudsProcessor::randomise()
{
    juce::Random rng;
    auto set = [this] (const char* id, float v)
    {
        if (auto* p = apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
    };
    set ("position", rng.nextFloat()); set ("size", 0.2f + 0.7f * rng.nextFloat()); set ("pitch", (float) (rng.nextInt (5) - 2) * 5.f * (rng.nextFloat() < 0.4f ? 1.f : 0.f));
    set ("density", 0.3f + 0.7f * rng.nextFloat()); set ("texture", rng.nextFloat()); set ("spread", rng.nextFloat());
    set ("feedback", 0.5f * rng.nextFloat()); set ("reverb", 0.1f + 0.5f * rng.nextFloat());
}

void PoomseCloudsProcessor::resetToDefaults()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->getDefaultValue());
            rp->endChangeGesture();
        }
    currentPreset.store (0);
}

void PoomseCloudsProcessor::applyPreset (int index)
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

juce::AudioProcessorEditor* PoomseCloudsProcessor::createEditor() { return new PoomseCloudsEditor (*this); }

void PoomseCloudsProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    apvts.state.setProperty ("preset", currentPreset.load(), nullptr);
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void PoomseCloudsProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset.store (juce::jlimit (0, getNumPresets() - 1, (int) apvts.state.getProperty ("preset", 0)));
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PoomseCloudsProcessor(); }
