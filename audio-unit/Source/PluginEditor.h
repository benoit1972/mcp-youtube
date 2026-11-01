#pragma once
#include "Look.h"
#include "PluginProcessor.h"

// Waveform of the loaded sample with the grain position / spray range; click or drag to set the position.
class WaveformView : public juce::Component, private juce::Timer
{
public:
    explicit WaveformView (GlitchGrainProcessor&);
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
    bool dragging = false;
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

    void addRow (int page, const char* title, Cell cell,
                 std::initializer_list<std::pair<const char*, const char*>> params);
    void showPage (int index);

    PoomseLookAndFeel laf;
    GlitchGrainProcessor& proc;
    juce::OwnedArray<Knob> knobs;
    std::array<Page, 3> pages;
    int currentPage = 0;

    WaveformView waveform;
    juce::TextButton tabButtons[3];
    juce::TextButton loadButton { "Load" }, randomButton { "Random" };
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoomseGlitchEditor)
};
