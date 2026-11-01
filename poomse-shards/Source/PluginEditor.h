#pragma once
#include "Look.h"
#include "PluginProcessor.h"

// The map. Every dot is a piece of the break: across = the place in the break, up = how bright it sounds (kicks at the bottom,
// hats at the top). A key plays the dot at its height, near the vertical line. Every hit that plays lights its dot.
// Below the map, the break itself with its cuts. Drop a drum loop anywhere on it.
class BreakView : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit BreakView (PoomseShardsProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> mapArea() const;
    juce::Rectangle<float> stripArea() const;
    void setFromMouse (juce::Point<float>);
    void gesture (bool begin);
    void pollHits();

    PoomseShardsProcessor& proc;
    PoomseShardsProcessor::View view;
    int seenVersion = -1;
    juce::String name, message;
    int messageTicks = 0;
    bool dropHover = false, dragging = false;
    int hitsSeen = 0;
    std::vector<float> glow, glowFragment;
};

class PoomseShardsEditor : public juce::AudioProcessorEditor
{
public:
    explicit PoomseShardsEditor (PoomseShardsProcessor&);
    ~PoomseShardsEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    Knob& makeKnob (const juce::String& paramId, const juce::String& text);
    void disarmReset();
    void stepPreset (int delta);

    PaperLookAndFeel laf;
    PoomseShardsProcessor& proc;
    BreakView breakView;
    juce::OwnedArray<Knob> knobs;
    juce::TextButton randomButton { "Random" }, resetButton { "Reset" }, editsButton { "New edits" };
    juce::TextButton prevPreset { "<" }, nextPreset { ">" };
    juce::ComboBox presetBox;
    bool resetArmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseShardsEditor)
};
