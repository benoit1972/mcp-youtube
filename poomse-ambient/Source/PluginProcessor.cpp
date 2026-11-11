#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using Range = juce::NormalisableRange<float>;
enum { pSound, pTone, pBloom, pTail, pLoops, pChord, pSpace, pLevel, pLatch, numRaw };
const char* const kIds[numRaw] = { "sound", "tone", "bloom", "tail", "loops", "chord", "space", "level", "latch" };
}

juce::AudioProcessorValueTreeState::ParameterLayout PoomseAmbientProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* n) { return juce::ParameterID { n, 1 }; };
    auto attr = [] (auto fn) { return juce::AudioParameterFloatAttributes().withStringFromValueFunction (fn); };
    auto pct = [] (float x, int) { return juce::String (juce::roundToInt (x * 100.f)) + " %"; };

    l.add (std::make_unique<juce::AudioParameterChoice> (id ("sound"), "Sound", juce::StringArray { "Piano", "Violin", "Pad", "Forest" }, 0));
    l.add (std::make_unique<F> (id ("tone"),  "Tone",  Range (0.f, 1.f, 0.001f), 0.85f, attr (pct)));
    l.add (std::make_unique<F> (id ("bloom"), "Bloom", Range (0.f, 1.f, 0.001f), 0.f, attr ([] (float x, int) { return x < 0.02f ? juce::String ("at once") : juce::String (x * x * 3.f, 1) + " s"; })));
    l.add (std::make_unique<F> (id ("tail"),  "Tail",  Range (0.f, 1.f, 0.001f), 0.5f, attr ([] (float x, int) { return juce::String (0.08f * std::pow (75.f, x), 1) + " s"; })));
    l.add (std::make_unique<F> (id ("loops"), "Loops", Range (0.f, 1.f, 0.001f), 0.25f, attr (pct)));
    l.add (std::make_unique<juce::AudioParameterChoice> (id ("chord"), "Chord", juce::StringArray { "Off", "Octave", "Fifth", "Major", "Minor", "Sus" }, 0));
    l.add (std::make_unique<F> (id ("space"), "Space", Range (0.f, 1.f, 0.001f), 0.4f, attr (pct)));
    l.add (std::make_unique<F> (id ("level"), "Level", Range (-48.f, 6.f, 0.1f), 0.f, attr ([] (float x, int) { return juce::String (x, 1) + " dB"; })));
    l.add (std::make_unique<juce::AudioParameterBool> (id ("latch"), "Latch", false));
    return l;
}

PoomseAmbientProcessor::PoomseAmbientProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int i = 0; i < numRaw; ++i) raw[i] = apvts.getRawParameterValue (kIds[i]);
    apvts.addParameterListener ("sound", this);
    buildSound (0);                                   // the first sound is ready when the window opens
}

PoomseAmbientProcessor::~PoomseAmbientProcessor()
{
    apvts.removeParameterListener ("sound", this);
    cancelPendingUpdate();
    for (int i = 0; i < 500 && building.load() > 0; ++i) juce::Thread::sleep (10);       // a sound that is being made finishes first
}

amb::SamplerParams PoomseAmbientProcessor::readParams() const
{
    amb::SamplerParams p;
    p.tone = raw[pTone]->load(); p.bloom = raw[pBloom]->load(); p.tail = raw[pTail]->load(); p.loops = raw[pLoops]->load();
    p.chord = (int) raw[pChord]->load(); p.latch = raw[pLatch]->load() > 0.5f;
    return p;
}

void PoomseAmbientProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    sampler.prepare (sampleRate); echo.prepare (sampleRate); reverb.prepare (sampleRate);
    audioBank = nullptr;
    internalBeat = 0.0;
    levelS.reset (sampleRate, 0.05);
}

void PoomseAmbientProcessor::publish (std::shared_ptr<const amb::Bank> b)
{
    View v;
    if (! b->zones.empty())
    {
        const auto& z = b->zoneFor (60);
        constexpr int bins = 360;
        v.peaks.assign ((size_t) bins, 0.f);
        const int n = z.length();
        float top = 1e-6f;
        for (int i = 0; i < n; ++i)
        {
            const auto k = (size_t) ((juce::int64) i * bins / n);
            v.peaks[k] = std::max (v.peaks[k], std::max (std::abs (z.x[0][(size_t) i]), std::abs (z.x[1][(size_t) i])));
            top = std::max (top, v.peaks[k]);
        }
        for (auto& p : v.peaks) p /= top;
        v.loops = z.loops(); v.loopStart = z.loops() ? (float) z.loopStart / (float) n : 0.f;
    }
    {
        const juce::SpinLock::ScopedLockType lock (viewLock);
        view = std::move (v);
    }
    const juce::SpinLock::ScopedLockType lock (kitLock);
    if (bank != nullptr) retired.push_back (std::move (bank));
    bank = std::move (b);
    retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const auto& p) { return p.use_count() == 1; }), retired.end());
    viewVersion.fetch_add (1);
}

PoomseAmbientProcessor::View PoomseAmbientProcessor::getView() const
{
    const juce::SpinLock::ScopedLockType lock (viewLock);
    return view;
}

// building a sound takes a moment (it is made, not loaded): in the background, and only the last one asked for is put in place
void PoomseAmbientProcessor::buildSound (int index)
{
    wanted.store (index);
    if (index < 0 || index >= amb::numFactory) index = 0;
    if (userBank[index] != nullptr) { publish (userBank[index]); return; }
    {
        const juce::ScopedLock sl (cacheLock);
        auto it = cache.find (index);
        if (it != cache.end()) { publish (it->second); return; }
    }
    building.fetch_add (1);
    const double sr = 44100.0;
    if (bank == nullptr)                              // the very first one: now
    {
        auto b = std::shared_ptr<const amb::Bank> (amb::makeFactory (index, sr));
        { const juce::ScopedLock sl (cacheLock); cache[index] = b; }
        building.fetch_sub (1);
        publish (b);
        return;
    }
    juce::Thread::launch ([this, index, sr]
    {
        auto b = std::shared_ptr<const amb::Bank> (amb::makeFactory (index, sr));
        { const juce::ScopedLock sl (cacheLock); cache[index] = b; }
        if (wanted.load() == index) publish (b);
        building.fetch_sub (1);
    });
}

void PoomseAmbientProcessor::handleAsyncUpdate() { buildSound (getSoundIndex()); }

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
        if (cat == amb::Field)                        // a place: let in by the keys, and it loops
        {
            b->pitched = false; z.root = 60;
            const int xf = (int) std::min (2.0 * rate, 0.3 * (double) end), n = end - xf;
            amb::detail::wrapLoop (z, n, xf);
            kind = "A PLACE, LOOPED";
        }
        else
        {
            std::vector<float> m (z.x[0].size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (z.x[0][i] + z.x[1][i]);
            z.root = amb::detectRootNote (m, rate, cat == amb::Pad ? 48 : 60);
            b->pitched = true;
            if (cat != amb::Short)                    // held: it loops while the key is down
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
    if (loaded.bank == nullptr || slot < 0 || slot >= amb::numFactory) return juce::Result::fail ("NOTHING TO LOAD");
    userBank[slot] = loaded.bank;
    apvts.state.setProperty ("file" + juce::String (slot), file.getFullPathName(), nullptr);
    if (auto* p = apvts.getParameter ("sound")) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 ((float) slot)); p->endChangeGesture(); }
    wanted.store (slot);
    publish (userBank[slot]);
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
    if (slot < 0 || slot >= amb::numFactory) return;
    userBank[slot] = nullptr;
    apvts.state.removeProperty ("file" + juce::String (slot), nullptr);
    if (getSoundIndex() == slot) buildSound (slot);
}

juce::String PoomseAmbientProcessor::customName (int slot) const
{
    return juce::File (apvts.state.getProperty ("file" + juce::String (slot)).toString()).getFileName();
}

void PoomseAmbientProcessor::uiNote (int note, bool on)
{
    int s1, n1, s2, n2;
    uiFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) { uiNotes[(size_t) s1] = { note, on }; uiFifo.finishedWrite (1); }
}

void PoomseAmbientProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    if (buffer.getNumChannels() < 2) return;
    {
        const juce::SpinLock::ScopedTryLockType lock (kitLock);
        if (lock.isLocked() && bank != audioBank) { audioBank = bank; sampler.setBank (audioBank.get()); }
    }
    if (clearRequest.exchange (false)) sampler.killAll();
    if (audioBank == nullptr) return;

    double bpm = 120.0, hostBeat = -1.0;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto ppq = pos->getPpqPosition()) hostBeat = *ppq;
            if (! pos->getIsPlaying()) hostBeat = -1.0;
        }
    const int n = buffer.getNumSamples();
    const double spq = currentSampleRate * 60.0 / std::max (20.0, bpm);
    const double beatStart = hostBeat >= 0.0 ? hostBeat : internalBeat;
    internalBeat = beatStart + (double) n / spq;

    const amb::SamplerParams p = readParams();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    int pos = 0;
    auto render = [&] (int upTo)
    {
        if (upTo > pos) { sampler.process (upTo - pos, p, beatStart + (double) pos / spq, bpm, L + pos, R + pos); pos = upTo; }
    };
    {
        int s1, n1, s2, n2;
        uiFifo.prepareToRead (uiFifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) { const auto u = uiNotes[(size_t) (s1 + i)]; if (u.on) sampler.noteOn (u.note, 0.8f, p, beatStart); else sampler.noteOff (u.note); }
        for (int i = 0; i < n2; ++i) { const auto u = uiNotes[(size_t) (s2 + i)]; if (u.on) sampler.noteOn (u.note, 0.8f, p, beatStart); else sampler.noteOff (u.note); }
        uiFifo.finishedRead (n1 + n2);
    }
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        render (std::clamp (meta.samplePosition, 0, n));
        const double beatNow = beatStart + (double) pos / spq;
        if (m.isNoteOn())        sampler.noteOn (m.getNoteNumber(), m.getFloatVelocity(), p, beatNow);
        else if (m.isNoteOff())  sampler.noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) sampler.allNotesOff();
    }
    render (n);

    const float space = raw[pSpace]->load();
    const float echoTime = (float) (spq * 0.75);
    levelS.setTargetValue (juce::Decibels::decibelsToGain (raw[pLevel]->load()));
    for (int i = 0; i < n; ++i)
    {
        float el = L[i], er = R[i];
        echo.process (el, er, echoTime, 0.4f, 0.3f * space);
        float rl = 0.f, rr = 0.f;
        reverb.process (el, er, rl, rr);
        const float g = levelS.getNextValue();
        L[i] = (el + rl * space * 1.1f) * g;
        R[i] = (er + rr * space * 1.1f) * g;
    }
}

void PoomseAmbientProcessor::resetToDefaults()
{
    for (auto* q : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (q))
        { rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->getDefaultValue()); rp->endChangeGesture(); }
    for (int i = 0; i < amb::numFactory; ++i) restoreOriginal (i);
    clearRequest.store (true);
    buildSound (0);
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
            for (int k = 0; k < amb::numFactory; ++k)
            {
                userBank[k] = nullptr;
                const auto path = apvts.state.getProperty ("file" + juce::String (k)).toString();
                if (path.isNotEmpty() && juce::File (path).existsAsFile()) loadSound (k, juce::File (path));
                else apvts.state.removeProperty ("file" + juce::String (k), nullptr);
            }
            triggerAsyncUpdate();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PoomseAmbientProcessor(); }
