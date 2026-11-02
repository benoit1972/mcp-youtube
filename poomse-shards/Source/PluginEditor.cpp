#include "PluginEditor.h"

namespace
{
constexpr int kWidth = 780, kHeight = 700, kPad = 28;
constexpr float kInset = 10.f;
const char* const kAudioExt = "wav;aif;aiff;mp3;flac;m4a;ogg;caf";
const char* const kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

//==============================================================================
BreakView::BreakView (PoomseShardsProcessor& p) : proc (p)
{
    name = proc.getBreakName();
    hitsSeen = proc.getHitCount();
    startTimerHz (30);
}

juce::Rectangle<float> BreakView::mapArea() const   { return getLocalBounds().toFloat().withTrimmedBottom (54.f); }
juce::Rectangle<float> BreakView::stripArea() const { return getLocalBounds().toFloat().removeFromBottom (46.f); }

void BreakView::pollHits()
{
    const int count = proc.getHitCount();
    for (int i = std::max (hitsSeen, count - 24); i < count; ++i)
    {
        const int h = proc.getHit (i);
        const size_t idx = (size_t) (h & 0xffff);
        if (idx >= glow.size()) continue;
        if (h & 0x40000) glowGhost[idx] = 1.f;
        else if (h & 0x10000) glowRoll[idx] = 1.f;
        else glow[idx] = 1.f;
        if (h & 0x20000) glowRev[idx] = 1.f;
    }
    hitsSeen = count;
    for (size_t i = 0; i < glow.size(); ++i) { glow[i] *= 0.9f; glowRoll[i] *= 0.88f; glowRev[i] *= 0.92f; glowGhost[i] *= 0.88f; }
}

void BreakView::timerCallback()
{
    const int v = proc.getViewVersion();
    if (v != seenVersion)
    {
        view = proc.getView(); seenVersion = v; name = proc.getBreakName();
        const size_t n = view.marks.size();
        glow.assign (n, 0.f); glowRoll.assign (n, 0.f); glowRev.assign (n, 0.f); glowGhost.assign (n, 0.f);
        hitsSeen = proc.getHitCount();
    }
    pollHits();
    if (messageTicks > 0 && --messageTicks == 0) message = {};
    repaint();
}

void BreakView::paint (juce::Graphics& g)
{
    const auto all = getLocalBounds().toFloat();
    g.setColour (look::paper2);
    g.fillRoundedRectangle (all, 6.f);

    const auto mapR = mapArea(), m = mapR.reduced (kInset), strip = stripArea().reduced (kInset, 6.f);
    auto px = [&] (float x01) { return m.getX() + x01 * m.getWidth(); };
    auto py = [&] (float y01) { return m.getBottom() - 6.f - y01 * (m.getHeight() - 12.f); };

    g.setColour (look::rule);
    for (int i = 0; i <= 16; ++i) g.fillRect (px ((float) i / 16.f) - 0.5f, m.getY(), 1.f, m.getHeight());      // (the 1/16 grid of the loop)
    for (int i = 0; i <= 2; ++i)  g.fillRect (m.getX(), py ((float) i / 2.f) - 0.5f, m.getWidth(), 1.f);

    // the hits of the loop
    for (size_t i = 0; i < view.marks.size(); ++i)
    {
        const auto& k = view.marks[i];
        const float r = 2.5f + 4.f * k.level;
        const float cx = px (k.x), cy = py (k.y);
        const float gl = glow[i], gr = glowRoll[i], gh = glowGhost[i], rv = glowRev[i];
        const float a = std::max ({ gl, gr, gh });
        if (a > 0.02f)
        {
            g.setColour (look::accent.withAlpha (0.16f * a)); g.fillEllipse (cx - 8.f - 16.f * a, cy - 8.f - 16.f * a, 16.f + 32.f * a, 16.f + 32.f * a);
            g.setColour (gl >= std::max (gr, gh) ? look::accent : gr >= gh ? look::accent.withAlpha (0.7f) : look::accent.withAlpha (0.5f));
            const float rr = gh > gl && gh > gr ? r * 0.7f : r + 2.f;
            g.fillEllipse (cx - rr, cy - rr, rr * 2.f, rr * 2.f);
        }
        if (rv > 0.05f) { g.setColour (look::accent.withAlpha (rv)); g.drawEllipse (cx - r - 5.f, cy - r - 5.f, (r + 5.f) * 2.f, (r + 5.f) * 2.f, 1.6f); }
        g.setColour (look::ink.withAlpha (0.35f + 0.55f * k.level));
        g.fillEllipse (cx - r, cy - r, r * 2.f, r * 2.f);
    }

    // the playhead
    const float phase = proc.getLoopPhase();
    g.setColour (look::accent.withAlpha (0.55f));
    g.fillRect (px (phase) - 0.5f, m.getY(), 1.f, m.getHeight());

    // the break itself, below, with its cuts and the playhead
    g.setColour (look::dim.withAlpha (0.7f));
    if (! view.peaks.empty())
    {
        const float step = strip.getWidth() / (float) view.peaks.size(), mid = strip.getCentreY();
        for (size_t i = 0; i < view.peaks.size(); ++i)
        {
            const float h = std::max (1.2f, view.peaks[i] * strip.getHeight() * 0.9f);
            g.fillRect (strip.getX() + (float) i * step, mid - h * 0.5f, std::max (1.f, step * 0.72f), h);
        }
    }
    for (size_t i = 0; i < view.marks.size(); ++i)
    {
        const auto& k = view.marks[i];
        const float x = strip.getX() + (view.origin + k.x * (1.f - view.origin)) * strip.getWidth();
        const float gl = std::max (glow[i], glowRoll[i]);
        if (gl > 0.05f) { g.setColour (look::accent.withAlpha (0.7f * gl)); g.fillRect (x, strip.getY(), std::max (2.f, k.w * (1.f - view.origin) * strip.getWidth()), strip.getHeight()); }
        g.setColour (look::ink.withAlpha (0.5f));
        g.fillRect (x - 0.5f, strip.getBottom() - 6.f, 1.f, 6.f);
    }
    g.setColour (look::accent);
    g.fillRect (strip.getX() + (view.origin + phase * (1.f - view.origin)) * strip.getWidth() - 1.f, strip.getY() - 2.f, 2.f, strip.getHeight() + 4.f);

    // words
    g.setFont (look::caps (9.5f, true, 0.14f));
    g.setColour (look::dim);
    g.drawText ("BRIGHT  -  HATS", mapR.reduced (kInset + 2.f, 6.f), juce::Justification::topRight, false);
    g.drawText ("DARK  -  KICKS", mapR.reduced (kInset + 2.f, 6.f), juce::Justification::bottomRight, false);
    const juce::String head = message.isNotEmpty() ? message
        : name.toUpperCase() + "   -   " + juce::String (view.marks.size()) + " HITS FOUND";
    g.setColour (message.isNotEmpty() ? look::accent : look::ink);
    g.drawText (head, mapR.reduced (kInset + 2.f, 6.f), juce::Justification::topLeft, false);

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
    else    { g.setColour (hover ? look::paper2 : look::paper); g.fillRoundedRectangle (r, 8.f); g.setColour (hover ? look::ink : (isFill ? look::dim : look::rule)); g.drawRoundedRectangle (r.reduced (0.5f), 8.f, isFill ? 1.8f : 1.4f); }
    const auto fg = on ? look::paper : look::ink, dim = on ? look::paper.withAlpha (0.8f) : look::dim;
    g.setColour (dim);
    g.setFont (look::caps (9.f, true, 0.1f));
    g.drawText (noteName, r.reduced (8.f, 6.f), juce::Justification::topLeft, false);
    g.setColour (fg);
    g.setFont (look::caps (13.f, true, 0.1f));
    g.drawText (label.toUpperCase(), r.reduced (6.f, 0.f), juce::Justification::centred, false);
    g.setColour (dim);
    g.setFont (juce::Font (juce::FontOptions (10.5f)));
    g.drawText (tagline, r.reduced (6.f, 7.f), juce::Justification::centredBottom, false);
}

//==============================================================================
PoomseShardsEditor::Knob& PoomseShardsEditor::makeKnob (const juce::String& paramId, const juce::String& text)
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

PoomseShardsEditor::PoomseShardsEditor (PoomseShardsProcessor& p) : AudioProcessorEditor (&p), proc (p), breakView (p)
{
    setLookAndFeel (&laf);
    addAndMakeVisible (breakView);

    // two zones: the playback pads (C3..B3 on the white keys) and the fills (C1..B1)
    static const int whites[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < sh::numPlays; ++i)
    {
        auto* pad = pads.add (new PadButton (proc, i, 60 + whites[i], sh::kPlayNames[i], juce::String (names[whites[i]]) + "3", sh::kPlayTags[i], false));
        pad->onHover = [this] (int k) { helpText = k >= 0 ? sh::kPlayHelp[k] : ""; repaint(); };
        addAndMakeVisible (pad);
    }
    for (int i = 0; i < sh::numFills; ++i)
    {
        auto* pad = fillPads.add (new PadButton (proc, i, 36 + whites[i], sh::kFillNames[i], juce::String (names[whites[i]]) + "1", sh::kFillTags[i], true));
        pad->onHover = [this] (int k) { helpText = k >= 0 ? sh::kFillHelp[k] : ""; repaint(); };
        addAndMakeVisible (pad);
    }
    latchButton.setComponentID ("toggle");
    latchButton.setClickingTogglesState (true);
    latchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "latch", latchButton);
    addAndMakeVisible (latchButton);

    makeKnob ("mutate", "Mutate"); makeKnob ("ramp", "Ramp"); makeKnob ("groove", "Groove"); makeKnob ("tune", "Tune");
    makeKnob ("quant", "Start on"); makeKnob ("bars", "Loop length"); makeKnob ("level", "Level");

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
            juce::Timer::callAfterDelay (3000, [safe = juce::Component::SafePointer<PoomseShardsEditor> (this)] { if (safe != nullptr) safe->disarmReset(); });
        }
        else { proc.resetToDefaults(); presetBox.setSelectedItemIndex (0, juce::dontSendNotification); disarmReset(); }
    };

    for (int i = 0; i < PoomseShardsProcessor::getNumPresets(); ++i) presetBox.addItem (PoomseShardsProcessor::getPresetName (i), i + 1);
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

PoomseShardsEditor::~PoomseShardsEditor() { stopTimer(); setLookAndFeel (nullptr); }

void PoomseShardsEditor::timerCallback()
{
    const int mask = proc.getActiveKeys();
    for (int i = 0; i < pads.size(); ++i) pads[i]->setLit ((mask & (1 << i)) != 0);
    for (int i = 0; i < fillPads.size(); ++i) fillPads[i]->setLit ((mask & (1 << (8 + i))) != 0);
    const int seed = proc.getSeed();
    if (seed != shownSeed) { shownSeed = seed; repaint(); }
}

void PoomseShardsEditor::load (const juce::File& file)
{
    // the answer is immediate: the name appears at once, and the loading itself starts a moment later, once it has been drawn
    breakView.setDropHover (false);
    breakView.showMessage ("LOADING " + file.getFileName().toUpperCase() + "...", 30);
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PoomseShardsEditor> (this), file]
    {
        if (safe == nullptr) return;
        const auto result = safe->proc.loadBreak (file);
        safe->breakView.showMessage (result.wasOk() ? "LOADED " + file.getFileName().toUpperCase() : result.getErrorMessage(), result.wasOk() ? 4 : 10);
    });
}

bool PoomseShardsEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files) if (juce::File (f).existsAsFile()) return true;      // whatever it is, the reader decides, and says why if it cannot
    return false;
}

void PoomseShardsEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files) if (juce::File (f).existsAsFile()) { load (juce::File (f)); return; }
    breakView.setDropHover (false);
    breakView.showMessage ("NOTHING USABLE WAS DROPPED (A FILE FROM THE FINDER IS EXPECTED)");
}

void PoomseShardsEditor::disarmReset() { resetArmed = false; resetButton.setButtonText ("Reset"); }

void PoomseShardsEditor::stepPreset (int delta)
{
    const int n = PoomseShardsProcessor::getNumPresets();
    presetBox.setSelectedItemIndex (((presetBox.getSelectedItemIndex() + delta) % n + n) % n, juce::sendNotificationSync);
}

void PoomseShardsEditor::paint (juce::Graphics& g)
{
    g.fillAll (look::paper);
    const auto word = look::caps (21.f, true, 0.28f);
    g.setFont (word);
    const int w1 = (int) std::ceil (word.getStringWidthFloat ("POOMSE"));
    g.setColour (look::ink);
    g.drawText ("POOMSE", kPad, 16, w1 + 4, 30, juce::Justification::centredLeft, false);
    g.setColour (look::accent);
    g.drawText ("SHARDS", kPad + w1 - 4, 16, 160, 30, juce::Justification::centredLeft, false);

    g.setFont (look::caps (9.5f, true, 0.14f));
    g.setColour (look::dim);
    g.drawText ("VERSION " + juce::String (shownSeed), rerollButton.getX() - 4, 50, rerollButton.getWidth() + 8, 12, juce::Justification::centred, false);
    g.drawText ("PLAYBACK  -  WHICH VERSION OF THE LOOP PLAYS  (C3 TO B3)", kPad, 316, 500, 12, juce::Justification::centredLeft, false);
    g.drawText ("FILLS  -  THEY LAST AS LONG AS THE KEY IS DOWN, THEN THE PLAYBACK IS BACK  (C1 TO B1)", kPad, 432, 700, 12, juce::Justification::centredLeft, false);
    g.setColour (look::ink);
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.drawText (helpText.isNotEmpty() ? helpText : juce::String ("Point at a pad to see what it does. Mutate: how much of the loop the playback pad takes hold of. Reroll: another version of the same. Drop a sound anywhere on the window."),
                kPad, getHeight() - 36, getWidth() - 2 * kPad, 20, juce::Justification::centredLeft, false);
}

void PoomseShardsEditor::resized()
{
    randomButton.setBounds (getWidth() - kPad - 104, 18, 104, 30);
    resetButton.setBounds (randomButton.getX() - 10 - 80, 18, 80, 30);
    rerollButton.setBounds (resetButton.getX() - 10 - 90, 18, 90, 30);
    prevPreset.setBounds (kPad, 62, 26, 28);
    presetBox.setBounds (kPad + 32, 62, 250, 28);
    nextPreset.setBounds (kPad + 288, 62, 26, 28);

    const int contentW = getWidth() - 2 * kPad;
    breakView.setBounds (kPad, 100, contentW, 204);
    latchButton.setBounds (getWidth() - kPad - 84, 310, 84, 22);
    const int pw = contentW / sh::numPlays;
    for (int i = 0; i < pads.size(); ++i)     pads[i]->setBounds (kPad + i * pw, 336, pw, 92);
    for (int i = 0; i < fillPads.size(); ++i) fillPads[i]->setBounds (kPad + i * pw, 448, pw, 84);

    const int cw = contentW / knobs.size();
    for (int i = 0; i < knobs.size(); ++i)
    {
        juce::Rectangle<int> cell (kPad + i * cw, 546, cw, 112);
        knobs[i]->name.setBounds (cell.removeFromTop (16));
        knobs[i]->slider.setBounds (cell);
    }
}
