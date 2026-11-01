#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <mutex>

namespace
{
using Range = juce::NormalisableRange<float>;
const char* const kIds[10] = { "place", "shatter", "grid", "ramp", "scatter", "fall", "snap", "tune", "level", "seed" };

struct PresetParam { const char* id; float value; };
struct Preset { const char* name; std::vector<PresetParam> params; };
const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list = {
        { "Init", {} },
        { "Tight",           { { "shatter", 0.f }, { "snap", 0.55f } } },
        { "Glass Snare",     { { "shatter", 0.6f }, { "grid", 4 }, { "ramp", 0.7f }, { "scatter", 0.15f }, { "snap", 0.4f } } },
        { "Falling Rolls",   { { "shatter", 0.75f }, { "grid", 5 }, { "ramp", -0.3f }, { "fall", -0.8f }, { "scatter", 0.2f }, { "snap", 0.5f } } },
        { "Broken Clock",    { { "shatter", 0.8f }, { "grid", 1 }, { "ramp", 0.f }, { "scatter", 0.6f }, { "snap", 0.5f } } },
        { "Scatter Storm",   { { "shatter", 1.f }, { "grid", 6 }, { "ramp", 0.8f }, { "scatter", 1.f }, { "fall", 0.3f }, { "snap", 0.35f } } },
        { "Slow Motion",     { { "tune", -7.f }, { "snap", 1.f }, { "shatter", 0.25f }, { "grid", 0 }, { "ramp", -0.5f }, { "scatter", 0.3f } } },
    };
    return list;
}

std::shared_ptr<const sh::Kit> builtInKit()
{
    static std::shared_ptr<const sh::Kit> k;
    static std::once_flag once;
    std::call_once (once, [] { auto m = std::make_shared<sh::Kit> (sh::makeDefaultKit()); m->analyse(); k = m; });
    return k;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout PoomseShardsProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* n) { return juce::ParameterID { n, 1 }; };
    auto attr = [] (auto fn) { return juce::AudioParameterFloatAttributes().withStringFromValueFunction (fn); };
    auto pct = [] (float x, int) { return juce::String (juce::roundToInt (x * 100.f)) + " %"; };
    auto signedPct = [] (float x, int) { return juce::String (juce::roundToInt (x * 100.f)) + " %"; };

    l.add (std::make_unique<F> (id ("place"),   "Place",   Range (0.f, 1.f, 0.0005f), 0.f, attr (pct)));
    l.add (std::make_unique<F> (id ("shatter"), "Shatter", Range (0.f, 1.f, 0.001f), 0.35f, attr (pct)));
    juce::StringArray grids;
    for (int d : sh::kGridDiv) grids.add ("1/" + juce::String (d));
    l.add (std::make_unique<juce::AudioParameterChoice> (id ("grid"), "Grid", grids, 4));
    l.add (std::make_unique<F> (id ("ramp"),    "Ramp",    Range (-1.f, 1.f, 0.001f), 0.4f, attr (signedPct)));
    l.add (std::make_unique<F> (id ("scatter"), "Scatter", Range (0.f, 1.f, 0.001f), 0.3f, attr (pct)));
    l.add (std::make_unique<F> (id ("fall"),    "Fall",    Range (-1.f, 1.f, 0.001f), 0.f, attr (signedPct)));
    l.add (std::make_unique<F> (id ("snap"),    "Snap",    Range (0.f, 1.f, 0.001f), 0.8f,
                                attr ([] (float x, int) { return juce::String (juce::roundToInt ((float) sh::snapSeconds (x) * 1000.f)) + " ms"; })));
    l.add (std::make_unique<F> (id ("tune"),    "Tune",    Range (-12.f, 12.f, 0.01f), 0.f,
                                attr ([] (float x, int) { return juce::String (x, 1) + " st"; })));
    l.add (std::make_unique<F> (id ("level"),   "Level",   Range (-48.f, 6.f, 0.1f), 0.f,
                                attr ([] (float x, int) { return juce::String (x, 1) + " dB"; })));
    l.add (std::make_unique<juce::AudioParameterInt> (id ("seed"), "Edits (seed)", 1, 999, 1));
    return l;
}

PoomseShardsProcessor::PoomseShardsProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int i = 0; i < 10; ++i) raw[i] = apvts.getRawParameterValue (kIds[i]);
    useBuiltInBreak();
}

int PoomseShardsProcessor::getNumPresets() { return (int) presets().size(); }
juce::String PoomseShardsProcessor::getPresetName (int i) { return presets()[(size_t) i].name; }

void PoomseShardsProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    audioKit = nullptr;
    internalBeat = 0.0;
    levelS.reset (sampleRate, 0.05);
}

void PoomseShardsProcessor::publish (std::shared_ptr<const sh::Kit> k)
{
    View v;
    {
        constexpr int bins = 360;
        v.peaks.assign ((size_t) bins, 0.f);
        const int n = (int) k->x.size();
        float top = 1e-6f;
        for (int i = 0; i < n; ++i)
        {
            const auto b = (size_t) ((juce::int64) i * bins / n);
            v.peaks[b] = std::max (v.peaks[b], std::abs (k->x[(size_t) i]));
            top = std::max (top, v.peaks[b]);
        }
        for (auto& p : v.peaks) p /= top;
        for (const auto& s : k->slices) v.marks.push_back ({ s.x, s.y, (float) s.length / (float) std::max (1, n), s.level });
    }
    {
        const juce::SpinLock::ScopedLockType lock (viewLock);
        view = std::move (v);
        viewVersion.fetch_add (1);
    }
    const juce::SpinLock::ScopedLockType lock (kitLock);
    if (kit != nullptr) retired.push_back (std::move (kit));
    kit = std::move (k);
    retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const auto& p) { return p.use_count() == 1; }), retired.end());
}

PoomseShardsProcessor::View PoomseShardsProcessor::getView() const
{
    const juce::SpinLock::ScopedLockType lock (viewLock);
    return view;
}

void PoomseShardsProcessor::useBuiltInBreak()
{
    apvts.state.setProperty ("break", juce::String(), nullptr);
    publish (builtInKit());
}

juce::String PoomseShardsProcessor::getBreakName() const
{
    const auto path = apvts.state.getProperty ("break").toString();
    return path.isNotEmpty() ? juce::File (path).getFileName() : juce::String ("a broken beat (built in)");
}

bool PoomseShardsProcessor::loadBreak (const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples < 4096 || reader->sampleRate <= 0.0) return false;

    const int len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60.0));
    juce::AudioBuffer<float> tmp (2, len);
    tmp.clear();
    if (! reader->read (&tmp, 0, len, 0, true, true)) return false;

    auto k = std::make_shared<sh::Kit>();
    k->sampleRate = reader->sampleRate;
    k->x.resize ((size_t) len);
    const float* a = tmp.getReadPointer (0);
    const float* b = tmp.getReadPointer (reader->numChannels > 1 ? 1 : 0);
    for (int i = 0; i < len; ++i) k->x[(size_t) i] = 0.5f * (a[i] + b[i]);
    k->analyse();                       // cut the break into its hits
    if (k->slices.empty()) return false;
    apvts.state.setProperty ("break", file.getFullPathName(), nullptr);
    publish (std::move (k));
    return true;
}

void PoomseShardsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    if (buffer.getNumChannels() < 2) return;

    {
        const juce::SpinLock::ScopedTryLockType lock (kitLock);
        if (lock.isLocked() && kit != audioKit)
        {
            audioKit = kit;
            engine.setKit (audioKit.get());
        }
    }
    if (audioKit == nullptr) return;

    double bpm = 120.0, hostBeat = -1.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto ppq = pos->getPpqPosition()) hostBeat = *ppq;
        }
    const int n = buffer.getNumSamples();
    const double spb = currentSampleRate * 60.0 / std::max (20.0, bpm);
    const double beatStart = hostBeat >= 0.0 ? hostBeat : internalBeat;
    internalBeat = beatStart + (double) n / spb;

    sh::Params p;
    p.place = raw[0]->load();  p.shatter = raw[1]->load();  p.grid = (int) raw[2]->load();  p.ramp = raw[3]->load();
    p.scatter = raw[4]->load(); p.fall = raw[5]->load();    p.snap = raw[6]->load();        p.tune = raw[7]->load();
    p.seed = (int) raw[9]->load(); p.bpm = bpm;
    levelS.setTargetValue (juce::Decibels::decibelsToGain (raw[8]->load()));

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    int pos = 0;
    auto render = [&] (int upTo)
    {
        if (upTo > pos) { engine.process (upTo - pos, L + pos, R + pos); pos = upTo; }
    };
    for (const auto meta : midi)               // hits start exactly where they were written
    {
        const auto m = meta.getMessage();
        const int at = std::clamp (meta.samplePosition, 0, n);
        render (at);
        if (m.isNoteOn()) engine.noteOn (m.getNoteNumber(), m.getFloatVelocity(), p, beatStart + (double) at / spb);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
    }
    render (n);

    for (int i = 0; i < n; ++i) { const float g = levelS.getNextValue(); L[i] *= g; R[i] *= g; }
}

void PoomseShardsProcessor::randomise()
{
    juce::Random rng;
    auto set = [this] (const char* id, float v)
    {
        if (auto* p = apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
    };
    auto between = [&] (float lo, float hi) { return lo + (hi - lo) * rng.nextFloat(); };
    set ("place", between (0.f, 1.f));    set ("shatter", between (0.1f, 0.9f)); set ("grid", (float) rng.nextInt (7));
    set ("ramp", between (-0.8f, 0.9f));  set ("scatter", between (0.f, 0.8f));  set ("fall", rng.nextFloat() < 0.6f ? 0.f : between (-0.8f, 0.5f));
    set ("snap", between (0.3f, 1.f));
}

void PoomseShardsProcessor::newEdits()
{
    if (auto* p = apvts.getParameter ("seed"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) (1 + juce::Random::getSystemRandom().nextInt (999))));
        p->endChangeGesture();
    }
}

void PoomseShardsProcessor::resetToDefaults()
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

void PoomseShardsProcessor::applyPreset (int index)
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

juce::AudioProcessorEditor* PoomseShardsProcessor::createEditor() { return new PoomseShardsEditor (*this); }

void PoomseShardsProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    apvts.state.setProperty ("preset", currentPreset.load(), nullptr);
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void PoomseShardsProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset.store (juce::jlimit (0, getNumPresets() - 1, (int) apvts.state.getProperty ("preset", 0)));
            const auto path = apvts.state.getProperty ("break").toString();
            if (path.isNotEmpty() && juce::File (path).existsAsFile() && loadBreak (juce::File (path))) return;
            useBuiltInBreak();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PoomseShardsProcessor(); }
