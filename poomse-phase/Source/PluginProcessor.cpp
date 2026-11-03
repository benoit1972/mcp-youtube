#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <mutex>

namespace
{
using Range = juce::NormalisableRange<float>;
enum { pSet, pRate, pDensity, pVariation, pRing, pSpace, pTune, pQuant, pLevel, pSeed, pLatch, numRaw };
const char* const kIds[numRaw] = { "set", "rate", "density", "variation", "ring", "space", "tune", "quant", "level", "seed", "latch" };

struct PresetParam { const char* id; float value; };
struct Preset { const char* name; std::vector<PresetParam> params; };
const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list = {
        { "Init", {} },
        { "Music Box",   { { "set", 0 }, { "density", 0.4f }, { "ring", 0.7f }, { "space", 0.5f } } },
        { "Rain",        { { "set", 2 }, { "rate", 2 }, { "density", 0.35f }, { "ring", 0.3f }, { "space", 0.6f } } },
        { "Engine",      { { "set", 1 }, { "rate", 1 }, { "density", 0.85f }, { "variation", 0.1f }, { "ring", 0.15f }, { "space", 0.15f } } },
        { "Drift",       { { "set", 2 }, { "rate", 0 }, { "density", 0.5f }, { "variation", 0.5f }, { "ring", 0.9f }, { "space", 0.8f } } },
        { "Steady",      { { "set", 3 }, { "density", 0.6f }, { "variation", 0.15f }, { "ring", 0.4f } } },
    };
    return list;
}

std::shared_ptr<const ph::Kit> builtInKit()
{
    static std::shared_ptr<const ph::Kit> k;
    static std::once_flag once;
    std::call_once (once, [] { auto m = std::make_shared<ph::Kit> (ph::makeDefaultKit()); m->analyse(); k = m; });
    return k;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout PoomsePhaseProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* n) { return juce::ParameterID { n, 1 }; };
    auto attr = [] (auto fn) { return juce::AudioParameterFloatAttributes().withStringFromValueFunction (fn); };
    auto pct = [] (float x, int) { return juce::String (juce::roundToInt (x * 100.f)) + " %"; };

    l.add (std::make_unique<juce::AudioParameterChoice> (id ("set"), "Cycles", juce::StringArray { "Reich", "Tight", "Long", "Even" }, 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (id ("rate"), "Step", juce::StringArray { "1/8", "1/16", "1/32", "1/16T" }, 1));
    l.add (std::make_unique<F> (id ("density"),   "Density",   Range (0.f, 1.f, 0.001f), 0.55f, attr (pct)));
    l.add (std::make_unique<F> (id ("variation"), "Variation", Range (0.f, 1.f, 0.001f), 0.25f, attr (pct)));
    l.add (std::make_unique<F> (id ("ring"),      "Ring",      Range (0.f, 1.f, 0.001f), 0.5f, attr (pct)));
    l.add (std::make_unique<F> (id ("space"),     "Space",     Range (0.f, 1.f, 0.001f), 0.35f, attr (pct)));
    l.add (std::make_unique<F> (id ("tune"),      "Tune",      Range (-12.f, 12.f, 0.01f), 0.f, attr ([] (float x, int) { return juce::String (x, 1) + " st"; })));
    l.add (std::make_unique<juce::AudioParameterChoice> (id ("quant"), "Start on", juce::StringArray { "At once", "1/4", "1/8", "1/16" }, 2));
    l.add (std::make_unique<F> (id ("level"),     "Level",     Range (-48.f, 6.f, 0.1f), 0.f, attr ([] (float x, int) { return juce::String (x, 1) + " dB"; })));
    l.add (std::make_unique<juce::AudioParameterInt> (id ("seed"), "Version", 1, 999, 1));
    l.add (std::make_unique<juce::AudioParameterBool> (id ("latch"), "Latch", true));
    return l;
}

PoomsePhaseProcessor::PoomsePhaseProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int i = 0; i < numRaw; ++i) raw[i] = apvts.getRawParameterValue (kIds[i]);
    useBuiltInSound();
}

int PoomsePhaseProcessor::getNumPresets() { return (int) presets().size(); }
juce::String PoomsePhaseProcessor::getPresetName (int i) { return presets()[(size_t) i].name; }

ph::Params PoomsePhaseProcessor::readParams() const
{
    ph::Params p;
    p.set = (int) raw[pSet]->load(); p.rate = (int) raw[pRate]->load();
    p.density = raw[pDensity]->load(); p.variation = raw[pVariation]->load(); p.ring = raw[pRing]->load();
    p.tune = raw[pTune]->load(); p.quant = (int) raw[pQuant]->load(); p.seed = (int) raw[pSeed]->load(); p.latch = raw[pLatch]->load() > 0.5f;
    return p;
}

void PoomsePhaseProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    echo.prepare (sampleRate);
    reverb.prepare (sampleRate);
    audioKit = nullptr;
    internalBeat = 0.0;
    levelS.reset (sampleRate, 0.05);
}

void PoomsePhaseProcessor::publish (std::shared_ptr<const ph::Kit> k, int root)
{
    const juce::SpinLock::ScopedLockType lock (kitLock);
    if (kit != nullptr) retired.push_back (std::move (kit));
    kit = std::move (k);
    kitRoot = root;
    retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const auto& p) { return p.use_count() == 1; }), retired.end());
    soundVersion.fetch_add (1);
}

void PoomsePhaseProcessor::useBuiltInSound()
{
    apvts.state.setProperty ("sound", juce::String(), nullptr);
    publish (builtInKit(), ph::kBuiltInRoot);
}

juce::String PoomsePhaseProcessor::getSoundName() const
{
    const auto path = apvts.state.getProperty ("sound").toString();
    return path.isNotEmpty() ? juce::File (path).getFileName() : juce::String ("eight glass notes (built in)");
}

juce::Result PoomsePhaseProcessor::readSound (const juce::File& file, Loaded& out, int attempts)
{
    juce::String why = "CANNOT READ " + file.getFileName().toUpperCase();
    for (int attempt = 0; attempt < std::max (1, attempts); ++attempt)
    {
        if (attempt > 0) juce::Thread::sleep (700);
        std::vector<float> mono; double rate = 0.0; int channels = 0, bits = 0; juce::String format;
        {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
            if (reader != nullptr && reader->sampleRate > 0.0 && reader->lengthInSamples > 0)
            {
                const int len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60.0));
                juce::AudioBuffer<float> tmp (2, len);
                tmp.clear();
                if (! reader->read (&tmp, 0, len, 0, true, true)) { why = "COULD NOT READ THE AUDIO IN " + file.getFileName().toUpperCase(); continue; }
                mono.resize ((size_t) len);
                const float* l = tmp.getReadPointer (0);
                const float* r = tmp.getReadPointer (reader->numChannels > 1 ? 1 : 0);
                for (int i = 0; i < len; ++i) mono[(size_t) i] = 0.5f * (l[i] + r[i]);
                rate = reader->sampleRate; channels = (int) reader->numChannels; bits = (int) reader->bitsPerSample; format = reader->getFormatName();
            }
            else
            {
                juce::MemoryBlock bytes;
                if (! file.existsAsFile()) { why = file.getFileName().toUpperCase() + " IS NOT THERE (YET?)"; continue; }
                if (! file.loadFileAsData (bytes)) { why = "CANNOT OPEN " + file.getFileName().toUpperCase() + " (NO PERMISSION? OR STILL ARRIVING FROM ICLOUD?)"; continue; }
                const auto aiff = ph::parseAiff (static_cast<const uint8_t*> (bytes.getData()), bytes.getSize());
                if (! aiff.ok)
                {
                    why = "CANNOT READ " + file.getFileName().toUpperCase() + ": " + juce::String (aiff.why) + "  [" + juce::String ((juce::int64) bytes.getSize()) + " BYTES]";
                    if (bytes.getSize() == 0) continue;
                    return juce::Result::fail (why);
                }
                mono = aiff.mono; rate = aiff.sampleRate; channels = aiff.channels; bits = aiff.bits; format = juce::String (aiff.format);
            }
        }
        if (mono.size() < 2048) return juce::Result::fail (file.getFileName().toUpperCase() + " IS TOO SHORT");

        auto k = std::make_shared<ph::Kit>();
        k->sampleRate = rate;
        k->x = std::move (mono);
        int end = (int) k->x.size();
        while (end > 1000 && std::abs (k->x[(size_t) end - 1]) < 0.002f) --end;
        k->x.resize ((size_t) end);
        k->analyse();
        if (k->slices.empty()) return juce::Result::fail (file.getFileName().toUpperCase() + " IS SILENT");

        const int root = ph::detectRootNote (k->x, rate, 60);
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        out.kit = k; out.root = root;
        out.info = file.getFileName().toUpperCase() + " - " + format.toUpperCase() + " - " + juce::String ((double) k->x.size() / rate, 1) + " S - "
                   + juce::String (channels) + " CH - " + juce::String (juce::roundToInt (rate)) + " HZ - " + juce::String (bits) + " BIT - "
                   + juce::String ((int) k->slices.size()) + " PIECES - PITCH " + names[((root % 12) + 12) % 12] + juce::String (root / 12 - 1);
        return juce::Result::ok();
    }
    return juce::Result::fail (why);
}

juce::Result PoomsePhaseProcessor::installSound (const Loaded& loaded, const juce::File& file)
{
    if (loaded.kit == nullptr) return juce::Result::fail ("NOTHING TO LOAD");
    apvts.state.setProperty ("sound", file.getFullPathName(), nullptr);
    publish (loaded.kit, loaded.root);
    return juce::Result::ok();
}

juce::Result PoomsePhaseProcessor::loadSound (const juce::File& file)
{
    Loaded l;
    const auto r = readSound (file, l, 1);
    return r.wasOk() ? installSound (l, file) : r;
}

void PoomsePhaseProcessor::uiNote (int note, bool on)
{
    int s1, n1, s2, n2;
    uiFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) { uiNotes[(size_t) s1] = { note, on }; uiFifo.finishedWrite (1); }
}

void PoomsePhaseProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    if (buffer.getNumChannels() < 2) return;

    {
        const juce::SpinLock::ScopedTryLockType lock (kitLock);
        if (lock.isLocked() && kit != audioKit)
        {
            audioKit = kit; audioRoot = kitRoot;
            engine.setKit (audioKit.get(), audioRoot);
        }
    }
    if (audioKit == nullptr) return;

    double bpm = 120.0, hostBeat = -1.0;
    bool playing = true;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto ppq = pos->getPpqPosition()) hostBeat = *ppq;
            playing = pos->getIsPlaying();
        }
    const int n = buffer.getNumSamples();
    const double spq = currentSampleRate * 60.0 / std::max (20.0, bpm);
    const double beatStart = hostBeat >= 0.0 ? hostBeat : internalBeat;
    internalBeat = beatStart + (double) n / spq;

    const ph::Params p = readParams();
    levelS.setTargetValue (juce::Decibels::decibelsToGain (raw[pLevel]->load()));

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    int pos = 0;
    auto render = [&] (int upTo)
    {
        if (upTo > pos) { engine.process (upTo - pos, p, beatStart + (double) pos / spq, bpm, playing, L + pos, R + pos); pos = upTo; }
    };
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

    // space: a ping-pong echo on the dotted eighth, and a reverb behind it
    const float space = raw[pSpace]->load();
    const float echoTime = (float) (spq * 0.75);
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];
        float el = l, er = r;
        echo.process (el, er, echoTime, 0.45f, 0.5f * space);
        float rl = 0.f, rr = 0.f;
        reverb.process (el, er, rl, rr);
        const float g = levelS.getNextValue();
        L[i] = (el + rl * space * 0.9f) * g;
        R[i] = (er + rr * space * 0.9f) * g;
    }
}

void PoomsePhaseProcessor::randomise()
{
    juce::Random rng;
    auto set = [this] (const char* id, float v)
    {
        if (auto* p = apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
    };
    set ("set", (float) rng.nextInt (4)); set ("density", 0.3f + 0.6f * rng.nextFloat()); set ("variation", 0.6f * rng.nextFloat());
    set ("ring", 0.2f + 0.7f * rng.nextFloat()); set ("space", 0.7f * rng.nextFloat());
    reroll();
}

void PoomsePhaseProcessor::reroll()
{
    if (auto* p = apvts.getParameter ("seed"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) (1 + juce::Random::getSystemRandom().nextInt (999))));
        p->endChangeGesture();
    }
}

void PoomsePhaseProcessor::resetToDefaults()
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

void PoomsePhaseProcessor::applyPreset (int index)
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

juce::AudioProcessorEditor* PoomsePhaseProcessor::createEditor() { return new PoomsePhaseEditor (*this); }

void PoomsePhaseProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    apvts.state.setProperty ("preset", currentPreset.load(), nullptr);
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void PoomsePhaseProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset.store (juce::jlimit (0, getNumPresets() - 1, (int) apvts.state.getProperty ("preset", 0)));
            const auto path = apvts.state.getProperty ("sound").toString();
            if (path.isNotEmpty() && juce::File (path).existsAsFile() && loadSound (juce::File (path)).wasOk()) return;
            useBuiltInSound();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PoomsePhaseProcessor(); }
