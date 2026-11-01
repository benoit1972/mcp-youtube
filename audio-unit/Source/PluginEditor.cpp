#include "PluginEditor.h"

namespace
{
constexpr int kWidth = 780, kHeight = 540, kPad = 28, kContentTop = 112;
constexpr int kWaveHeight = 96, kTitleHeight = 20, kRowGap = 10, kNameHeight = 14;

int cellHeight (int cell) { return cell == 0 ? 128 : cell == 1 ? 104 : 96; }
} // namespace

//==============================================================================
WaveformView::WaveformView (GlitchGrainProcessor& p) : proc (p)
{
    peaks = proc.getPeaks();
    name = proc.getSampleName();
    lastPos = proc.apvts.getRawParameterValue ("position")->load();
    lastSpray = proc.apvts.getRawParameterValue ("spray")->load();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    startTimerHz (30);
}

void WaveformView::timerCallback()
{
    const float pos = proc.apvts.getRawParameterValue ("position")->load();
    const float spray = proc.apvts.getRawParameterValue ("spray")->load();
    const auto n = proc.getSampleName();
    if (pos != lastPos || spray != lastSpray || n != name)
    {
        lastPos = pos; lastSpray = spray;
        if (n != name) { name = n; peaks = proc.getPeaks(); }
        repaint();
    }
}

void WaveformView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (look::panel);
    g.fillRoundedRectangle (r, 8.f);

    const auto inner = r.reduced (10.f, 14.f);
    if (! peaks.empty())
    {
        const float step = inner.getWidth() / (float) peaks.size();
        const float mid = inner.getCentreY();
        for (size_t i = 0; i < peaks.size(); ++i)
        {
            const float h = std::max (1.5f, peaks[i] * inner.getHeight());
            g.setColour (look::track.brighter (0.25f));
            g.fillRoundedRectangle (inner.getX() + (float) i * step + step * 0.2f, mid - h * 0.5f, std::max (1.f, step * 0.6f), h, 1.f);
        }
    }

    // spray range + position cursor
    const float px = inner.getX() + lastPos * inner.getWidth();
    const float sw = lastSpray * inner.getWidth();
    g.setColour (look::accent.withAlpha (0.16f));
    g.fillRect (juce::Rectangle<float> (px - sw, r.getY() + 1.f, sw * 2.f, r.getHeight() - 2.f).getIntersection (r));
    g.setColour (look::accent);
    g.fillRect (px - 1.f, r.getY() + 6.f, 2.f, r.getHeight() - 12.f);
    g.fillEllipse (px - 4.f, r.getY() + 3.f, 8.f, 8.f);

    g.setColour (look::dim);
    g.setFont (look::caps (10.5f, true, 0.12f));
    g.drawText (name.toUpperCase(), getLocalBounds().reduced (14, 6), juce::Justification::topLeft, false);
    g.drawText ("CLICK / DRAG = POSITION", getLocalBounds().reduced (14, 6), juce::Justification::bottomRight, false);
}

void WaveformView::setPositionFrom (const juce::MouseEvent& e)
{
    const float x = (float) (e.x - 10) / (float) std::max (1, getWidth() - 20);
    if (auto* p = proc.apvts.getParameter ("position"))
        p->setValueNotifyingHost (juce::jlimit (0.f, 1.f, x));
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (auto* p = proc.apvts.getParameter ("position")) p->beginChangeGesture();
    dragging = true;
    setPositionFrom (e);
}
void WaveformView::mouseDrag (const juce::MouseEvent& e) { if (dragging) setPositionFrom (e); }
void WaveformView::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
        if (auto* p = proc.apvts.getParameter ("position")) p->endChangeGesture();
    dragging = false;
}

//==============================================================================
void PoomseGlitchEditor::addRow (int page, const char* title, Cell cell,
                                 std::initializer_list<std::pair<const char*, const char*>> params)
{
    Row row;
    row.title = title;
    row.cell = cell;
    for (const auto& pr : params)
    {
        auto* k = knobs.add (new Knob());
        k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 110, 18);
        k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        k->slider.setColour (juce::Slider::textBoxTextColourId, look::text);
        k->name.setText (juce::String (pr.second).toUpperCase(), juce::dontSendNotification);
        k->name.setJustificationType (juce::Justification::centred);
        k->name.setFont (look::caps (10.5f));
        k->name.setColour (juce::Label::textColourId, look::dim);
        k->name.setInterceptsMouseClicks (false, false);
        k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, pr.first, k->slider);
        addChildComponent (k->slider);
        addChildComponent (k->name);
        row.knobs.add (k);
    }
    pages[(size_t) page].rows.push_back (std::move (row));
}

PoomseGlitchEditor::PoomseGlitchEditor (GlitchGrainProcessor& p)
    : AudioProcessorEditor (&p), proc (p), waveform (p)
{
    setLookAndFeel (&laf);

    // page 0: GRAIN (home, light)
    pages[0].waveform = true;
    addRow (0, "Grain cloud", Cell::Big, { { "position", "Position" }, { "spray", "Spray" }, { "size", "Grain size" },
                                           { "density", "Density" }, { "pitch", "Pitch" } });
    addRow (0, "Texture & output", Cell::Small, { { "shape", "Window" }, { "pitchSpray", "Pitch spray" }, { "reverse", "Reverse" },
                                                  { "pan", "Stereo" }, { "level", "Level" } });
    // page 1: PLAY
    addRow (1, "Envelope", Cell::Big, { { "attack", "Attack" }, { "decay", "Decay" }, { "sustain", "Sustain" }, { "release", "Release" } });
    addRow (1, "Drift  -  generative notes", Cell::Big, { { "drift", "Drift" }, { "driftRate", "Rate" }, { "driftScale", "Scale" },
                                                         { "driftRoot", "Root" }, { "driftLength", "Length" } });
    // page 2: FX
    addRow (2, "Stutter & glitch", Cell::Mini, { { "stutter", "Stutter" }, { "stutterRate", "Rate" }, { "stutterPitch", "Pitch" },
                                                 { "glitch", "Glitch" }, { "glitchRate", "Rate" } });
    addRow (2, "Tape & delay", Cell::Mini, { { "tape", "Tape" }, { "delayTime", "Time" }, { "delayFb", "Feedback" }, { "delayMix", "Mix" } });
    addRow (2, "Reverb & shimmer", Cell::Mini, { { "reverbSize", "Size" }, { "reverbDamp", "Damping" }, { "reverbMix", "Mix" }, { "shimmer", "Shimmer" } });

    addChildComponent (waveform);

    const char* tabNames[] = { "Grain", "Play", "FX" };
    for (int i = 0; i < 3; ++i)
    {
        auto& b = tabButtons[i];
        b.setButtonText (tabNames[i]);
        b.setComponentID ("tab");
        b.setClickingTogglesState (true);
        b.setRadioGroupId (101);
        b.onClick = [this, i] { showPage (i); };
        addAndMakeVisible (b);
    }
    tabButtons[0].setToggleState (true, juce::dontSendNotification);

    loadButton.setComponentID ("pill");
    randomButton.setComponentID ("pillAccent");
    addAndMakeVisible (loadButton);
    addAndMakeVisible (randomButton);

    randomButton.onClick = [this] { proc.randomise(); };
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose an audio file", juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.m4a");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (f != juce::File()) proc.loadSample (f);
                              });
    };

    setSize (kWidth, kHeight);
    showPage (0);
}

PoomseGlitchEditor::~PoomseGlitchEditor() { setLookAndFeel (nullptr); }

void PoomseGlitchEditor::showPage (int index)
{
    currentPage = index;
    for (size_t pi = 0; pi < pages.size(); ++pi)
    {
        const bool on = (int) pi == index;
        for (auto& row : pages[pi].rows)
            for (auto* k : row.knobs) { k->slider.setVisible (on); k->name.setVisible (on); }
    }
    waveform.setVisible (pages[(size_t) index].waveform);
    repaint();
}

void PoomseGlitchEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (look::bgTop, 0.f, 0.f, look::bgBottom, 0.f, (float) getHeight(), false));
    g.fillAll();

    // wordmark
    g.setFont (look::caps (21.f, true, 0.28f));
    g.setColour (look::text);
    g.drawText ("POOMSE", kPad, 16, 120, 30, juce::Justification::centredLeft, false);
    g.setColour (look::accent);
    g.drawText ("GLITCH", kPad + 112, 16, 140, 30, juce::Justification::centredLeft, false);

    // separator under the tabs
    g.setColour (look::line);
    g.fillRect (kPad, 100, getWidth() - 2 * kPad, 1);

    // section titles of the current page
    g.setFont (look::caps (10.5f, true, 0.2f));
    for (const auto& row : pages[(size_t) currentPage].rows)
    {
        if (row.title.isEmpty()) continue;
        const auto t = row.title.toUpperCase();
        g.setColour (look::dim);
        g.drawText (t, row.titleBounds, juce::Justification::centredLeft, false);
        const int tw = (int) look::caps (10.5f, true, 0.2f).getStringWidthFloat (t) + 12;
        g.setColour (look::line);
        g.fillRect (row.titleBounds.getX() + tw, row.titleBounds.getCentreY(), row.titleBounds.getWidth() - tw, 1);
    }
}

void PoomseGlitchEditor::resized()
{
    randomButton.setBounds (getWidth() - kPad - 104, 18, 104, 30);
    loadButton.setBounds (randomButton.getX() - 10 - 80, 18, 80, 30);
    for (int i = 0; i < 3; ++i)
        tabButtons[i].setBounds (kPad + i * 88, 66, 80, 30);

    const int contentW = getWidth() - 2 * kPad;
    const int cellW = contentW / 5;

    for (auto& page : pages)
    {
        int y = kContentTop;
        if (page.waveform)
        {
            waveform.setBounds (kPad, y, contentW, kWaveHeight);
            y += kWaveHeight + 14;
        }
        for (auto& row : page.rows)
        {
            if (row.title.isNotEmpty())
            {
                row.titleBounds = { kPad, y, contentW, kTitleHeight };
                y += kTitleHeight;
            }
            const int h = cellHeight ((int) row.cell);
            const int x0 = kPad + ((5 - row.knobs.size()) * cellW) / 2;
            for (int i = 0; i < row.knobs.size(); ++i)
            {
                juce::Rectangle<int> cell (x0 + i * cellW, y, cellW, h);
                row.knobs[i]->name.setBounds (cell.removeFromTop (kNameHeight));
                row.knobs[i]->slider.setBounds (cell);
            }
            y += h + kRowGap;
        }
    }
}
