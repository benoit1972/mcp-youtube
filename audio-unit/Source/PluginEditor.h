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

    Knob& addKnob (const char* paramId, const char* text);

    GlitchGrainProcessor& processor;
    juce::OwnedArray<Knob> knobs;
    juce::TextButton loadButton { "Load sample..." };
    juce::Label sampleLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    // rows of knobs
    juce::Array<Knob*> rowGrains, rowPitch, rowEnv, rowFx;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlitchGrainEditor)
};
