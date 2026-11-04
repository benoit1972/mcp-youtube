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
    juce::StringArray helps;
    juce::TextButton randomButton { "Random" }, resetButton { "Reset" }, rerollButton { "Reroll" }, prevPreset { "<" }, nextPreset { ">" };
    juce::ComboBox presetBox;
    juce::String helpText;
    int hovered = -1, shownSeed = -1;
    bool resetArmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseWarpEditor)
};
