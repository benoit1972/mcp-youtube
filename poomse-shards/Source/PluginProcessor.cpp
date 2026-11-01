#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <mutex>

namespace
{
using Range = juce::NormalisableRange<float>;
// index into raw[]: the order of the parameters below
enum { pMutate, pShuffle, pReverse, pRamp, pPitch, pGhosts, pHoles, pGroove, pGate, pTune, pQuant, pBars, pLevel, pSeed, numRaw };
const char* const kIds[numRaw] = { "mutate", "shuffle", "reverse", "ramp", "pitch", "ghosts", "holes", "groove", "gate", "tune", "quant", "bars", "level", "seed" };

struct PresetParam { const char* id; float value; };
struct Preset { const char* name; std::vector<PresetParam> params; };
const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list = {
        { "Init", {} },
        { "Barely There",     { { "mutate", 0.2f }, { "shuffle", 0.3f }, { "reverse", 0.2f }, { "pitch", 0.2f }, { "ghosts", 0.6f }, { "holes", 0.2f }, { "ramp", 0.3f } } },
        { "Loose Hat",        { { "mutate", 0.4f }, { "ghosts", 0.9f }, { "shuffle", 0.4f }, { "reverse", 0.2f }, { "pitch", 0.2f }, { "holes", 0.3f }, { "gate", 0.7f } } },
        { "Rolls and Bends",  { { "mutate", 0.6f }, { "ramp", 0.9f }, { "pitch", 0.8f }, { "ghosts", 0.4f }, { "shuffle", 0.4f }, { "gate", 0.8f } } },
        { "Backwards Room",   { { "mutate", 0.7f }, { "reverse", 1.f }, { "shuffle", 0.7f }, { "holes", 0.6f }, { "ramp", -0.4f }, { "pitch", 0.3f } } },
        { "Another Loop",     { { "mutate", 1.f }, { "shuffle", 0.9f }, { "reverse", 0.7f }, { "pitch", 0.7f }, { "ghosts", 0.8f }, { "holes", 0.6f }, { "ramp", 0.8f } } },
        { "Snapped",          { { "mutate", 0.5f }, { "groove", 0.f }, { "gate", 0.5f }, { "ghosts", 0.7f }, { "shuffle", 0.6f }, { "pitch", 0.1f } } },
        { "Slow Motion",      { { "mutate", 0.5f }, { "tune", -6.f }, { "gate", 1.f }, { "ramp", -0.7f }, { "holes", 0.7f }, { "reverse", 0.5f } } },
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

    l.add (std::make_unique<F> (id ("mutate"),  "Mutate",  Range (0.f, 1.f, 0.001f), 0.45f, attr (pct)));
    l.add (std::make_unique<F> (id ("shuffle"), "Shuffle", Range (0.f, 1.f, 0.001f), 0.5f, attr (pct)));
    l.add (std::make_unique<F> (id ("reverse"), "Reverse", Range (0.f, 1.f, 0.001f), 0.5f, attr (pct)));
    l.add (std::make_unique<F> (id ("ramp"),    "Ramp",    Range (-1.f, 1.f, 0.001f), 0.5f, attr (pct)));
    l.add (std::make_unique<F> (id ("pitch"),   "Pitch",   Range (0.f, 1.f, 0.001f), 0.5f, attr (pct)));
    l.add (std::make_unique<F> (id ("ghosts"),  "Ghosts",  Range (0.f, 1.f, 0.001f), 0.5f, attr (pct)));
    l.add (std::make_unique<F> (id ("holes"),   "Holes",   Range (0.f, 1.f, 0.001f), 0.5f, attr (pct)));
    l.add (std::make_unique<F> (id ("groove"),  "Groove",  Range (0.f, 1.f, 0.001f), 1.f, attr (pct)));
    l.add (std::make_unique<F> (id ("gate"),    "Gate",    Range (0.1f, 1.f, 0.001f), 0.9f, attr (pct)));
    l.add (std::make_unique<F> (id ("tune"),    "Tune",    Range (-12.f, 12.f, 0.01f), 0.f, attr ([] (float x, int) { return juce::String (x, 1) + " st"; })));
    l.add (std::make_unique<juce::AudioParameterChoice> (id ("quant"), "Start on", juce::StringArray { "At once", "1/4", "1/8", "1/16" }, 3));
    l.add (std::make_unique<juce::AudioParameterChoice> (id ("bars"), "Loop length", juce::StringArray { "1 bar", "2 bars", "4 bars", "8 bars" }, 1));
    l.add (std::make_unique<F> (id ("level"),   "Level",   Range (-48.f, 6.f, 0.1f), 0.f, attr ([] (float x, int) { return juce::String (x, 1) + " dB"; })));
    l.add (std::make_unique<juce::AudioParameterInt> (id ("seed"), "Edits (seed)", 1, 999, 1));
    return l;
}

PoomseShardsProcessor::PoomseShardsProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int i = 0; i < numRaw; ++i) raw[i] = apvts.getRawParameterValue (kIds[i]);
    useBuiltInBreak();
}

int PoomseShardsProcessor::getNumPresets() { return (int) presets().size(); }
juce::String PoomseShardsProcessor::getPresetName (int i) { return presets()[(size_t) i].name; }

sh::Params PoomseShardsProcessor::readParams() const
{
    sh::Params p;
    p.mutate = raw[pMutate]->load();   p.shuffle = raw[pShuffle]->load(); p.reverse = raw[pReverse]->load(); p.ramp = raw[pRamp]->load();
    p.pitch = raw[pPitch]->load();     p.ghosts = raw[pGhosts]->load();   p.holes = raw[pHoles]->load();     p.groove = raw[pGroove]->load();
    p.gate = raw[pGate]->load();       p.tune = raw[pTune]->load();       p.quant = (int) raw[pQuant]->load(); p.seed = (int) raw[pSeed]->load();
    return p;
}

void PoomseShardsProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    audioKit = nullptr;
    lastBars = -1;
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
        const int origin = k->slices.empty() ? 0 : k->slices.front().start;
        const float span = (float) std::max (1, n - origin);
        v.origin = (float) origin / (float) std::max (1, n);
        for (const auto& s : k->slices) v.marks.push_back ({ (float) (s.start - origin) / span, s.y, (float) s.length / span, s.level });
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
    return path.isNotEmpty() ? juce::File (path).getFileName() : juce::String ("boom, tchak, a hat in the air (built in)");
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
    // cut the trailing silence so that the length of the file is the length of the loop
    int end = len;
    while (end > 1000 && std::abs (k->x[(size_t) end - 1]) < 0.002f) --end;
    k->x.resize ((size_t) end);
    k->analyse();                       // cut the break into its hits
    if (k->slices.empty()) return false;

    // guess how many bars it is: the tempo that comes out should be one a person would play
    const double seconds = (double) k->x.size() / k->sampleRate;
    int best = 1; double bestScore = 1e9;
    for (int i = 0; i < 4; ++i)
    {
        const double beats = 4.0 * (double) (1 << i), tempo = 60.0 * beats / seconds;
        const double score = std::abs (std::log (tempo / 100.0));
        if (score < bestScore) { bestScore = score; best = i; }
    }
    if (auto* p = apvts.getParameter ("bars")) p->setValueNotifyingHost (p->convertTo0to1 ((float) best));
    apvts.state.setProperty ("break", file.getFullPathName(), nullptr);
    publish (std::move (k));
    return true;
}

void PoomseShardsProcessor::uiNote (int note, bool on)
{
    int s1, n1, s2, n2;
    uiFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) { uiNotes[(size_t) s1] = { note, on }; uiFifo.finishedWrite (1); }
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
            lastBars = -1;
        }
    }
    if (audioKit == nullptr) return;
    const int bars = 1 << juce::jlimit (0, 3, (int) raw[pBars]->load());
    if (bars != lastBars) { engine.setLoopBeats (4.0 * bars); lastBars = bars; }

    double bpm = 120.0, hostBeat = -1.0;
    bool playing = true;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto ppq = pos->getPpqPosition()) hostBeat = *ppq;
            playing = pos->getIsPlaying();
        }
    const int n = buffer.getNumSamples();
    const double spq = currentSampleRate * 60.0 / std::max (20.0, bpm);
    const double beatStart = hostBeat >= 0.0 ? hostBeat : internalBeat;
    internalBeat = beatStart + (double) n / spq;

    const sh::Params p = readParams();
    levelS.setTargetValue (juce::Decibels::decibelsToGain (raw[pLevel]->load()));

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    int pos = 0;
    auto render = [&] (int upTo)
    {
        if (upTo > pos) { engine.process (upTo - pos, p, beatStart + (double) pos / spq, bpm, playing, L + pos, R + pos); pos = upTo; }
    };
    // the pads of the interface first (at the start of the block), then the keys of the host, each where it was played
    {
        int s1, n1, s2, n2;
        uiFifo.prepareToRead (uiFifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) { const auto u = uiNotes[(size_t) (s1 + i)]; if (u.on) engine.noteOn (u.note, 0.85f, p); else engine.noteOff (u.note); }
        for (int i = 0; i < n2; ++i) { const auto u = uiNotes[(size_t) (s2 + i)]; if (u.on) engine.noteOn (u.note, 0.85f, p); else engine.noteOff (u.note); }
        uiFifo.finishedRead (n1 + n2);
    }
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        render (std::clamp (meta.samplePosition, 0, n));
        if (m.isNoteOn())        engine.noteOn (m.getNoteNumber(), m.getFloatVelocity(), p);
        else if (m.isNoteOff())  engine.noteOff (m.getNoteNumber());
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
    set ("mutate", between (0.2f, 0.9f)); set ("shuffle", between (0.1f, 1.f)); set ("reverse", between (0.f, 1.f)); set ("ramp", between (-0.8f, 1.f));
    set ("pitch", between (0.f, 0.9f));   set ("ghosts", between (0.1f, 1.f));  set ("holes", between (0.f, 0.8f));
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
            if (rp->getParameterID() == "bars") continue;          // the length of the loop belongs to the break, not to the sound
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
            if (path.isNotEmpty() && juce::File (path).existsAsFile())
            {
                const float savedBars = raw[pBars]->load();               // the saved length wins over the guess
                if (loadBreak (juce::File (path)))
                {
                    if (auto* p = apvts.getParameter ("bars")) p->setValueNotifyingHost (p->convertTo0to1 (savedBars));
                    return;
                }
            }
            useBuiltInBreak();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PoomseShardsProcessor(); }
