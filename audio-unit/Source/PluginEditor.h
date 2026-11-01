#pragma once
#include "PluginProcessor.h"

class GlitchGrainEditor : public juce::AudioProcessorEditor
{
public:
    explicit GlitchGrainEditor (GlitchGrainProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Section
    {
        juce::String title;
        int column, row;
        juce::Array<Knob*> knobs;
    };

    Knob& addKnob (const char* paramId, const char* text);
    void addSection (const char* title, int column, int row,
                     std::initializer_list<std::pair<const char*, const char*>> params);

    GlitchGrainProcessor& processor;
    juce::OwnedArray<Knob> knobs;
    std::vector<Section> sections;
    juce::TextButton loadButton { "Load sample..." };
    juce::Label sampleLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlitchGrainEditor)
};
