#pragma once
#include "Look.h"
#include "PluginProcessor.h"

// The flock, seen: every voice leaves a ribbon as it glides from note to note of the scale (the faint lines). Time runs to the right.
class SkyView : public juce::Component, private juce::Timer
{
public:
    explicit SkyView (PoomseMurmurProcessor&);
    void paint (juce::Graphics&) override;
    void showMessage (const juce::String& m, int seconds = 5) { message = m; messageTicks = 30 * seconds; repaint(); }
    void setDropHover (bool on, const juce::String& fileName = {}) { dropHover = on; dropName = fileName; repaint(); }

private:
    static constexpr int history = 700;
    void timerCallback() override;
    PoomseMurmurProcessor& proc;
    std::array<std::array<float, history>, mur::kMaxVoices> pitch {}, level {};
    int head = 0, messageTicks = 0;
    juce::String message, dropName;
    bool dropHover = false;
};

class PoomseMurmurEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit PoomseMurmurEditor (PoomseMurmurProcessor&);
    ~PoomseMurmurEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int, int) override { sky.setDropHover (true, files.isEmpty() ? juce::String() : juce::File (files[0]).getFileName()); }
    void fileDragExit (const juce::StringArray&) override { sky.setDropHover (false); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void makeKnob (const juce::String& id, const juce::String& text, const juce::String& help);
    void load (const juce::File& file);
    void setSound (int index);
    void disarmReset();
    void timerCallback() override;

    PaperLookAndFeel laf;
    PoomseMurmurProcessor& proc;
    SkyView sky;
    juce::OwnedArray<Knob> knobs;
    juce::StringArray helps;
    juce::ComboBox scaleBox, keyBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> scaleAttachment, keyAttachment;
    juce::TextButton glassButton { "Glass" }, violinButton { "Violin" }, originalButton { "Back to the original" };
    juce::TextButton resetButton { "Reset" }, rerollButton { "Reroll" }, freeButton { "New each time" }, randomButton { "Random" };
    juce::String helpText;
    int hovered = -1, shownSeed = -1, shownSound = -1;
    bool resetArmed = false, shownCustom = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseMurmurEditor)
};
