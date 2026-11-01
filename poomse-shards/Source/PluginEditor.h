#pragma once
#include "Look.h"
#include "PluginProcessor.h"

// The loop, seen as a map: every dot is a hit of the break (across = its place in the loop, up = how bright it sounds, kicks at
// the bottom, hats at the top). The line is the playhead. Every hit that sounds lights its dot: a roll, a backwards hit, a ghost
// each have their own mark, so the damage can be seen as it happens. Below, the break itself. Drop a drum loop on it.
class BreakView : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit BreakView (PoomseShardsProcessor&);
    void paint (juce::Graphics&) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void pollHits();
    juce::Rectangle<float> mapArea() const;
    juce::Rectangle<float> stripArea() const;

    PoomseShardsProcessor& proc;
    PoomseShardsProcessor::View view;
    int seenVersion = -1, hitsSeen = 0, messageTicks = 0;
    juce::String name, message;
    bool dropHover = false;
    std::vector<float> glow, glowRoll, glowRev, glowGhost;
};

// A pad: held with the mouse like a key is held on the keyboard.
class PadButton : public juce::Component
{
public:
    PadButton (PoomseShardsProcessor& p, int noteNumber, juce::String text, juce::String sub, bool smallPad)
        : proc (p), note (noteNumber), label (std::move (text)), subLabel (std::move (sub)), small (smallPad) {}
    void setLit (bool on) { if (on != lit) { lit = on; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { down = true; proc.uiNote (note, true); repaint(); }
    void mouseUp (const juce::MouseEvent&) override { down = false; proc.uiNote (note, false); repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    PoomseShardsProcessor& proc;
    int note;
    juce::String label, subLabel;
    bool small, lit = false, down = false, hover = false;
};

class PoomseShardsEditor : public juce::AudioProcessorEditor, private juce::Timer
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
    void timerCallback() override;

    PaperLookAndFeel laf;
    PoomseShardsProcessor& proc;
    BreakView breakView;
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<PadButton> fillPads, keyPads;
    juce::TextButton randomButton { "Random" }, resetButton { "Reset" }, editsButton { "New edits" };
    juce::TextButton prevPreset { "<" }, nextPreset { ">" };
    juce::ComboBox presetBox;
    bool resetArmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseShardsEditor)
};
