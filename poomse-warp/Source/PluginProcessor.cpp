#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using Range = juce::NormalisableRange<float>;
enum { pWarp, pTwist, pRatio, pPluck, pCutoff, pReso, pDetune, pAttack, pDecay, pSustain, pRelease, pSpace, pLevel, numRaw };
const char* const kIds[numRaw] = { "warp", "twist", "ratio", "pluck", "cutoff", "reso", "detune", "attack", "decay", "sustain", "release", "space", "level" };

struct PresetParam { const char* id; float value; };
struct Preset { const char* name; std::vector<PresetParam> params; };
const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list = {
        { "Init", {} },
        { "Glass Pluck",  { { "warp", 0.55f }, { "twist", 0.35f }, { "ratio", 3 }, { "pluck", 0.8f }, { "decay", 0.3f }, { "sustain", 0.f }, { "release", 0.4f }, { "space", 0.35f } } },
        { "Soft Pad",     { { "warp", 0.2f }, { "attack", 0.6f }, { "sustain", 0.8f }, { "release", 0.7f }, { "detune", 0.6f }, { "cutoff", 0.6f }, { "space", 0.5f } } },
        { "Metal Bell",   { { "warp", 0.1f }, { "twist", 0.65f }, { "ratio", 5 }, { "pluck", 0.5f }, { "decay", 0.6f }, { "sustain", 0.f }, { "release", 0.6f }, { "space", 0.45f } } },
        { "Acid Bite",    { { "warp", 0.8f }, { "pluck", 0.9f }, { "cutoff", 0.35f }, { "reso", 0.7f }, { "decay", 0.25f }, { "sustain", 0.2f }, { "release", 0.15f }, { "detune", 0.f }, { "space", 0.1f } } },
        { "Deep Sub",     { { "warp", 0.05f }, { "ratio", 0 }, { "cutoff", 0.45f }, { "pluck", 0.2f }, { "detune", 0.1f }, { "space", 0.05f } } },
    };
    return list;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout PoomseWarpProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* n) { return juce::ParameterID { n, 1 }; };
    auto attr = [] (auto fn) { return juce::AudioParameterFloatAttributes().withStringFromValueFunction (fn); };
    auto pct = [] (float x, int) { return juce::String (juce::roundToInt (x * 100.f)) + " %"; };
    auto f01 = [&] (const char* n, const char* label, float def) { l.add (std::make_unique<F> (id (n), label, Range (0.f, 1.f, 0.001f), def, attr (pct))); };

    f01 ("warp", "Warp", 0.35f); f01 ("twist", "Twist", 0.f);
    l.add (std::make_unique<juce::AudioParameterChoice> (id ("ratio"), "Ratio", juce::StringArray { "0.5", "1", "2", "3", "4", "5", "7" }, 1));
    f01 ("pluck", "Pluck", 0.4f);
    l.add (std::make_unique<F> (id ("cutoff"), "Cutoff", Range (0.f, 1.f, 0.001f), 0.8f, attr ([] (float x, int) { return juce::String (juce::roundToInt (40.f * std::pow (450.f, x))) + " Hz"; })));
    f01 ("reso", "Reso", 0.15f); f01 ("detune", "Detune", 0.2f);
    l.add (std::make_unique<F> (id ("attack"), "Attack", Range (0.f, 1.f, 0.001f), 0.05f, attr ([] (float x, int) { return juce::String (juce::roundToInt (1000.f * 0.001f * std::pow (2000.f, x))) + " ms"; })));
    l.add (std::make_unique<F> (id ("decay"), "Decay", Range (0.f, 1.f, 0.001f), 0.45f, attr ([] (float x, int) { return juce::String (0.05f * std::pow (100.f, x), 2) + " s"; })));
    f01 ("sustain", "Sustain", 0.6f);
    l.add (std::make_unique<F> (id ("release"), "Release", Range (0.f, 1.f, 0.001f), 0.35f, attr ([] (float x, int) { return juce::String (0.02f * std::pow (200.f, x), 2) + " s"; })));
    f01 ("space", "Space", 0.25f);
    l.add (std::make_unique<F> (id ("level"), "Level", Range (-48.f, 6.f, 0.1f), 0.f, attr ([] (float x, int) { return juce::String (x, 1) + " dB"; })));
    return l;
}

PoomseWarpProcessor::PoomseWarpProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int i = 0; i < numRaw; ++i) raw[i] = apvts.getRawParameterValue (kIds[i]);
}

int PoomseWarpProcessor::getNumPresets() { return (int) presets().size(); }
juce::String PoomseWarpProcessor::getPresetName (int i) { return presets()[(size_t) i].name; }

wp::Params PoomseWarpProcessor::readParams() const
{
    wp::Params p;
    p.warp = raw[pWarp]->load(); p.twist = raw[pTwist]->load(); p.ratio = (int) raw[pRatio]->load(); p.pluck = raw[pPluck]->load();
    p.cutoff = raw[pCutoff]->load(); p.reso = raw[pReso]->load(); p.detune = raw[pDetune]->load(); p.attack = raw[pAttack]->load();
    p.decay = raw[pDecay]->load(); p.sustain = raw[pSustain]->load(); p.release = raw[pRelease]->load();
    return p;
}

void PoomseWarpProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate); echo.prepare (sampleRate); reverb.prepare (sampleRate);
    levelS.reset (sampleRate, 0.05);
}

void PoomseWarpProcessor::uiNote (int note, bool on)
{
    int s1, n1, s2, n2;
    uiFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) { uiNotes[(size_t) s1] = { note, on }; uiFifo.finishedWrite (1); }
}

void PoomseWarpProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    if (buffer.getNumChannels() < 2) return;
    if (clearRequest.exchange (false)) engine.killAll();

    double bpm = 120.0;
    if (auto* head = getPlayHead()) if (auto pos = head->getPosition()) if (auto b = pos->getBpm()) bpm = *b;
    const int n = buffer.getNumSamples();
    const wp::Params p = readParams();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    int pos = 0;
    auto render = [&] (int upTo) { if (upTo > pos) { engine.process (upTo - pos, p, L + pos, R + pos); pos = upTo; } };
    {
        int s1, n1, s2, n2;
        uiFifo.prepareToRead (uiFifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) { const auto u = uiNotes[(size_t) (s1 + i)]; if (u.on) engine.noteOn (u.note, 0.8f); else engine.noteOff (u.note); }
        for (int i = 0; i < n2; ++i) { const auto u = uiNotes[(size_t) (s2 + i)]; if (u.on) engine.noteOn (u.note, 0.8f); else engine.noteOff (u.note); }
        uiFifo.finishedRead (n1 + n2);
    }
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        render (std::clamp (meta.samplePosition, 0, n));
        if (m.isNoteOn())        engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff())  engine.noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
        else if (m.isPitchWheel()) engine.pitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.f * 2.f);
        else if (m.isControllerOfType (1)) engine.modWheel ((float) m.getControllerValue() / 127.f);
    }
    render (n);

    const float space = raw[pSpace]->load();
    const float echoTime = (float) (currentSampleRate * 60.0 / std::max (20.0, bpm) * 0.75);
    levelS.setTargetValue (juce::Decibels::decibelsToGain (raw[pLevel]->load()));
    for (int i = 0; i < n; ++i)
    {
        float el = L[i], er = R[i];
        echo.process (el, er, echoTime, 0.4f, 0.45f * space);
        float rl = 0.f, rr = 0.f;
        reverb.process (el, er, rl, rr);
        const float g = levelS.getNextValue();
        L[i] = (el + rl * space * 0.9f) * g;
        R[i] = (er + rr * space * 0.9f) * g;
    }
}

void PoomseWarpProcessor::randomise()
{
    juce::Random rng;
    auto set = [this] (const char* id, float v) { if (auto* q = apvts.getParameter (id)) { q->beginChangeGesture(); q->setValueNotifyingHost (q->convertTo0to1 (v)); q->endChangeGesture(); } };
    set ("warp", rng.nextFloat()); set ("twist", rng.nextFloat() < 0.5f ? 0.f : rng.nextFloat() * 0.8f); set ("ratio", (float) rng.nextInt (7));
    set ("pluck", rng.nextFloat()); set ("cutoff", 0.4f + 0.6f * rng.nextFloat()); set ("reso", 0.6f * rng.nextFloat());
    set ("decay", rng.nextFloat()); set ("sustain", rng.nextFloat()); set ("release", 0.7f * rng.nextFloat());
    set ("detune", 0.7f * rng.nextFloat()); set ("space", 0.7f * rng.nextFloat());
}

void PoomseWarpProcessor::resetToDefaults()
{
    for (auto* q : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (q))
        { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->getDefaultValue()); rp->endChangeGesture(); }
    clearRequest.store (true);
    currentPreset.store (0);
}

void PoomseWarpProcessor::applyPreset (int index)
{
    if (index < 0 || index >= getNumPresets()) return;
    for (auto* q : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (q)) { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->getDefaultValue()); rp->endChangeGesture(); }
    for (const auto& pp : presets()[(size_t) index].params)
        if (auto* rp = apvts.getParameter (pp.id)) { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 (pp.value)); rp->endChangeGesture(); }
    currentPreset.store (index);
}

juce::AudioProcessorEditor* PoomseWarpProcessor::createEditor() { return new PoomseWarpEditor (*this); }

void PoomseWarpProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    apvts.state.setProperty ("preset", currentPreset.load(), nullptr);
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void PoomseWarpProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset.store (juce::jlimit (0, getNumPresets() - 1, (int) apvts.state.getProperty ("preset", 0)));
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PoomseWarpProcessor(); }
