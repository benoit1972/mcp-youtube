#include "PluginEditor.h"

namespace
{
constexpr int kWidth = 780, kHeight = 690, kPad = 28;
constexpr float kInset = 12.f;
const char* const kAudioExt = "wav;aif;aiff;mp3;flac;m4a;ogg;caf";
const char* const kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

//==============================================================================
PhaseView::PhaseView (PoomsePhaseProcessor& p) : proc (p)
{
    hitsSeen = proc.getHitCount();
    startTimerHz (30);
}

void PhaseView::timerCallback()
{
    const int count = proc.getHitCount();
    for (int i = std::max (hitsSeen, count - 24); i < count; ++i)
    {
        const int layer = proc.getHit (i) & 0xff;
        if (layer >= 0 && layer < 16) glow[layer] = 1.f;
    }
    hitsSeen = count;
    for (auto& g : glow) g *= 0.88f;
    if (messageTicks > 0 && --messageTicks == 0) message = {};
    repaint();
}

void PhaseView::paint (juce::Graphics& g)
{
    const auto all = getLocalBounds().toFloat();
    g.setColour (look::paper2);
    g.fillRoundedRectangle (all, 6.f);
    const auto m = all.reduced (kInset, kInset + 8.f).withTrimmedBottom (4.f);

    int rows = 0, lens[16] {};
    for (int i = 0; i < 16; ++i) { lens[i] = proc.layerLength (i); if (lens[i] > 0) rows = i + 1; }
    if (rows == 0)
    {
        g.setColour (look::dim);
        g.setFont (look::caps (12.f, true, 0.25f));
        g.drawText ("HOLD A KEY", all, juce::Justification::centred, false);
    }
    const float rowH = rows > 0 ? std::min (22.f, m.getHeight() / (float) rows) : 20.f;
    for (int li = 0; li < rows; ++li)
    {
        const int len = lens[li];
        if (len <= 0) continue;
        const float y = m.getY() + 6.f + (float) li * rowH + rowH * 0.5f;
        const int pos = proc.layerPosition (li);
        const uint64_t mask = proc.layerMask (li);
        const float cw = m.getWidth() / (float) std::max (len, 1);
        g.setColour (look::rule);
        g.fillRect (m.getX(), y - 0.5f, m.getWidth(), 1.f);
        for (int s = 0; s < len; ++s)
        {
            const float x = m.getX() + ((float) s + 0.5f) * cw;
            const bool on = s < 64 && ((mask >> s) & 1ull) != 0ull;
            const bool now = s == pos;
            const float r = std::min (rowH * 0.32f, cw * 0.38f);
            if (on) { g.setColour (look::ink.withAlpha (0.8f)); g.fillEllipse (x - r, y - r, r * 2.f, r * 2.f); }
            else    { g.setColour (look::dim.withAlpha (0.55f)); g.fillRect (x - 0.5f, y - 2.f, 1.f, 4.f); }
            if (now)
            {
                const float a = std::max (0.4f, glow[li]);
                g.setColour (look::accent.withAlpha (a));
                const float rr = r + 2.5f + 3.f * glow[li];
                g.drawEllipse (x - rr, y - rr, rr * 2.f, rr * 2.f, 1.8f);
                if (on) { g.setColour (look::accent); g.fillEllipse (x - r, y - r, r * 2.f, r * 2.f); }
            }
        }
    }
    g.setFont (look::caps (9.5f, true, 0.14f));
    const juce::String head = message.isNotEmpty() ? message : proc.getSoundName().toUpperCase();
    g.setColour (message.isNotEmpty() ? look::accent : look::dim);
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
void PadButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (3.f);
    const bool on = lit || down;
    if (on) { g.setColour (look::accent); g.fillRoundedRectangle (r, 8.f); }
    else    { g.setColour (hover ? look::paper2 : look::paper); g.fillRoundedRectangle (r, 8.f); g.setColour (hover ? look::ink : look::rule); g.drawRoundedRectangle (r.reduced (0.5f), 8.f, 1.4f); }
    const auto fg = on ? look::paper : look::ink, dim = on ? look::paper.withAlpha (0.8f) : look::dim;
    g.setColour (dim);
    g.setFont (look::caps (9.f, true, 0.1f));
    g.drawText (noteName, r.reduced (8.f, 6.f), juce::Justification::topLeft, false);
    g.setColour (fg);
    g.setFont (look::caps (13.f, true, 0.1f));
    g.drawText (label.toUpperCase(), r.reduced (6.f, 0.f), juce::Justification::centred, false);
    if (tagline.isNotEmpty())
    {
        g.setColour (dim);
        g.setFont (juce::Font (juce::FontOptions (10.5f)));
        g.drawText (tagline, r.reduced (6.f, 7.f), juce::Justification::centredBottom, false);
    }
}

//==============================================================================
PoomsePhaseEditor::Knob& PoomsePhaseEditor::makeKnob (const juce::String& paramId, const juce::String& text)
{
    auto* k = knobs.add (new Knob());
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 86, 18);
    k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k->name.setText (text.toUpperCase(), juce::dontSendNotification);
    k->name.setJustificationType (juce::Justification::centred);
    k->name.setFont (look::caps (10.5f));
    k->name.setColour (juce::Label::textColourId, look::dim);
    k->name.setInterceptsMouseClicks (false, false);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k->slider);
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->name);
    return *k;
}

PoomsePhaseEditor::PoomsePhaseEditor (PoomsePhaseProcessor& p) : AudioProcessorEditor (&p), proc (p), view (p)
{
    setLookAndFeel (&laf);
    addAndMakeVisible (view);

    for (int i = 0; i < ph::numSets; ++i)
    {
        auto* pad = setPads.add (new PadButton (ph::kSetNames[i], {}, ph::kSetTags[i]));
        pad->onDown = [this, i] { chooseSet (i); };
        pad->onHover = [this, i] (bool in) { helpText = in ? ph::kSetHelp[i] : ""; repaint(); };
        addAndMakeVisible (pad);
    }
    for (int i = 0; i < 12; ++i)
    {
        auto* pad = keyPads.add (new PadButton (kNoteNames[i], "", ""));
        const int note = 60 + i;
        pad->onDown = [this, note] { proc.uiNote (note, true); };
        pad->onUp   = [this, note] { proc.uiNote (note, false); };
        pad->onHover = [this] (bool in) { helpText = in ? "A key makes two cycles: its note and the octave above. Hold several: they slip against each other. (C4 to B4, or play your keyboard)" : ""; repaint(); };
        addAndMakeVisible (pad);
    }
    latchButton.setComponentID ("toggle");
    latchButton.setClickingTogglesState (true);
    latchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "latch", latchButton);
    addAndMakeVisible (latchButton);

    makeKnob ("density", "Density"); makeKnob ("variation", "Variation"); makeKnob ("ring", "Ring"); makeKnob ("space", "Space");
    makeKnob ("rate", "Step"); makeKnob ("tune", "Tune"); makeKnob ("quant", "Start on"); makeKnob ("level", "Level");

    randomButton.setComponentID ("ink");
    randomButton.onClick = [this] { proc.randomise(); };
    rerollButton.setComponentID ("pill");
    rerollButton.onClick = [this] { proc.reroll(); };
    resetButton.setComponentID ("pill");
    resetButton.onClick = [this]
    {
        if (! resetArmed)
        {
            resetArmed = true;
            resetButton.setButtonText ("Sure?");
            juce::Timer::callAfterDelay (3000, [safe = juce::Component::SafePointer<PoomsePhaseEditor> (this)] { if (safe != nullptr) safe->disarmReset(); });
        }
        else { proc.resetToDefaults(); presetBox.setSelectedItemIndex (0, juce::dontSendNotification); disarmReset(); }
    };

    for (int i = 0; i < PoomsePhaseProcessor::getNumPresets(); ++i) presetBox.addItem (PoomsePhaseProcessor::getPresetName (i), i + 1);
    presetBox.setSelectedItemIndex (proc.getCurrentPreset(), juce::dontSendNotification);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedItemIndex(); if (i >= 0) proc.applyPreset (i); };
    prevPreset.setComponentID ("pill"); nextPreset.setComponentID ("pill");
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (+1); };
    for (auto* c : { (juce::Component*) &randomButton, (juce::Component*) &resetButton, (juce::Component*) &rerollButton,
                     (juce::Component*) &prevPreset, (juce::Component*) &nextPreset, (juce::Component*) &presetBox })
        addAndMakeVisible (c);

    setSize (kWidth, kHeight);
    startTimerHz (20);
}

PoomsePhaseEditor::~PoomsePhaseEditor() { stopTimer(); setLookAndFeel (nullptr); }

void PoomsePhaseEditor::chooseSet (int i)
{
    if (auto* prm = proc.apvts.getParameter ("set"))
    {
        prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i)); prm->endChangeGesture();
    }
}

void PoomsePhaseEditor::timerCallback()
{
    const int set = (int) proc.apvts.getRawParameterValue ("set")->load();
    if (set != shownSet) { shownSet = set; for (int i = 0; i < setPads.size(); ++i) setPads[i]->setLit (i == set); }
    const int seed = proc.getSeed();
    if (seed != shownSeed) { shownSeed = seed; repaint(); }
}

void PoomsePhaseEditor::load (const juce::File& file)
{
    view.setDropHover (false);
    view.showMessage ("LOADING " + file.getFileName().toUpperCase() + "...", 60);
    juce::Thread::launch ([safe = juce::Component::SafePointer<PoomsePhaseEditor> (this), file]
    {
        PoomsePhaseProcessor::Loaded loaded;
        const auto read = PoomsePhaseProcessor::readSound (file, loaded, 10);
        juce::MessageManager::callAsync ([safe, file, loaded, read]
        {
            if (safe == nullptr) return;
            const auto result = read.wasOk() ? safe->proc.installSound (loaded, file) : read;
            safe->view.showMessage (result.wasOk() ? "LOADED  " + loaded.info : result.getErrorMessage(), result.wasOk() ? 8 : 15);
        });
    });
}

bool PoomsePhaseEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files) { const juce::File ff (f); if (ff.existsAsFile() || ff.hasFileExtension (kAudioExt)) return true; }
    return false;
}

void PoomsePhaseEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files) { const juce::File ff (f); if (ff.existsAsFile() || ff.hasFileExtension (kAudioExt)) { load (ff); return; } }
    view.setDropHover (false);
    view.showMessage ("NOTHING USABLE WAS DROPPED (A FILE FROM THE FINDER IS EXPECTED)");
}

void PoomsePhaseEditor::disarmReset() { resetArmed = false; resetButton.setButtonText ("Reset"); }

void PoomsePhaseEditor::stepPreset (int delta)
{
    const int n = PoomsePhaseProcessor::getNumPresets();
    presetBox.setSelectedItemIndex (((presetBox.getSelectedItemIndex() + delta) % n + n) % n, juce::sendNotificationSync);
}

void PoomsePhaseEditor::paint (juce::Graphics& g)
{
    g.fillAll (look::paper);
    const auto word = look::caps (21.f, true, 0.28f);
    g.setFont (word);
    const int w1 = (int) std::ceil (word.getStringWidthFloat ("POOMSE"));
    g.setColour (look::ink);
    g.drawText ("POOMSE", kPad, 16, w1 + 4, 30, juce::Justification::centredLeft, false);
    g.setColour (look::accent);
    g.drawText ("PHASE", kPad + w1 - 4, 16, 160, 30, juce::Justification::centredLeft, false);

    g.setFont (look::caps (9.5f, true, 0.14f));
    g.setColour (look::dim);
    g.drawText ("VERSION " + juce::String (shownSeed), rerollButton.getX() - 4, 50, rerollButton.getWidth() + 8, 12, juce::Justification::centred, false);
    g.drawText ("CYCLES  -  HOW LONG EACH LOOP IS, IN STEPS", kPad, 326, 500, 12, juce::Justification::centredLeft, false);
    g.drawText ("KEYS  -  HOLD ONE, OR SEVERAL  (C4 TO B4)", kPad, 432, 500, 12, juce::Justification::centredLeft, false);
    g.setColour (look::ink);
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.drawText (helpText.isNotEmpty() ? helpText : juce::String ("Hold a key: its cycles start together, then slip apart. Latch keeps the keys down. Drop a sound anywhere on the window."),
                kPad, getHeight() - 34, getWidth() - 2 * kPad, 20, juce::Justification::centredLeft, false);
}

void PoomsePhaseEditor::resized()
{
    randomButton.setBounds (getWidth() - kPad - 104, 18, 104, 30);
    resetButton.setBounds (randomButton.getX() - 10 - 80, 18, 80, 30);
    rerollButton.setBounds (resetButton.getX() - 10 - 90, 18, 90, 30);
    prevPreset.setBounds (kPad, 62, 26, 28);
    presetBox.setBounds (kPad + 32, 62, 250, 28);
    nextPreset.setBounds (kPad + 288, 62, 26, 28);

    const int contentW = getWidth() - 2 * kPad;
    view.setBounds (kPad, 100, contentW, 216);
    const int sw = contentW / setPads.size();
    for (int i = 0; i < setPads.size(); ++i) setPads[i]->setBounds (kPad + i * sw, 344, sw, 80);
    latchButton.setBounds (getWidth() - kPad - 84, 428, 84, 22);
    const int kw = contentW / 12;
    for (int i = 0; i < keyPads.size(); ++i) keyPads[i]->setBounds (kPad + i * kw, 452, kw, 66);

    const int cw = contentW / knobs.size();
    for (int i = 0; i < knobs.size(); ++i)
    {
        juce::Rectangle<int> cell (kPad + i * cw, 536, cw, 112);
        knobs[i]->name.setBounds (cell.removeFromTop (16));
        knobs[i]->slider.setBounds (cell);
    }
}
