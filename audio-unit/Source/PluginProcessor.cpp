#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    return l;
}

GlitchGrainProcessor::GlitchGrainProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    publishSource (gg::SourceBuffer::makeDefault());
}

void GlitchGrainProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    fx.prepare (sampleRate);
    level.reset (sampleRate, 0.03);
    level.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param ("level")));
}

bool GlitchGrainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void GlitchGrainProcessor::publishSource (std::shared_ptr<const gg::SourceBuffer> s)
{
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

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    const int n = buffer.getNumSamples();
    const auto& src = *audioSource;

    int pos = 0;
    auto render = [&] (int end)
    {
        if (end > pos) engine.render (L + pos, R + pos, end - pos, src, gp);
        pos = std::max (pos, end);
    };

    for (const auto meta : midi)
    {
        render (std::min (meta.samplePosition, n));
        const auto m = meta.getMessage();
        if (m.isNoteOn())                          engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff())                    engine.noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
    }
    render (n);

    level.setTargetValue (juce::Decibels::decibelsToGain (param ("level")));
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];
        fx.process (l, r, fp);
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

juce::AudioProcessorEditor* GlitchGrainProcessor::createEditor() { return new GlitchGrainEditor (*this); }

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
