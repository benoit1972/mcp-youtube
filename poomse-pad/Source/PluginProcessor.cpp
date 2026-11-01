#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstring>

using Range = juce::NormalisableRange<float>;

juce::AudioProcessorValueTreeState::ParameterLayout PoomsePadProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using C = juce::AudioParameterChoice;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    int v = 1;
    auto pid = [&v] (const char* id) { return juce::ParameterID { id, v }; };

    auto pct = [] (float x, int) { return juce::String (juce::roundToInt (x * 100.f)) + " %"; };
    auto sec = [] (float x, int) { return juce::String (x, x < 10.f ? 2 : 1) + " s"; };
    auto hz  = [] (float x, int) { return juce::String (x, 2) + " Hz"; };
    auto db  = [] (float x, int) { return juce::String (x, 1) + " dB"; };
    auto attr = [] (auto fn) { return juce::AudioParameterFloatAttributes().withStringFromValueFunction (fn); };

    // --- pad ---
    l.add (std::make_unique<F> (pid ("time"),   "Time",   Range (0.1f, 60.f, 0.01f, 0.4f), 4.f, attr (sec)));
    l.add (std::make_unique<F> (pid ("rise"),   "Rise",   Range (0.02f, 10.f, 0.01f, 0.4f), 0.8f, attr (sec)));
    l.add (std::make_unique<juce::AudioParameterBool> (pid ("hold"), "Hold", false));
    l.add (std::make_unique<F> (pid ("sub"),    "Sub (-12 st)",    Range (0.f, 1.f, 0.001f), 0.f, attr (pct)));
    l.add (std::make_unique<F> (pid ("body"),   "Body",            Range (0.f, 1.f, 0.001f), 1.f, attr (pct)));
    l.add (std::make_unique<F> (pid ("fifth"),  "Fifth (+7 st)",   Range (0.f, 1.f, 0.001f), 0.f, attr (pct)));
    l.add (std::make_unique<F> (pid ("octave"), "Octave (+12 st)", Range (0.f, 1.f, 0.001f), 0.f, attr (pct)));
    l.add (std::make_unique<F> (pid ("evolve"), "Evolve",          Range (0.f, 1.f, 0.001f), 0.3f, attr (pct)));
    l.add (std::make_unique<F> (pid ("rate"),   "Evolve Rate",     Range (0.01f, 1.f, 0.001f, 0.4f), 0.1f, attr (hz)));
    // --- tone, space, output ---
    l.add (std::make_unique<F> (pid ("tone"),    "Tone",          Range (-1.f, 1.f, 0.001f), 0.f, attr (pct)));
    l.add (std::make_unique<F> (pid ("width"),   "Width",         Range (0.f, 1.f, 0.001f), 0.7f, attr (pct)));
    l.add (std::make_unique<F> (pid ("revSize"), "Reverb Size",   Range (0.f, 1.f, 0.001f), 0.7f, attr (pct)));
    l.add (std::make_unique<F> (pid ("revMix"),  "Reverb Mix",    Range (0.f, 1.f, 0.001f), 0.3f, attr (pct)));
    l.add (std::make_unique<F> (pid ("mix"),     "Mix",           Range (0.f, 1.f, 0.001f), 1.f, attr (pct)));
    l.add (std::make_unique<F> (pid ("level"),   "Level",         Range (-48.f, 6.f, 0.1f), 0.f, attr (db)));

    // --- 8 macros: automatable, these are the knobs to map in a Live rack. Their targets live in the matrix. ---
    for (int m = 0; m < gg::kNumMacros; ++m)
        l.add (std::make_unique<F> (juce::ParameterID { juce::String ("macro") + juce::String (m + 1), 1 }, "Macro " + juce::String (m + 1),
                                    Range (0.f, 1.f, 0.001f), 0.f, attr (pct)));

    // --- 2 LFOs ---
    const juce::StringArray lfoSync { "Free", "1 bar", "1/2", "1/4", "1/8", "1/16" };
    const juce::StringArray lfoShape { "Sine", "Triangle", "Saw", "Square", "Random" };
    const juce::StringArray divisions { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T", "1/32T" };
    for (int i = 1; i <= 2; ++i)
    {
        const auto n = juce::String (i);
        l.add (std::make_unique<F> (juce::ParameterID { "lfo" + n + "Rate", 1 }, "LFO " + n + " Rate", Range (0.02f, 20.f, 0.01f, 0.4f), 1.f, attr (hz)));
        l.add (std::make_unique<C> (juce::ParameterID { "lfo" + n + "Sync", 1 }, "LFO " + n + " Sync", lfoSync, 0));
        l.add (std::make_unique<C> (juce::ParameterID { "lfo" + n + "Shape", 1 }, "LFO " + n + " Shape", lfoShape, 0));
    }
    // --- euclidean sequencer ---
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "seqSteps", 1 }, "Seq Steps", 1, 16, 8));
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "seqHits", 1 }, "Seq Hits", 0, 16, 3));
    l.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "seqRot", 1 }, "Seq Rotate", 0, 15, 0));
    l.add (std::make_unique<C> (juce::ParameterID { "seqRate", 1 }, "Seq Rate", divisions, 2));
    l.add (std::make_unique<F> (juce::ParameterID { "seqDecay", 1 }, "Seq Decay", Range (0.01f, 2.f, 0.01f, 0.5f), 0.25f, attr (sec)));
    l.add (std::make_unique<F> (juce::ParameterID { "seqRandom", 1 }, "Seq Random", Range (0.f, 1.f, 0.001f), 0.f, attr (pct)));
    return l;
}

PoomsePadProcessor::PoomsePadProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
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
    for (auto& m : matrix) m.store (0.f); // the matrix starts empty
    for (auto& v : sourceDisplay) v.store (0.f);
    for (auto& v : spectrumDisplay) v.store (0.f);
}

float PoomsePadProcessor::param (const char* id) const
{
    const int d = gg::destIndexOf (id);
    if (d == 0) return apvts.getRawParameterValue (id)->load();

    const float raw = destRaw[(size_t) (d - 1)]->load();
    const float off = modOffsets[(size_t) (d - 1)];
    if (off == 0.f) return raw;
    auto* p = destParam[(size_t) (d - 1)];
    return p->convertFrom0to1 (juce::jlimit (0.f, 1.f, p->convertTo0to1 (raw) + off));
}

void PoomsePadProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    padEngine.prepare (sampleRate);
    reverb.prepare (sampleRate);
    lfo[0].prepare (sampleRate, 0x1F123u);
    lfo[1].prepare (sampleRate, 0x2B4C5u);
    seq.prepare (sampleRate, 0x3D7E9u);
    levelSmooth.reset (sampleRate, 0.03);
    mixSmooth.reset (sampleRate, 0.03);
    levelSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param ("level")));
    mixSmooth.setCurrentAndTargetValue (param ("mix"));
    reverbCounter = 0;
}

bool PoomsePadProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void PoomsePadProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2) return;

    double hostBpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm = *bpm;

    const int n = buffer.getNumSamples();

    // modulation sources -> matrix -> per-destination offsets (once per block)
    {
        float src[gg::kNumSources];
        for (int m = 0; m < gg::kNumMacros; ++m) src[m] = macroRaw[(size_t) m]->load();
        for (int i = 0; i < 2; ++i)
            src[gg::kSrcLfo1 + i] = lfo[i].advance (n, lfoRaw[i][0]->load(), (int) lfoRaw[i][1]->load(), hostBpm, (int) lfoRaw[i][2]->load());
        gg::SeqParams sp;
        sp.steps = (int) seqRaw[0]->load();    sp.hits = (int) seqRaw[1]->load();
        sp.rotation = (int) seqRaw[2]->load();  sp.rateDiv = (int) seqRaw[3]->load();
        sp.decaySec = seqRaw[4]->load();       sp.randomAmt = seqRaw[5]->load();
        sp.bpm = hostBpm;
        src[gg::kSrcSeq] = seq.advance (n, sp);

        gg::Matrix mat;
        for (int sIdx = 0; sIdx < gg::kNumSources; ++sIdx)
            for (int d = 0; d < gg::kNumDests; ++d)
                mat.amt[sIdx][d] = matrix[(size_t) (sIdx * gg::kNumDests + d)].load();
        gg::accumulate (src, mat, modOffsets.data());
        for (int sIdx = 0; sIdx < gg::kNumSources; ++sIdx) sourceDisplay[(size_t) sIdx].store (src[sIdx]);
    }

    pad::PadParams pp;
    pp.riseSec = param ("rise");     pp.timeSec = param ("time");
    pp.hold = apvts.getRawParameterValue ("hold")->load() > 0.5f;
    pp.sub = param ("sub");          pp.body = param ("body");
    pp.fifth = param ("fifth");      pp.octave = param ("octave");
    pp.tone = param ("tone");        pp.width = param ("width");
    pp.evolve = param ("evolve");    pp.evolveRate = param ("rate");

    const float revMix = param ("revMix");
    reverb.setSize (param ("revSize"));
    mixSmooth.setTargetValue (param ("mix"));
    levelSmooth.setTargetValue (juce::Decibels::decibelsToGain (param ("level")));

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    for (int i = 0; i < n; ++i)
    {
        const float dryL = L[i], dryR = R[i];
        float wl, wr;
        padEngine.process (dryL, dryR, pp, wl, wr);

        float rl, rr;
        reverb.process (wl, wr, rl, rr);
        wl += rl * revMix;
        wr += rr * revMix;

        const float mix = mixSmooth.getNextValue(), g = levelSmooth.getNextValue();
        L[i] = gg::softClip ((dryL * (1.f - mix) + wl * mix) * g);
        R[i] = gg::softClip ((dryR * (1.f - mix) + wr * mix) * g);
    }

    const auto& disp = padEngine.getDisplay();
    for (int b = 0; b < pad::kDisplayBands; ++b) spectrumDisplay[(size_t) b].store (disp[(size_t) b]);
}

void PoomsePadProcessor::randomise (RandomScope scope)
{
    // Normalised (0..1) ranges that keep the result musical; anything not listed is fully random.
    struct R { const char* id; float lo, hi; };
    static const R ranges[] = {
        { "time", 0.25f, 0.85f },   { "rise", 0.15f, 0.7f },   { "evolve", 0.1f, 0.8f },  { "rate", 0.f, 0.6f },
        { "sub", 0.f, 0.7f },       { "body", 0.5f, 1.f },     { "fifth", 0.f, 0.6f },    { "octave", 0.f, 0.7f },
        { "tone", 0.3f, 0.7f },     { "width", 0.3f, 1.f },    { "revSize", 0.3f, 0.9f }, { "revMix", 0.1f, 0.6f },
    };
    static const char* const padIds[] = { "time", "rise", "evolve", "rate", "sub", "body", "fifth", "octave" };
    static const char* const toneIds[] = { "tone", "width", "revSize", "revMix" };

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
    auto randomList = [&] (const char* const* ids, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
        {
            float lo = 0.f, hi = 1.f;
            for (const auto& r : ranges)
                if (std::strcmp (r.id, ids[i]) == 0) { lo = r.lo; hi = r.hi; }
            setNormalised (ids[i], lo + (hi - lo) * rng.nextFloat());
        }
    };

    // Mix, Level and Hold are never randomised.
    if (scope == RandomScope::Pad || scope == RandomScope::AllSound) randomList (padIds, std::size (padIds));
    if (scope == RandomScope::Tone || scope == RandomScope::AllSound) randomList (toneIds, std::size (toneIds));

    if (scope == RandomScope::Mod)
    {
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

void PoomsePadProcessor::resetToDefaults()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->getDefaultValue());
            rp->endChangeGesture();
        }
    for (int sIdx = 0; sIdx < gg::kNumSources; ++sIdx)
        for (int d = 0; d < gg::kNumDests; ++d)
            setMatrix (sIdx, d, 0.f);
}

juce::AudioProcessorEditor* PoomsePadProcessor::createEditor() { return new PoomsePadEditor (*this); }

void PoomsePadProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    apvts.state.setProperty ("matrix", serialiseMatrix(), nullptr);
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void PoomsePadProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            restoreMatrix (apvts.state.getProperty ("matrix").toString());
        }
}

juce::String PoomsePadProcessor::serialiseMatrix() const
{
    juce::StringArray parts;
    for (int i = 0; i < gg::kNumSources * gg::kNumDests; ++i)
        parts.add (juce::String (matrix[(size_t) i].load(), 3));
    return parts.joinIntoString (" ");
}

void PoomsePadProcessor::restoreMatrix (const juce::String& text)
{
    if (text.isEmpty()) return;
    juce::StringArray parts;
    parts.addTokens (text, " ", "");
    parts.removeEmptyStrings();
    if (parts.size() != gg::kNumSources * gg::kNumDests) return;
    for (int i = 0; i < parts.size(); ++i)
        matrix[(size_t) i].store (juce::jlimit (-1.f, 1.f, parts[i].getFloatValue()));
    ++matrixVersion;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PoomsePadProcessor(); }
