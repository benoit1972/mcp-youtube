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

    l.add (std::make_unique<F> (pid ("position"),   "Position",     Range (0.f, 1.f, 0.001f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (pid ("scan"),       "Scan Speed",   Range (-2.f, 2.f, 0.01f), 1.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return juce::String (x, 2) + " x"; })));
    l.add (std::make_unique<F> (pid ("spray"),      "Spray",        Range (0.f, 1.f, 0.001f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (pid ("size"),       "Grain Size",   Range (5.f, 500.f, 0.1f, 0.4f), 120.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    l.add (std::make_unique<F> (pid ("density"),    "Density",      Range (1.f, 200.f, 0.1f, 0.5f), 30.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (hz)));
    l.add (std::make_unique<F> (pid ("shape"),      "Window Shape", Range (0.f, 1.f, 0.001f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (pid ("pitch"),      "Pitch",        Range (-24.f, 24.f, 0.01f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (st)));
    l.add (std::make_unique<F> (pid ("pitchSpray"), "Pitch Spray",  Range (0.f, 12.f, 0.01f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (st)));
    l.add (std::make_unique<F> (pid ("reverse"),    "Reverse",      Range (0.f, 1.f, 0.001f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (pid ("pan"),        "Stereo Spread", Range (0.f, 1.f, 0.001f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (pid ("level"),      "Level",        Range (-48.f, 6.f, 0.1f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (db)));

    l.add (std::make_unique<F> (pid ("attack"),  "Attack",  Range (0.001f, 5.f, 0.001f, 0.35f), 0.01f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (sec)));
    l.add (std::make_unique<F> (pid ("decay"),   "Decay",   Range (0.005f, 5.f, 0.001f, 0.35f), 0.3f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (sec)));
    l.add (std::make_unique<F> (pid ("sustain"), "Sustain", Range (0.f, 1.f, 0.001f), 1.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (pid ("release"), "Release", Range (0.005f, 8.f, 0.001f, 0.35f), 0.4f,
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

    // 8 macros: automatable, these are the knobs to map in a Live rack. Their targets live in the matrix.
    for (int m = 0; m < gg::kNumMacros; ++m)
        l.add (std::make_unique<F> (juce::ParameterID { juce::String ("macro") + juce::String (m + 1), 1 }, "Macro " + juce::String (m + 1),
                                    Range (0.f, 1.f, 0.001f), 0.f,
                                    juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    // 2 LFOs
    const juce::StringArray lfoSync { "Free", "1 bar", "1/2", "1/4", "1/8", "1/16" };
    const juce::StringArray lfoShape { "Sine", "Triangle", "Saw", "Square", "Random" };
    for (int i = 1; i <= 2; ++i)
    {
        const auto n = juce::String (i);
        l.add (std::make_unique<F> (juce::ParameterID { "lfo" + n + "Rate", 1 }, "LFO " + n + " Rate", Range (0.02f, 20.f, 0.01f, 0.4f), 1.f,
                                    juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return juce::String (x, 2) + " Hz"; })));
        l.add (std::make_unique<C> (juce::ParameterID { "lfo" + n + "Sync", 1 }, "LFO " + n + " Sync", lfoSync, 0));
        l.add (std::make_unique<C> (juce::ParameterID { "lfo" + n + "Shape", 1 }, "LFO " + n + " Shape", lfoShape, 0));
    }
    // euclidean sequencer
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "seqSteps", 1 }, "Seq Steps", 1, 16, 8));
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "seqHits", 1 }, "Seq Hits", 0, 16, 3));
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "seqRot", 1 }, "Seq Rotate", 0, 15, 0));
    l.add (std::make_unique<C> (juce::ParameterID { "seqRate", 1 }, "Seq Rate", divisions, 2));
    l.add (std::make_unique<F> (juce::ParameterID { "seqDecay", 1 }, "Seq Decay", Range (0.01f, 2.f, 0.01f, 0.5f), 0.25f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (sec)));
    l.add (std::make_unique<F> (juce::ParameterID { "seqRandom", 1 }, "Seq Random", Range (0.f, 1.f, 0.001f), 0.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
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
        macroRaw[(size_t) m] = apvts.getRawParameterValue ("macro" + juce::String (m + 1));
    for (int i = 0; i < 2; ++i)
    {
        const auto n = juce::String (i + 1);
        lfoRaw[i][0] = apvts.getRawParameterValue ("lfo" + n + "Rate");
        lfoRaw[i][1] = apvts.getRawParameterValue ("lfo" + n + "Sync");
        lfoRaw[i][2] = apvts.getRawParameterValue ("lfo" + n + "Shape");
    }
    {
        const char* ids[6] = { "seqSteps", "seqHits", "seqRot", "seqRate", "seqDecay", "seqRandom" };
        for (int k = 0; k < 6; ++k) seqRaw[k] = apvts.getRawParameterValue (ids[k]);
    }

    const auto dm = gg::defaultMatrix();
    for (int sIdx = 0; sIdx < gg::kNumSources; ++sIdx)
        for (int d = 0; d < gg::kNumDests; ++d)
            matrix[(size_t) (sIdx * gg::kNumDests + d)].store (dm.amt[sIdx][d]);
    for (auto& v : sourceDisplay) v.store (0.f);
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
    lfo[0].prepare (sampleRate, 0x1F123u);
    lfo[1].prepare (sampleRate, 0x2B4C5u);
    seq.prepare (sampleRate, 0x3D7E9u);
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

    // host tempo first: the LFOs and the sequencer are tempo-aware
    double hostBpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm = *bpm;

    // modulation sources -> matrix -> per-destination offsets (once per block)
    {
        const int nBlock = buffer.getNumSamples();
        float src[gg::kNumSources];
        for (int m = 0; m < gg::kNumMacros; ++m) src[m] = macroRaw[(size_t) m]->load();
        for (int i = 0; i < 2; ++i)
            src[gg::kSrcLfo1 + i] = lfo[i].advance (nBlock, lfoRaw[i][0]->load(), (int) lfoRaw[i][1]->load(), hostBpm, (int) lfoRaw[i][2]->load());
        gg::SeqParams sp;
        sp.steps = (int) seqRaw[0]->load();   sp.hits = (int) seqRaw[1]->load();
        sp.rotation = (int) seqRaw[2]->load(); sp.rateDiv = (int) seqRaw[3]->load();
        sp.decaySec = seqRaw[4]->load();      sp.randomAmt = seqRaw[5]->load();
        sp.bpm = hostBpm;
        src[gg::kSrcSeq] = seq.advance (nBlock, sp);

        gg::Matrix mat;
        for (int sIdx = 0; sIdx < gg::kNumSources; ++sIdx)
            for (int d = 0; d < gg::kNumDests; ++d)
                mat.amt[sIdx][d] = matrix[(size_t) (sIdx * gg::kNumDests + d)].load();
        gg::accumulate (src, mat, modOffsets.data());
        for (int sIdx = 0; sIdx < gg::kNumSources; ++sIdx) sourceDisplay[(size_t) sIdx].store (src[sIdx]);
    }

    gg::GrainParams gp;
    gp.position = param ("position");   gp.scan = param ("scan");   gp.spray = param ("spray");
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
    fp.bpm = hostBpm;

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
        L[i] = gg::softClip (l * g); // transparent below 0.8, rounds off the peaks above
        R[i] = gg::softClip (r * g);
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
        { "scan", 0.40f, 0.85f },
        { "pitchSpray", 0.f, 0.5f }, { "reverse", 0.f, 0.7f },
        { "attack", 0.f, 0.45f },   { "decay", 0.10f, 0.70f },    { "sustain", 0.40f, 1.f },
        { "release", 0.10f, 0.50f },
        { "stutter", 0.f, 0.5f },   { "stutterPitch", 0.25f, 0.75f }, { "glitch", 0.f, 0.4f },
        { "tape", 0.f, 0.7f },      { "delayFb", 0.f, 0.6f },     { "delayMix", 0.f, 0.45f },
        { "reverbSize", 0.2f, 0.8f }, { "reverbMix", 0.05f, 0.45f }, { "shimmer", 0.f, 0.5f },
    };
    static const char* const grain[] = { "position", "scan", "spray", "size", "density", "shape", "pitch", "pitchSpray", "reverse", "pan" };
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

    if (scope == RandomScope::Mod)
    {
        // macros get random targets in the matrix (LFO / sequencer columns are kept) and random values
        gg::Matrix mat;
        for (int sIdx = 0; sIdx < gg::kNumSources; ++sIdx)
            for (int d = 0; d < gg::kNumDests; ++d) mat.amt[sIdx][d] = getMatrix (sIdx, d);
        gg::Rng grng ((uint32_t) rng.nextInt() | 1u);
        gg::randomAssignMacros (grng, mat);
        for (int sIdx = 0; sIdx < gg::kNumMacros; ++sIdx)
            for (int d = 0; d < gg::kNumDests; ++d) setMatrix (sIdx, d, mat.amt[sIdx][d]);
        for (int m = 0; m < gg::kNumMacros; ++m)
            setNormalised ("macro" + juce::String (m + 1), 0.1f + 0.8f * rng.nextFloat());
    }
}

void GlitchGrainProcessor::resetToDefaults()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->getDefaultValue());
            rp->endChangeGesture();
        }

    const auto dm = gg::defaultMatrix();
    for (int sIdx = 0; sIdx < gg::kNumSources; ++sIdx)
        for (int d = 0; d < gg::kNumDests; ++d)
            setMatrix (sIdx, d, dm.amt[sIdx][d]);
}

juce::AudioProcessorEditor* GlitchGrainProcessor::createEditor() { return new PoomseGlitchEditor (*this); }

void GlitchGrainProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    apvts.state.setProperty ("matrix", serialiseMatrix(), nullptr);
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void GlitchGrainProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            restoreMatrix (apvts.state.getProperty ("matrix").toString());
            const auto path = apvts.state.getProperty ("samplePath").toString();
            if (path.isNotEmpty())
            {
                const juce::File f (path);
                if (f.existsAsFile()) loadSample (f);
            }
        }
}

juce::String GlitchGrainProcessor::serialiseMatrix() const
{
    juce::StringArray parts;
    for (int i = 0; i < gg::kNumSources * gg::kNumDests; ++i)
        parts.add (juce::String (matrix[(size_t) i].load(), 3));
    return parts.joinIntoString (" ");
}

void GlitchGrainProcessor::restoreMatrix (const juce::String& text)
{
    if (text.isEmpty()) return; // old state: keep the defaults
    juce::StringArray parts;
    parts.addTokens (text, " ", "");
    parts.removeEmptyStrings();
    if (parts.size() != gg::kNumSources * gg::kNumDests) return;
    for (int i = 0; i < parts.size(); ++i)
        matrix[(size_t) i].store (juce::jlimit (-1.f, 1.f, parts[i].getFloatValue()));
    ++matrixVersion;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GlitchGrainProcessor(); }
