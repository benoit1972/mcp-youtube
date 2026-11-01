#pragma once
#include "Look.h"
#include "PluginProcessor.h"

// The shape of the piece: the scenes that have played (light / deep / still), the arc, and where we are.
class FormView : public juce::Component, private juce::Timer
{
public:
    explicit FormView (PoomseAmbientProcessor& p) : proc (p) { startTimerHz (20); }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { if (isVisible()) repaint(); }
    PoomseAmbientProcessor& proc;
};

// Drop zone for the sample of one layer. It pulses when the layer plays a note.
class LayerZone : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    LayerZone (PoomseAmbientProcessor& p, int slotIndex);
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    bool tuned() const;
    void nudgeRoot (int delta);

    PoomseAmbientProcessor& proc;
    const int slot;
    std::vector<float> peaks;
    juce::String name, message;
    int messageTicks = 0;
    float act = 0.f;
    bool dropHover = false, night = true;
    juce::TextButton rootDown { "-" }, rootUp { "+" };
};

class PoomseAmbientEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PoomseAmbientEditor (PoomseAmbientProcessor&);
    ~PoomseAmbientEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Cell { Big, Small, Mini };
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    struct Row
    {
        juce::String title;
        Cell cell;
        int cols = 5;
        juce::Array<Knob*> knobs;
        juce::Rectangle<int> titleBounds;
    };
    struct Page { bool form = false; std::vector<Row> rows; };

    Knob& makeKnob (const juce::String& paramId, const juce::String& text);
    void addRow (int page, const char* title, Cell cell, int cols, std::initializer_list<std::pair<const char*, const char*>> params);
    void showPage (int index);
    void disarmReset();
    void stepPreset (int delta);
    void timerCallback() override;

    PoomseLookAndFeel laf;
    PoomseAmbientProcessor& proc;
    juce::OwnedArray<Knob> knobs;
    std::array<Page, 3> pages;               // 0 Piece, 1 Layers (custom), 2 Sound
    int currentPage = 0;

    FormView form;
    std::array<std::unique_ptr<LayerZone>, amb::kNumLayers> zones;
    std::array<std::array<Knob*, 3>, amb::kNumLayers> layerKnobs {};
    juce::Array<Knob*> nightOnlyKnobs;       // dark, crackle, tape: greyed out in Clean style

    juce::TextButton tabButtons[3];
    juce::TextButton randomButton { "Random" }, resetButton { "Reset" }, nightButton { "Night" }, keepButton { "Keep seed" };
    juce::TextButton prevPreset { "<" }, nextPreset { ">" };
    juce::ComboBox presetBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> nightAttachment;
    bool resetArmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseAmbientEditor)
};
