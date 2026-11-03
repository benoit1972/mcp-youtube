#pragma once
#include "Look.h"
#include "PluginProcessor.h"

// The memory of the plugin, seen: the last 8 seconds of what came in (the present is on the right), and the grains that are taken from it.
class CloudView : public juce::Component, private juce::Timer
{
public:
    explicit CloudView (PoomseCloudsProcessor&);
    void paint (juce::Graphics&) override;

private:
    struct Mark { float start, span, a; };
    void timerCallback() override;
    PoomseCloudsProcessor& proc;
    int hitsSeen = 0;
    std::vector<Mark> marks;
    float head = 0.f;
    std::array<float, cl::Engine::bins> peaks {};
};

class PoomseCloudsEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PoomseCloudsEditor (PoomseCloudsProcessor&);
    ~PoomseCloudsEditor() override;
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
    PoomseCloudsProcessor& proc;
    CloudView view;
    juce::OwnedArray<Knob> knobs;
    juce::StringArray helps;
    juce::TextButton randomButton { "Random" }, resetButton { "Reset" }, freezeButton { "Freeze" }, gridButton { "Grid" };
    juce::TextButton prevPreset { "<" }, nextPreset { ">" };
    juce::ComboBox presetBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment, gridAttachment;
    juce::String helpText;
    int hovered = -1;
    bool resetArmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseCloudsEditor)
};
