#pragma once
#include "Look.h"
#include "PluginProcessor.h"

// Live spectrum of the pad (what is actually being synthesised), auto-scaled so it always shows the shape.
// It is also the drop zone: drag an audio file from the Finder onto it to load the sample.
class SpectrumView : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit SpectrumView (PoomsePadProcessor& p);
    void paint (juce::Graphics&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    PoomsePadProcessor& proc;
    float peakDb = -60.f;
    std::array<float, pad::kDisplayBands> smooth {};
    std::vector<float> peaks;
    juce::String name, message;
    int messageTicks = 0;
    bool dropHover = false;
};

// Pattern of the euclidean sequencer (hits filled, current step ringed).
class EuclidView : public juce::Component, private juce::Timer
{
public:
    explicit EuclidView (PoomsePadProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { if (isVisible()) repaint(); }
    PoomsePadProcessor& proc;
};

// Modulation matrix: rows = parameters, columns = sources (8 macros, 2 LFOs, sequencer).
// Drag a cell up/down to set its amount (Shift = fine), double-click to clear, wheel to scroll.
class ModMatrixView : public juce::Component, private juce::Timer
{
public:
    explicit ModMatrixView (PoomsePadProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverSrc = hoverDest = -1; repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override { if (isVisible()) repaint(); }
    bool cellAt (juce::Point<int>, int& src, int& dest) const;
    int maxScroll() const;

    PoomsePadProcessor& proc;
    float scroll = 0.f;
    int hoverSrc = -1, hoverDest = -1, dragSrc = -1, dragDest = -1, dragStartY = 0;
    float dragStartValue = 0.f;
};

class PoomsePadEditor : public juce::AudioProcessorEditor
{
public:
    explicit PoomsePadEditor (PoomsePadProcessor&);
    ~PoomsePadEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Cell { Big, Small, Mini };
    static constexpr int kNumKnobPages = 3;   // Pad, Tone, Mod   (page 3 = Matrix)

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
    struct Page
    {
        bool spectrum = false;
        std::vector<Row> rows;
    };

    void addRow (int page, const char* title, Cell cell, int cols,
                 std::initializer_list<std::pair<const char*, const char*>> params);
    void showPage (int index);
    void disarmReset();
    void stepPreset (int delta);

    PoomseLookAndFeel laf;
    PoomsePadProcessor& proc;
    juce::OwnedArray<Knob> knobs;
    std::array<Page, kNumKnobPages> pages;
    int currentPage = 0;

    SpectrumView spectrum;
    EuclidView euclid;
    ModMatrixView matrixView;
    juce::TextButton tabButtons[4];
    juce::TextButton randomButton { "Random" }, resetButton { "Reset" }, holdButton { "Hold" };
    juce::TextButton prevPreset { "<" }, nextPreset { ">" }, loopButton { "Loop" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopAttachment;
    juce::ComboBox presetBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> holdAttachment;
    bool resetArmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomsePadEditor)
};
