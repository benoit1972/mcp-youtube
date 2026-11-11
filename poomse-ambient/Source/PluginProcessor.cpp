#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using Range = juce::NormalisableRange<float>;
enum { pShort, pLong, pPad, pField, pScale, pKey, pJourney, pSilence, pDepth, pSurprise, pBars, pSeed, pReverb, pLevel, pGShort, pGLong, pGPad, pGField, pGSize, pScatter, pMove, pRate, numRaw };
const char* const kIds[numRaw] = { "short", "long", "pad", "field", "scale", "key", "journey", "silence", "depth", "surprise", "bars", "seed", "reverb", "level", "gshort", "glong", "gpad", "gfield", "gsize", "scatter", "move", "rate" };
constexpr int kLfoBars[5] = { 1, 2, 4, 8, 16 };
}

juce::AudioProcessorValueTreeState::ParameterLayout PoomseAmbientProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* n) { return juce::ParameterID { n, 1 }; };
    auto attr = [] (auto fn) { return juce::AudioParameterFloatAttributes().withStringFromValueFunction (fn); };
    auto pct = [] (float x, int) { return juce::String (juce::roundToInt (x * 100.f)) + " %"; };

    l.add (std::make_unique<F> (id ("short"), "Short", Range (0.f, 1.f, 0.001f), 0.8f, attr (pct)));
    l.add (std::make_unique<F> (id ("long"),  "Long",  Range (0.f, 1.f, 0.001f), 0.7f, attr (pct)));
    l.add (std::make_unique<F> (id ("pad"),   "Pad",   Range (0.f, 1.f, 0.001f), 0.8f, attr (pct)));
    l.add (std::make_unique<F> (id ("field"), "Field", Range (0.f, 1.f, 0.001f), 0.6f, attr (pct)));
    {
        juce::StringArray scales, keys;
        for (int i = 0; i < amb::kNumScales; ++i) scales.add (amb::kScales[i].name);
        for (int i = 0; i < 13; ++i) keys.add (amb::kKeyNames[i]);
        l.add (std::make_unique<juce::AudioParameterChoice> (id ("scale"), "Scale", scales, 1));
        l.add (std::make_unique<juce::AudioParameterChoice> (id ("key"), "Key", keys, 0));
    }
    l.add (std::make_unique<F> (id ("journey"),  "Journey",  Range (0.f, 1.f, 0.001f), 0.7f, attr (pct)));
    l.add (std::make_unique<F> (id ("silence"),  "Silence",  Range (0.f, 1.f, 0.001f), 0.45f, attr (pct)));
    l.add (std::make_unique<F> (id ("depth"),    "Depth",    Range (0.f, 1.f, 0.001f), 0.5f, attr (pct)));
    l.add (std::make_unique<F> (id ("surprise"), "Surprise", Range (0.f, 1.f, 0.001f), 0.3f, attr (pct)));
    l.add (std::make_unique<juce::AudioParameterInt> (id ("bars"), "Length (bars)", 8, 480, 120));
    l.add (std::make_unique<juce::AudioParameterInt> (id ("seed"), "Version (0 = new each time)", 0, 9999, 0));
    l.add (std::make_unique<F> (id ("reverb"), "Reverb", Range (0.f, 1.f, 0.001f), 0.5f, attr (pct)));
    l.add (std::make_unique<F> (id ("level"),  "Level",  Range (-48.f, 6.f, 0.1f), 0.f, attr ([] (float x, int) { return juce::String (x, 1) + " dB"; })));
    l.add (std::make_unique<F> (id ("gshort"), "Grains (short)", Range (0.f, 1.f, 0.001f), 0.f, attr (pct)));
    l.add (std::make_unique<F> (id ("glong"),  "Grains (long)",  Range (0.f, 1.f, 0.001f), 0.3f, attr (pct)));
    l.add (std::make_unique<F> (id ("gpad"),   "Grains (pad)",   Range (0.f, 1.f, 0.001f), 0.4f, attr (pct)));
    l.add (std::make_unique<F> (id ("gfield"), "Grains (field)", Range (0.f, 1.f, 0.001f), 0.3f, attr (pct)));
    l.add (std::make_unique<F> (id ("gsize"),  "Grain size", Range (0.f, 1.f, 0.001f), 0.4f, attr ([] (float x, int) { return juce::String (juce::roundToInt (30.f + 370.f * x)) + " ms"; })));
    l.add (std::make_unique<F> (id ("scatter"), "Scatter", Range (0.f, 1.f, 0.001f), 0.3f, attr (pct)));
    l.add (std::make_unique<F> (id ("move"),   "Movement", Range (0.f, 1.f, 0.001f), 0.35f, attr (pct)));
    l.add (std::make_unique<juce::AudioParameterChoice> (id ("rate"), "Movement speed", juce::StringArray { "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" }, 2));
    return l;
}

PoomseAmbientProcessor::PoomseAmbientProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout()),
      seedRng ((uint32_t) juce::Time::currentTimeMillis() | 1u)
{
    for (int i = 0; i < numRaw; ++i) raw[i] = apvts.getRawParameterValue (kIds[i]);
    for (auto& a : activityShown) a.store (0.f);
    buildFactory();
}

PoomseAmbientProcessor::~PoomseAmbientProcessor()
{
    for (int i = 0; i < 1000 && building.load() > 0; ++i) juce::Thread::sleep (10);       // the sounds that are being made finish first
}

// the four sounds are made in the background, one after the other; the piece can start as soon as the first ones are there
void PoomseAmbientProcessor::buildFactory()
{
    building.fetch_add (1);
    juce::Thread::launch ([this]
    {
        for (int s = 0; s < 4; ++s)
        {
            auto b = std::shared_ptr<const amb::Bank> (amb::makeFactory (s, 44100.0));
            {
                const juce::SpinLock::ScopedLockType lock (bankLock);
                factory[s] = b;
            }
            if (userBank[s] == nullptr) setCurrent (s, b);
        }
        building.fetch_sub (1);
    });
}

void PoomseAmbientProcessor::setCurrent (int slot, std::shared_ptr<const amb::Bank> b)
{
    const juce::SpinLock::ScopedLockType lock (bankLock);
    if (current[slot] != nullptr) retired.push_back (std::move (current[slot]));
    current[slot] = std::move (b);
    retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const auto& p) { return p.use_count() == 1; }), retired.end());
}

void PoomseAmbientProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const size_t n = (size_t) std::max (64, samplesPerBlock);
    for (auto* v : { &dryL, &dryR, &sendL, &sendR }) v->assign (n, 0.f);
    orchestra.prepare (sampleRate);
    reverb.prepare (sampleRate); reverb.setSize (0.88f);
    for (auto& a : audioBank) a = nullptr;
    levelS.reset (sampleRate, 0.08); reverbS.reset (sampleRate, 0.08);
    internalBeat = 0.0;
}

juce::Result PoomseAmbientProcessor::readSound (const juce::File& file, int slot, Loaded& out, int attempts)
{
    juce::String why = "CANNOT READ " + file.getFileName().toUpperCase();
    for (int attempt = 0; attempt < std::max (1, attempts); ++attempt)
    {
        if (attempt > 0) juce::Thread::sleep (700);
        std::vector<float> mono; double rate = 0.0; int channels = 0, bits = 0; juce::String format;
        std::vector<float> left, right;
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
                const float* l = tmp.getReadPointer (0);
                const float* r = tmp.getReadPointer (reader->numChannels > 1 ? 1 : 0);
                left.assign (l, l + len); right.assign (r, r + len);
                rate = reader->sampleRate; channels = (int) reader->numChannels; bits = (int) reader->bitsPerSample; format = reader->getFormatName();
            }
            else
            {
                juce::MemoryBlock bytes;
                if (! file.existsAsFile()) { why = file.getFileName().toUpperCase() + " IS NOT THERE (YET?)"; continue; }
                if (! file.loadFileAsData (bytes)) { why = "CANNOT OPEN " + file.getFileName().toUpperCase() + " (NO PERMISSION? OR STILL ARRIVING FROM ICLOUD?)"; continue; }
                const auto aiff = amb::parseAiff (static_cast<const uint8_t*> (bytes.getData()), bytes.getSize());
                if (! aiff.ok)
                {
                    why = "CANNOT READ " + file.getFileName().toUpperCase() + ": " + juce::String (aiff.why) + "  [" + juce::String ((juce::int64) bytes.getSize()) + " BYTES]";
                    if (bytes.getSize() == 0) continue;
                    return juce::Result::fail (why);
                }
                left = aiff.mono; right = aiff.mono;
                rate = aiff.sampleRate; channels = aiff.channels; bits = aiff.bits; format = juce::String (aiff.format);
            }
        }
        if (left.size() < 2048) return juce::Result::fail (file.getFileName().toUpperCase() + " IS TOO SHORT");
        int end = (int) left.size();
        while (end > 1000 && std::abs (left[(size_t) end - 1]) < 0.002f && std::abs (right[(size_t) end - 1]) < 0.002f) --end;
        left.resize ((size_t) end); right.resize ((size_t) end);
        const double seconds = (double) end / rate;

        auto b = std::make_shared<amb::Bank>();
        amb::Zone z; z.sampleRate = rate; z.x[0] = std::move (left); z.x[1] = std::move (right);
        const int cat = amb::kSoundCategory[std::clamp (slot, 0, (int) amb::numFactory - 1)];
        b->category = cat;
        juce::String kind;
        if (cat == amb::CatField)                        // a place: let in by the keys, and it loops
        {
            b->pitched = false; z.root = 60;
            const int xf = (int) std::min (2.0 * rate, 0.3 * (double) end), n = end - xf;
            amb::detail::wrapLoop (z, n, xf);
            kind = "A PLACE, LOOPED";
        }
        else
        {
            std::vector<float> m (z.x[0].size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (z.x[0][i] + z.x[1][i]);
            z.root = amb::detectRootNote (m, rate, cat == amb::CatPad ? 48 : 60);
            b->pitched = true;
            if (cat != amb::CatShort)                    // held: it loops while the key is down
            {
                const int a = (int) (0.35 * (double) end);
                amb::detail::makeLoop (z, a, end, (int) std::max (16.0, std::min (0.5 * rate, 0.6 * (double) a)));
                kind = "HELD, ";
            }
            else kind = "SHORT, ";
            static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            kind += juce::String (names[((z.root % 12) + 12) % 12]) + juce::String (z.root / 12 - 1);
        }
        z.x[0].shrink_to_fit(); z.x[1].shrink_to_fit();
        b->zones.push_back (std::move (z));
        out.bank = b;
        out.info = file.getFileName().toUpperCase() + " - " + format.toUpperCase() + " - " + juce::String (seconds, 1) + " S - " + juce::String (channels) + " CH - "
                   + juce::String (juce::roundToInt (rate)) + " HZ - " + juce::String (bits) + " BIT - " + kind;
        return juce::Result::ok();
    }
    return juce::Result::fail (why);
}

juce::Result PoomseAmbientProcessor::installSound (int slot, const Loaded& loaded, const juce::File& file)
{
    if (loaded.bank == nullptr || slot < 0 || slot >= 4) return juce::Result::fail ("NOTHING TO LOAD");
    userBank[slot] = loaded.bank;
    apvts.state.setProperty ("file" + juce::String (slot), file.getFullPathName(), nullptr);
    setCurrent (slot, userBank[slot]);
    return juce::Result::ok();
}

juce::Result PoomseAmbientProcessor::loadSound (int slot, const juce::File& file)
{
    Loaded l;
    const auto r = readSound (file, slot, l, 1);
    return r.wasOk() ? installSound (slot, l, file) : r;
}

void PoomseAmbientProcessor::restoreOriginal (int slot)
{
    if (slot < 0 || slot >= 4) return;
    userBank[slot] = nullptr;
    apvts.state.removeProperty ("file" + juce::String (slot), nullptr);
    std::shared_ptr<const amb::Bank> f;
    { const juce::SpinLock::ScopedLockType lock (bankLock); f = factory[slot]; }
    if (f != nullptr) setCurrent (slot, f);
}

juce::String PoomseAmbientProcessor::customName (int slot) const
{
    return juce::File (apvts.state.getProperty ("file" + juce::String (slot)).toString()).getFileName();
}

void PoomseAmbientProcessor::reroll()
{
    if (auto* p = apvts.getParameter ("seed"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) (1 + juce::Random::getSystemRandom().nextInt (9999))));
        p->endChangeGesture();
    }
}

void PoomseAmbientProcessor::freeVersion()
{
    if (auto* p = apvts.getParameter ("seed")) { p->beginChangeGesture(); p->setValueNotifyingHost (0.f); p->endChangeGesture(); }
}

// new settings and a new version, within what sounds good
void PoomseAmbientProcessor::randomise()
{
    juce::Random rng;
    auto set = [this] (const char* id, float v) { if (auto* q = apvts.getParameter (id)) { q->beginChangeGesture(); q->setValueNotifyingHost (q->convertTo0to1 (v)); q->endChangeGesture(); } };
    auto between = [&] (float a, float b) { return a + (b - a) * rng.nextFloat(); };
    set ("scale", (float) rng.nextInt (amb::kNumScales));
    set ("silence", between (0.2f, 0.8f)); set ("depth", between (0.1f, 0.9f)); set ("surprise", between (0.1f, 0.6f)); set ("journey", between (0.4f, 0.95f));
    set ("short", between (0.5f, 0.95f)); set ("long", between (0.3f, 0.85f)); set ("pad", between (0.5f, 0.95f)); set ("field", between (0.2f, 0.8f));
    set ("gshort", rng.nextFloat() < 0.5f ? 0.f : between (0.f, 0.5f)); set ("glong", between (0.f, 0.7f)); set ("gpad", between (0.1f, 0.8f)); set ("gfield", between (0.f, 0.7f));
    set ("gsize", between (0.1f, 0.9f)); set ("scatter", between (0.1f, 0.7f)); set ("move", between (0.1f, 0.7f)); set ("rate", (float) rng.nextInt (5));
    reroll();
}

void PoomseAmbientProcessor::resetToDefaults()
{
    for (auto* q : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (q))
        { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->getDefaultValue()); rp->endChangeGesture(); }
    for (int i = 0; i < 4; ++i) restoreOriginal (i);
    clearRequest.store (true);
}

void PoomseAmbientProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    if (buffer.getNumChannels() < 2) return;
    {
        const juce::SpinLock::ScopedTryLockType lock (bankLock);
        if (lock.isLocked())
            for (int s = 0; s < 4; ++s)
                if (current[s] != audioBank[s]) { audioBank[s] = current[s]; orchestra.setBank (s, audioBank[s].get()); }
    }
    if (clearRequest.exchange (false)) orchestra.killAll();

    // host timing: the position in beats drives the whole piece (when the host is stopped, time runs by itself so that the keys can be played live)
    double bpm = 120.0, hostBeat = -1.0;
    int beatsPerBar = 4;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto ppq = pos->getPpqPosition()) if (pos->getIsPlaying()) hostBeat = *ppq;
            if (auto ts = pos->getTimeSignature()) beatsPerBar = std::max (1, ts->numerator * 4 / std::max (1, ts->denominator));
        }
    const int n = buffer.getNumSamples();
    const double beatStart = hostBeat >= 0.0 ? hostBeat : internalBeat;
    const double spb = currentSampleRate * 60.0 / std::max (20.0, bpm);
    internalBeat = beatStart + (double) n / spb;

    amb::GlobalParams gp;
    gp.scale = (int) raw[pScale]->load(); gp.key = (int) raw[pKey]->load() - 1; gp.journey = raw[pJourney]->load(); gp.silence = raw[pSilence]->load();
    gp.depth = raw[pDepth]->load(); gp.surprise = raw[pSurprise]->load(); gp.pieceBars = (int) raw[pBars]->load(); gp.beatsPerBar = beatsPerBar;
    amb::LayerParams lps[amb::kNumLayers];
    for (int l = 0; l < amb::kNumLayers; ++l) lps[l].amount = raw[amb::kLayerSlot[l]]->load();     // short, long, pad, field: slots 0..3 are the first four parameters

    amb::Texture tex;
    for (int k = 0; k < 4; ++k) tex.grain[k] = raw[pGShort + k]->load();
    tex.size = raw[pGSize]->load(); tex.scatter = raw[pScatter]->load(); tex.lfoDepth = raw[pMove]->load(); tex.lfoBars = kLfoBars[std::clamp ((int) raw[pRate]->load(), 0, 4)];
    const int pv = previewMask.exchange (0);
    for (int s = 0; s < 4; ++s) if (pv & (1 << s)) orchestra.preview (s, s == 2 ? 48 : s == 1 ? 67 : 60);

    // MIDI: a held note is the key and the length of the piece; its release ends it
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const double beat = beatStart + (double) meta.samplePosition / spb;
        if (m.isNoteOn())
        {
            int s = (int) raw[pSeed]->load();
            if (s <= 0) s = 1 + (int) (seedRng.nextU32() % 9999u);
            seedInUse.store (s);
            orchestra.noteOn (m.getNoteNumber(), (uint32_t) s, beat, gp);
        }
        else if (m.isNoteOff())                          orchestra.noteOff (m.getNoteNumber(), beat);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) orchestra.allNotesOff();
    }

    reverbS.setTargetValue (raw[pReverb]->load());
    levelS.setTargetValue (juce::Decibels::decibelsToGain (raw[pLevel]->load()));
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    const int chunk = (int) dryL.size();
    for (int off = 0; off < n; off += chunk)
    {
        const int c = std::min (chunk, n - off);
        for (auto* v : { &dryL, &dryR, &sendL, &sendR }) std::fill (v->begin(), v->begin() + c, 0.f);
        orchestra.process (c, beatStart + (double) off / spb, bpm, gp, lps, tex, dryL.data(), dryR.data(), sendL.data(), sendR.data());
        for (int i = 0; i < c; ++i)
        {
            float rl, rr;
            reverb.process (sendL[(size_t) i], sendR[(size_t) i], rl, rr);
            const float rv = reverbS.getNextValue() * 1.4f, g = levelS.getNextValue();
            L[off + i] = std::tanh ((dryL[(size_t) i] + rl * rv) * g);
            R[off + i] = std::tanh ((dryR[(size_t) i] + rr * rv) * g);
        }
    }

    // what the interface shows
    running.store (orchestra.pieceRunning());
    progressShown.store (orchestra.progress());
    chordRootShown.store (orchestra.chordRoot());
    chordQualityShown.store (orchestra.chordDim() ? 2 : orchestra.chordMinor() ? 1 : 0);          // 2: neither major nor minor
    moodShown.store (orchestra.mood());
    for (int s = 0; s < 4; ++s)
    {
        float a = 0.f;
        for (int l = 0; l < amb::kNumLayers; ++l) if (amb::kLayerSlot[l] == s) a = std::max (a, orchestra.layerActivity (l));
        activityShown[(size_t) s].store (a);
    }
}

juce::AudioProcessorEditor* PoomseAmbientProcessor::createEditor() { return new PoomseAmbientEditor (*this); }

void PoomseAmbientProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void PoomseAmbientProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            for (int k = 0; k < 4; ++k)
            {
                const auto path = apvts.state.getProperty ("file" + juce::String (k)).toString();
                if (path.isNotEmpty() && juce::File (path).existsAsFile()) loadSound (k, juce::File (path));
                else { apvts.state.removeProperty ("file" + juce::String (k), nullptr); restoreOriginal (k); }
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PoomseAmbientProcessor(); }
