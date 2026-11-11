#pragma once
#include "Look.h"
#include "PluginProcessor.h"

// The piece, seen: the chord that is playing, the mood of the moment, and where we are on the arc of the whole piece.
class PieceView : public juce::Component, private juce::Timer
{
public:
    explicit PieceView (PoomseAmbientProcessor&);
    void paint (juce::Graphics&) override;
    void showMessage (const juce::String& m, int seconds = 5) { message = m; messageTicks = 30 * seconds; repaint(); }
    void setDropHover (bool on, const juce::String& fileName = {}) { dropHover = on; dropName = fileName; repaint(); }

private:
    void timerCallback() override { if (messageTicks > 0 && --messageTicks == 0) message = {}; repaint(); }
    PoomseAmbientProcessor& proc;
    int messageTicks = 0;
    juce::String message, dropName;
    bool dropHover = false;
};

class SoundPad : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    SoundPad (juce::String t, juce::String sub) : label (std::move (t)), tagline (std::move (sub)) {}
    void setActivity (float a) { if (std::abs (a - activity) > 0.03f) { activity = a; repaint(); } }
    void setCustom (const juce::String& name) { if (name != custom) { custom = name; repaint(); } }
    void paint (juce::Graphics&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { dropOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override { dropOver = false; repaint(); if (onFiles) onFiles (files); }
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) { if (onRestore) onRestore(); return; } down = true; if (onDown) onDown(); repaint(); }
    void mouseUp (const juce::MouseEvent&) override { down = false; repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    std::function<void()> onDown, onRestore;
    std::function<void (const juce::StringArray&)> onFiles;
private:
    juce::String label, tagline, custom;
    float activity = 0.f;
    bool down = false, hover = false, dropOver = false;
};

class PoomseAmbientEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit PoomseAmbientEditor (PoomseAmbientProcessor&);
    ~PoomseAmbientEditor() override;
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
    Knob* makeKnob (juce::OwnedArray<Knob>& into, const juce::String& id, const juce::String& text, const juce::String& help);
    void load (const juce::File& file, int slot);
    void disarmReset();
    void timerCallback() override;

    PaperLookAndFeel laf;
    PoomseAmbientProcessor& proc;
    PieceView view;
    juce::OwnedArray<Knob> amountKnobs, grainKnobs, knobs;
    juce::ComboBox scaleBox, keyBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> scaleAttachment, keyAttachment;
    int selectedPad = 0;
    juce::OwnedArray<SoundPad> pads;
    juce::StringArray helps;
    juce::TextButton resetButton { "Reset" }, rerollButton { "Reroll" }, freeButton { "New each time" }, randomButton { "Random" };
    juce::String helpText;
    int hovered = -1, shownSeed = -1;
    bool resetArmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseAmbientEditor)
};
