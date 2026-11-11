#include "PluginEditor.h"

namespace
{
constexpr int kWidth = 780, kHeight = 600, kPad = 28;
constexpr float kInset = 12.f;
const char* const kAudioExt = "wav;aif;aiff;mp3;flac;m4a;ogg;caf";
}

//==============================================================================
SoundView::SoundView (PoomseAmbientProcessor& p) : proc (p) { startTimerHz (30); }

void SoundView::timerCallback()
{
    const int v = proc.getViewVersion();
    if (v != seenVersion) { view = proc.getView(); seenVersion = v; }
    for (int i = 0; i < amb::Sampler::displayVoices; ++i) marks[i] = proc.voicePosition (i);
    if (messageTicks > 0 && --messageTicks == 0) message = {};
    repaint();
}

void SoundView::paint (juce::Graphics& g)
{
    const auto all = getLocalBounds().toFloat();
    g.setColour (look::paper2);
    g.fillRoundedRectangle (all, 6.f);
    const auto m = all.reduced (kInset, kInset + 8.f);
    if (view.loops)
    {
        g.setColour (look::accent.withAlpha (0.07f));
        g.fillRect (m.getX() + view.loopStart * m.getWidth(), m.getY(), (1.f - view.loopStart) * m.getWidth(), m.getHeight());
    }
    g.setColour (look::dim.withAlpha (0.75f));
    if (! view.peaks.empty())
    {
        const float step = m.getWidth() / (float) view.peaks.size(), mid = m.getCentreY();
        for (size_t i = 0; i < view.peaks.size(); ++i)
        {
            const float h = std::max (1.2f, std::sqrt (view.peaks[i]) * m.getHeight() * 0.92f);
            g.fillRect (m.getX() + (float) i * step, mid - h * 0.5f, std::max (1.f, step * 0.72f), h);
        }
    }
    for (float mk : marks)
    {
        if (mk < 0.f) continue;
        g.setColour (look::accent);
        g.fillRect (m.getX() + std::min (1.f, mk) * m.getWidth() - 1.f, m.getY(), 2.f, m.getHeight());
    }
    g.setFont (look::caps (9.5f, true, 0.14f));
    const juce::String head = message.isNotEmpty() ? message : (proc.isBuilding() ? juce::String ("MAKING THE SOUND...") : juce::String ("DROP ANY SOUND ON THE WINDOW TO PLAY IT"));
    g.setColour (message.isNotEmpty() || proc.isBuilding() ? look::accent : look::dim);
    g.drawText (head, all.reduced (kInset, 6.f), juce::Justification::topLeft, true);

    if (dropHover)
    {
        g.setColour (look::paper.withAlpha (0.82f)); g.fillRoundedRectangle (all, 6.f);
        g.setColour (look::accent); g.drawRoundedRectangle (all.reduced (1.f), 6.f, 2.f);
        g.setFont (look::caps (15.f, true, 0.3f));
        g.drawText (dropName.isNotEmpty() ? "LET GO TO LOAD " + dropName.toUpperCase() : juce::String ("DROP A SOUND"), getLocalBounds().reduced (20), juce::Justification::centred, true);
    }
}

//==============================================================================
void SoundPad::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (3.f);
    const bool on = lit || down;
    if (on) { g.setColour (look::accent); g.fillRoundedRectangle (r, 8.f); }
    else { g.setColour (hover ? look::paper2 : look::paper); g.fillRoundedRectangle (r, 8.f); g.setColour (hover ? look::ink : look::rule); g.drawRoundedRectangle (r.reduced (0.5f), 8.f, 1.4f); }
    const auto fg = on ? look::paper : (available ? look::ink : look::dim.withAlpha (0.6f)), dim = on ? look::paper.withAlpha (0.85f) : look::dim;
    g.setColour (fg);
    g.setFont (look::caps (15.f, true, 0.12f));
    g.drawText (label.toUpperCase(), r.reduced (6.f, 0.f).withTrimmedBottom (8.f), juce::Justification::centred, false);
    g.setColour (dim);
    g.setFont (juce::Font (juce::FontOptions (11.f)));
    g.drawText (tagline, r.reduced (6.f, 8.f), juce::Justification::centredBottom, false);
}

//==============================================================================
void PoomseAmbientEditor::makeKnob (const juce::String& id, const juce::String& text, const juce::String& help)
{
    auto* k = knobs.add (new Knob());
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 18);
    k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k->name.setText (text.toUpperCase(), juce::dontSendNotification);
    k->name.setJustificationType (juce::Justification::centred);
    k->name.setFont (look::caps (10.5f));
    k->name.setColour (juce::Label::textColourId, look::dim);
    k->name.setInterceptsMouseClicks (false, false);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    helps.add (help);
    addAndMakeVisible (k->slider); addAndMakeVisible (k->name);
}

PoomseAmbientEditor::PoomseAmbientEditor (PoomseAmbientProcessor& p) : AudioProcessorEditor (&p), proc (p), view (p)
{
    setLookAndFeel (&laf);
    addAndMakeVisible (view);

    for (int i = 0; i <= amb::numFactory; ++i)
    {
        const bool yours = i == amb::numFactory;
        auto* pad = pads.add (new SoundPad (yours ? "Yours" : amb::kSoundNames[i], yours ? "drop a sound" : amb::kSoundTags[i]));
        pad->onDown = [this, i] { choose (i); };
        pad->onUp = [this, i]
        {
            const int note = i == amb::SPad ? 48 : 60;
            proc.uiNote (note, false);
        };
        addAndMakeVisible (pad);
    }
    latchButton.setComponentID ("toggle");
    latchButton.setClickingTogglesState (true);
    latchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "latch", latchButton);
    addAndMakeVisible (latchButton);

    makeKnob ("tone", "Tone", "Dark on the left, open on the right.");
    makeKnob ("bloom", "Bloom", "How slowly a note swells in. Left: at once. Right: three seconds.");
    makeKnob ("tail", "Tail", "How long a note takes to fade after you let go of the key.");
    makeKnob ("loops", "Loops", "A held key says its note again and again, softly, at a period of its own (5, 7, 9... beats). Several keys drift against each other.");
    makeKnob ("chord", "Chord", "A key plays a chord instead of a single note.");
    makeKnob ("space", "Space", "Reverb and a soft echo.");
    makeKnob ("level", "Level", "Output level.");
    for (int i = 0; i < knobs.size(); ++i) knobs[i]->slider.setName ("k" + juce::String (i));

    resetButton.setComponentID ("pill");
    resetButton.onClick = [this]
    {
        if (! resetArmed)
        {
            resetArmed = true; resetButton.setButtonText ("Sure?");
            juce::Timer::callAfterDelay (3000, [safe = juce::Component::SafePointer<PoomseAmbientEditor> (this)] { if (safe != nullptr) safe->disarmReset(); });
        }
        else { proc.resetToDefaults(); disarmReset(); }
    };
    addAndMakeVisible (resetButton);
    setSize (kWidth, kHeight);
    startTimerHz (10);
}

PoomseAmbientEditor::~PoomseAmbientEditor() { stopTimer(); setLookAndFeel (nullptr); }

void PoomseAmbientEditor::choose (int index)
{
    if (index == amb::numFactory && ! proc.hasUserSound()) { view.showMessage ("DROP A SOUND ON THE WINDOW AND IT IS HERE", 6); return; }
    if (auto* prm = proc.apvts.getParameter ("sound"))
    {
        prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) index)); prm->endChangeGesture();
    }
    // a note to hear it (let go with the mouse)
    proc.uiNote (index == amb::SPad ? 48 : 60, true);
}

void PoomseAmbientEditor::timerCallback()
{
    int h = -1;
    for (int i = 0; i < knobs.size(); ++i) if (knobs[i]->slider.isMouseOverOrDragging()) h = i;
    if (h != hovered) { hovered = h; repaint(); }
    const int s = proc.getSoundIndex();
    if (s != shownSound) { shownSound = s; for (int i = 0; i < pads.size(); ++i) pads[i]->setLit (i == s); }
    pads[amb::numFactory]->setAvailable (proc.hasUserSound());
}

void PoomseAmbientEditor::load (const juce::File& file)
{
    view.setDropHover (false);
    view.showMessage ("LOADING " + file.getFileName().toUpperCase() + "...", 60);
    juce::Thread::launch ([safe = juce::Component::SafePointer<PoomseAmbientEditor> (this), file]
    {
        PoomseAmbientProcessor::Loaded loaded;
        const auto read = PoomseAmbientProcessor::readSound (file, loaded, 10);
        juce::MessageManager::callAsync ([safe, file, loaded, read]
        {
            if (safe == nullptr) return;
            const auto result = read.wasOk() ? safe->proc.installSound (loaded, file) : read;
            safe->view.showMessage (result.wasOk() ? "LOADED  " + loaded.info : result.getErrorMessage(), result.wasOk() ? 8 : 15);
        });
    });
}

bool PoomseAmbientEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files) { const juce::File ff (f); if (ff.existsAsFile() || ff.hasFileExtension (kAudioExt)) return true; }
    return false;
}

void PoomseAmbientEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files) { const juce::File ff (f); if (ff.existsAsFile() || ff.hasFileExtension (kAudioExt)) { load (ff); return; } }
    view.setDropHover (false);
    view.showMessage ("NOTHING USABLE WAS DROPPED (A FILE FROM THE FINDER IS EXPECTED)");
}

void PoomseAmbientEditor::disarmReset() { resetArmed = false; resetButton.setButtonText ("Reset"); }

void PoomseAmbientEditor::paint (juce::Graphics& g)
{
    g.fillAll (look::paper);
    const auto word = look::caps (21.f, true, 0.28f);
    g.setFont (word);
    const int w1 = (int) std::ceil (word.getStringWidthFloat ("POOMSE"));
    g.setColour (look::ink);
    g.drawText ("POOMSE", kPad, 16, w1 + 4, 30, juce::Justification::centredLeft, false);
    g.setColour (look::accent);
    g.drawText ("AMBIENT", kPad + w1 - 4, 16, 200, 30, juce::Justification::centredLeft, false);

    // the four kinds of sound
    g.setFont (look::caps (9.5f, true, 0.14f));
    g.setColour (look::dim);
    for (int i = 0; i < pads.size(); ++i)
    {
        const juce::String cat = i < amb::numFactory ? juce::String (amb::kCategoryNames[amb::kSoundCategory[i]]) : juce::String ("YOUR OWN");
        g.drawText (cat, pads[i]->getX(), pads[i]->getY() - 16, pads[i]->getWidth(), 12, juce::Justification::centred, false);
    }
    g.setColour (look::ink);
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.drawText (hovered >= 0 && hovered < helps.size() ? helps[hovered] : juce::String ("Choose a sound, then play. Hold a key; Loops makes it say its note again. Drop any sound on the window."),
                kPad, getHeight() - 36, getWidth() - 2 * kPad, 20, juce::Justification::centredLeft, false);
}

void PoomseAmbientEditor::resized()
{
    resetButton.setBounds (getWidth() - kPad - 80, 18, 80, 30);
    latchButton.setBounds (resetButton.getX() - 10 - 84, 18, 84, 30);
    const int contentW = getWidth() - 2 * kPad;
    const int pw = contentW / pads.size();
    for (int i = 0; i < pads.size(); ++i) pads[i]->setBounds (kPad + i * pw, 82, pw, 92);
    view.setBounds (kPad, 196, contentW, 190);
    const int cw = contentW / knobs.size();
    for (int i = 0; i < knobs.size(); ++i)
    {
        juce::Rectangle<int> cell (kPad + i * cw, 410, cw, 130);
        knobs[i]->name.setBounds (cell.removeFromTop (16));
        knobs[i]->slider.setBounds (cell);
    }
}
