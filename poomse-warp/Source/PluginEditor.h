#pragma once
#include "Look.h"
#include "PluginProcessor.h"

// One cycle of the oscillator as it is now: the shape that Warp, Twist and Ratio make.
class WaveView : public juce::Component, private juce::Timer
{
public:
    explicit WaveView (PoomseWarpProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    PoomseWarpProcessor& proc;
};

class KeyPad : public juce::Component
{
public:
    KeyPad (juce::String t) : label (std::move (t)) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { down = true; if (onDown) onDown(); repaint(); }
    void mouseUp (const juce::MouseEvent&) override { down = false; if (onUp) onUp(); repaint(); }
    std::function<void()> onDown, onUp;
private:
    juce::String label; bool down = false;
};

class PoomseWarpEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PoomseWarpEditor (PoomseWarpProcessor&);
    ~PoomseWarpEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void makeKnob (const juce::String& id, const juce::String& text, const juce::String& help);
    void disarmReset();
    void stepPreset (int delta);
    void timerCallback() override;

    PaperLookAndFeel laf;
    PoomseWarpProcessor& proc;
    WaveView view;
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<KeyPad> keys;
    juce::StringArray helps;
    juce::TextButton randomButton { "Random" }, resetButton { "Reset" }, prevPreset { "<" }, nextPreset { ">" };
    juce::ComboBox presetBox;
    juce::String helpText;
    int hovered = -1;
    bool resetArmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseWarpEditor)
};
