#include "PluginEditor.h"

namespace { constexpr int kWidth = 780, kHeight = 560, kPad = 28; constexpr float kInset = 12.f; }

CloudView::CloudView (PoomseCloudsProcessor& p) : proc (p)
{
    hitsSeen = proc.getEngine().hitCount();
    startTimerHz (30);
}

void CloudView::timerCallback()
{
    const auto& e = proc.getEngine();
    const int count = e.hitCount();
    for (int i = std::max (hitsSeen, count - 24); i < count; ++i)
    {
        const int h = e.hitAt (i);
        marks.push_back ({ (float) (h & 4095) / 4096.f, (float) ((h >> 12) & 4095) / 4096.f, 1.f });
    }
    hitsSeen = count;
    for (auto& m : marks) m.a *= 0.92f;
    marks.erase (std::remove_if (marks.begin(), marks.end(), [] (const Mark& m) { return m.a < 0.03f; }), marks.end());
    head = e.writePosition();
    for (int i = 0; i < cl::Engine::bins; ++i) peaks[(size_t) i] = e.peak (i);
    repaint();
}

void CloudView::paint (juce::Graphics& g)
{
    const auto all = getLocalBounds().toFloat();
    g.setColour (look::paper2);
    g.fillRoundedRectangle (all, 6.f);
    const auto m = all.reduced (kInset, kInset + 6.f);
    // x: the position in the memory, turned so that the present (the write head) is on the right edge
    auto xOf = [&] (float pos) { float a = pos - head; while (a < 0.f) a += 1.f; while (a >= 1.f) a -= 1.f; return m.getX() + a * m.getWidth(); };

    for (const auto& mk : marks)
    {
        const float x0 = xOf (mk.start), w = std::max (2.f, mk.span * m.getWidth());
        g.setColour (look::accent.withAlpha (0.18f * mk.a + 0.02f));
        g.fillRect (x0, m.getY(), std::min (w, m.getRight() - x0), m.getHeight());
    }
    g.setColour (look::dim.withAlpha (0.7f));
    const float bw = m.getWidth() / (float) cl::Engine::bins, mid = m.getCentreY();
    for (int i = 0; i < cl::Engine::bins; ++i)
    {
        const float x = xOf ((float) i / (float) cl::Engine::bins);
        const float h = std::max (1.2f, std::sqrt (peaks[(size_t) i]) * m.getHeight() * 0.9f);
        g.fillRect (x, mid - h * 0.5f, std::max (1.f, bw * 0.75f), h);
    }
    g.setColour (look::accent);
    g.fillRect (m.getRight() - 1.5f, m.getY(), 2.f, m.getHeight());
    g.setFont (look::caps (9.5f, true, 0.14f));
    g.setColour (look::dim);
    g.drawText ("8 SECONDS AGO", all.reduced (kInset, 6.f), juce::Justification::topLeft, false);
    g.drawText ("NOW", all.reduced (kInset, 6.f), juce::Justification::topRight, false);
}

//==============================================================================
void PoomseCloudsEditor::makeKnob (const juce::String& paramId, const juce::String& text, const juce::String& help)
{
    auto* k = knobs.add (new Knob());
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 18);
    k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k->name.setText (text.toUpperCase(), juce::dontSendNotification);
    k->name.setJustificationType (juce::Justification::centred);
    k->name.setFont (look::caps (10.f));
    k->name.setColour (juce::Label::textColourId, look::dim);
    k->name.setInterceptsMouseClicks (false, false);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k->slider);
    helps.add (help);
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->name);
}

PoomseCloudsEditor::PoomseCloudsEditor (PoomseCloudsProcessor& p) : AudioProcessorEditor (&p), proc (p), view (p)
{
    setLookAndFeel (&laf);
    addAndMakeVisible (view);

    makeKnob ("position", "Position", "Where in the last seconds the grains are taken. Left: far in the past. Right: right now.");
    makeKnob ("size", "Size", "How long each grain is: from a click to a long wash.");
    makeKnob ("pitch", "Pitch", "Transposes the grains, two octaves up or down.");
    makeKnob ("density", "Density", "How many grains. Low: sparse and random. High: a dense, regular cloud. With Grid: the division (1/4 to 1/64).");
    makeKnob ("texture", "Texture", "The shape of a grain: plucked and sharp (left), round (middle), a soft flat cushion (right).");
    makeKnob ("spread", "Spread", "How far the grains are thrown to the left and right.");
    makeKnob ("blend", "Blend", "Dry sound on the left, clouds on the right.");
    makeKnob ("feedback", "Feedback", "The clouds go back into the memory: they feed themselves.");
    makeKnob ("reverb", "Reverb", "A long reverb behind the clouds.");
    makeKnob ("level", "Level", "Output level.");
    for (int i = 0; i < knobs.size(); ++i)
    {
        knobs[i]->slider.onDragStart = [this, i] { hovered = i; repaint(); };
        knobs[i]->slider.addMouseListener (this, false);
    }

    freezeButton.setComponentID ("toggle"); freezeButton.setClickingTogglesState (true);
    gridButton.setComponentID ("toggle");   gridButton.setClickingTogglesState (true);
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "freeze", freezeButton);
    gridAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "grid", gridButton);
    addAndMakeVisible (freezeButton); addAndMakeVisible (gridButton);

    randomButton.setComponentID ("ink");
    randomButton.onClick = [this] { proc.randomise(); };
    resetButton.setComponentID ("pill");
    resetButton.onClick = [this]
    {
        if (! resetArmed)
        {
            resetArmed = true;
            resetButton.setButtonText ("Sure?");
            juce::Timer::callAfterDelay (3000, [safe = juce::Component::SafePointer<PoomseCloudsEditor> (this)] { if (safe != nullptr) safe->disarmReset(); });
        }
        else { proc.resetToDefaults(); presetBox.setSelectedItemIndex (0, juce::dontSendNotification); disarmReset(); }
    };
    for (int i = 0; i < PoomseCloudsProcessor::getNumPresets(); ++i) presetBox.addItem (PoomseCloudsProcessor::getPresetName (i), i + 1);
    presetBox.setSelectedItemIndex (proc.getCurrentPreset(), juce::dontSendNotification);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedItemIndex(); if (i >= 0) proc.applyPreset (i); };
    prevPreset.setComponentID ("pill"); nextPreset.setComponentID ("pill");
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (+1); };
    for (auto* c : { (juce::Component*) &randomButton, (juce::Component*) &resetButton, (juce::Component*) &prevPreset, (juce::Component*) &nextPreset, (juce::Component*) &presetBox })
        addAndMakeVisible (c);

    setSize (kWidth, kHeight);
    startTimerHz (10);
}

PoomseCloudsEditor::~PoomseCloudsEditor() { stopTimer(); setLookAndFeel (nullptr); }

void PoomseCloudsEditor::timerCallback()
{
    int h = -1;
    for (int i = 0; i < knobs.size(); ++i) if (knobs[i]->slider.isMouseOverOrDragging()) h = i;
    if (h != hovered) { hovered = h; repaint(); }
}

void PoomseCloudsEditor::disarmReset() { resetArmed = false; resetButton.setButtonText ("Reset"); }

void PoomseCloudsEditor::stepPreset (int delta)
{
    const int n = PoomseCloudsProcessor::getNumPresets();
    presetBox.setSelectedItemIndex (((presetBox.getSelectedItemIndex() + delta) % n + n) % n, juce::sendNotificationSync);
}

void PoomseCloudsEditor::paint (juce::Graphics& g)
{
    g.fillAll (look::paper);
    const auto word = look::caps (21.f, true, 0.28f);
    g.setFont (word);
    const int w1 = (int) std::ceil (word.getStringWidthFloat ("POOMSE"));
    g.setColour (look::ink);
    g.drawText ("POOMSE", kPad, 16, w1 + 4, 30, juce::Justification::centredLeft, false);
    g.setColour (look::accent);
    g.drawText ("CLOUDS", kPad + w1 - 4, 16, 160, 30, juce::Justification::centredLeft, false);
    g.setColour (look::ink);
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.drawText (hovered >= 0 && hovered < helps.size() ? helps[hovered] : juce::String ("Play sound into it, then take it apart. Freeze holds what you have heard. Grid makes the grains follow the tempo."),
                kPad, getHeight() - 34, getWidth() - 2 * kPad, 20, juce::Justification::centredLeft, false);
}

void PoomseCloudsEditor::resized()
{
    randomButton.setBounds (getWidth() - kPad - 104, 18, 104, 30);
    resetButton.setBounds (randomButton.getX() - 10 - 80, 18, 80, 30);
    prevPreset.setBounds (kPad, 62, 26, 28);
    presetBox.setBounds (kPad + 32, 62, 250, 28);
    nextPreset.setBounds (kPad + 288, 62, 26, 28);
    gridButton.setBounds (getWidth() - kPad - 84, 64, 84, 26);
    freezeButton.setBounds (gridButton.getX() - 10 - 100, 64, 100, 26);

    const int contentW = getWidth() - 2 * kPad;
    view.setBounds (kPad, 104, contentW, 220);
    const int cw = contentW / 5;
    for (int i = 0; i < knobs.size(); ++i)
    {
        juce::Rectangle<int> cell (kPad + (i % 5) * cw, 340 + (i / 5) * 100, cw, 100);
        knobs[i]->name.setBounds (cell.removeFromTop (16));
        knobs[i]->slider.setBounds (cell);
    }
}
