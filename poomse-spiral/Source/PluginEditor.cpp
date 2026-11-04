#include "PluginEditor.h"

namespace { constexpr int kWidth = 780, kHeight = 590, kPad = 28; constexpr float kInset = 12.f; }

void EchoView::paint (juce::Graphics& g)
{
    const auto all = getLocalBounds().toFloat();
    g.setColour (look::paper2);
    g.fillRoundedRectangle (all, 6.f);
    const auto m = all.reduced (kInset, kInset + 6.f);
    auto xOf = [&] (float pos) { float a = pos - head; while (a < 0.f) a += 1.f; while (a >= 1.f) a -= 1.f; return m.getX() + a * m.getWidth(); };
    g.setColour (look::accent.withAlpha (0.85f));
    const float bw = m.getWidth() / (float) sp::Engine::bins, mid = m.getCentreY();
    for (int i = 0; i < sp::Engine::bins; ++i)
    {
        const float x = xOf ((float) i / (float) sp::Engine::bins);
        const float h = std::max (1.2f, std::sqrt (std::min (1.f, peaks[(size_t) i] * 0.5f)) * m.getHeight() * 0.9f);
        g.fillRect (x, mid - h * 0.5f, std::max (1.f, bw * 0.75f), h);
    }
    g.setFont (look::caps (9.5f, true, 0.14f));
    g.setColour (look::dim);
    g.drawText ("THE ECHOES, 9 SECONDS BACK", all.reduced (kInset, 6.f), juce::Justification::topLeft, false);
    g.drawText ("NOW", all.reduced (kInset, 6.f), juce::Justification::topRight, false);
}

//====================================================================
void PoomseSpiralEditor::makeKnob (const juce::String& paramId, const juce::String& text, const juce::String& help)
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

PoomseSpiralEditor::PoomseSpiralEditor (PoomseSpiralProcessor& p) : AudioProcessorEditor (&p), proc (p), view (p)
{
    setLookAndFeel (&laf);
    addAndMakeVisible (view);

    makeKnob ("time", "Time", "The delay, locked to the tempo of the song.");
    makeKnob ("pitchL", "Pitch L", "How much the left echoes are transposed at every pass. Try +7 or +12: they climb.");
    makeKnob ("pitchR", "Pitch R", "The same for the right echoes. Different values make a wide, spiralling stereo.");
    makeKnob ("feedback", "Feedback", "How many times the echoes go round.");
    makeKnob ("tone", "Tone", "Dark on the left: every pass loses some brightness.");
    makeKnob ("drive", "Drive", "Warm saturation inside the loop: each pass gets a little dirtier.");
    makeKnob ("drift", "Drift", "A slow tape-like wobble of the delay time, different on each side.");
    makeKnob ("smear", "Smear", "Small: grainy, robotic echoes. Large: soft, diffuse, almost a reverb.");
    makeKnob ("mix", "Mix", "Dry sound on the left, echoes on the right.");
    makeKnob ("level", "Level", "Output level.");

    holdButton.setComponentID ("toggle"); holdButton.setClickingTogglesState (true);
    holdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "hold", holdButton);
    addAndMakeVisible (holdButton);

    randomButton.setComponentID ("ink");
    randomButton.onClick = [this] { proc.randomise(); };
    resetButton.setComponentID ("pill");
    resetButton.onClick = [this]
    {
        if (! resetArmed)
        {
            resetArmed = true;
            resetButton.setButtonText ("Sure?");
            juce::Timer::callAfterDelay (3000, [safe = juce::Component::SafePointer<PoomseSpiralEditor> (this)] { if (safe != nullptr) safe->disarmReset(); });
        }
        else { proc.resetToDefaults(); presetBox.setSelectedItemIndex (0, juce::dontSendNotification); disarmReset(); }
    };
    for (int i = 0; i < PoomseSpiralProcessor::getNumPresets(); ++i) presetBox.addItem (PoomseSpiralProcessor::getPresetName (i), i + 1);
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

PoomseSpiralEditor::~PoomseSpiralEditor() { stopTimer(); setLookAndFeel (nullptr); }

void PoomseSpiralEditor::timerCallback()
{
    int h = -1;
    for (int i = 0; i < knobs.size(); ++i) if (knobs[i]->slider.isMouseOverOrDragging()) h = i;
    if (h != hovered) { hovered = h; repaint(); }
}

void PoomseSpiralEditor::disarmReset() { resetArmed = false; resetButton.setButtonText ("Reset"); }

void PoomseSpiralEditor::stepPreset (int delta)
{
    const int n = PoomseSpiralProcessor::getNumPresets();
    presetBox.setSelectedItemIndex (((presetBox.getSelectedItemIndex() + delta) % n + n) % n, juce::sendNotificationSync);
}

void PoomseSpiralEditor::paint (juce::Graphics& g)
{
    g.fillAll (look::paper);
    const auto word = look::caps (21.f, true, 0.28f);
    g.setFont (word);
    const int w1 = (int) std::ceil (word.getStringWidthFloat ("POOMSE"));
    g.setColour (look::ink);
    g.drawText ("POOMSE", kPad, 16, w1 + 4, 30, juce::Justification::centredLeft, false);
    g.setColour (look::accent);
    g.drawText ("SPIRAL", kPad + w1 - 4, 16, 160, 30, juce::Justification::centredLeft, false);
    g.setColour (look::ink);
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.drawText (hovered >= 0 && hovered < helps.size() ? helps[hovered] : juce::String ("Set a Pitch: every echo is higher or lower than the one before. Hold keeps them turning."),
                kPad, getHeight() - 34, getWidth() - 2 * kPad, 20, juce::Justification::centredLeft, false);
}

void PoomseSpiralEditor::resized()
{
    randomButton.setBounds (getWidth() - kPad - 104, 18, 104, 30);
    resetButton.setBounds (randomButton.getX() - 10 - 80, 18, 80, 30);
    prevPreset.setBounds (kPad, 62, 26, 28);
    presetBox.setBounds (kPad + 32, 62, 250, 28);
    nextPreset.setBounds (kPad + 288, 62, 26, 28);
    holdButton.setBounds (getWidth() - kPad - 100, 64, 100, 26);

    const int contentW = getWidth() - 2 * kPad;
    view.setBounds (kPad, 104, contentW, 220);
    const int cw = contentW / 5;
    for (int i = 0; i < knobs.size(); ++i)
    {
        juce::Rectangle<int> cell (kPad + (i % 5) * cw, 330 + (i / 5) * 110, cw, 110);
        knobs[i]->name.setBounds (cell.removeFromTop (16));
        knobs[i]->slider.setBounds (cell);
    }
}
