#pragma once
#include "Look.h"
#include "PluginProcessor.h"

// The memory of the plugin, seen: the last 8 seconds of what came in (the present is on the right), and the grains that are taken from it.
class EchoView : public juce::Component, private juce::Timer
{
public:
    explicit EchoView (PoomseSpiralProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { head = proc.getEngine().writePosition(); for (int i = 0; i < sp::Engine::bins; ++i) peaks[(size_t) i] = proc.getEngine().peak (i); repaint(); }
    PoomseSpiralProcessor& proc;
    float head = 0.f;
    std::array<float, sp::Engine::bins> peaks {};
};

class PoomseSpiralEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PoomseSpiralEditor (PoomseSpiralProcessor&);
    ~PoomseSpiralEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void makeKnob (const juce::String& paramId, const juce::String& text, const juce::String& help);
    void disarmReset();
    void stepPreset (int delta);
    void timerCallback() override;

    PaperLookAndFeel laf;
    PoomseSpiralProcessor& proc;
    EchoView view;
    juce::OwnedArray<Knob> knobs;
    juce::StringArray helps;
    juce::TextButton randomButton { "Random" }, resetButton { "Reset" }, holdButton { "Hold" }, riseButton { "Rise" }, rerollButton { "Reroll" };
    juce::TextButton prevPreset { "<" }, nextPreset { ">" };
    juce::ComboBox presetBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> holdAttachment, riseAttachment;
    juce::String helpText;
    int hovered = -1;
    int shownSeed = -1;
    bool resetArmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseSpiralEditor)
};
