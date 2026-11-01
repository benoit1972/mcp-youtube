#include "PluginEditor.h"

namespace
{
constexpr int kWidth = 780, kHeight = 560, kPad = 28, kContentTop = 112;
constexpr int kFormHeight = 150, kTitleHeight = 20, kRowGap = 10, kNameHeight = 14;
int cellHeight (int cell) { return cell == 0 ? 128 : cell == 1 ? 104 : 96; }
juce::Colour moodColour (int mood) { return mood == amb::Light ? look::moodLight : mood == amb::Deep ? look::moodDeep : look::moodStill; }
const char* moodName (int mood) { return mood == amb::Light ? "LIGHT" : mood == amb::Deep ? "DEEP" : "STILL"; }
juce::String layerLabel (int slot, bool night) { return slot == amb::Beat && ! night ? juce::String ("Bells") : juce::String (amb::kLayerNames[slot]); }
}

//==============================================================================
void FormView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (look::panel);
    g.fillRoundedRectangle (r, 8.f);

    const float progress = proc.getProgress();
    const bool playing = progress >= 0.f;
    const auto timeline = juce::Rectangle<float> (r.getX() + 14.f, r.getY() + 34.f, r.getWidth() - 28.f, 62.f);

    // the scenes played so far, as blocks
    g.setColour (look::bgBottom.withAlpha (0.55f));
    g.fillRoundedRectangle (timeline, 4.f);
    if (playing)
    {
        const int n = proc.getSceneCount();
        for (int i = 0; i < n; ++i)
        {
            const float a = proc.getSceneStart (i), b = i + 1 < n ? proc.getSceneStart (i + 1) : progress;
            auto block = juce::Rectangle<float> (timeline.getX() + timeline.getWidth() * juce::jlimit (0.f, 1.f, a), timeline.getY(),
                                                 std::max (1.5f, timeline.getWidth() * juce::jlimit (0.f, 1.f, b - a)), timeline.getHeight());
            g.setColour (moodColour (proc.getSceneMood (i)));
            g.fillRect (block.reduced (0.5f, 0.f));
        }
    }

    // the planned arc (how full the piece is meant to be, from the "Journey" setting)
    juce::Path arc;
    const float journey = proc.apvts.getRawParameterValue ("journey")->load();
    for (int i = 0; i <= 120; ++i)
    {
        const float p = (float) i / 120.f;
        const float x = timeline.getX() + timeline.getWidth() * p, y = timeline.getBottom() - 4.f - (timeline.getHeight() - 10.f) * amb::arcDensity (p, journey);
        if (i == 0) arc.startNewSubPath (x, y); else arc.lineTo (x, y);
    }
    g.setColour (look::accent.withAlpha (0.75f));
    g.strokePath (arc, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (playing)   // where we are
    {
        const float x = timeline.getX() + timeline.getWidth() * juce::jlimit (0.f, 1.f, progress);
        g.setColour (look::text);
        g.fillRect (x - 1.f, timeline.getY() - 4.f, 2.f, timeline.getHeight() + 8.f);
    }

    // text
    g.setFont (look::caps (9.5f, true, 0.14f));
    g.setColour (look::dim);
    const int bars = (int) proc.apvts.getRawParameterValue ("bars")->load();
    g.drawText (playing ? "BAR " + juce::String (1 + (int) (progress * (float) bars)) + " / " + juce::String (bars)
                        : juce::String ("HOLD ONE LONG NOTE FOR THE LENGTH OF THE PIECE"),
                getLocalBounds().reduced (14, 10).withTrimmedRight (200), juce::Justification::topLeft, false);
    const int seed = proc.getSeedInUse();
    if (seed > 0) g.drawText ("SEED " + juce::String (seed), getLocalBounds().reduced (14, 10).withTrimmedRight (96), juce::Justification::topRight, false);

    // layers: a light for each one that plays a note
    const bool night = proc.isNight();
    const float cw = timeline.getWidth() / (float) amb::kNumLayers;
    for (int k = 0; k < amb::kNumLayers; ++k)
    {
        const float a = juce::jlimit (0.f, 1.f, proc.getActivity (k)), cx = timeline.getX() + cw * ((float) k + 0.18f), cy = timeline.getBottom() + 22.f;
        g.setColour (look::accent.withAlpha (0.14f + 0.86f * a));
        g.fillEllipse (cx - 5.f, cy - 5.f, 10.f, 10.f);
        g.setColour (look::dim);
        g.drawText (layerLabel (k, night).toUpperCase(), juce::Rectangle<float> (cx + 9.f, cy - 7.f, cw - 20.f, 14.f).toNearestInt(), juce::Justification::centredLeft, false);
    }
    // legend
    float lx = r.getRight() - 14.f - 176.f;
    for (int m = 0; m < 3; ++m)
    {
        g.setColour (moodColour (m)); g.fillRect (lx, r.getBottom() - 21.f, 8.f, 8.f);
        g.setColour (look::dim);      g.drawText (moodName (m), juce::Rectangle<float> (lx + 11.f, r.getBottom() - 25.f, 46.f, 16.f).toNearestInt(), juce::Justification::centredLeft, false);
        lx += 60.f;
    }
}

//==============================================================================
LayerZone::LayerZone (PoomseAmbientProcessor& p, int slotIndex) : proc (p), slot (slotIndex)
{
    peaks = proc.getPeaks (slot);
    name = proc.getSampleName (slot);
    rootDown.setComponentID ("pill"); rootUp.setComponentID ("pill");
    rootDown.onClick = [this] { nudgeRoot (-1); };
    rootUp.onClick = [this] { nudgeRoot (+1); };
    addChildComponent (rootDown); addChildComponent (rootUp);
    startTimerHz (20);
}

bool LayerZone::tuned() const { return slot == amb::Keys || slot == amb::Vocals || slot == amb::Pad || slot == amb::Sub || (slot == amb::Beat && ! night); }

void LayerZone::nudgeRoot (int delta)
{
    if (auto* pr = proc.apvts.getParameter (juce::String (amb::kLayerNames[slot]).toLowerCase() + "Root"))
        pr->setValueNotifyingHost (pr->convertTo0to1 ((float) juce::jlimit (0, 127, (int) pr->convertFrom0to1 (pr->getValue()) + delta)));
}

void LayerZone::resized()
{
    rootDown.setBounds (getWidth() - 44, getHeight() - 24, 18, 18);
    rootUp.setBounds (getWidth() - 24, getHeight() - 24, 18, 18);
}

void LayerZone::timerCallback()
{
    if (! isVisible()) return;
    const auto n = proc.getSampleName (slot);
    if (n != name) { name = n; peaks = proc.getPeaks (slot); }
    if (messageTicks > 0 && --messageTicks == 0) message = {};
    act = juce::jmax (act * 0.9f, proc.getActivity (slot));
    const bool nowNight = proc.isNight();
    if (nowNight != night) { night = nowNight; peaks = proc.getPeaks (slot); }
    rootDown.setVisible (tuned()); rootUp.setVisible (tuned());
    repaint();
}

bool LayerZone::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;mp3;flac;m4a;ogg;caf")) return true;
    return false;
}

void LayerZone::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (! file.hasFileExtension ("wav;aif;aiff;mp3;flac;m4a;ogg;caf")) continue;
        if (proc.loadSample (slot, file)) { name = proc.getSampleName (slot); peaks = proc.getPeaks (slot); message = {}; }
        else { message = "COULD NOT READ"; messageTicks = 90; }
        break;
    }
    repaint();
}

void LayerZone::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (look::panel);
    g.fillRoundedRectangle (r, 8.f);
    g.setColour (look::accent.withAlpha (0.15f + 0.75f * juce::jlimit (0.f, 1.f, act)));
    g.drawRoundedRectangle (r.reduced (0.75f), 8.f, 1.4f);

    const auto inner = r.reduced (9.f, 26.f);
    if (! peaks.empty())
    {
        const float step = inner.getWidth() / (float) peaks.size(), mid = inner.getCentreY();
        g.setColour (look::accent.withAlpha (0.2f + 0.4f * juce::jlimit (0.f, 1.f, act)));
        for (size_t i = 0; i < peaks.size(); ++i)
        {
            const float h = std::max (1.5f, peaks[i] * inner.getHeight() * 0.9f);
            g.fillRoundedRectangle (inner.getX() + (float) i * step + step * 0.15f, mid - h * 0.5f, std::max (1.f, step * 0.7f), h, 1.f);
        }
    }

    g.setColour (look::text);
    g.setFont (look::caps (12.f, true, 0.12f));
    g.drawText (layerLabel (slot, night).toUpperCase(), getLocalBounds().reduced (9, 6), juce::Justification::topLeft, false);
    g.setColour (look::dim);
    g.setFont (look::caps (8.5f, true, 0.08f));
    g.drawText (name.toUpperCase(), getLocalBounds().reduced (9, 6).withTrimmedTop (14), juce::Justification::topLeft, true);

    juce::String bottom;
    if (message.isNotEmpty()) bottom = message;
    else if (tuned())
    {
        const auto* pr = proc.apvts.getRawParameterValue (juce::String (amb::kLayerNames[slot]).toLowerCase() + "Root");
        bottom = "ROOT " + juce::MidiMessage::getMidiNoteName ((int) pr->load(), true, true, 3).toUpperCase();
    }
    else bottom = "DROP A SAMPLE";
    g.drawText (bottom, getLocalBounds().reduced (9, 6), juce::Justification::bottomLeft, false);

    if (dropHover)
    {
        g.setColour (look::accent.withAlpha (0.16f)); g.fillRoundedRectangle (r, 8.f);
        g.setColour (look::accent); g.drawRoundedRectangle (r.reduced (1.f), 8.f, 2.f);
        g.setFont (look::caps (11.f, true, 0.2f));
        g.drawText ("DROP", getLocalBounds(), juce::Justification::centred, false);
    }
}

//==============================================================================
PoomseAmbientEditor::Knob& PoomseAmbientEditor::makeKnob (const juce::String& paramId, const juce::String& text)
{
    auto* k = knobs.add (new Knob());
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 110, 18);
    k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k->slider.setColour (juce::Slider::textBoxTextColourId, look::text);
    k->name.setText (text.toUpperCase(), juce::dontSendNotification);
    k->name.setJustificationType (juce::Justification::centred);
    k->name.setFont (look::caps (10.5f));
    k->name.setColour (juce::Label::textColourId, look::dim);
    k->name.setInterceptsMouseClicks (false, false);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k->slider);
    addChildComponent (k->slider);
    addChildComponent (k->name);
    return *k;
}

void PoomseAmbientEditor::addRow (int page, const char* title, Cell cell, int cols,
                                  std::initializer_list<std::pair<const char*, const char*>> params)
{
    Row row;
    row.title = title; row.cell = cell; row.cols = cols;
    for (const auto& pr : params) row.knobs.add (&makeKnob (pr.first, pr.second));
    pages[(size_t) page].rows.push_back (std::move (row));
}

PoomseAmbientEditor::PoomseAmbientEditor (PoomseAmbientProcessor& p)
    : AudioProcessorEditor (&p), proc (p), form (p)
{
    setLookAndFeel (&laf);

    // page 0: the piece
    pages[0].form = true;
    addRow (0, "The piece  -  four feelings and a key", Cell::Mini, 7,
            { { "space", "Space" }, { "depth", "Depth" }, { "surprise", "Surprise" }, { "journey", "Journey" },
              { "bars", "Length" }, { "scale", "Scale" }, { "seed", "Seed" } });
    // page 2: the sound
    addRow (2, "Night colour  -  only in the Night style", Cell::Big, 3, { { "dark", "Dark" }, { "crackle", "Crackle" }, { "tape", "Tape" } });
    for (auto* k : pages[2].rows[0].knobs) nightOnlyKnobs.add (k);
    addRow (2, "Space", Cell::Big, 4, { { "reverb", "Reverb" }, { "echo", "Echo" }, { "cloud", "Cloud" }, { "level", "Level" } });
    // page 1: the layers (custom: a drop zone and three knobs per layer)
    for (int k = 0; k < amb::kNumLayers; ++k)
    {
        zones[(size_t) k] = std::make_unique<LayerZone> (proc, k);
        addChildComponent (*zones[(size_t) k]);
        const auto id = juce::String (amb::kLayerNames[k]).toLowerCase();
        layerKnobs[(size_t) k][0] = &makeKnob (id + "Level", "Level");
        layerKnobs[(size_t) k][1] = &makeKnob (id + "Amount", "Amount");
        layerKnobs[(size_t) k][2] = &makeKnob (id + "Space", "Space");
    }
    addChildComponent (form);

    const char* tabNames[] = { "Piece", "Layers", "Sound" };
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

    randomButton.setComponentID ("pillAccent");
    addAndMakeVisible (randomButton);
    randomButton.onClick = [this]
    {
        using S = PoomseAmbientProcessor::RandomScope;
        const bool all = juce::ModifierKeys::currentModifiers.isShiftDown();
        proc.randomise (all ? S::AllSound : currentPage == 0 ? S::Piece : currentPage == 1 ? S::Layers : S::Sound);
    };

    resetButton.setComponentID ("pill");
    addAndMakeVisible (resetButton);
    resetButton.onClick = [this]
    {
        if (! resetArmed)
        {
            resetArmed = true;
            resetButton.setButtonText ("Sure?");
            juce::Timer::callAfterDelay (3000, [safe = juce::Component::SafePointer<PoomseAmbientEditor> (this)] { if (safe != nullptr) safe->disarmReset(); });
        }
        else { proc.resetToDefaults(); presetBox.setSelectedItemIndex (0, juce::dontSendNotification); disarmReset(); }
    };

    nightButton.setComponentID ("toggle");   // on = Night (dusty, ghost beat), off = Clean (bells, no dust)
    nightButton.setClickingTogglesState (true);
    nightAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "night", nightButton);
    addAndMakeVisible (nightButton);

    keepButton.setComponentID ("pill");
    keepButton.onClick = [this] { proc.keepSeed(); };
    addChildComponent (keepButton);

    for (int i = 0; i < PoomseAmbientProcessor::getNumPresets(); ++i) presetBox.addItem (PoomseAmbientProcessor::getPresetName (i), i + 1);
    presetBox.setSelectedItemIndex (proc.getCurrentPreset(), juce::dontSendNotification);
    presetBox.onChange = [this] { const int i = presetBox.getSelectedItemIndex(); if (i >= 0) proc.applyPreset (i); };
    prevPreset.setComponentID ("pill"); nextPreset.setComponentID ("pill");
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (+1); };
    addAndMakeVisible (presetBox); addAndMakeVisible (prevPreset); addAndMakeVisible (nextPreset);

    setSize (kWidth, kHeight);
    showPage (0);
    startTimerHz (5);
}

PoomseAmbientEditor::~PoomseAmbientEditor() { stopTimer(); setLookAndFeel (nullptr); }

void PoomseAmbientEditor::timerCallback()
{
    const float a = proc.isNight() ? 1.f : 0.3f;   // the night colour knobs rest in the Clean style
    for (auto* k : nightOnlyKnobs) { k->slider.setAlpha (a); k->name.setAlpha (a); }
}

void PoomseAmbientEditor::disarmReset() { resetArmed = false; resetButton.setButtonText ("Reset"); }

void PoomseAmbientEditor::stepPreset (int delta)
{
    const int n = PoomseAmbientProcessor::getNumPresets();
    presetBox.setSelectedItemIndex (((presetBox.getSelectedItemIndex() + delta) % n + n) % n, juce::sendNotificationSync);
}

void PoomseAmbientEditor::showPage (int index)
{
    currentPage = index;
    for (size_t pi = 0; pi < pages.size(); ++pi)
        for (auto& row : pages[pi].rows)
            for (auto* k : row.knobs) { k->slider.setVisible ((int) pi == index); k->name.setVisible ((int) pi == index); }
    for (int k = 0; k < amb::kNumLayers; ++k)
    {
        zones[(size_t) k]->setVisible (index == 1);
        for (auto* kn : layerKnobs[(size_t) k]) { kn->slider.setVisible (index == 1); kn->name.setVisible (index == 1); }
    }
    form.setVisible (index == 0);
    keepButton.setVisible (index == 0);
    resized();
    repaint();
}

void PoomseAmbientEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (look::bgTop, 0.f, 0.f, look::bgBottom, 0.f, (float) getHeight(), false));
    g.fillAll();

    const auto wordFont = look::caps (21.f, true, 0.28f);
    g.setFont (wordFont);
    const int poomseW = (int) std::ceil (wordFont.getStringWidthFloat ("POOMSE"));
    g.setColour (look::text);
    g.drawText ("POOMSE", kPad, 16, poomseW + 4, 30, juce::Justification::centredLeft, false);
    g.setColour (look::accent);
    g.drawText ("AMBIENT", kPad + poomseW - 4, 16, 190, 30, juce::Justification::centredLeft, false);

    g.setColour (look::line);
    g.fillRect (kPad, 100, getWidth() - 2 * kPad, 1);
    g.setFont (look::caps (9.5f, true, 0.12f));
    g.setColour (look::dim);
    g.drawText ("RANDOM = THIS PAGE   -   SHIFT = ALL", getWidth() - kPad - 260, 50, 260, 12, juce::Justification::centredRight, false);

    if (currentPage == 1)
    {
        g.setFont (juce::Font (juce::FontOptions (11.f)));
        g.drawText ("Drop a sample on each layer.  Amount = how often it plays.  Space = how much goes into the reverb.  +/- = the note the sample is in.",
                    kPad, getHeight() - 26, getWidth() - 2 * kPad, 16, juce::Justification::centredLeft, false);
        return;
    }
    if (currentPage == 0)
    {
        g.setFont (juce::Font (juce::FontOptions (11.f)));
        g.drawText ("Put one long note in a clip (as long as the piece): its pitch is the key. Everything else is composed for you, and is never the same twice.",
                    kPad, getHeight() - 26, getWidth() - 2 * kPad, 16, juce::Justification::centredLeft, false);
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

void PoomseAmbientEditor::resized()
{
    randomButton.setBounds (getWidth() - kPad - 104, 18, 104, 30);
    resetButton.setBounds (randomButton.getX() - 10 - 90, 18, 90, 30);
    nightButton.setBounds (resetButton.getX() - 10 - 80, 18, 80, 30);
    for (int i = 0; i < 3; ++i) tabButtons[i].setBounds (kPad + i * 88, 66, 80, 30);
    prevPreset.setBounds (getWidth() - kPad - 330, 66, 26, 30);
    presetBox.setBounds (getWidth() - kPad - 300, 66, 244, 30);
    nextPreset.setBounds (getWidth() - kPad - 52, 66, 26, 30);

    const int contentW = getWidth() - 2 * kPad;

    // page 1: six columns, each a drop zone over three knobs
    const int colW = contentW / amb::kNumLayers;
    for (int k = 0; k < amb::kNumLayers; ++k)
    {
        const int x = kPad + k * colW;
        zones[(size_t) k]->setBounds (x + 3, kContentTop, colW - 6, 104);
        for (int j = 0; j < 3; ++j)
        {
            juce::Rectangle<int> cell (x, kContentTop + 112 + j * 98, colW, 96);
            layerKnobs[(size_t) k][(size_t) j]->name.setBounds (cell.removeFromTop (kNameHeight));
            layerKnobs[(size_t) k][(size_t) j]->slider.setBounds (cell);
        }
    }

    // pages 0 and 2: rows of knobs
    for (size_t pi = 0; pi < pages.size(); ++pi)
    {
        auto& page = pages[pi];
        int y = kContentTop;
        if (page.form)
        {
            form.setBounds (kPad, y, contentW, kFormHeight);
            keepButton.setBounds (kPad + contentW - 14 - 84, y + 6, 84, 22);
            keepButton.toFront (false);
            y += kFormHeight + 14;
        }
        for (auto& row : page.rows)
        {
            if (row.title.isNotEmpty()) { row.titleBounds = { kPad, y, contentW, kTitleHeight }; y += kTitleHeight; }
            const int h = cellHeight ((int) row.cell), cw = contentW / row.cols;
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
