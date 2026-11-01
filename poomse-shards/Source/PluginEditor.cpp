#include "PluginEditor.h"

namespace
{
constexpr int kWidth = 780, kHeight = 640, kPad = 28;
constexpr float kInset = 10.f;
const char* const kAudioExt = "wav;aif;aiff;mp3;flac;m4a;ogg;caf";
}

//==============================================================================
BreakView::BreakView (PoomseShardsProcessor& p) : proc (p)
{
    name = proc.getBreakName();
    hitsSeen = proc.getHitCount();
    startTimerHz (30);
}

juce::Rectangle<float> BreakView::mapArea() const   { return getLocalBounds().toFloat().withTrimmedBottom (66.f); }
juce::Rectangle<float> BreakView::stripArea() const { return getLocalBounds().toFloat().removeFromBottom (56.f); }

void BreakView::pollHits()
{
    const int count = proc.getHitCount();
    for (int i = std::max (hitsSeen, count - 24); i < count; ++i)
    {
        const int h = proc.getHit (i);
        const size_t idx = (size_t) (h & 0xffff);
        if (idx < glow.size()) { if (h & 0x10000) glowFragment[idx] = 1.f; else glow[idx] = 1.f; }
    }
    hitsSeen = count;
    for (auto& g : glow) g *= 0.9f;
    for (auto& g : glowFragment) g *= 0.88f;
}

void BreakView::timerCallback()
{
    const int v = proc.getViewVersion();
    if (v != seenVersion)
    {
        view = proc.getView(); seenVersion = v; name = proc.getBreakName();
        glow.assign (view.marks.size(), 0.f); glowFragment.assign (view.marks.size(), 0.f);
        hitsSeen = proc.getHitCount();
    }
    pollHits();
    if (messageTicks > 0 && --messageTicks == 0) message = {};
    repaint();
}

bool BreakView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files) if (juce::File (f).hasFileExtension (kAudioExt)) return true;
    return false;
}

void BreakView::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (! file.hasFileExtension (kAudioExt)) continue;
        if (! proc.loadBreak (file)) { message = "NO HITS FOUND IN THIS FILE"; messageTicks = 90; }
        break;
    }
    repaint();
}

void BreakView::gesture (bool begin)
{
    if (auto* p = proc.apvts.getParameter ("place")) { if (begin) p->beginChangeGesture(); else p->endChangeGesture(); }
}

void BreakView::setFromMouse (juce::Point<float> pt)
{
    const auto m = mapArea().reduced (kInset);
    const float x = juce::jlimit (0.f, 1.f, (pt.x - m.getX()) / m.getWidth());
    if (auto* p = proc.apvts.getParameter ("place")) p->setValueNotifyingHost (p->convertTo0to1 (x));
}

void BreakView::mouseDown (const juce::MouseEvent& e) { dragging = true; gesture (true); setFromMouse (e.position); }
void BreakView::mouseDrag (const juce::MouseEvent& e) { if (dragging) setFromMouse (e.position); }
void BreakView::mouseUp (const juce::MouseEvent&)      { if (dragging) gesture (false); dragging = false; }

void BreakView::paint (juce::Graphics& g)
{
    const auto all = getLocalBounds().toFloat();
    g.setColour (look::paper2);
    g.fillRoundedRectangle (all, 6.f);

    const auto mapR = mapArea(), m = mapR.reduced (kInset), strip = stripArea().reduced (kInset, 6.f);
    auto px = [&] (float x01) { return m.getX() + x01 * m.getWidth(); };
    auto py = [&] (float y01) { return m.getBottom() - y01 * m.getHeight(); };

    // faint ruling; the five horizontal lines are the octaves of the keyboard, C1 at the bottom to C5 at the top
    g.setColour (look::rule);
    for (int i = 0; i <= 8; ++i) g.fillRect (px ((float) i / 8.f) - 0.5f, m.getY(), 1.f, m.getHeight());
    for (int i = 0; i <= 4; ++i) g.fillRect (m.getX(), py ((float) i / 4.f) - 0.5f, m.getWidth(), 1.f);

    const float place = proc.apvts.getRawParameterValue ("place")->load();

    // the band the hits are taken from, and its line
    const float cx = px (place);
    g.setColour (look::accent.withAlpha (0.07));
    g.fillRect (juce::Rectangle<float> (cx - 24.f, m.getY(), 48.f, m.getHeight()));
    g.setColour (look::accent.withAlpha (0.55f));
    g.fillRect (cx - 0.5f, m.getY(), 1.f, m.getHeight());

    // the pieces of the break
    for (size_t i = 0; i < view.marks.size(); ++i)
    {
        const auto& k = view.marks[i];
        const float r = 2.f + 4.f * k.level;
        const float gl = i < glow.size() ? glow[i] : 0.f, gf = i < glowFragment.size() ? glowFragment[i] : 0.f;
        if (gl > 0.02f || gf > 0.02f)
        {
            const float a = std::max (gl, gf);
            g.setColour (look::accent.withAlpha (0.16f * a)); g.fillEllipse (px (k.x) - 6.f - 16.f * a, py (k.y) - 6.f - 16.f * a, 12.f + 32.f * a, 12.f + 32.f * a);
            g.setColour (gl >= gf ? look::accent : look::accent.withAlpha (0.65f));
            g.fillEllipse (px (k.x) - r - 2.f, py (k.y) - r - 2.f, (r + 2.f) * 2.f, (r + 2.f) * 2.f);
        }
        g.setColour (look::ink.withAlpha (0.35f + 0.55f * k.level));
        g.fillEllipse (px (k.x) - r, py (k.y) - r, r * 2.f, r * 2.f);
    }

    // the break, below, with its cuts
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
        const float gl = i < glow.size() ? std::max (glow[i], glowFragment[i]) : 0.f;
        if (gl > 0.05f)
        {
            g.setColour (look::accent.withAlpha (0.75f * gl));
            g.fillRect (strip.getX() + k.x * strip.getWidth(), strip.getY(), std::max (2.f, k.w * strip.getWidth()), strip.getHeight());
        }
        g.setColour (look::ink.withAlpha (0.55f));
        g.fillRect (strip.getX() + k.x * strip.getWidth() - 0.5f, strip.getBottom() - 6.f, 1.f, 6.f);
    }
    g.setColour (look::accent);
    g.fillRect (strip.getX() + place * strip.getWidth() - 1.f, strip.getY() - 2.f, 2.f, strip.getHeight() + 4.f);

    // words
    g.setFont (look::caps (9.5f, true, 0.14f));
    g.setColour (look::dim);
    static const char* oct[5] = { "C1", "C2", "C3", "C4", "C5" };
    for (int i = 0; i < 5; ++i)
        g.drawText (oct[i], juce::Rectangle<float> (m.getX() + 3.f, py ((float) i / 4.f) - (i == 0 ? 14.f : 12.f), 30.f, 12.f), juce::Justification::centredLeft, false);
    g.drawText ("BRIGHT  -  HATS, RIMS, PINGS", mapR.reduced (kInset + 2.f, 6.f), juce::Justification::topRight, false);
    g.drawText ("DARK  -  KICKS, TOMS", mapR.reduced (kInset + 2.f, 6.f), juce::Justification::bottomRight, false);
    const juce::String head = message.isNotEmpty() ? message
        : name.toUpperCase() + "   -   " + juce::String (view.marks.size()) + " HITS FOUND";
    g.setColour (message.isNotEmpty() ? look::accent : look::ink);
    g.drawText (head, mapR.reduced (kInset + 2.f, 6.f).withTrimmedLeft (24.f), juce::Justification::topLeft, false);

    if (dropHover)
    {
        g.setColour (look::paper.withAlpha (0.82f)); g.fillRoundedRectangle (all, 6.f);
        g.setColour (look::accent); g.drawRoundedRectangle (all.reduced (1.f), 6.f, 2.f);
        g.setFont (look::caps (15.f, true, 0.3f));
        g.drawText ("DROP A DRUM BREAK", getLocalBounds(), juce::Justification::centred, false);
    }
}

//==============================================================================
PoomseShardsEditor::Knob& PoomseShardsEditor::makeKnob (const juce::String& paramId, const juce::String& text)
{
    auto* k = knobs.add (new Knob());
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 18);
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

    makeKnob ("place", "Place"); makeKnob ("shatter", "Shatter"); makeKnob ("grid", "Grid"); makeKnob ("ramp", "Ramp");
    makeKnob ("scatter", "Scatter"); makeKnob ("fall", "Fall"); makeKnob ("snap", "Snap"); makeKnob ("tune", "Tune"); makeKnob ("level", "Level");

    randomButton.setComponentID ("ink");
    randomButton.onClick = [this] { proc.randomise(); };
    editsButton.setComponentID ("pill");
    editsButton.onClick = [this] { proc.newEdits(); };
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
    for (auto* c : { (juce::Component*) &randomButton, (juce::Component*) &resetButton, (juce::Component*) &editsButton,
                     (juce::Component*) &prevPreset, (juce::Component*) &nextPreset, (juce::Component*) &presetBox })
        addAndMakeVisible (c);

    setSize (kWidth, kHeight);
}

PoomseShardsEditor::~PoomseShardsEditor() { setLookAndFeel (nullptr); }

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

    g.setColour (look::dim);
    g.setFont (juce::Font (juce::FontOptions (11.f)));
    g.drawText ("Low keys play dark hits, high keys bright ones (C1 to C5). Place chooses where in the break they come from. Shatter breaks a hit into a burst, in time.",
                kPad, getHeight() - 30, getWidth() - 2 * kPad, 16, juce::Justification::centredLeft, false);
}

void PoomseShardsEditor::resized()
{
    randomButton.setBounds (getWidth() - kPad - 104, 18, 104, 30);
    resetButton.setBounds (randomButton.getX() - 10 - 80, 18, 80, 30);
    editsButton.setBounds (resetButton.getX() - 10 - 110, 18, 110, 30);
    prevPreset.setBounds (kPad, 62, 26, 28);
    presetBox.setBounds (kPad + 32, 62, 250, 28);
    nextPreset.setBounds (kPad + 288, 62, 26, 28);

    breakView.setBounds (kPad, 104, getWidth() - 2 * kPad, 340);
    const int cw = (getWidth() - 2 * kPad) / knobs.size();
    for (int i = 0; i < knobs.size(); ++i)
    {
        juce::Rectangle<int> cell (kPad + i * cw, 462, cw, 124);
        knobs[i]->name.setBounds (cell.removeFromTop (16));
        knobs[i]->slider.setBounds (cell);
    }
}
