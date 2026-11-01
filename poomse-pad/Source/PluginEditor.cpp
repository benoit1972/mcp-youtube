#include "PluginEditor.h"

namespace
{
constexpr int kWidth = 780, kHeight = 560, kPad = 28, kContentTop = 112;
constexpr int kSpectrumHeight = 104, kTitleHeight = 20, kRowGap = 10, kNameHeight = 14;

int cellHeight (int cell) { return cell == 0 ? 128 : cell == 1 ? 104 : 96; }
} // namespace

//==============================================================================
void SpectrumView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (look::panel);
    g.fillRoundedRectangle (r, 8.f);
    const auto inner = r.reduced (12.f, 14.f);

    // dB values, auto-scaled to a 55 dB window below the (slowly following) maximum
    float maxDb = -200.f;
    std::array<float, pad::kDisplayBands> db {};
    for (int b = 0; b < pad::kDisplayBands; ++b)
    {
        db[(size_t) b] = 20.f * std::log10 (proc.getDisplayBand (b) + 1e-6f);
        maxDb = std::max (maxDb, db[(size_t) b]);
    }
    peakDb += (juce::jmax (maxDb, -30.f) - peakDb) * 0.08f;

    juce::Path fill, line;
    for (int b = 0; b < pad::kDisplayBands; ++b)
    {
        const float target = juce::jlimit (0.f, 1.f, (db[(size_t) b] - (peakDb - 55.f)) / 55.f);
        smooth[(size_t) b] += (target - smooth[(size_t) b]) * 0.25f;
        const float x = inner.getX() + inner.getWidth() * (float) b / (float) (pad::kDisplayBands - 1);
        const float y = inner.getBottom() - smooth[(size_t) b] * inner.getHeight();
        if (b == 0) { fill.startNewSubPath (x, inner.getBottom()); fill.lineTo (x, y); line.startNewSubPath (x, y); }
        else        { fill.lineTo (x, y); line.lineTo (x, y); }
    }
    fill.lineTo (inner.getRight(), inner.getBottom());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (look::accent.withAlpha (0.45f), 0.f, inner.getY(),
                                             look::accent.withAlpha (0.03f), 0.f, inner.getBottom(), false));
    g.fillPath (fill);
    g.setColour (look::accent);
    g.strokePath (line, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (look::dim);
    g.setFont (look::caps (9.5f, true, 0.12f));
    g.drawText ("30 HZ", getLocalBounds().reduced (12, 5), juce::Justification::bottomLeft, false);
    g.drawText ("16 KHZ", getLocalBounds().reduced (12, 5), juce::Justification::bottomRight, false);
    g.drawText ("PAD SPECTRUM", getLocalBounds().reduced (12, 5), juce::Justification::topLeft, false);
}

//==============================================================================
SamplePanel::SamplePanel (PoomsePadProcessor& p, int slotIndex) : proc (p), slot (slotIndex)
{
    peaks = proc.getPeaks (slot);
    name = proc.getSampleName (slot);
    loopToggle.setComponentID ("toggle");
    loopToggle.setClickingTogglesState (true);
    loopAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, slot == 0 ? "loopA" : "loopB", loopToggle);
    addAndMakeVisible (loopToggle);
    startTimerHz (30);
}

void SamplePanel::resized()
{
    loopToggle.setBounds (getWidth() - 62, getHeight() - 28, 52, 20);
}

void SamplePanel::timerCallback()
{
    if (! isVisible()) return;
    const auto n = proc.getSampleName (slot);
    if (n != name) { name = n; peaks = proc.getPeaks (slot); }
    if (messageTicks > 0 && --messageTicks == 0) message = {};
    const float m = proc.getMorphDisplay();
    weight = slot == 0 ? 1.f - m : m;
    repaint();
}

bool SamplePanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;mp3;flac;m4a;ogg;caf")) return true;
    return false;
}

void SamplePanel::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (! file.hasFileExtension ("wav;aif;aiff;mp3;flac;m4a;ogg;caf")) continue;
        if (proc.loadSample (slot, file)) { name = proc.getSampleName (slot); peaks = proc.getPeaks (slot); message = {}; }
        else { message = "COULD NOT READ"; messageTicks = 90; }
        break; // one sample at a time
    }
    repaint();
}

void SamplePanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (look::panel);
    g.fillRoundedRectangle (r, 8.f);

    // outline brightness = how present this sample currently is
    g.setColour (look::accent.withAlpha (0.12f + 0.7f * weight));
    g.drawRoundedRectangle (r.reduced (0.75f), 8.f, 1.4f);

    const auto inner = r.reduced (10.f, 24.f);
    if (! peaks.empty())
    {
        const float step = inner.getWidth() / (float) peaks.size(), mid = inner.getCentreY();
        g.setColour (look::accent.withAlpha (0.18f + 0.5f * weight));
        for (size_t i = 0; i < peaks.size(); ++i)
        {
            const float h = std::max (1.5f, peaks[i] * inner.getHeight());
            g.fillRoundedRectangle (inner.getX() + (float) i * step + step * 0.2f, mid - h * 0.5f, std::max (1.f, step * 0.6f), h, 1.f);
        }
    }

    g.setColour (look::text.withAlpha (0.35f + 0.65f * weight));
    g.setFont (look::caps (15.f, true, 0.1f));
    g.drawText (slot == 0 ? "A" : "B", getLocalBounds().reduced (11, 6), juce::Justification::topLeft, false);

    g.setColour (look::dim);
    g.setFont (look::caps (9.f, true, 0.1f));
    g.drawText (name.toUpperCase(), getLocalBounds().reduced (11, 6).withTrimmedLeft (18), juce::Justification::topLeft, true);
    g.drawText (message.isNotEmpty() ? message : juce::String ("DROP SAMPLE " + juce::String (slot == 0 ? "A" : "B")),
                getLocalBounds().reduced (11, 6), juce::Justification::bottomLeft, false);

    if (dropHover)
    {
        g.setColour (look::accent.withAlpha (0.16f));
        g.fillRoundedRectangle (r, 8.f);
        g.setColour (look::accent);
        g.drawRoundedRectangle (r.reduced (1.f), 8.f, 2.f);
        g.setFont (look::caps (12.f, true, 0.2f));
        g.drawText ("DROP TO LOAD", getLocalBounds(), juce::Justification::centred, false);
    }
}

void EuclidView::paint (juce::Graphics& g)
{
    const int steps = (int) proc.apvts.getRawParameterValue ("seqSteps")->load();
    const int hits = juce::jmin (steps, (int) proc.apvts.getRawParameterValue ("seqHits")->load());
    const int rot = (int) proc.apvts.getRawParameterValue ("seqRot")->load();
    const int current = proc.getSeqStep() % juce::jmax (1, steps);
    const float pitch = juce::jmin (22.f, (float) getWidth() / (float) juce::jmax (1, steps));
    const float x0 = (float) getWidth() - pitch * (float) steps;
    const float cy = (float) getHeight() * 0.5f;
    for (int i = 0; i < steps; ++i)
    {
        const float cx = x0 + pitch * ((float) i + 0.5f);
        const bool hit = gg::euclidHit (i, steps, hits, rot);
        const float r = hit ? 5.f : 3.f;
        if (hit) { g.setColour (look::accent); g.fillEllipse (cx - r, cy - r, r * 2.f, r * 2.f); }
        else     { g.setColour (look::lineHi); g.drawEllipse (cx - r, cy - r, r * 2.f, r * 2.f, 1.2f); }
        if (i == current)
        {
            g.setColour (look::text);
            g.drawEllipse (cx - 8.f, cy - 8.f, 16.f, 16.f, 1.5f);
        }
    }
}

//==============================================================================
namespace
{
constexpr int kLabelW = 122, kHeaderH = 44, kRowH = 20;
const char* const kSourceNames[gg::kNumSources] = { "M1", "M2", "M3", "M4", "M5", "M6", "M7", "M8", "LFO1", "LFO2", "SEQ" };
}

ModMatrixView::ModMatrixView (PoomsePadProcessor& p) : proc (p)
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    startTimerHz (20);
}

int ModMatrixView::maxScroll() const
{
    return juce::jmax (0, gg::kNumDests * kRowH - (getHeight() - kHeaderH));
}

bool ModMatrixView::cellAt (juce::Point<int> pt, int& src, int& dest) const
{
    if (pt.x < kLabelW || pt.y < kHeaderH) return false;
    const int cw = (getWidth() - kLabelW) / gg::kNumSources;
    src = (pt.x - kLabelW) / cw;
    dest = (int) ((float) (pt.y - kHeaderH) + scroll) / kRowH;
    return src >= 0 && src < gg::kNumSources && dest >= 0 && dest < gg::kNumDests;
}

void ModMatrixView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (look::panel);
    g.fillRoundedRectangle (r, 8.f);

    const int cw = (getWidth() - kLabelW) / gg::kNumSources;

    // header: source names + live value bars
    for (int s = 0; s < gg::kNumSources; ++s)
    {
        const auto cell = juce::Rectangle<int> (kLabelW + s * cw, 0, cw, kHeaderH);
        g.setColour (s >= gg::kNumMacros ? look::accent : look::dim);
        g.setFont (look::caps (10.5f));
        g.drawText (kSourceNames[s], cell.withTrimmedBottom (16), juce::Justification::centred, false);

        const float v = proc.getSourceValue (s);
        const bool bipolar = s == gg::kSrcLfo1 || s == gg::kSrcLfo2;
        auto bar = juce::Rectangle<float> ((float) cell.getX() + 6.f, (float) kHeaderH - 14.f, (float) cw - 12.f, 4.f);
        g.setColour (look::track);
        g.fillRoundedRectangle (bar, 2.f);
        g.setColour (look::accent);
        if (bipolar)
        {
            const float mid = bar.getCentreX(), end = mid + v * bar.getWidth() * 0.5f;
            g.fillRect (juce::Rectangle<float> (std::min (mid, end), bar.getY(), std::abs (end - mid), bar.getHeight()));
        }
        else
            g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.f, 1.f, v)));
        if (s == gg::kNumMacros - 1 || s == gg::kSrcLfo2)
        {
            g.setColour (look::line);
            g.fillRect (cell.getRight() - 1, 6, 1, getHeight() - 12);
        }
    }
    g.setColour (look::line);
    g.fillRect (8, kHeaderH - 1, getWidth() - 16, 1);

    // rows (clipped under the header)
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (0, kHeaderH, getWidth(), getHeight() - kHeaderH);
    for (int d = 0; d < gg::kNumDests; ++d)
    {
        const int y = kHeaderH + d * kRowH - (int) scroll;
        if (y + kRowH < kHeaderH || y > getHeight()) continue;

        bool used = false;
        for (int s = 0; s < gg::kNumSources; ++s) used = used || proc.getMatrix (s, d) != 0.f;
        if (d % 2 == 0) { g.setColour (look::bgBottom.withAlpha (0.25f)); g.fillRect (4, y, getWidth() - 8, kRowH); }

        g.setColour (used ? look::text : look::dim);
        g.setFont (juce::Font (juce::FontOptions (11.5f)));
        g.drawText (gg::kModDests[d].label, 12, y, kLabelW - 14, kRowH, juce::Justification::centredLeft, false);

        for (int s = 0; s < gg::kNumSources; ++s)
        {
            const float a = proc.getMatrix (s, d);
            auto cell = juce::Rectangle<float> ((float) (kLabelW + s * cw) + 2.f, (float) y + 2.f, (float) cw - 4.f, (float) kRowH - 4.f);
            const bool hot = (s == hoverSrc && d == hoverDest) || (s == dragSrc && d == dragDest);
            g.setColour (look::bgBottom.withAlpha (0.55f));
            g.fillRoundedRectangle (cell, 3.f);
            if (a != 0.f)
            {
                const auto c = a > 0.f ? look::accent : look::negative;
                g.setColour (c.withAlpha (0.18f + 0.62f * std::abs (a)));
                g.fillRoundedRectangle (cell, 3.f);
                g.setColour (look::text);
                g.setFont (juce::Font (juce::FontOptions (10.5f)));
                g.drawText (juce::String (juce::roundToInt (a * 100.f)), cell.toNearestInt(), juce::Justification::centred, false);
            }
            if (hot) { g.setColour (look::lineHi); g.drawRoundedRectangle (cell, 3.f, 1.2f); }
        }
    }

    // scroll indicator
    if (maxScroll() > 0)
    {
        const float trackH = (float) (getHeight() - kHeaderH - 8);
        const float thumbH = juce::jmax (24.f, trackH * trackH / (float) (gg::kNumDests * kRowH));
        const float y = (float) kHeaderH + 4.f + (trackH - thumbH) * scroll / (float) maxScroll();
        g.setColour (look::lineHi);
        g.fillRoundedRectangle ((float) getWidth() - 6.f, y, 3.f, thumbH, 1.5f);
    }
}

void ModMatrixView::mouseMove (const juce::MouseEvent& e)
{
    int s, d;
    if (cellAt (e.getPosition(), s, d)) { hoverSrc = s; hoverDest = d; }
    else hoverSrc = hoverDest = -1;
    repaint();
}

void ModMatrixView::mouseDown (const juce::MouseEvent& e)
{
    int s, d;
    if (cellAt (e.getPosition(), s, d))
    {
        dragSrc = s; dragDest = d;
        dragStartY = e.y;
        dragStartValue = proc.getMatrix (s, d);
    }
    else dragSrc = dragDest = -1;
}

void ModMatrixView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragSrc < 0) return;
    const float range = e.mods.isShiftDown() ? 700.f : 140.f; // pixels for the full -1..+1 swing
    float v = dragStartValue + (float) (dragStartY - e.y) / range * 2.f;
    v = std::round (juce::jlimit (-1.f, 1.f, v) * 100.f) / 100.f;
    if (std::abs (v) < 0.02f) v = 0.f; // easy to land exactly on "off"
    proc.setMatrix (dragSrc, dragDest, v);
    repaint();
}

void ModMatrixView::mouseDoubleClick (const juce::MouseEvent& e)
{
    int s, d;
    if (cellAt (e.getPosition(), s, d)) { proc.setMatrix (s, d, 0.f); repaint(); }
}

void ModMatrixView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    scroll = juce::jlimit (0.f, (float) maxScroll(), scroll - w.deltaY * 140.f);
    repaint();
}


//==============================================================================
void PoomsePadEditor::addRow (int page, const char* title, Cell cell, int cols,
                              std::initializer_list<std::pair<const char*, const char*>> params)
{
    Row row;
    row.title = title;
    row.cell = cell;
    row.cols = cols;
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

PoomsePadEditor::PoomsePadEditor (PoomsePadProcessor& p)
    : AudioProcessorEditor (&p), proc (p), spectrum (p), panelA (p, 0), panelB (p, 1), euclid (p), matrixView (p)
{
    setLookAndFeel (&laf);

    // page 0: PAD (home)
    pages[0].spectrum = true;
    addRow (0, "Pad", Cell::Big, 6, { { "morph", "Morph A-B" }, { "time", "Time" }, { "rise", "Rise" }, { "evolve", "Evolve" },
                                      { "rate", "Evolve rate" }, { "mix", "Mix" } });
    addRow (0, "Layers  -  the same sound, shifted", Cell::Small, 4, { { "sub", "Sub -12" }, { "body", "Body" },
                                                                        { "fifth", "Fifth +7" }, { "octave", "Octave +12" } });
    // page 1: TONE
    pages[1].spectrum = true;
    addRow (1, "Tone, space & output", Cell::Big, 5, { { "tone", "Tone" }, { "width", "Width" }, { "revSize", "Reverb size" },
                                                       { "revMix", "Reverb mix" }, { "level", "Level" } });
    // page 2: MOD sources
    addRow (2, "Macros  -  map these to your Rack in Live, aim them in the Matrix", Cell::Mini, 8,
            { { "macro1", "Macro 1" }, { "macro2", "Macro 2" }, { "macro3", "Macro 3" }, { "macro4", "Macro 4" },
              { "macro5", "Macro 5" }, { "macro6", "Macro 6" }, { "macro7", "Macro 7" }, { "macro8", "Macro 8" } });
    addRow (2, "LFO 1  /  LFO 2", Cell::Mini, 6, { { "lfo1Rate", "L1 Rate" }, { "lfo1Sync", "L1 Sync" }, { "lfo1Shape", "L1 Shape" },
                                                   { "lfo2Rate", "L2 Rate" }, { "lfo2Sync", "L2 Sync" }, { "lfo2Shape", "L2 Shape" } });
    addRow (2, "Euclidean sequencer", Cell::Mini, 6, { { "seqSteps", "Steps" }, { "seqHits", "Hits" }, { "seqRot", "Rotate" },
                                                       { "seqRate", "Rate" }, { "seqDecay", "Decay" }, { "seqRandom", "Random" } });

    addChildComponent (spectrum);
    addChildComponent (panelA);
    addChildComponent (panelB);
    addChildComponent (euclid);
    addChildComponent (matrixView);

    const char* tabNames[] = { "Pad", "Tone", "Mod", "Matrix" };
    for (int i = 0; i < 4; ++i)
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

    randomButton.setComponentID ("pillAccent");
    addAndMakeVisible (randomButton);
    // Random acts on the page you are on; on Mod/Matrix it gives the 8 macros random targets and values.
    // Shift + click = Pad + Tone.
    randomButton.onClick = [this]
    {
        using S = PoomsePadProcessor::RandomScope;
        const bool all = juce::ModifierKeys::currentModifiers.isShiftDown();
        proc.randomise (all ? S::AllSound : currentPage == 0 ? S::Pad : currentPage == 1 ? S::Tone : S::Mod);
    };

    // Reset needs a second click within 3 s, so a stray click never wipes a sound in progress
    resetButton.setComponentID ("pill");
    addAndMakeVisible (resetButton);
    resetButton.onClick = [this]
    {
        if (! resetArmed)
        {
            resetArmed = true;
            resetButton.setButtonText ("Sure?");
            juce::Timer::callAfterDelay (3000, [safe = juce::Component::SafePointer<PoomsePadEditor> (this)]
                                         { if (safe != nullptr) safe->disarmReset(); });
        }
        else
        {
            proc.resetToDefaults();
            presetBox.setSelectedItemIndex (0, juce::dontSendNotification); // Reset = the "Init" preset
            disarmReset();
        }
    };

    holdButton.setComponentID ("toggle");
    holdButton.setClickingTogglesState (true);
    holdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "hold", holdButton);
    addAndMakeVisible (holdButton);

    layersButton.setComponentID ("toggle");   // off = Morph (A turns into B), on = Layers (A and B always together)
    layersButton.setClickingTogglesState (true);
    layersAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "layers", layersButton);
    addAndMakeVisible (layersButton);

    // preset menu: < [ name v ] >
    for (int i = 0; i < PoomsePadProcessor::getNumPresets(); ++i)
        presetBox.addItem (PoomsePadProcessor::getPresetName (i), i + 1);
    presetBox.setSelectedItemIndex (proc.getCurrentPreset(), juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        const int i = presetBox.getSelectedItemIndex();
        if (i >= 0) proc.applyPreset (i);
    };
    prevPreset.setComponentID ("pill");
    nextPreset.setComponentID ("pill");
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (+1); };
    addAndMakeVisible (presetBox);
    addAndMakeVisible (prevPreset);
    addAndMakeVisible (nextPreset);

    setSize (kWidth, kHeight);
    showPage (0);
}

void PoomsePadEditor::stepPreset (int delta)
{
    const int n = PoomsePadProcessor::getNumPresets();
    const int i = ((presetBox.getSelectedItemIndex() + delta) % n + n) % n;
    presetBox.setSelectedItemIndex (i, juce::sendNotificationSync); // onChange applies it
}

PoomsePadEditor::~PoomsePadEditor() { setLookAndFeel (nullptr); }

void PoomsePadEditor::disarmReset()
{
    resetArmed = false;
    resetButton.setButtonText ("Reset");
}

void PoomsePadEditor::showPage (int index)
{
    currentPage = index;
    for (size_t pi = 0; pi < pages.size(); ++pi)
    {
        const bool on = (int) pi == index;
        for (auto& row : pages[pi].rows)
            for (auto* k : row.knobs) { k->slider.setVisible (on); k->name.setVisible (on); }
    }
    const bool showSpectrum = index < kNumKnobPages && pages[(size_t) index].spectrum;
    spectrum.setVisible (showSpectrum);
    panelA.setVisible (showSpectrum);
    panelB.setVisible (showSpectrum);
    layersButton.setVisible (showSpectrum);
    euclid.setVisible (index == 2);
    matrixView.setVisible (index == 3);
    resized();
    repaint();
}

void PoomsePadEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (look::bgTop, 0.f, 0.f, look::bgBottom, 0.f, (float) getHeight(), false));
    g.fillAll();

    // wordmark: PAD sits right next to POOMSE (measured, so it never overlaps nor drifts away)
    const auto wordFont = look::caps (21.f, true, 0.28f);
    g.setFont (wordFont);
    const int poomseW = (int) std::ceil (wordFont.getStringWidthFloat ("POOMSE"));
    g.setColour (look::text);
    g.drawText ("POOMSE", kPad, 16, poomseW + 4, 30, juce::Justification::centredLeft, false);
    g.setColour (look::accent);
    g.drawText ("PAD", kPad + poomseW - 4, 16, 120, 30, juce::Justification::centredLeft, false);

    g.setColour (look::line);
    g.fillRect (kPad, 100, getWidth() - 2 * kPad, 1);

    g.setFont (look::caps (9.5f, true, 0.12f));
    g.setColour (look::dim);
    g.drawText ("RANDOM = THIS PAGE   -   SHIFT = ALL", getWidth() - kPad - 260, 50, 260, 12, juce::Justification::centredRight, false);

    if (currentPage == 3)
    {
        g.setFont (juce::Font (juce::FontOptions (11.f)));
        g.drawText ("Drag a cell up / down = amount (Shift = fine)   -   double-click = clear   -   wheel = scroll   -   teal = +, amber = -",
                    kPad, getHeight() - 26, getWidth() - 2 * kPad, 16, juce::Justification::centredLeft, false);
        return;
    }

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

void PoomsePadEditor::resized()
{
    randomButton.setBounds (getWidth() - kPad - 104, 18, 104, 30);
    resetButton.setBounds (randomButton.getX() - 10 - 90, 18, 90, 30);
    holdButton.setBounds (resetButton.getX() - 8 - 70, 18, 70, 30);
    prevPreset.setBounds (244, 18, 26, 30);
    presetBox.setBounds (274, 18, 150, 30);
    nextPreset.setBounds (428, 18, 26, 30);
    for (int i = 0; i < 4; ++i)
        tabButtons[i].setBounds (kPad + i * 88, 66, 80, 30);

    const int contentW = getWidth() - 2 * kPad;
    matrixView.setBounds (kPad, kContentTop, contentW, getHeight() - kContentTop - 34);

    for (size_t pi = 0; pi < pages.size(); ++pi)
    {
        auto& page = pages[pi];
        int y = kContentTop;
        if (page.spectrum)
        {
            const int zoneW = 176, gap = 8;
            panelA.setBounds (kPad, y, zoneW, kSpectrumHeight);
            panelB.setBounds (kPad + contentW - zoneW, y, zoneW, kSpectrumHeight);
            spectrum.setBounds (kPad + zoneW + gap, y, contentW - 2 * (zoneW + gap), kSpectrumHeight);
            layersButton.setBounds (spectrum.getRight() - 70, y + 6, 58, 22);
            layersButton.toFront (false);
            y += kSpectrumHeight + 14;
        }
        for (auto& row : page.rows)
        {
            if (row.title.isNotEmpty())
            {
                row.titleBounds = { kPad, y, contentW, kTitleHeight };
                y += kTitleHeight;
            }
            if (pi == 2 && row.title.startsWith ("Euclidean"))
                euclid.setBounds (kPad + contentW - 360, y - kTitleHeight, 360, kTitleHeight);
            const int h = cellHeight ((int) row.cell);
            const int cw = contentW / row.cols;
            const int x0 = kPad + ((row.cols - row.knobs.size()) * cw) / 2;
            for (int i = 0; i < row.knobs.size(); ++i)
            {
                juce::Rectangle<int> cell (x0 + i * cw, y, cw, h);
                row.knobs[i]->name.setBounds (cell.removeFromTop (kNameHeight));
                row.knobs[i]->slider.setBounds (cell);
            }
            y += h + kRowGap;
        }
    }
}
