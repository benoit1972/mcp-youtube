#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstring>

using Range = juce::NormalisableRange<float>;

juce::AudioProcessorValueTreeState::ParameterLayout GlitchGrainProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using C = juce::AudioParameterChoice;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    int v = 1;
    auto pid = [&v] (const char* id) { return juce::ParameterID { id, v }; };

    auto pct = [] (float x, int) { return juce::String (juce::roundToInt (x * 100.f)) + " %"; };
    auto ms  = [] (float x, int) { return juce::String (x, 1) + " ms"; };
    auto hz  = [] (float x, int) { return juce::String (x, 1) + " /s"; };
    auto st  = [] (float x, int) { return juce::String (x, 1) + " st"; };
    auto sec = [] (float x, int) { return juce::String (x, 2) + " s"; };
    auto db  = [] (float x, int) { return juce::String (x, 1) + " dB"; };

    const juce::StringArray divisions { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T", "1/32T" };

    l.add (std::make_unique<F> (pid ("position"),   "Position",     Range (0.f, 1.f, 0.001f), 0.30f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (pid ("spray"),      "Spray",        Range (0.f, 1.f, 0.001f), 0.10f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (pid ("size"),       "Grain Size",   Range (5.f, 500.f, 0.1f, 0.4f), 80.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    l.add (std::make_unique<F> (pid ("density"),    "Density",      Range (1.f, 200.f, 0.1f, 0.5f), 20.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (hz)));
    l.add (std::make_unique<F> (pid ("shape"),      "Window Shape", Range (0.f, 1.f, 0.001f), 0.30f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (pid ("pitch"),      "Pitch",        Range (-24.f, 24.f, 0.01f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (st)));
    l.add (std::make_unique<F> (pid ("pitchSpray"), "Pitch Spray",  Range (0.f, 12.f, 0.01f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (st)));
    l.add (std::make_unique<F> (pid ("reverse"),    "Reverse",      Range (0.f, 1.f, 0.001f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (pid ("pan"),        "Stereo Spread", Range (0.f, 1.f, 0.001f), 0.5f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (pid ("level"),      "Level",        Range (-48.f, 6.f, 0.1f), -6.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (db)));

    l.add (std::make_unique<F> (pid ("attack"),  "Attack",  Range (0.001f, 5.f, 0.001f, 0.35f), 0.01f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (sec)));
    l.add (std::make_unique<F> (pid ("decay"),   "Decay",   Range (0.005f, 5.f, 0.001f, 0.35f), 0.3f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (sec)));
    l.add (std::make_unique<F> (pid ("sustain"), "Sustain", Range (0.f, 1.f, 0.001f), 0.8f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (pid ("release"), "Release", Range (0.005f, 8.f, 0.001f, 0.35f), 0.5f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (sec)));

    l.add (std::make_unique<F> (pid ("stutter"),      "Stutter",       Range (0.f, 1.f, 0.001f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<C> (pid ("stutterRate"),  "Stutter Rate",  divisions, 2));
    l.add (std::make_unique<F> (pid ("stutterPitch"), "Stutter Pitch", Range (-12.f, 12.f, 0.01f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (st)));
    l.add (std::make_unique<F> (pid ("glitch"),       "Glitch",        Range (0.f, 1.f, 0.001f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<C> (pid ("glitchRate"),   "Glitch Rate",   divisions, 2));

    // tape & delay
    l.add (std::make_unique<F> (pid ("tape"),       "Tape",           Range (0.f, 1.f, 0.001f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (pid ("delayTime"),  "Delay Time",     Range (10.f, 1500.f, 1.f, 0.5f), 380.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return juce::String (juce::roundToInt (x)) + " ms"; })));
    l.add (std::make_unique<F> (pid ("delayFb"),    "Delay Feedback", Range (0.f, 0.85f, 0.001f), 0.4f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (pid ("delayMix"),   "Delay Mix",      Range (0.f, 1.f, 0.001f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    // reverb & shimmer
    l.add (std::make_unique<F> (pid ("reverbSize"), "Reverb Size",    Range (0.f, 1.f, 0.001f), 0.7f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (pid ("reverbDamp"), "Reverb Damping", Range (0.f, 1.f, 0.001f), 0.4f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (pid ("reverbMix"),  "Reverb Mix",     Range (0.f, 1.f, 0.001f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (pid ("shimmer"),    "Shimmer",        Range (0.f, 1.f, 0.001f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    // Drift: generative notes
    l.add (std::make_unique<F> (pid ("drift"),      "Drift",          Range (0.f, 1.f, 0.001f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<C> (pid ("driftRate"),  "Drift Rate",     juce::StringArray { "1 bar", "1/2", "1/4", "1/8", "1/16" }, 1));
    l.add (std::make_unique<C> (pid ("driftScale"), "Drift Scale",    juce::StringArray { "Major", "Minor", "Pentatonic", "Dorian", "Lydian", "Whole tone", "From MIDI" }, 2));
    l.add (std::make_unique<C> (pid ("driftRoot"),  "Drift Root",     juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    l.add (std::make_unique<F> (pid ("driftLength"), "Drift Length",  Range (0.25f, 6.f, 0.01f, 0.5f), 1.5f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return juce::String (x, 2) + " steps"; })));

    // 8 macros (automatable: these are the 8 knobs to map in a Live rack) and 3 assignment slots each
    juce::StringArray destNames { "None" };
    for (const auto& d : gg::kModDests) destNames.add (d.label);
    for (int m = 0; m < gg::kNumMacros; ++m)
    {
        l.add (std::make_unique<F> (juce::ParameterID { ("macro" + juce::String (m + 1)).toRawUTF8(), 1 }, "Macro " + juce::String (m + 1),
                                    Range (0.f, 1.f, 0.001f), 0.f,
                                    juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
        for (int sl = 0; sl < gg::kSlotsPerMacro; ++sl)
        {
            const auto tag = "m" + juce::String (m + 1) + "s" + juce::String (sl + 1);
            const auto def = gg::defaultSlot (m, sl);
            l.add (std::make_unique<C> (juce::ParameterID { (tag + "d").toRawUTF8(), 1 }, "Macro " + juce::String (m + 1) + " slot " + juce::String (sl + 1) + " target",
                                        destNames, def.dest, juce::AudioParameterChoiceAttributes().withAutomatable (false)));
            l.add (std::make_unique<F> (juce::ParameterID { (tag + "a").toRawUTF8(), 1 }, "Macro " + juce::String (m + 1) + " slot " + juce::String (sl + 1) + " amount",
                                        Range (-1.f, 1.f, 0.01f), def.amount,
                                        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withAutomatable (false)));
        }
    }
    return l;
}

GlitchGrainProcessor::GlitchGrainProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    events.reserve (1024);
    for (int i = 0; i < gg::kNumDests; ++i)
    {
        destParam[(size_t) i] = apvts.getParameter (gg::kModDests[i].id);
        destRaw[(size_t) i] = apvts.getRawParameterValue (gg::kModDests[i].id);
    }
    for (int m = 0; m < gg::kNumMacros; ++m)
    {
        macroRaw[(size_t) m] = apvts.getRawParameterValue ("macro" + juce::String (m + 1));
        for (int sl = 0; sl < gg::kSlotsPerMacro; ++sl)
        {
            const auto tag = "m" + juce::String (m + 1) + "s" + juce::String (sl + 1);
            slotDestRaw[m][sl] = apvts.getRawParameterValue (tag + "d");
            slotAmtRaw[m][sl] = apvts.getRawParameterValue (tag + "a");
        }
    }
    publishSource (gg::SourceBuffer::makeDefault());
}

float GlitchGrainProcessor::param (const char* id) const
{
    const int d = gg::destIndexOf (id);
    if (d == 0) return apvts.getRawParameterValue (id)->load();

    const float raw = destRaw[(size_t) (d - 1)]->load();
    const float off = modOffsets[(size_t) (d - 1)];
    if (off == 0.f) return raw;
    auto* p = destParam[(size_t) (d - 1)];
    return p->convertFrom0to1 (juce::jlimit (0.f, 1.f, p->convertTo0to1 (raw) + off));
}

void GlitchGrainProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    fx.prepare (sampleRate);
    ambient.prepare (sampleRate);
    noteGen.prepare (sampleRate);
    lastHeard.fill (-1000000000000LL);
    sampleClock = 0;
    level.reset (sampleRate, 0.03);
    level.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param ("level")));
}

bool GlitchGrainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void GlitchGrainProcessor::publishSource (std::shared_ptr<const gg::SourceBuffer> s)
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
        peaks = std::move (pk);
    }

    const juce::SpinLock::ScopedLockType lock (sourceLock);
    if (source != nullptr) retired.push_back (std::move (source));
    source = std::move (s);
    // free retired buffers that the audio thread no longer references
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [] (const auto& p) { return p.use_count() == 1; }),
                   retired.end());
}

void GlitchGrainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    if (buffer.getNumChannels() < 2) return;

    {
        const juce::SpinLock::ScopedTryLockType lock (sourceLock);
        if (lock.isLocked() && source != audioSource) audioSource = source;
    }
    if (audioSource == nullptr) return;

    {
        float macros[gg::kNumMacros];
        gg::Slot slots[gg::kNumMacros][gg::kSlotsPerMacro];
        for (int m = 0; m < gg::kNumMacros; ++m)
        {
            macros[m] = macroRaw[(size_t) m]->load();
            for (int sl = 0; sl < gg::kSlotsPerMacro; ++sl)
                slots[m][sl] = { (int) (slotDestRaw[m][sl]->load() + 0.5f), slotAmtRaw[m][sl]->load() };
        }
        gg::accumulate (macros, slots, modOffsets.data());
    }

    gg::GrainParams gp;
    gp.position = param ("position");   gp.spray = param ("spray");
    gp.sizeMs = param ("size");         gp.density = param ("density");
    gp.shape = param ("shape");         gp.pitch = param ("pitch");
    gp.pitchSpray = param ("pitchSpray"); gp.reverse = param ("reverse");
    gp.panSpread = param ("pan");
    gp.attack = param ("attack");       gp.decay = param ("decay");
    gp.sustain = param ("sustain");     gp.release = param ("release");

    gg::FxParams fp;
    fp.stutterAmount = param ("stutter");
    fp.stutterDiv = (int) param ("stutterRate");
    fp.stutterPitch = param ("stutterPitch");
    fp.glitchAmount = param ("glitch");
    fp.glitchDiv = (int) param ("glitchRate");
    fp.bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                fp.bpm = *bpm;

    gg::AmbientParams ap;
    ap.tape = param ("tape");
    ap.delayMs = param ("delayTime");   ap.delayFeedback = param ("delayFb");   ap.delayMix = param ("delayMix");
    ap.reverbSize = param ("reverbSize"); ap.reverbDamp = param ("reverbDamp");
    ap.reverbMix = param ("reverbMix"); ap.shimmer = param ("shimmer");

    gg::GenParams dp;
    dp.probability = param ("drift");   dp.rateDiv = (int) param ("driftRate");
    dp.scale = (int) param ("driftScale"); dp.root = (int) param ("driftRoot");
    dp.length = param ("driftLength");  dp.bpm = fp.bpm;

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    const int n = buffer.getNumSamples();
    const auto& src = *audioSource;

    // Gather MIDI + generated notes into one sample-ordered list (no allocation: capacity reserved).
    events.clear();
    auto addEv = [this] (Ev e) { if (events.size() < events.capacity()) events.push_back (e); };
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int off = std::min (meta.samplePosition, n - 1);
        if (m.isNoteOn())
        {
            addEv ({ off, 0, m.getNoteNumber(), m.getFloatVelocity() });
            lastHeard[(size_t) (m.getNoteNumber() % 12)] = sampleClock + off; // Drift "From MIDI" scale
        }
        else if (m.isNoteOff())                          addEv ({ off, 1, m.getNoteNumber(), 0.f });
        else if (m.isAllNotesOff() || m.isAllSoundOff()) addEv ({ off, 2, 0, 0.f });
    }
    // "From MIDI": the scale is every pitch class played on the keyboard during the last 20 s
    dp.scaleMask = 0;
    for (int pc = 0; pc < 12; ++pc)
        if (sampleClock - lastHeard[(size_t) pc] < (juce::int64) (20.0 * currentSampleRate))
            dp.scaleMask |= 1u << pc;
    sampleClock += n;

    gg::NoteEvent gen[64];
    const int numGen = noteGen.generate (n, dp, gen, 64);
    for (int i = 0; i < numGen; ++i)
        addEv ({ gen[i].offset, gen[i].on ? 0 : 1, gen[i].note, gen[i].velocity });
    for (size_t i = 1; i < events.size(); ++i) // stable insertion sort by offset
    {
        const Ev e = events[i];
        size_t j = i;
        while (j > 0 && events[j - 1].offset > e.offset) { events[j] = events[j - 1]; --j; }
        events[j] = e;
    }

    int pos = 0;
    auto render = [&] (int end)
    {
        if (end > pos) engine.render (L + pos, R + pos, end - pos, src, gp);
        pos = std::max (pos, end);
    };
    for (const auto& e : events)
    {
        render (e.offset);
        if (e.kind == 0)      engine.noteOn (e.note, e.vel);
        else if (e.kind == 1) engine.noteOff (e.note);
        else                  engine.allNotesOff();
    }
    render (n);
    fp.active = engine.anyHeld(); // stutter/glitch only while a key is held

    level.setTargetValue (juce::Decibels::decibelsToGain (param ("level")));
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];
        fx.process (l, r, fp);
        ambient.process (l, r, ap);
        const float g = level.getNextValue();
        L[i] = std::tanh (l * g); // soft limiter keeps dense clouds / glitches from clipping
        R[i] = std::tanh (r * g);
    }
}

bool GlitchGrainProcessor::loadSample (const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples < 64 || reader->sampleRate <= 0.0) return false;

    const int len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60.0));
    juce::AudioBuffer<float> tmp (2, len);
    tmp.clear();
    if (! reader->read (&tmp, 0, len, 0, true, true)) return false;

    auto s = std::make_shared<gg::SourceBuffer>();
    s->sampleRate = reader->sampleRate;
    const float* a = tmp.getReadPointer (0);
    const float* b = tmp.getReadPointer (reader->numChannels > 1 ? 1 : 0);
    s->l.assign (a, a + len);
    s->r.assign (b, b + len);

    publishSource (std::move (s));
    apvts.state.setProperty ("samplePath", file.getFullPathName(), nullptr);
    return true;
}

juce::String GlitchGrainProcessor::getSampleName() const
{
    const auto path = apvts.state.getProperty ("samplePath").toString();
    return path.isEmpty() ? "Built-in drone" : juce::File (path).getFileName();
}

std::vector<float> GlitchGrainProcessor::getPeaks() const
{
    const juce::SpinLock::ScopedLockType pl (peakLock);
    return peaks;
}

void GlitchGrainProcessor::randomise (RandomScope scope)
{
    // Normalised (0..1) ranges that keep the result playable; anything not listed is fully random.
    struct R { const char* id; float lo, hi; };
    static const R ranges[] = {
        { "size", 0.10f, 0.85f },   { "density", 0.20f, 0.90f },  { "pitch", 0.25f, 0.75f },
        { "pitchSpray", 0.f, 0.5f }, { "reverse", 0.f, 0.7f },
        { "attack", 0.f, 0.45f },   { "decay", 0.10f, 0.70f },    { "sustain", 0.40f, 1.f },
        { "release", 0.10f, 0.50f },
        { "stutter", 0.f, 0.5f },   { "stutterPitch", 0.25f, 0.75f }, { "glitch", 0.f, 0.4f },
        { "tape", 0.f, 0.7f },      { "delayFb", 0.f, 0.6f },     { "delayMix", 0.f, 0.45f },
        { "reverbSize", 0.2f, 0.8f }, { "reverbMix", 0.05f, 0.45f }, { "shimmer", 0.f, 0.5f },
    };
    static const char* const grain[] = { "position", "spray", "size", "density", "shape", "pitch", "pitchSpray", "reverse", "pan" };
    static const char* const play[] = { "attack", "decay", "sustain", "release" };
    static const char* const fx[] = { "stutter", "stutterRate", "stutterPitch", "glitch", "glitchRate", "tape", "delayTime",
                                      "delayFb", "delayMix", "reverbSize", "reverbDamp", "reverbMix", "shimmer" };

    juce::Random rng;
    auto setNormalised = [this] (const juce::String& id, float v)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (v);
            p->endChangeGesture();
        }
    };
    auto randomList = [&] (const char* const* ids, size_t n)
    {
        for (size_t i = 0; i < n; ++i)
        {
            float lo = 0.f, hi = 1.f;
            for (const auto& r : ranges)
                if (std::strcmp (r.id, ids[i]) == 0) { lo = r.lo; hi = r.hi; }
            setNormalised (ids[i], lo + (hi - lo) * rng.nextFloat());
        }
    };

    // Level (master volume) and Drift (note generator) are never randomised.
    if (scope == RandomScope::Grain || scope == RandomScope::AllSound) randomList (grain, std::size (grain));
    if (scope == RandomScope::Play  || scope == RandomScope::AllSound) randomList (play, std::size (play));
    if (scope == RandomScope::Fx    || scope == RandomScope::AllSound) randomList (fx, std::size (fx));

    if (scope == RandomScope::Macros)
    {
        gg::Rng grng ((uint32_t) rng.nextInt() | 1u);
        gg::Slot slots[gg::kNumMacros][gg::kSlotsPerMacro];
        gg::randomAssign (grng, slots);
        for (int m = 0; m < gg::kNumMacros; ++m)
            for (int sl = 0; sl < gg::kSlotsPerMacro; ++sl)
            {
                const auto tag = "m" + juce::String (m + 1) + "s" + juce::String (sl + 1);
                if (auto* pd = apvts.getParameter (tag + "d")) setNormalised (tag + "d", pd->convertTo0to1 ((float) slots[m][sl].dest));
                if (auto* pa = apvts.getParameter (tag + "a")) setNormalised (tag + "a", pa->convertTo0to1 (slots[m][sl].amount));
            }
    }
}

juce::AudioProcessorEditor* GlitchGrainProcessor::createEditor() { return new PoomseGlitchEditor (*this); }

void GlitchGrainProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void GlitchGrainProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            const auto path = apvts.state.getProperty ("samplePath").toString();
            if (path.isNotEmpty())
            {
                const juce::File f (path);
                if (f.existsAsFile()) loadSample (f);
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GlitchGrainProcessor(); }
