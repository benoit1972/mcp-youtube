#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstring>

using Range = juce::NormalisableRange<float>;

namespace
{
const char* const kLayerIds[amb::kNumLayers] = { "keys", "vocals", "pad", "beat", "atmos", "sub" };
const float kDefLevel[amb::kNumLayers]  = { 0.80f, 0.70f, 0.60f, 0.60f, 0.50f, 0.70f };
const float kDefAmount[amb::kNumLayers] = { 0.85f, 0.70f, 0.60f, 0.50f, 0.90f, 0.70f };
const float kDefSpace[amb::kNumLayers]  = { 0.60f, 0.85f, 0.80f, 0.70f, 0.30f, 0.15f };
const int   kDefRoot[amb::kNumLayers]   = { 60, 60, 60, 60, 60, 36 };
juce::String layerParam (int l, const char* suffix) { return juce::String (kLayerIds[l]) + suffix; }
}

juce::AudioProcessorValueTreeState::ParameterLayout PoomseAmbientProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto pct = [] (float x, int) { return juce::String (juce::roundToInt (x * 100.f)) + " %"; };
    auto attr = [] (auto fn) { return juce::AudioParameterFloatAttributes().withStringFromValueFunction (fn); };
    auto id = [] (const juce::String& name) { return juce::ParameterID { name, 1 }; };

    // --- the piece ---
    juce::StringArray scales;
    for (const auto* n : amb::kScaleNames) scales.add (n);
    l.add (std::make_unique<juce::AudioParameterChoice> (id ("scale"), "Scale", scales, 2));
    l.add (std::make_unique<F> (id ("journey"),  "Journey",  Range (0.f, 1.f, 0.001f), 0.7f, attr (pct)));
    l.add (std::make_unique<F> (id ("space"),    "Space",    Range (0.f, 1.f, 0.001f), 0.5f, attr (pct)));
    l.add (std::make_unique<F> (id ("depth"),    "Depth",    Range (0.f, 1.f, 0.001f), 0.5f, attr (pct)));
    l.add (std::make_unique<F> (id ("surprise"), "Surprise", Range (0.f, 1.f, 0.001f), 0.3f, attr (pct)));
    l.add (std::make_unique<juce::AudioParameterInt> (id ("bars"), "Length (bars)", 8, 480, 120));
    l.add (std::make_unique<juce::AudioParameterInt> (id ("seed"), "Seed (0 = new each time)", 0, 9999, 0));
    l.add (std::make_unique<juce::AudioParameterBool> (id ("night"), "Night style", true));

    // --- sound ---
    l.add (std::make_unique<F> (id ("dark"),    "Dark",    Range (0.f, 1.f, 0.001f), 0.35f, attr (pct)));
    l.add (std::make_unique<F> (id ("crackle"), "Crackle", Range (0.f, 1.f, 0.001f), 0.25f, attr (pct)));
    l.add (std::make_unique<F> (id ("tape"),    "Tape",    Range (0.f, 1.f, 0.001f), 0.30f, attr (pct)));
    l.add (std::make_unique<F> (id ("reverb"),  "Reverb",  Range (0.f, 1.f, 0.001f), 0.50f, attr (pct)));
    l.add (std::make_unique<F> (id ("echo"),    "Echo",    Range (0.f, 1.f, 0.001f), 0.15f, attr (pct)));
    l.add (std::make_unique<F> (id ("cloud"),   "Cloud",   Range (0.f, 1.f, 0.001f), 0.30f, attr (pct)));
    l.add (std::make_unique<F> (id ("level"),   "Level",   Range (-48.f, 6.f, 0.1f), 0.f,
                                attr ([] (float x, int) { return juce::String (x, 1) + " dB"; })));

    // --- the six layers ---
    for (int k = 0; k < amb::kNumLayers; ++k)
    {
        const juce::String n = juce::String (amb::kLayerNames[k]);
        l.add (std::make_unique<F> (id (layerParam (k, "Level")),  n + " level",  Range (0.f, 1.f, 0.001f), kDefLevel[k],  attr (pct)));
        l.add (std::make_unique<F> (id (layerParam (k, "Amount")), n + " amount", Range (0.f, 1.f, 0.001f), kDefAmount[k], attr (pct)));
        l.add (std::make_unique<F> (id (layerParam (k, "Space")),  n + " space",  Range (0.f, 1.f, 0.001f), kDefSpace[k],  attr (pct)));
        l.add (std::make_unique<juce::AudioParameterInt> (id (layerParam (k, "Root")), n + " root note", 0, 127, kDefRoot[k]));
    }
    return l;
}

PoomseAmbientProcessor::PoomseAmbientProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int k = 0; k < amb::kNumLayers; ++k)
    {
        layerRaw[k][0] = apvts.getRawParameterValue (layerParam (k, "Level"));
        layerRaw[k][1] = apvts.getRawParameterValue (layerParam (k, "Amount"));
        layerRaw[k][2] = apvts.getRawParameterValue (layerParam (k, "Space"));
        layerRaw[k][3] = apvts.getRawParameterValue (layerParam (k, "Root"));
    }
    const char* g[8] = { "scale", "journey", "space", "depth", "surprise", "bars", "seed", "night" };
    for (int i = 0; i < 8; ++i) globalRaw[i] = apvts.getRawParameterValue (g[i]);
    const char* f[7] = { "dark", "crackle", "tape", "reverb", "echo", "cloud", "level" };
    for (int i = 0; i < 7; ++i) fxRaw[i] = apvts.getRawParameterValue (f[i]);
    for (auto& a : sceneStart) a.store (0.f);
    for (auto& a : sceneMood) a.store (0);
    for (auto& a : activity) a.store (0.f);
    apvts.addParameterListener ("night", this);
    installBuiltIns();
}

PoomseAmbientProcessor::~PoomseAmbientProcessor()
{
    apvts.removeParameterListener ("night", this);
    cancelPendingUpdate();
}

void PoomseAmbientProcessor::parameterChanged (const juce::String& paramId, float)
{
    if (paramId == "night") triggerAsyncUpdate();   // may come from the audio thread: the work is done on the message thread
}
void PoomseAmbientProcessor::handleAsyncUpdate() { installBuiltIns(); }

void PoomseAmbientProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const size_t n = (size_t) std::max (64, samplesPerBlock);
    for (auto* v : { &dryL, &dryR, &sendL, &sendR, &cloudInL, &cloudInR }) v->assign (n, 0.f);
    engine.prepare (sampleRate);
    cloud.prepare (sampleRate);
    reverb.prepare (sampleRate); reverb.setSize (0.88f);
    echo.prepare (sampleRate);
    tape.prepare (sampleRate);
    dark.prepare (sampleRate);
    crackle.prepare (sampleRate);
    for (auto* s : { &darkS, &crackleS, &tapeS, &reverbS, &echoS, &cloudS, &levelS }) s->reset (sampleRate, 0.08);
    internalBeat = 0.0;
    seedRng = gg::Rng ((uint32_t) juce::Time::currentTimeMillis() | 1u);   // a different piece every time the plugin is loaded
}

bool PoomseAmbientProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PoomseAmbientProcessor::publishSource (int slot, std::shared_ptr<const amb::SourceBuffer> s)
{
    {
        constexpr int bins = 320;
        std::vector<float> pk ((size_t) bins, 0.f);
        const int n = s->size();
        float top = 1e-6f;
        for (int i = 0; i < n; ++i)
        {
            const auto b = (size_t) ((juce::int64) i * bins / n);
            pk[b] = std::max ({ pk[b], std::abs (s->l[(size_t) i]), std::abs (s->r[(size_t) i]) });
            top = std::max (top, pk[b]);
        }
        for (auto& v : pk) v /= top;
        const juce::SpinLock::ScopedLockType pl (peakLock);
        peaks[slot] = std::move (pk);
    }
    const juce::SpinLock::ScopedLockType lock (sourceLock);
    if (source[slot] != nullptr) retired.push_back (std::move (source[slot]));
    source[slot] = std::move (s);
    retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const auto& p) { return p.use_count() == 1; }), retired.end());
}

bool PoomseAmbientProcessor::slotUsesBuiltIn (int slot) const
{
    return apvts.state.getProperty ("sample" + juce::String (slot)).toString().isEmpty();
}

// (re)install the built-in sound of every slot that has no sample of its own, in the current style
void PoomseAmbientProcessor::installBuiltIns()
{
    const bool night = isNight();
    for (int k = 0; k < amb::kNumLayers; ++k)
    {
        if (! slotUsesBuiltIn (k)) continue;
        publishSource (k, amb::SourceBuffer::makeDefault (k, 48000.0, night));
        if (k == amb::Beat)    // its pitch reference differs between the drum hit and the bells
            if (auto* p = apvts.getParameter (layerParam (k, "Root")))
                p->setValueNotifyingHost (p->convertTo0to1 (night ? 60.f : 72.f));
    }
}

void PoomseAmbientProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    if (buffer.getNumChannels() < 2) return;

    const amb::SourceBuffer* srcs[amb::kNumLayers];
    {
        const juce::SpinLock::ScopedTryLockType lock (sourceLock);
        if (lock.isLocked())
            for (int k = 0; k < amb::kNumLayers; ++k)
                if (source[k] != audioSource[k]) { engine.killLayer (k); audioSource[k] = source[k]; }
    }
    for (int k = 0; k < amb::kNumLayers; ++k) { if (audioSource[k] == nullptr) return; srcs[k] = audioSource[k].get(); }
    engine.setSources (srcs);

    // host timing: the position in beats drives the whole piece
    double bpm = 120.0, hostBeat = -1.0;
    int beatsPerBar = 4;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto ppq = pos->getPpqPosition()) hostBeat = *ppq;
            if (auto ts = pos->getTimeSignature()) beatsPerBar = std::max (1, ts->numerator * 4 / std::max (1, ts->denominator));
        }
    const int n = buffer.getNumSamples();
    const double beatStart = hostBeat >= 0.0 ? hostBeat : internalBeat;
    const double spb = currentSampleRate * 60.0 / std::max (20.0, bpm);
    internalBeat = beatStart + (double) n / spb;

    amb::GlobalParams gp;
    gp.scale = (int) globalRaw[0]->load();   gp.journey = globalRaw[1]->load();
    gp.space = globalRaw[2]->load();         gp.depth = globalRaw[3]->load();
    gp.surprise = globalRaw[4]->load();      gp.pieceBars = (int) globalRaw[5]->load();
    gp.night = globalRaw[7]->load() > 0.5f;  gp.beatsPerBar = beatsPerBar;
    amb::LayerParams lps[amb::kNumLayers];
    for (int k = 0; k < amb::kNumLayers; ++k)
    {
        lps[k].level = layerRaw[k][0]->load();  lps[k].density = layerRaw[k][1]->load();
        lps[k].space = layerRaw[k][2]->load();  lps[k].root = (int) layerRaw[k][3]->load();
    }

    // MIDI: a held note is the tonic and the length of the piece; its release ends it
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const double beat = beatStart + (double) meta.samplePosition / spb;
        if (m.isNoteOn())
        {
            int s = (int) globalRaw[6]->load();
            if (s <= 0) s = 1 + (int) (seedRng.nextU32() % 9999u);
            seedInUse.store (s);
            engine.noteOn (m.getNoteNumber(), (uint32_t) s, beat, gp, lps);
        }
        else if (m.isNoteOff())                          engine.noteOff (m.getNoteNumber(), beat);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
    }

    const bool night = gp.night;
    darkS.setTargetValue (night ? fxRaw[0]->load() : 0.f);
    crackleS.setTargetValue (night ? fxRaw[1]->load() : 0.f);
    tapeS.setTargetValue (night ? fxRaw[2]->load() : 0.f);
    reverbS.setTargetValue (fxRaw[3]->load());
    echoS.setTargetValue (fxRaw[4]->load());
    cloudS.setTargetValue (fxRaw[5]->load());
    levelS.setTargetValue (juce::Decibels::decibelsToGain (fxRaw[6]->load()));
    const float echoSamples = (float) std::min (2.9 * currentSampleRate, 1.5 * spb);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    const int chunk = (int) dryL.size();
    for (int off = 0; off < n; off += chunk)
    {
        const int c = std::min (chunk, n - off);
        for (auto* v : { &dryL, &dryR, &sendL, &sendR, &cloudInL, &cloudInR }) std::fill (v->begin(), v->begin() + c, 0.f);
        engine.process (c, beatStart + (double) off / spb, bpm, gp, lps, dryL.data(), dryR.data(), sendL.data(), sendR.data(), cloudInL.data(), cloudInR.data());

        for (int i = 0; i < c; ++i)
        {
            float l = dryL[(size_t) i], r = dryR[(size_t) i];
            float cl, cr;
            cloud.process (cloudInL[(size_t) i], cloudInR[(size_t) i], cloudS.getNextValue(), cl, cr);   // the granular halo
            l += cl; r += cr;
            float rl, rr;
            reverb.process (sendL[(size_t) i] + 0.5f * cl, sendR[(size_t) i] + 0.5f * cr, rl, rr);       // one big reverb for everything
            const float rv = reverbS.getNextValue() * 1.4f;
            l += rl * rv; r += rr * rv;
            echo.process (l, r, echoSamples, 0.45f, echoS.getNextValue() * 0.6f);
            tape.process (l, r, tapeS.getNextValue());
            dark.process (l, r, darkS.getNextValue());
            float kl, kr;
            crackle.process (crackleS.getNextValue(), kl, kr);                                            // the dust sits on top
            l += kl; r += kr;
            const float g = levelS.getNextValue();
            L[off + i] = gg::softClip (l * g);
            R[off + i] = gg::softClip (r * g);
        }
    }

    // what the interface shows
    progressDisplay.store (engine.progress());
    const int ns = std::min (64, engine.numScenes());
    const double pieceBeats = std::max (1.0, (double) gp.pieceBars * beatsPerBar);
    for (int i = 0; i < ns; ++i)
    {
        sceneStart[(size_t) i].store ((float) ((engine.scene (i).startBeat - engine.pieceStartBeat()) / pieceBeats));
        sceneMood[(size_t) i].store (engine.scene (i).mood);
    }
    sceneCount.store (engine.pieceRunning() ? ns : 0);
    for (int k = 0; k < amb::kNumLayers; ++k) activity[(size_t) k].store (engine.layerActivity (k));
}

// ---------------------------------------------------------------------------------------------------------------
bool PoomseAmbientProcessor::loadSample (int slot, const juce::File& file)
{
    if (slot < 0 || slot >= amb::kNumLayers) return false;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples < 64 || reader->sampleRate <= 0.0) return false;

    const int len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 90.0));
    juce::AudioBuffer<float> tmp (2, len);
    tmp.clear();
    if (! reader->read (&tmp, 0, len, 0, true, true)) return false;

    auto s = std::make_shared<amb::SourceBuffer>();
    s->sampleRate = reader->sampleRate;
    const float* a = tmp.getReadPointer (0);
    const float* b = tmp.getReadPointer (reader->numChannels > 1 ? 1 : 0);
    s->l.assign (a, a + len);
    s->r.assign (b, b + len);

    // tuned layers: find the note of the sample so nobody has to type it (the beat's hit and the atmosphere have none)
    const bool tuned = slot == amb::Keys || slot == amb::Vocals || slot == amb::Pad || slot == amb::Sub || (slot == amb::Beat && ! isNight());
    if (tuned)
    {
        std::vector<float> mono ((size_t) len);
        for (int i = 0; i < len; ++i) mono[(size_t) i] = 0.5f * (s->l[(size_t) i] + s->r[(size_t) i]);
        const int fallback = (int) layerRaw[slot][3]->load();
        const int root = amb::detectRootNote (mono, s->sampleRate, fallback);
        if (auto* p = apvts.getParameter (layerParam (slot, "Root"))) p->setValueNotifyingHost (p->convertTo0to1 ((float) std::clamp (root, 0, 127)));
    }

    publishSource (slot, std::move (s));
    apvts.state.setProperty ("sample" + juce::String (slot), file.getFullPathName(), nullptr);
    return true;
}

juce::String PoomseAmbientProcessor::getSampleName (int slot) const
{
    const auto path = apvts.state.getProperty ("sample" + juce::String (slot)).toString();
    return path.isNotEmpty() ? juce::File (path).getFileName() : juce::String ("Built-in");
}

std::vector<float> PoomseAmbientProcessor::getPeaks (int slot) const
{
    const juce::SpinLock::ScopedLockType pl (peakLock);
    return peaks[slot];
}

// ---------------------------------------------------------------------------------------------------------------
void PoomseAmbientProcessor::randomise (RandomScope scope)
{
    juce::Random rng;
    auto setReal = [this] (const juce::String& id, float v)
    {
        if (auto* p = apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
    };
    auto between = [&] (float lo, float hi) { return lo + (hi - lo) * rng.nextFloat(); };

    if (scope == RandomScope::Piece || scope == RandomScope::AllSound)
    {
        static const int scales[] = { 2, 3, 4, 5, 1, 6 };
        setReal ("space", between (0.25f, 0.85f));     setReal ("depth", between (0.f, 1.f));
        setReal ("surprise", between (0.1f, 0.6f));    setReal ("journey", between (0.4f, 1.f));
        setReal ("scale", (float) scales[rng.nextInt (6)]);
    }
    if (scope == RandomScope::Layers || scope == RandomScope::AllSound)
        for (int k = 0; k < amb::kNumLayers; ++k)
        {
            setReal (layerParam (k, "Amount"), k == amb::Atmos ? between (0.5f, 1.f) : between (0.2f, 1.f));
            setReal (layerParam (k, "Level"), between (0.5f, 0.9f));
            setReal (layerParam (k, "Space"), between (0.1f, 0.9f));
        }
    if (scope == RandomScope::Sound || scope == RandomScope::AllSound)
    {
        setReal ("dark", between (0.1f, 0.7f));   setReal ("crackle", between (0.f, 0.5f)); setReal ("tape", between (0.f, 0.6f));
        setReal ("reverb", between (0.3f, 0.8f)); setReal ("echo", between (0.f, 0.4f));    setReal ("cloud", between (0.1f, 0.6f));
    }
}

void PoomseAmbientProcessor::resetToDefaults()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->getDefaultValue());
            rp->endChangeGesture();
        }
    currentPreset.store (0);
    installBuiltIns();
}

void PoomseAmbientProcessor::applyPreset (int index)
{
    const auto& list = amb::presets();
    if (index < 0 || index >= (int) list.size()) return;
    resetToDefaults();
    for (const auto& p : list[(size_t) index].params)
        if (auto* rp = apvts.getParameter (p.id))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->convertTo0to1 (p.value));
            rp->endChangeGesture();
        }
    installBuiltIns();
    currentPreset.store (index);
}

void PoomseAmbientProcessor::keepSeed()
{
    const int s = seedInUse.load();
    if (s > 0)
        if (auto* p = apvts.getParameter ("seed")) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 ((float) s)); p->endChangeGesture(); }
}

juce::AudioProcessorEditor* PoomseAmbientProcessor::createEditor() { return new PoomseAmbientEditor (*this); }

void PoomseAmbientProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    apvts.state.setProperty ("preset", currentPreset.load(), nullptr);
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void PoomseAmbientProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset.store (juce::jlimit (0, getNumPresets() - 1, (int) apvts.state.getProperty ("preset", 0)));
            installBuiltIns();
            for (int k = 0; k < amb::kNumLayers; ++k)
            {
                const auto path = apvts.state.getProperty ("sample" + juce::String (k)).toString();
                if (path.isNotEmpty() && juce::File (path).existsAsFile()) loadSample (k, juce::File (path));
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PoomseAmbientProcessor(); }
