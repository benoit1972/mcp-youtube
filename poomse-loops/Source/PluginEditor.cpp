#include "PluginEditor.h"

namespace
{
constexpr int kWidth = 780, kHeight = 640, kPad = 28;
constexpr float kInset = 10.f;
const char* const kAudioExt = "wav;aif;aiff;mp3;flac;m4a;ogg;caf";
}

//==============================================================================
RecordView::RecordView (PoomseLoopsProcessor& p) : proc (p)
{
    name = proc.getRecordName();
    startTimerHz (30);
}

juce::Rectangle<float> RecordView::mapArea() const   { return getLocalBounds().toFloat().withTrimmedBottom (66.f); }
juce::Rectangle<float> RecordView::stripArea() const { return getLocalBounds().toFloat().removeFromBottom (56.f); }

void RecordView::timerCallback()
{
    const int v = proc.getViewVersion();
    if (v != seenVersion) { view = proc.getView(); seenVersion = v; name = proc.getRecordName(); }
    if (messageTicks > 0 && --messageTicks == 0) message = {};
    repaint();
}

bool RecordView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files) if (juce::File (f).hasFileExtension (kAudioExt)) return true;
    return false;
}

void RecordView::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (! file.hasFileExtension (kAudioExt)) continue;
        if (! proc.loadRecord (file)) { message = "COULD NOT READ THIS FILE"; messageTicks = 90; }
        break;
    }
    repaint();
}

void RecordView::gesture (bool begin)
{
    for (const char* id : { "position", "size" })
        if (auto* p = proc.apvts.getParameter (id)) { if (begin) p->beginChangeGesture(); else p->endChangeGesture(); }
}

void RecordView::setFromMouse (juce::Point<float> pt)
{
    const auto m = mapArea().reduced (kInset);
    const float x = juce::jlimit (0.f, 1.f, (pt.x - m.getX()) / m.getWidth());
    if (auto* p = proc.apvts.getParameter ("position")) p->setValueNotifyingHost (p->convertTo0to1 (x));
    if (pt.y < mapArea().getBottom())
    {
        const float y = juce::jlimit (0.f, 1.f, 1.f - (pt.y - m.getY()) / m.getHeight());
        if (auto* p = proc.apvts.getParameter ("size")) p->setValueNotifyingHost (p->convertTo0to1 (y));
    }
}

void RecordView::mouseDown (const juce::MouseEvent& e) { dragging = true; gesture (true); setFromMouse (e.position); }
void RecordView::mouseDrag (const juce::MouseEvent& e) { if (dragging) setFromMouse (e.position); }
void RecordView::mouseUp (const juce::MouseEvent&)      { if (dragging) gesture (false); dragging = false; }

void RecordView::paint (juce::Graphics& g)
{
    const auto all = getLocalBounds().toFloat();
    g.setColour (look::paper2);
    g.fillRoundedRectangle (all, 6.f);

    const auto mapR = mapArea(), m = mapR.reduced (kInset), strip = stripArea().reduced (kInset, 6.f);
    auto px = [&] (float x01) { return m.getX() + x01 * m.getWidth(); };
    auto py = [&] (float y01) { return m.getBottom() - y01 * m.getHeight(); };

    // faint ruling
    g.setColour (look::rule);
    for (int i = 0; i <= 10; ++i) g.fillRect (px ((float) i / 10.f) - 0.5f, m.getY(), 1.f, m.getHeight());
    for (int i = 0; i <= 4; ++i)  g.fillRect (m.getX(), py ((float) i / 4.f) - 0.5f, m.getWidth(), 1.f);

    // the loops the finder found
    for (const auto& k : view.marks)
    {
        const float r = 1.3f + 3.2f * juce::jlimit (0.f, 1.f, (k.score - 0.6f) / 0.4f);
        g.setColour (look::ink.withAlpha (0.22f + 0.6f * k.level));
        g.fillEllipse (px (k.x) - r, py (k.y) - r, r * 2.f, r * 2.f);
    }

    // the keys that are held
    const float pos = proc.apvts.getRawParameterValue ("position")->load();
    const float sizeY = proc.apvts.getRawParameterValue ("size")->load();
    for (int i = 0; i < lp::Engine::maxVoices; ++i)
    {
        const int idx = proc.getVoiceLoop (i);
        if (idx < 0 || idx >= (int) view.marks.size()) continue;
        const auto& k = view.marks[(size_t) idx];
        g.setColour (look::accent.withAlpha (0.18f)); g.fillEllipse (px (k.x) - 11.f, py (k.y) - 11.f, 22.f, 22.f);
        g.setColour (look::accent);                   g.fillEllipse (px (k.x) - 4.5f, py (k.y) - 4.5f, 9.f, 9.f);
        // and where it sits in the record
        g.setColour (look::accent.withAlpha (0.7f));
        g.fillRect (strip.getX() + k.x * strip.getWidth(), strip.getY(), std::max (2.f, k.w * strip.getWidth()), strip.getHeight());
    }

    // the cross-hair
    const float cx = px (pos), cy = py (sizeY);
    g.setColour (look::accent.withAlpha (0.55f));
    g.fillRect (cx - 0.5f, m.getY(), 1.f, m.getHeight());
    g.fillRect (m.getX(), cy - 0.5f, m.getWidth(), 1.f);
    g.setColour (look::accent);
    g.drawEllipse (cx - 9.f, cy - 9.f, 18.f, 18.f, 1.6f);

    // the record, below
    g.setColour (look::dim.withAlpha (0.8f));
    if (! view.peaks.empty())
    {
        const float step = strip.getWidth() / (float) view.peaks.size(), mid = strip.getCentreY();
        for (size_t i = 0; i < view.peaks.size(); ++i)
        {
            const float h = std::max (1.2f, view.peaks[i] * strip.getHeight() * 0.9f);
            g.fillRect (strip.getX() + (float) i * step, mid - h * 0.5f, std::max (1.f, step * 0.72f), h);
        }
    }
    g.setColour (look::accent);
    g.fillRect (strip.getX() + pos * strip.getWidth() - 1.f, strip.getY() - 2.f, 2.f, strip.getHeight() + 4.f);

    // words
    g.setFont (look::caps (9.5f, true, 0.14f));
    g.setColour (look::dim);
    g.drawText ("LONG", mapR.reduced (kInset + 2.f, 6.f), juce::Justification::topRight, false);
    g.drawText ("SHORT", mapR.reduced (kInset + 2.f, 6.f), juce::Justification::bottomRight, false);
    g.drawText ("START OF THE RECORD", mapR.reduced (kInset + 2.f, 6.f), juce::Justification::bottomLeft, false);
    const juce::String head = message.isNotEmpty() ? message
        : name.toUpperCase() + "   -   " + juce::String (view.marks.size()) + " LOOPS FOUND";
    g.setColour (message.isNotEmpty() ? look::accent : look::ink);
    g.drawText (head, mapR.reduced (kInset + 2.f, 6.f), juce::Justification::topLeft, false);
    g.setColour (look::accent);
    const float ms = (float) lp::Atlas::yToSeconds (sizeY) * 1000.f;
    g.drawText (juce::String (juce::roundToInt (pos * 100.f)) + " %   " + juce::String (juce::roundToInt (ms)) + " MS",
                juce::Rectangle<float> (cx > m.getCentreX() ? cx - 110.f : cx + 14.f, cy - 22.f, 96.f, 14.f),
                cx > m.getCentreX() ? juce::Justification::centredRight : juce::Justification::centredLeft, false);

    if (dropHover)
    {
        g.setColour (look::paper.withAlpha (0.82f)); g.fillRoundedRectangle (all, 6.f);
        g.setColour (look::accent); g.drawRoundedRectangle (all.reduced (1.f), 6.f, 2.f);
        g.setFont (look::caps (15.f, true, 0.3f));
        g.drawText ("DROP A RECORD", getLocalBounds(), juce::Justification::centred, false);
    }
}

//==============================================================================
PoomseLoopsEditor::Knob& PoomseLoopsEditor::makeKnob (const juce::String& paramId, const juce::String& text)
{
    auto* k = knobs.add (new Knob());
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 110, 18);
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

PoomseLoopsEditor::PoomseLoopsEditor (PoomseLoopsProcessor& p) : AudioProcessorEditor (&p), proc (p), recordView (p)
{
    setLookAndFeel (&laf);
    addAndMakeVisible (recordView);

    makeKnob ("seam", "Seam"); makeKnob ("moire", "Moire"); makeKnob ("scan", "Scan");
    makeKnob ("pulse", "Pulse"); makeKnob ("tail", "Tail"); makeKnob ("level", "Level");

    randomButton.setComponentID ("ink");
    randomButton.onClick = [this] { proc.randomise(); };
    resetButton.setComponentID ("pill");
    resetButton.onClick = [this]
    {
        if (! resetArmed)
        {
            resetArmed = true;
            resetButton.setButtonText ("Sure?");
            juce::Timer::callAfterDelay (3000, [safe = juce::Component::SafePointer<PoomseLoopsEditor> (this)] { if (safe != nullptr) safe->disarmReset(); });
        }
        else { proc.resetToDefaults(); presetBox.setSelectedItemIndex (0, juce::dontSendNotification); disarmReset(); }
    };
    placesButton.setComponentID ("toggle");
    placesButton.setClickingTogglesState (true);
    placesAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "places", placesButton);

    for (int i = 0; i < PoomseLoopsProcessor::getNumPresets(); ++i) presetBox.addItem (PoomseLoopsProcessor::getPresetName (i), i + 1);
    presetBox.setSelectedItemIndex (proc.getCurrentPreset(), juce::dontSendNotification);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedItemIndex(); if (i >= 0) proc.applyPreset (i); };
    prevPreset.setComponentID ("pill"); nextPreset.setComponentID ("pill");
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (+1); };
    for (auto* c : { (juce::Component*) &randomButton, (juce::Component*) &resetButton, (juce::Component*) &placesButton,
                     (juce::Component*) &prevPreset, (juce::Component*) &nextPreset, (juce::Component*) &presetBox })
        addAndMakeVisible (c);

    setSize (kWidth, kHeight);
    startTimerHz (8);
}

PoomseLoopsEditor::~PoomseLoopsEditor() { stopTimer(); setLookAndFeel (nullptr); }

void PoomseLoopsEditor::timerCallback()
{
    placesButton.setButtonText (placesButton.getToggleState() ? "Keys = places" : "Keys = pitch");
}

void PoomseLoopsEditor::disarmReset() { resetArmed = false; resetButton.setButtonText ("Reset"); }

void PoomseLoopsEditor::stepPreset (int delta)
{
    const int n = PoomseLoopsProcessor::getNumPresets();
    presetBox.setSelectedItemIndex (((presetBox.getSelectedItemIndex() + delta) % n + n) % n, juce::sendNotificationSync);
}

void PoomseLoopsEditor::paint (juce::Graphics& g)
{
    g.fillAll (look::paper);
    const auto word = look::caps (21.f, true, 0.28f);
    g.setFont (word);
    const int w1 = (int) std::ceil (word.getStringWidthFloat ("POOMSE"));
    g.setColour (look::ink);
    g.drawText ("POOMSE", kPad, 16, w1 + 4, 30, juce::Justification::centredLeft, false);
    g.setColour (look::accent);
    g.drawText ("LOOPS", kPad + w1 - 4, 16, 160, 30, juce::Justification::centredLeft, false);

    g.setColour (look::dim);
    g.setFont (juce::Font (juce::FontOptions (11.f)));
    g.drawText ("Drop a record on the map. Play keys: each one takes the loop under the cross-hair. Click or drag on the map to move it.",
                kPad, getHeight() - 30, getWidth() - 2 * kPad, 16, juce::Justification::centredLeft, false);
}

void PoomseLoopsEditor::resized()
{
    randomButton.setBounds (getWidth() - kPad - 104, 18, 104, 30);
    resetButton.setBounds (randomButton.getX() - 10 - 80, 18, 80, 30);
    placesButton.setBounds (resetButton.getX() - 10 - 130, 18, 130, 30);
    prevPreset.setBounds (kPad, 62, 26, 28);
    presetBox.setBounds (kPad + 32, 62, 250, 28);
    nextPreset.setBounds (kPad + 288, 62, 26, 28);

    recordView.setBounds (kPad, 104, getWidth() - 2 * kPad, 340);
    const int cw = (getWidth() - 2 * kPad) / knobs.size();
    for (int i = 0; i < knobs.size(); ++i)
    {
        juce::Rectangle<int> cell (kPad + i * cw, 462, cw, 124);
        knobs[i]->name.setBounds (cell.removeFromTop (16));
        knobs[i]->slider.setBounds (cell);
    }
}
