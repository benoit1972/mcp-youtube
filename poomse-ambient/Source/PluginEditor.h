#pragma once
#include "Look.h"
#include "PluginProcessor.h"

// The sound that is loaded, seen as a wave, with a mark for every note that is playing in it.
class SoundView : public juce::Component, private juce::Timer
{
public:
    explicit SoundView (PoomseAmbientProcessor&);
    void paint (juce::Graphics&) override;
    void showMessage (const juce::String& m, int seconds = 5) { message = m; messageTicks = 30 * seconds; repaint(); }
    void setDropHover (bool on, const juce::String& fileName = {}) { dropHover = on; dropName = fileName; repaint(); }

private:
    void timerCallback() override;
    PoomseAmbientProcessor& proc;
    PoomseAmbientProcessor::View view;
    int seenVersion = -1, messageTicks = 0;
    float marks[amb::Sampler::displayVoices] {};
    juce::String message, dropName;
    bool dropHover = false;
};

class SoundPad : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    SoundPad (juce::String t, juce::String sub) : label (std::move (t)), tagline (std::move (sub)) {}
    void setLit (bool on) { if (on != lit) { lit = on; repaint(); } }
    void setCustom (const juce::String& name) { if (name != custom) { custom = name; repaint(); } }
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { dropOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override { dropOver = false; repaint(); if (onFiles) onFiles (files); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) { if (onRestore) onRestore(); return; } down = true; if (onDown) onDown(); repaint(); }
    void mouseUp (const juce::MouseEvent&) override { if (! down) return; down = false; if (onUp) onUp(); repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    std::function<void()> onDown, onUp, onRestore;
    std::function<void (const juce::StringArray&)> onFiles;
private:
    juce::String label, tagline, custom;
    bool lit = false, down = false, hover = false, dropOver = false;
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
    void makeKnob (const juce::String& id, const juce::String& text, const juce::String& help);
    void choose (int index);
    void load (const juce::File& file, int slot);
    void disarmReset();
    void timerCallback() override;

    PaperLookAndFeel laf;
    PoomseAmbientProcessor& proc;
    SoundView view;
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<SoundPad> pads;
    juce::StringArray helps;
    juce::TextButton resetButton { "Reset" }, latchButton { "Latch" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> latchAttachment;
    juce::String helpText;
    int hovered = -1, shownSound = -1;
    bool resetArmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseAmbientEditor)
};
