#pragma once
#include "Look.h"
#include "PluginProcessor.h"

// The loop, seen as a map: every dot is a hit of the break (across = its place in the loop, up = how bright it sounds, kicks at
// the bottom, hats at the top). The line is the playhead. Every hit that sounds lights its dot: a roll, a backwards hit, a ghost
// each have their own mark, so what happens to the loop can be seen as it happens. Below, the break itself.
class BreakView : public juce::Component, private juce::Timer
{
public:
    explicit BreakView (PoomseShardsProcessor&);
    void paint (juce::Graphics&) override;
    void showMessage (const juce::String& m) { message = m; messageTicks = 120; }
    void setDropHover (bool on) { dropHover = on; repaint(); }

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
    PadButton (PoomseShardsProcessor& p, int keyIndex, int noteNumber, juce::String text, juce::String sub, juce::String tag)
        : proc (p), key (keyIndex), note (noteNumber), label (std::move (text)), noteName (std::move (sub)), tagline (std::move (tag)) {}
    void setLit (bool on) { if (on != lit) { lit = on; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { down = true; proc.uiNote (note, true); repaint(); }
    void mouseUp (const juce::MouseEvent&) override { down = false; proc.uiNote (note, false); repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; if (onHover) onHover (key); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; if (onHover) onHover (-1); repaint(); }
    std::function<void (int)> onHover;

private:
    PoomseShardsProcessor& proc;
    int key, note;
    juce::String label, noteName, tagline;
    bool lit = false, down = false, hover = false;
};

class PoomseShardsEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit PoomseShardsEditor (PoomseShardsProcessor&);
    ~PoomseShardsEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    // a sound dropped anywhere on the window is taken
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { breakView.setDropHover (true); }
    void fileDragExit (const juce::StringArray&) override { breakView.setDropHover (false); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    Knob& makeKnob (const juce::String& paramId, const juce::String& text);
    void load (const juce::File& file);
    void disarmReset();
    void stepPreset (int delta);
    void timerCallback() override;

    PaperLookAndFeel laf;
    PoomseShardsProcessor& proc;
    BreakView breakView;
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<PadButton> pads;
    juce::TextButton randomButton { "Random" }, resetButton { "Reset" }, rerollButton { "Reroll" }, loadButton { "Load" };
    juce::TextButton prevPreset { "<" }, nextPreset { ">" };
    juce::ComboBox presetBox;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String helpText;
    int shownSeed = -1;
    bool resetArmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseShardsEditor)
};
