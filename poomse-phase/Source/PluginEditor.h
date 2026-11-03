#pragma once
#include "Look.h"
#include "PluginProcessor.h"

// Every key you hold makes two cycles (the note and its octave). Each cycle is a row of steps of its own length: they all start together
// and slip apart. A filled dot is a step that sounds, the ring is the step that is playing now.
class PhaseView : public juce::Component, private juce::Timer
{
public:
    explicit PhaseView (PoomsePhaseProcessor&);
    void paint (juce::Graphics&) override;
    void showMessage (const juce::String& m, int seconds = 5) { message = m; messageTicks = 30 * seconds; repaint(); }
    void setDropHover (bool on, const juce::String& fileName = {}) { dropHover = on; dropName = fileName; repaint(); }

private:
    void timerCallback() override;
    PoomsePhaseProcessor& proc;
    int hitsSeen = 0, messageTicks = 0;
    juce::String message, dropName;
    bool dropHover = false;
    float glow[16] {};
};

class PadButton : public juce::Component
{
public:
    PadButton (juce::String text, juce::String sub, juce::String tag) : label (std::move (text)), noteName (std::move (sub)), tagline (std::move (tag)) {}
    void setLit (bool on) { if (on != lit) { lit = on; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { down = true; if (onDown) onDown(); repaint(); }
    void mouseUp (const juce::MouseEvent&) override { down = false; if (onUp) onUp(); repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; if (onHover) onHover (true); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; if (onHover) onHover (false); repaint(); }
    std::function<void()> onDown, onUp;
    std::function<void (bool)> onHover;

private:
    juce::String label, noteName, tagline;
    bool lit = false, down = false, hover = false;
};

class PoomsePhaseEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit PoomsePhaseEditor (PoomsePhaseProcessor&);
    ~PoomsePhaseEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int, int) override { view.setDropHover (true, files.isEmpty() ? juce::String() : juce::File (files[0]).getFileName()); }
    void fileDragExit (const juce::StringArray&) override { view.setDropHover (false); }
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
    void chooseSet (int i);

    PaperLookAndFeel laf;
    PoomsePhaseProcessor& proc;
    PhaseView view;
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<PadButton> setPads, keyPads;
    juce::TextButton randomButton { "Random" }, resetButton { "Reset" }, rerollButton { "Reroll" }, latchButton { "Latch" };
    juce::TextButton prevPreset { "<" }, nextPreset { ">" };
    juce::ComboBox presetBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> latchAttachment;
    juce::String helpText;
    int shownSeed = -1, shownSet = -1;
    bool resetArmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomsePhaseEditor)
};
