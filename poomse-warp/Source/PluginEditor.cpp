#include "PluginEditor.h"

namespace { constexpr int kWidth = 780, kHeight = 500, kPad = 28; }

void WaveView::paint (juce::Graphics& g)
{
    const auto all = getLocalBounds().toFloat();
    g.setColour (look::paper2);
    g.fillRoundedRectangle (all, 6.f);
    const auto m = all.reduced (16.f, 14.f);
    const float warp = *proc.apvts.getRawParameterValue ("warp"), twist = *proc.apvts.getRawParameterValue ("twist");
    const float ratio = wp::kRatios[std::clamp ((int) *proc.apvts.getRawParameterValue ("ratio"), 0, 6)];
    g.setColour (look::rule);
    g.fillRect (m.getX(), m.getCentreY() - 0.5f, m.getWidth(), 1.f);
    juce::Path path;
    const int N = 400;
    for (int i = 0; i <= N; ++i)
    {
        const float ph = (float) i / (float) N;
        float mp = ph * ratio; mp -= std::floor (mp);
        const float y = wp::shape (std::min (ph, 0.9999f), mp, warp, twist);
        const float px = m.getX() + ph * m.getWidth(), py = m.getCentreY() - y * m.getHeight() * 0.46f;
        if (i == 0) path.startNewSubPath (px, py); else path.lineTo (px, py);
    }
    g.setColour (look::accent);
    g.strokePath (path, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setFont (look::caps (9.5f, true, 0.14f));
    g.setColour (look::dim);
    g.drawText ("ONE CYCLE OF THE OSCILLATOR", all.reduced (14.f, 6.f), juce::Justification::topLeft, false);
}

void PoomseWarpEditor::makeKnob (const juce::String& id, const juce::String& text, const juce::String& help)
{
    auto* k = knobs.add (new Knob());
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 18);
    k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k->name.setText (text.toUpperCase(), juce::dontSendNotification);
    k->name.setJustificationType (juce::Justification::centred);
    k->name.setFont (look::caps (10.f));
    k->name.setColour (juce::Label::textColourId, look::dim);
    k->name.setInterceptsMouseClicks (false, false);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    helps.add (help);
    addAndMakeVisible (k->slider); addAndMakeVisible (k->name);
}

PoomseWarpEditor::PoomseWarpEditor (PoomseWarpProcessor& p) : AudioProcessorEditor (&p), proc (p), view (p)
{
    setLookAndFeel (&laf);
    addAndMakeVisible (view);
    makeKnob ("warp", "Warp", "Bends the sine: round on the left, sharp and bright on the right.");
    makeKnob ("twist", "Twist", "A second sine twists the first: metallic, bell-like sounds.");
    makeKnob ("ratio", "Ratio", "The pitch of that second sine, compared to the note. Whole numbers ring; 0.5 is deep.");
    makeKnob ("pluck", "Pluck", "How much Warp and the filter snap shut over the first moments of each note.");
    makeKnob ("cutoff", "Cutoff", "The filter: closed on the left, open on the right.");
    makeKnob ("reso", "Reso", "Emphasis at the cutoff.");
    makeKnob ("detune", "Detune", "Two copies of the sound, one on each side, slightly apart: wide and alive.");
    makeKnob ("attack", "Attack", "How slowly a note comes in.");
    makeKnob ("decay", "Decay", "How fast the note falls towards its Sustain level (and how long Pluck lasts).");
    makeKnob ("sustain", "Sustain", "The level a held note stays at.");
    makeKnob ("release", "Release", "How long a note rings after the key is let go.");
    makeKnob ("space", "Space", "A rhythmic echo and a long reverb.");
    makeKnob ("level", "Level", "Output level.");
    for (int i = 0; i < knobs.size(); ++i) knobs[i]->slider.setName ("k" + juce::String (i));

    randomButton.setComponentID ("ink");
    randomButton.onClick = [this] { proc.randomise(); };
    resetButton.setComponentID ("pill");
    resetButton.onClick = [this]
    {
        if (! resetArmed)
        {
            resetArmed = true; resetButton.setButtonText ("Sure?");
            juce::Timer::callAfterDelay (3000, [safe = juce::Component::SafePointer<PoomseWarpEditor> (this)] { if (safe != nullptr) safe->disarmReset(); });
        }
        else { proc.resetToDefaults(); presetBox.setSelectedItemIndex (0, juce::dontSendNotification); disarmReset(); }
    };
    for (int i = 0; i < PoomseWarpProcessor::getNumPresets(); ++i) presetBox.addItem (PoomseWarpProcessor::getPresetName (i), i + 1);
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

PoomseWarpEditor::~PoomseWarpEditor() { stopTimer(); setLookAndFeel (nullptr); }

void PoomseWarpEditor::timerCallback()
{
    int h = -1;
    for (int i = 0; i < knobs.size(); ++i) if (knobs[i]->slider.isMouseOverOrDragging()) h = i;
    if (h != hovered) { hovered = h; repaint(); }
}

void PoomseWarpEditor::disarmReset() { resetArmed = false; resetButton.setButtonText ("Reset"); }
void PoomseWarpEditor::stepPreset (int delta)
{
    const int n = PoomseWarpProcessor::getNumPresets();
    presetBox.setSelectedItemIndex (((presetBox.getSelectedItemIndex() + delta) % n + n) % n, juce::sendNotificationSync);
}

void PoomseWarpEditor::paint (juce::Graphics& g)
{
    g.fillAll (look::paper);
    const auto word = look::caps (21.f, true, 0.28f);
    g.setFont (word);
    const int w1 = (int) std::ceil (word.getStringWidthFloat ("POOMSE"));
    g.setColour (look::ink);
    g.drawText ("POOMSE", kPad, 16, w1 + 4, 30, juce::Justification::centredLeft, false);
    g.setColour (look::accent);
    g.drawText ("WARP", kPad + w1 - 4, 16, 160, 30, juce::Justification::centredLeft, false);
    g.setColour (look::ink);
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.drawText (hovered >= 0 && hovered < helps.size() ? helps[hovered] : juce::String ("Turn Warp and Twist first: that is where the sound is. Pluck makes it snap."),
                kPad, getHeight() - 34, getWidth() - 2 * kPad, 20, juce::Justification::centredLeft, false);
}

void PoomseWarpEditor::resized()
{
    randomButton.setBounds (getWidth() - kPad - 104, 18, 104, 30);
    resetButton.setBounds (randomButton.getX() - 10 - 80, 18, 80, 30);
    prevPreset.setBounds (kPad, 62, 26, 28);
    presetBox.setBounds (kPad + 32, 62, 250, 28);
    nextPreset.setBounds (kPad + 288, 62, 26, 28);
    const int contentW = getWidth() - 2 * kPad;
    view.setBounds (kPad, 100, contentW, 150);
    const int cols = 7, cw = contentW / cols;
    for (int i = 0; i < knobs.size(); ++i)
    {
        juce::Rectangle<int> cell (kPad + (i % cols) * cw, 264 + (i / cols) * 104, cw, 104);
        knobs[i]->name.setBounds (cell.removeFromTop (16));
        knobs[i]->slider.setBounds (cell);
    }
}
