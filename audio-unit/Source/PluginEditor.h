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

// Pattern of the euclidean sequencer (hits filled, current step ringed).
class EuclidView : public juce::Component, private juce::Timer
{
public:
    explicit EuclidView (GlitchGrainProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { if (isVisible()) repaint(); }
    GlitchGrainProcessor& proc;
};

// Modulation matrix: rows = parameters, columns = sources (8 macros, 2 LFOs, sequencer).
// Drag a cell up/down to set its amount (Shift = fine), double-click to clear, wheel to scroll.
class ModMatrixView : public juce::Component, private juce::Timer
{
public:
    explicit ModMatrixView (GlitchGrainProcessor&);
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

    GlitchGrainProcessor& proc;
    float scroll = 0.f;
    int hoverSrc = -1, hoverDest = -1, dragSrc = -1, dragDest = -1, dragStartY = 0;
    float dragStartValue = 0.f;
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
    static constexpr int kNumKnobPages = 4;   // Grain, Play, FX, Mod   (page 4 = Matrix)

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
        bool waveform = false;
        std::vector<Row> rows;
    };

    void addRow (int page, const char* title, Cell cell, int cols,
                 std::initializer_list<std::pair<const char*, const char*>> params);
    void showPage (int index);
    void disarmReset();

    PoomseLookAndFeel laf;
    GlitchGrainProcessor& proc;
    juce::OwnedArray<Knob> knobs;
    std::array<Page, kNumKnobPages> pages;
    int currentPage = 0;

    WaveformView waveform;
    EuclidView euclid;
    ModMatrixView matrixView;
    juce::TextButton tabButtons[5];
    juce::TextButton randomButton { "Random" }, resetButton { "Reset" };
    bool resetArmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseGlitchEditor)
};
