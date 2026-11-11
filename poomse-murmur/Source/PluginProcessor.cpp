#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using Range = juce::NormalisableRange<float>;
enum { pScale, pKey, pSwarm, pWild, pSpeed, pRange, pJourney, pBars, pSeed, pSpace, pLevel, pSound, numRaw };
const char* const kIds[numRaw] = { "scale", "key", "swarm", "wild", "speed", "range", "journey", "bars", "seed", "space", "level", "sound" };
}

juce::AudioProcessorValueTreeState::ParameterLayout PoomseMurmurProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* n) { return juce::ParameterID { n, 1 }; };
    auto attr = [] (auto fn) { return juce::AudioParameterFloatAttributes().withStringFromValueFunction (fn); };
    auto pct = [] (float x, int) { return juce::String (juce::roundToInt (x * 100.f)) + " %"; };
    {
        juce::StringArray scales, keys;
        for (int i = 0; i < mur::kNumScales; ++i) scales.add (mur::kScales[i].name);
        for (int i = 0; i < 13; ++i) keys.add (mur::kKeyNames[i]);
        l.add (std::make_unique<juce::AudioParameterChoice> (id ("scale"), "Scale", scales, 1));
        l.add (std::make_unique<juce::AudioParameterChoice> (id ("key"), "Key", keys, 0));
    }
    l.add (std::make_unique<juce::AudioParameterInt> (id ("swarm"), "Swarm", 1, 8, 5));
    l.add (std::make_unique<F> (id ("wild"),    "Wildness", Range (0.f, 1.f, 0.001f), 0.45f, attr (pct)));
    l.add (std::make_unique<F> (id ("speed"),   "Speed",    Range (0.f, 1.f, 0.001f), 0.5f, attr (pct)));
    l.add (std::make_unique<F> (id ("range"),   "Range",    Range (0.f, 1.f, 0.001f), 0.5f, attr (pct)));
    l.add (std::make_unique<F> (id ("journey"), "Journey",  Range (0.f, 1.f, 0.001f), 0.6f, attr (pct)));
    l.add (std::make_unique<juce::AudioParameterInt> (id ("bars"), "Length (bars)", 8, 480, 96));
    l.add (std::make_unique<juce::AudioParameterInt> (id ("seed"), "Version (0 = new each time)", 0, 9999, 0));
    l.add (std::make_unique<F> (id ("space"),   "Space",    Range (0.f, 1.f, 0.001f), 0.6f, attr (pct)));
    l.add (std::make_unique<F> (id ("level"),   "Level",    Range (-48.f, 6.f, 0.1f), 0.f, attr ([] (float x, int) { return juce::String (x, 1) + " dB"; })));
    l.add (std::make_unique<juce::AudioParameterChoice> (id ("sound"), "Sound", juce::StringArray { "Glass", "Violin" }, 0));
    return l;
}

PoomseMurmurProcessor::PoomseMurmurProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout()),
      seedRng ((uint32_t) juce::Time::currentTimeMillis() | 1u)
{
    for (int i = 0; i < numRaw; ++i) raw[i] = apvts.getRawParameterValue (kIds[i]);
    buildBuiltIns();
}

PoomseMurmurProcessor::~PoomseMurmurProcessor()
{
    for (int i = 0; i < 1000 && building.load() > 0; ++i) juce::Thread::sleep (10);
}

// the two built-in sounds are made in the background
void PoomseMurmurProcessor::buildBuiltIns()
{
    building.fetch_add (1);
    juce::Thread::launch ([this]
    {
        for (int w = 0; w < 2; ++w)
        {
            auto z = std::shared_ptr<const mur::Zone> (mur::makeBuiltIn (w, 44100.0));
            { const juce::SpinLock::ScopedLockType lock (zoneLock); builtIn[w] = z; }
        }
        building.fetch_sub (1);
    });
}

void PoomseMurmurProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const size_t n = (size_t) std::max (64, samplesPerBlock);
    for (auto* v : { &dryL, &dryR, &sendL, &sendR }) v->assign (n, 0.f);
    murmur.prepare (sampleRate);
    reverb.prepare (sampleRate); reverb.setSize (0.9f);
    audioZone = nullptr;
    levelS.reset (sampleRate, 0.08); reverbS.reset (sampleRate, 0.08);
    internalBeat = 0.0;
}

juce::Result PoomseMurmurProcessor::readSound (const juce::File& file, Loaded& out, int attempts)
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
                const auto aiff = mur::parseAiff (static_cast<const uint8_t*> (bytes.getData()), bytes.getSize());
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

        std::vector<float> m (left.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (left[i] + right[i]);
        const int root = mur::detectRootNote (m, rate, 60);
        auto zone = std::make_shared<mur::Zone> (mur::detail::makeSustained (std::move (left), std::move (right), rate, root));
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const juce::String kind = juce::String (names[((root % 12) + 12) % 12]) + juce::String (root / 12 - 1);
        out.zone = zone;
        out.info = file.getFileName().toUpperCase() + " - " + format.toUpperCase() + " - " + juce::String (seconds, 1) + " S - " + juce::String (channels) + " CH - "
                   + juce::String (juce::roundToInt (rate)) + " HZ - " + juce::String (bits) + " BIT - SUNG AT " + kind;
        return juce::Result::ok();
    }
    return juce::Result::fail (why);
}

juce::Result PoomseMurmurProcessor::installSound (const Loaded& loaded, const juce::File& file)
{
    if (loaded.zone == nullptr) return juce::Result::fail ("NOTHING TO LOAD");
    { const juce::SpinLock::ScopedLockType lock (zoneLock); if (userZone != nullptr) retired.push_back (userZone); userZone = loaded.zone;
      retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const auto& p) { return p.use_count() == 1; }), retired.end()); }
    apvts.state.setProperty ("file", file.getFullPathName(), nullptr);
    return juce::Result::ok();
}

juce::Result PoomseMurmurProcessor::loadSound (const juce::File& file)
{
    Loaded l;
    const auto r = readSound (file, l, 1);
    return r.wasOk() ? installSound (l, file) : r;
}

void PoomseMurmurProcessor::restoreOriginal()
{
    { const juce::SpinLock::ScopedLockType lock (zoneLock); if (userZone != nullptr) retired.push_back (userZone); userZone = nullptr; }
    apvts.state.removeProperty ("file", nullptr);
}

juce::String PoomseMurmurProcessor::customName() const
{
    return juce::File (apvts.state.getProperty ("file").toString()).getFileName();
}

void PoomseMurmurProcessor::reroll()
{
    if (auto* p = apvts.getParameter ("seed"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) (1 + juce::Random::getSystemRandom().nextInt (9999))));
        p->endChangeGesture();
    }
}

void PoomseMurmurProcessor::freeVersion()
{
    if (auto* p = apvts.getParameter ("seed")) { p->beginChangeGesture(); p->setValueNotifyingHost (0.f); p->endChangeGesture(); }
}

void PoomseMurmurProcessor::randomise()
{
    juce::Random rng;
    auto set = [this] (const char* id, float v) { if (auto* q = apvts.getParameter (id)) { q->beginChangeGesture(); q->setValueNotifyingHost (q->convertTo0to1 (v)); q->endChangeGesture(); } };
    auto between = [&] (float a, float b) { return a + (b - a) * rng.nextFloat(); };
    set ("scale", (float) rng.nextInt (mur::kNumScales));
    set ("swarm", (float) (3 + rng.nextInt (6))); set ("wild", between (0.15f, 0.9f)); set ("speed", between (0.25f, 0.85f));
    set ("range", between (0.3f, 0.95f)); set ("journey", between (0.3f, 0.9f)); set ("space", between (0.35f, 0.85f));
    reroll();
}

void PoomseMurmurProcessor::resetToDefaults()
{
    for (auto* q : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (q))
        { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->getDefaultValue()); rp->endChangeGesture(); }
    restoreOriginal();
    clearRequest.store (true);
}

void PoomseMurmurProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    if (buffer.getNumChannels() < 2) return;
    {
        const juce::SpinLock::ScopedTryLockType lock (zoneLock);
        if (lock.isLocked())
        {
            std::shared_ptr<const mur::Zone> z = userZone != nullptr ? userZone : builtIn[std::clamp ((int) raw[pSound]->load(), 0, 1)];
            if (z == nullptr) z = builtIn[0] != nullptr ? builtIn[0] : builtIn[1];
            if (z != nullptr && z != audioZone) { audioZone = z; murmur.setZone (audioZone.get()); }
        }
    }
    if (clearRequest.exchange (false)) murmur.killAll();
    if (audioZone == nullptr) return;

    // host timing (when the host is stopped, time runs by itself so that the keys can be played live)
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

    mur::FlockParams fp;
    fp.scale = (int) raw[pScale]->load(); fp.key = (int) raw[pKey]->load() - 1; fp.voices = (int) raw[pSwarm]->load();
    fp.wild = raw[pWild]->load(); fp.speed = raw[pSpeed]->load(); fp.range = raw[pRange]->load(); fp.journey = raw[pJourney]->load();
    fp.pieceBars = (int) raw[pBars]->load(); fp.beatsPerBar = beatsPerBar; fp.rootNote = audioZone->root;
    rootShown.store (audioZone->root);

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const double beat = beatStart + (double) meta.samplePosition / spb;
        if (m.isNoteOn())
        {
            int s = (int) raw[pSeed]->load();
            if (s <= 0) s = 1 + (int) (seedRng.nextU32() % 9999u);
            seedInUse.store (s);
            murmur.noteOn (m.getNoteNumber(), (uint32_t) s, beat, fp);
        }
        else if (m.isNoteOff())                          murmur.noteOff (m.getNoteNumber(), beat);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) murmur.allNotesOff();
    }

    reverbS.setTargetValue (raw[pSpace]->load());
    levelS.setTargetValue (juce::Decibels::decibelsToGain (raw[pLevel]->load()));
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    const int chunk = (int) dryL.size();
    for (int off = 0; off < n; off += chunk)
    {
        const int c = std::min (chunk, n - off);
        for (auto* v : { &dryL, &dryR, &sendL, &sendR }) std::fill (v->begin(), v->begin() + c, 0.f);
        murmur.process (c, beatStart + (double) off / spb, bpm, fp, dryL.data(), dryR.data(), sendL.data(), sendR.data());
        for (int i = 0; i < c; ++i)
        {
            float rl, rr;
            reverb.process (sendL[(size_t) i], sendR[(size_t) i], rl, rr);
            const float rv = reverbS.getNextValue() * 1.5f, g = levelS.getNextValue();
            L[off + i] = std::tanh ((dryL[(size_t) i] + rl * rv) * g);
            R[off + i] = std::tanh ((dryR[(size_t) i] + rr * rv) * g);
        }
    }
    progressShown.store (murmur.progress (beatStart + (double) n / spb, fp));
}

juce::AudioProcessorEditor* PoomseMurmurProcessor::createEditor() { return new PoomseMurmurEditor (*this); }

void PoomseMurmurProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void PoomseMurmurProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            const auto path = apvts.state.getProperty ("file").toString();
            if (path.isNotEmpty() && juce::File (path).existsAsFile() && loadSound (juce::File (path)).wasOk()) return;
            apvts.state.removeProperty ("file", nullptr);
            restoreOriginal();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PoomseMurmurProcessor(); }
