#pragma once
#include "Look.h"
#include "PluginProcessor.h"

// Waveform of the loaded sample with the grain position / spray range; click or drag to set the position.
class WaveformView : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit WaveformView (GlitchGrainProcessor&);

    // drag & drop an audio file from the Finder onto the waveform to load it
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void setPositionFrom (const juce::MouseEvent&);

    GlitchGrainProcessor& proc;
    std::vector<float> peaks;
    juce::String name;
    float lastPos = -1.f, lastSpray = -1.f;
    bool dragging = false, dropHover = false;
    juce::String message;   // short feedback (e.g. unreadable file), shown for ~3 s
    int messageTicks = 0;
};

class PoomseGlitchEditor : public juce::AudioProcessorEditor
{
public:
    explicit PoomseGlitchEditor (GlitchGrainProcessor&);
    ~PoomseGlitchEditor() override;
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
        juce::Array<Knob*> knobs;
        juce::Rectangle<int> titleBounds;
    };
    struct Page
    {
        bool waveform = false;
        std::vector<Row> rows;
    };
    // Macros page: one knob + 3 (target, amount) slots per macro
    struct MacroColumn
    {
        Knob knob;
        juce::ComboBox target[gg::kSlotsPerMacro];
        juce::Slider amount[gg::kSlotsPerMacro];
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> targetAtt[gg::kSlotsPerMacro];
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAtt[gg::kSlotsPerMacro];
    };
    void buildMacroPage();

    void addRow (int page, const char* title, Cell cell,
                 std::initializer_list<std::pair<const char*, const char*>> params);
    void showPage (int index);

    PoomseLookAndFeel laf;
    GlitchGrainProcessor& proc;
    juce::OwnedArray<Knob> knobs;
    std::array<Page, 3> pages;
    std::array<std::unique_ptr<MacroColumn>, gg::kNumMacros> macroCols;
    juce::Rectangle<int> macroTitleBounds, macroHintBounds;
    int currentPage = 0;

    WaveformView waveform;
    juce::TextButton tabButtons[4];
    juce::TextButton randomButton { "Random" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseGlitchEditor)
};
