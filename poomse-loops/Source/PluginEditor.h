#pragma once
#include "Look.h"
#include "PluginProcessor.h"

// The map. Every dot is a loop the finder found in the record: across = the place in the record, up = how long the loop is.
// The cross-hair is what the keys play (the loop closest to it). A key held lights up the loop it plays.
// Below the map, the record itself. Drop a sound file anywhere on it.
class RecordView : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit RecordView (PoomseLoopsProcessor&);
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

    PoomseLoopsProcessor& proc;
    PoomseLoopsProcessor::View view;
    int seenVersion = -1;
    juce::String name, message;
    int messageTicks = 0;
    bool dropHover = false, dragging = false;
};

class PoomseLoopsEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PoomseLoopsEditor (PoomseLoopsProcessor&);
    ~PoomseLoopsEditor() override;
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
    void timerCallback() override;

    PaperLookAndFeel laf;
    PoomseLoopsProcessor& proc;
    RecordView recordView;
    juce::OwnedArray<Knob> knobs;
    juce::TextButton randomButton { "Random" }, resetButton { "Reset" }, placesButton { "Keys = pitch" };
    juce::TextButton prevPreset { "<" }, nextPreset { ">" };
    juce::ComboBox presetBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> placesAttachment;
    bool resetArmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseLoopsEditor)
};
