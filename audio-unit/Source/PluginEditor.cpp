#include "PluginEditor.h"

GlitchGrainEditor::Knob& GlitchGrainEditor::addKnob (const char* paramId, const char* text)
{
    auto* k = knobs.add (new Knob());
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    k->label.setText (text, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramId, k->slider);
    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    return *k;
}

GlitchGrainEditor::GlitchGrainEditor (GlitchGrainProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    rowGrains.add (&addKnob ("position", "Position"), &addKnob ("spray", "Spray"), &addKnob ("size", "Grain Size"),
                   &addKnob ("density", "Density"), &addKnob ("shape", "Window"));
    rowPitch.add (&addKnob ("pitch", "Pitch"), &addKnob ("pitchSpray", "Pitch Spray"), &addKnob ("reverse", "Reverse"),
                  &addKnob ("pan", "Stereo"), &addKnob ("level", "Level"));
    rowEnv.add (&addKnob ("attack", "Attack"), &addKnob ("decay", "Decay"),
                &addKnob ("sustain", "Sustain"), &addKnob ("release", "Release"));
    rowFx.add (&addKnob ("stutter", "Stutter"), &addKnob ("stutterRate", "Stutter Rate"), &addKnob ("stutterPitch", "Stutter Pitch"),
               &addKnob ("glitch", "Glitch"), &addKnob ("glitchRate", "Glitch Rate"));

    sampleLabel.setText (processor.getSampleName(), juce::dontSendNotification);
    sampleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (sampleLabel);
    addAndMakeVisible (loadButton);

    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose an audio file", juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.m4a");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (f == juce::File()) return;
                                  if (processor.loadSample (f))
                                      sampleLabel.setText (processor.getSampleName(), juce::dontSendNotification);
                                  else
                                      sampleLabel.setText ("Could not read " + f.getFileName(), juce::dontSendNotification);
                              });
    };

    setSize (640, 560);
}

void GlitchGrainEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15161a));
    g.setColour (juce::Colour (0xffe8e8ea));
    g.setFont (juce::FontOptions (22.f, juce::Font::bold));
    g.drawText ("GLITCH GRAIN", 16, 10, 300, 30, juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (12.f, juce::Font::bold));
    g.setColour (juce::Colour (0xff6ec6ff));
    const char* titles[] = { "GRAINS", "PITCH / STEREO", "ENVELOPE", "STUTTER  &  GLITCH" };
    for (int i = 0; i < 4; ++i)
        g.drawText (titles[i], 16, 92 + i * 116, 300, 16, juce::Justification::centredLeft);
}

void GlitchGrainEditor::resized()
{
    auto top = getLocalBounds().reduced (16, 0).removeFromTop (60).withTrimmedTop (36);
    loadButton.setBounds (top.removeFromRight (130).reduced (0, 2));
    sampleLabel.setBounds (top.reduced (4, 0));

    auto layoutRow = [this] (const juce::Array<Knob*>& row, int y)
    {
        const int w = (getWidth() - 32) / 5;
        for (int i = 0; i < row.size(); ++i)
        {
            juce::Rectangle<int> r (16 + i * w, y, w, 96);
            row[i]->label.setBounds (r.removeFromTop (14));
            row[i]->slider.setBounds (r);
        }
    };
    layoutRow (rowGrains, 108);
    layoutRow (rowPitch, 224);
    layoutRow (rowEnv, 340);
    layoutRow (rowFx, 456);
}
