#include "PluginEditor.h"

namespace
{
constexpr int kMargin = 16, kColWidth = 480, kColGap = 8, kRowHeight = 116, kRowsTop = 76, kKnobBlock = 96;
}

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

void GlitchGrainEditor::addSection (const char* title, int column, int row,
                                    std::initializer_list<std::pair<const char*, const char*>> params)
{
    Section s { title, column, row, {} };
    for (const auto& pr : params)
        s.knobs.add (&addKnob (pr.first, pr.second));
    sections.push_back (std::move (s));
}

GlitchGrainEditor::GlitchGrainEditor (GlitchGrainProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    // left column: the instrument
    addSection ("GRAINS", 0, 0, { { "position", "Position" }, { "spray", "Spray" }, { "size", "Grain Size" },
                                  { "density", "Density" }, { "shape", "Window" } });
    addSection ("PITCH / STEREO", 0, 1, { { "pitch", "Pitch" }, { "pitchSpray", "Pitch Spray" }, { "reverse", "Reverse" },
                                          { "pan", "Stereo" }, { "level", "Level" } });
    addSection ("ENVELOPE", 0, 2, { { "attack", "Attack" }, { "decay", "Decay" }, { "sustain", "Sustain" }, { "release", "Release" } });
    addSection ("DRIFT  (generative notes)", 0, 3, { { "drift", "Drift" }, { "driftRate", "Rate" }, { "driftScale", "Scale" },
                                                     { "driftRoot", "Root" }, { "driftLength", "Length" } });
    // right column: effects
    addSection ("STUTTER  &  GLITCH", 1, 0, { { "stutter", "Stutter" }, { "stutterRate", "Stutter Rate" }, { "stutterPitch", "Stutter Pitch" },
                                              { "glitch", "Glitch" }, { "glitchRate", "Glitch Rate" } });
    addSection ("TAPE  &  DELAY", 1, 1, { { "tape", "Tape" }, { "delayTime", "Time" }, { "delayFb", "Feedback" }, { "delayMix", "Mix" } });
    addSection ("REVERB  &  SHIMMER", 1, 2, { { "reverbSize", "Size" }, { "reverbDamp", "Damping" }, { "reverbMix", "Mix" }, { "shimmer", "Shimmer" } });

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

    setSize (2 * kMargin + 2 * kColWidth + kColGap, kRowsTop + 4 * kRowHeight + 8);
}

void GlitchGrainEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15161a));
    g.setColour (juce::Colour (0xffe8e8ea));
    g.setFont (juce::FontOptions (22.f, juce::Font::bold));
    g.drawText ("GLITCH GRAIN", kMargin, 10, 300, 30, juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (12.f, juce::Font::bold));
    g.setColour (juce::Colour (0xff6ec6ff));
    for (const auto& s : sections)
        g.drawText (s.title, kMargin + s.column * (kColWidth + kColGap), kRowsTop - 16 + s.row * kRowHeight,
                    kColWidth, 16, juce::Justification::centredLeft);
}

void GlitchGrainEditor::resized()
{
    auto top = juce::Rectangle<int> (getWidth() - 2 * kMargin, 28).withPosition (kMargin, 12);
    loadButton.setBounds (top.removeFromRight (130));
    sampleLabel.setBounds (top.removeFromRight (kColWidth).reduced (4, 0));

    for (const auto& s : sections)
        for (int i = 0; i < s.knobs.size(); ++i)
        {
            juce::Rectangle<int> r (kMargin + s.column * (kColWidth + kColGap) + i * (kColWidth / 5),
                                    kRowsTop + s.row * kRowHeight, kColWidth / 5, kKnobBlock);
            s.knobs[i]->label.setBounds (r.removeFromTop (14));
            s.knobs[i]->slider.setBounds (r);
        }
}
