#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <mutex>

namespace
{
using Range = juce::NormalisableRange<float>;
const char* const kIds[9] = { "position", "size", "seam", "moire", "scan", "pulse", "tail", "level", "places" };

struct PresetParam { const char* id; float value; };
struct Preset { const char* name; std::vector<PresetParam> params; };
const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list = {
        { "Init", {} },
        { "Pulse Field",   { { "size", 0.25f }, { "seam", 0.05f }, { "pulse", 0.9f }, { "moire", 0.3f }, { "tail", 0.3f } } },
        { "Slow Chords",   { { "size", 0.85f }, { "seam", 0.95f }, { "pulse", 0.f }, { "moire", 0.5f }, { "tail", 0.8f } } },
        { "Slipping",      { { "size", 0.4f }, { "seam", 0.6f }, { "pulse", 0.3f }, { "moire", 1.f }, { "tail", 0.6f } } },
        { "Drift Through", { { "size", 0.6f }, { "seam", 0.8f }, { "moire", 0.5f }, { "scan", 0.45f }, { "pulse", 0.1f }, { "tail", 0.7f } } },
        { "Keys Are Places", { { "places", 1.f }, { "size", 0.7f }, { "seam", 0.7f }, { "pulse", 0.1f }, { "moire", 0.4f }, { "tail", 0.6f } } },
        { "Tiny Tones",    { { "size", 0.f }, { "seam", 0.2f }, { "pulse", 0.f }, { "moire", 0.2f }, { "tail", 0.4f } } },
    };
    return list;
}

std::shared_ptr<const lp::Record> builtInRecord()
{
    static std::shared_ptr<const lp::Record> r;
    static std::once_flag once;
    std::call_once (once, [] { auto m = std::make_shared<lp::Record> (lp::makeDefaultRecord()); m->analyse(); r = m; });
    return r;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout PoomseLoopsProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* n) { return juce::ParameterID { n, 1 }; };
    auto attr = [] (auto fn) { return juce::AudioParameterFloatAttributes().withStringFromValueFunction (fn); };
    auto pct = [] (float x, int) { return juce::String (juce::roundToInt (x * 100.f)) + " %"; };

    l.add (std::make_unique<F> (id ("position"), "Position", Range (0.f, 1.f, 0.0005f), 0.08f, attr (pct)));
    l.add (std::make_unique<F> (id ("size"), "Size", Range (0.f, 1.f, 0.0005f), 0.45f,
                                attr ([] (float x, int) { return juce::String (juce::roundToInt ((float) lp::Atlas::yToSeconds (x) * 1000.f)) + " ms"; })));
    l.add (std::make_unique<F> (id ("seam"),  "Seam",  Range (0.f, 1.f, 0.001f), 0.4f,  attr (pct)));
    l.add (std::make_unique<F> (id ("moire"), "Moire", Range (0.f, 1.f, 0.001f), 0.35f, attr (pct)));
    l.add (std::make_unique<F> (id ("scan"),  "Scan",  Range (-1.f, 1.f, 0.001f), 0.f,
                                attr ([] (float x, int) { return juce::String (juce::roundToInt (x * 100.f)) + " %"; })));
    l.add (std::make_unique<F> (id ("pulse"), "Pulse", Range (0.f, 1.f, 0.001f), 0.2f,  attr (pct)));
    l.add (std::make_unique<F> (id ("tail"),  "Tail",  Range (0.f, 1.f, 0.001f), 0.5f,
                                attr ([] (float x, int) { const float s = 0.05f * std::pow (120.f, x); return juce::String (s, s < 1.f ? 2 : 1) + " s"; })));
    l.add (std::make_unique<F> (id ("level"), "Level", Range (-48.f, 6.f, 0.1f), 0.f,
                                attr ([] (float x, int) { return juce::String (x, 1) + " dB"; })));
    l.add (std::make_unique<juce::AudioParameterBool> (id ("places"), "Keys = places", false));
    return l;
}

PoomseLoopsProcessor::PoomseLoopsProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int i = 0; i < 9; ++i) raw[i] = apvts.getRawParameterValue (kIds[i]);
    for (auto& v : voiceLoop) v.store (-1);
    useBuiltInRecord();
}

int PoomseLoopsProcessor::getNumPresets() { return (int) presets().size(); }
juce::String PoomseLoopsProcessor::getPresetName (int i) { return presets()[(size_t) i].name; }

void PoomseLoopsProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    audioRecord = nullptr;       // picked up again at the next block
    levelS.reset (sampleRate, 0.05);
}

void PoomseLoopsProcessor::publish (std::shared_ptr<const lp::Record> r)
{
    View v;
    {
        constexpr int bins = 360;
        v.peaks.assign ((size_t) bins, 0.f);
        const int n = (int) r->x.size();
        float top = 1e-6f;
        for (int i = 0; i < n; ++i)
        {
            const auto b = (size_t) ((juce::int64) i * bins / n);
            v.peaks[b] = std::max (v.peaks[b], std::abs (r->x[(size_t) i]));
            top = std::max (top, v.peaks[b]);
        }
        for (auto& p : v.peaks) p /= top;
        for (const auto& f : r->atlas.loops())
            v.marks.push_back ({ (float) f.start / (float) std::max (1, n), lp::Atlas::sizeToY ((double) f.length / r->sampleRate), (float) f.length / (float) std::max (1, n), f.score, f.level });
    }
    {
        const juce::SpinLock::ScopedLockType lock (viewLock);
        view = std::move (v);
        viewVersion.fetch_add (1);
    }
    const juce::SpinLock::ScopedLockType lock (recordLock);
    if (record != nullptr) retired.push_back (std::move (record));
    record = std::move (r);
    retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const auto& p) { return p.use_count() == 1; }), retired.end());
}

PoomseLoopsProcessor::View PoomseLoopsProcessor::getView() const
{
    const juce::SpinLock::ScopedLockType lock (viewLock);
    return view;
}

void PoomseLoopsProcessor::useBuiltInRecord()
{
    apvts.state.setProperty ("record", juce::String(), nullptr);
    publish (builtInRecord());
}

juce::String PoomseLoopsProcessor::getRecordName() const
{
    const auto path = apvts.state.getProperty ("record").toString();
    return path.isNotEmpty() ? juce::File (path).getFileName() : juce::String ("a worn jazz record (built in)");
}

bool PoomseLoopsProcessor::loadRecord (const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples < 4096 || reader->sampleRate <= 0.0) return false;

    const int len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 90.0));
    juce::AudioBuffer<float> tmp (2, len);
    tmp.clear();
    if (! reader->read (&tmp, 0, len, 0, true, true)) return false;

    auto r = std::make_shared<lp::Record>();
    r->sampleRate = reader->sampleRate;
    r->x.resize ((size_t) len);
    const float* a = tmp.getReadPointer (0);
    const float* b = tmp.getReadPointer (reader->numChannels > 1 ? 1 : 0);
    for (int i = 0; i < len; ++i) r->x[(size_t) i] = 0.5f * (a[i] + b[i]);
    r->analyse();                       // the search for loops (a fraction of a second)
    apvts.state.setProperty ("record", file.getFullPathName(), nullptr);
    publish (std::move (r));
    return true;
}

void PoomseLoopsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    if (buffer.getNumChannels() < 2) return;

    {
        const juce::SpinLock::ScopedTryLockType lock (recordLock);
        if (lock.isLocked() && record != audioRecord)
        {
            audioRecord = record;
            engine.setRecord (audioRecord.get());
        }
    }
    if (audioRecord == nullptr) return;

    lp::Params p;
    p.position = raw[0]->load(); p.size = raw[1]->load(); p.seam = raw[2]->load(); p.moire = raw[3]->load();
    p.scan = raw[4]->load();     p.pulse = raw[5]->load(); p.tail = raw[6]->load(); p.places = raw[8]->load() > 0.5f;
    levelS.setTargetValue (juce::Decibels::decibelsToGain (raw[7]->load()));

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    const int n = buffer.getNumSamples();
    int pos = 0;
    auto render = [&] (int upTo)
    {
        if (upTo > pos) { engine.process (upTo - pos, p, L + pos, R + pos); pos = upTo; }
    };
    for (const auto meta : midi)               // notes start exactly where they were played
    {
        const auto m = meta.getMessage();
        render (std::clamp (meta.samplePosition, 0, n));
        if (m.isNoteOn())       engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
    }
    render (n);

    for (int i = 0; i < n; ++i) { const float g = levelS.getNextValue(); L[i] *= g; R[i] *= g; }
    for (int i = 0; i < lp::Engine::maxVoices; ++i) voiceLoop[(size_t) i].store (engine.loopOfVoice (i));
}

void PoomseLoopsProcessor::randomise()
{
    juce::Random rng;
    auto set = [this] (const char* id, float v)
    {
        if (auto* p = apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
    };
    auto between = [&] (float lo, float hi) { return lo + (hi - lo) * rng.nextFloat(); };
    set ("position", between (0.f, 1.f)); set ("size", between (0.05f, 0.95f)); set ("seam", between (0.f, 1.f));
    set ("moire", between (0.f, 1.f));    set ("pulse", between (0.f, 0.8f));   set ("scan", rng.nextFloat() < 0.5f ? 0.f : between (-0.4f, 0.4f));
}

void PoomseLoopsProcessor::resetToDefaults()
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

void PoomseLoopsProcessor::applyPreset (int index)
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

juce::AudioProcessorEditor* PoomseLoopsProcessor::createEditor() { return new PoomseLoopsEditor (*this); }

void PoomseLoopsProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    apvts.state.setProperty ("preset", currentPreset.load(), nullptr);
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void PoomseLoopsProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset.store (juce::jlimit (0, getNumPresets() - 1, (int) apvts.state.getProperty ("preset", 0)));
            const auto path = apvts.state.getProperty ("record").toString();
            if (path.isNotEmpty() && juce::File (path).existsAsFile()) loadRecord (juce::File (path));
            else useBuiltInRecord();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PoomseLoopsProcessor(); }
