#include "PluginEditor.h"

namespace
{
constexpr int kWidth = 780, kHeight = 800, kPad = 28;
constexpr float kInset = 14.f;
const char* const kAudioExt = "wav;aif;aiff;mp3;flac;m4a;ogg;caf";
const char* const kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const char* const kMoodNames[3] = { "LIGHT", "DEEP", "STILL" };
}

//==============================================================================
PieceView::PieceView (PoomseAmbientProcessor& p) : proc (p) { startTimerHz (30); }

void PieceView::paint (juce::Graphics& g)
{
    const auto all = getLocalBounds().toFloat();
    g.setColour (look::paper2);
    g.fillRoundedRectangle (all, 6.f);
    const auto m = all.reduced (kInset, kInset);
    const float p = proc.progress();
    const bool on = proc.pieceRunning() && p >= 0.f;

    // the arc of the piece, as the Journey knob draws it
    const float journey = *proc.apvts.getRawParameterValue ("journey");
    auto area = m.withTrimmedLeft (150.f).withTrimmedTop (22.f);
    juce::Path arc;
    for (int i = 0; i <= 120; ++i)
    {
        const float x = (float) i / 120.f, y = amb::arcDensity (x, journey);
        const float px = area.getX() + x * area.getWidth(), py = area.getBottom() - y * area.getHeight();
        if (i == 0) arc.startNewSubPath (px, py); else arc.lineTo (px, py);
    }
    g.setColour (look::rule);
    g.fillRect (area.getX(), area.getBottom() - 0.5f, area.getWidth(), 1.f);
    g.setColour (look::dim.withAlpha (0.8f));
    g.strokePath (arc, juce::PathStrokeType (1.6f));
    if (on)
    {
        const float x = area.getX() + std::clamp (p, 0.f, 1.f) * area.getWidth();
        g.setColour (look::accent.withAlpha (0.12f));
        g.fillRect (area.getX(), area.getY(), x - area.getX(), area.getHeight());
        g.setColour (look::accent);
        g.fillRect (x - 1.f, area.getY() - 4.f, 2.f, area.getHeight() + 6.f);
    }

    // the chord
    g.setColour (on ? look::ink : look::rule);
    g.setFont (look::caps (40.f, true, 0.05f));
    const int q = proc.chordQuality();
    const juce::String chord = on ? juce::String (kNoteNames[((proc.chordRoot() % 12) + 12) % 12]) + (q == 1 ? "m" : q == 2 ? "sus" : "") : juce::String ("--");
    g.drawText (chord, m.getX(), m.getY() + 16.f, 140.f, 52.f, juce::Justification::centredLeft, false);
    g.setFont (look::caps (10.f, true, 0.16f));
    g.setColour (on ? look::accent : look::dim);
    g.drawText (on ? juce::String (kMoodNames[std::clamp (proc.mood(), 0, 2)]) : juce::String ("WAITING"), m.getX(), m.getY() + 70.f, 140.f, 14.f, juce::Justification::centredLeft, false);

    g.setFont (look::caps (9.5f, true, 0.14f));
    juce::String head;
    if (message.isNotEmpty()) head = message;
    else if (proc.isBuilding()) head = "MAKING THE SOUNDS...";
    else if (! on) head = "HOLD A NOTE IN A CLIP: ITS PITCH IS THE KEY, ITS LENGTH IS THE LENGTH OF THE PIECE";
    else head = "VERSION " + juce::String (proc.versionInUse()) + "   -   THE ARC OF THE PIECE";
    g.setColour (message.isNotEmpty() || proc.isBuilding() ? look::accent : look::dim);
    g.drawText (head, all.reduced (kInset, 8.f), juce::Justification::topLeft, true);

    if (dropHover)
    {
        g.setColour (look::paper.withAlpha (0.85f)); g.fillRoundedRectangle (all, 6.f);
        g.setColour (look::accent); g.drawRoundedRectangle (all.reduced (1.f), 6.f, 2.f);
        g.setFont (look::caps (13.f, true, 0.25f));
        g.drawText (dropName.isNotEmpty() ? "DROP " + dropName.toUpperCase() + " ON A PAD" : juce::String ("DROP IT ON A PAD"), getLocalBounds().reduced (20), juce::Justification::centred, true);
    }
}

//==============================================================================
void SoundPad::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (3.f);
    const bool on = activity > 0.25f || down;
    if (on) { g.setColour (look::accent.withAlpha (0.25f + 0.75f * std::min (1.f, activity + (down ? 1.f : 0.f)))); g.fillRoundedRectangle (r, 8.f); }
    else { g.setColour (hover ? look::paper2 : look::paper); g.fillRoundedRectangle (r, 8.f); g.setColour (hover ? look::ink : look::rule); g.drawRoundedRectangle (r.reduced (0.5f), 8.f, 1.4f); }
    const auto fg = look::ink, dim = look::dim;
    g.setColour (fg);
    g.setFont (look::caps (16.f, true, 0.12f));
    g.drawText (label.toUpperCase(), r.reduced (6.f, 0.f).withTrimmedBottom (10.f), juce::Justification::centred, false);
    g.setColour (dim);
    g.setFont (juce::Font (juce::FontOptions (11.f)));
    g.drawText (custom.isNotEmpty() ? custom : tagline, r.reduced (8.f, 8.f), juce::Justification::centredBottom, true);
    if (custom.isNotEmpty()) { g.setColour (look::accent); g.fillEllipse (r.getRight() - 14.f, r.getY() + 8.f, 6.f, 6.f); }
    if (dropOver) { g.setColour (look::accent); g.drawRoundedRectangle (r.reduced (1.f), 8.f, 2.5f); }
}

//==============================================================================
PoomseAmbientEditor::Knob* PoomseAmbientEditor::makeKnob (juce::OwnedArray<Knob>& into, const juce::String& id, const juce::String& text, const juce::String& help)
{
    auto* k = into.add (new Knob());
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 18);
    k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k->name.setText (text.toUpperCase(), juce::dontSendNotification);
    k->name.setJustificationType (juce::Justification::centred);
    k->name.setFont (look::caps (10.f));
    k->name.setColour (juce::Label::textColourId, look::dim);
    k->name.setInterceptsMouseClicks (false, false);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    helps.add (help);
    addAndMakeVisible (k->slider); addAndMakeVisible (k->name);
    return k;
}

PoomseAmbientEditor::PoomseAmbientEditor (PoomseAmbientProcessor& p) : AudioProcessorEditor (&p), proc (p), view (p)
{
    setLookAndFeel (&laf);
    addAndMakeVisible (view);

    static const char* titles[4] = { "Short", "Long", "Pad", "Field" };
    static const char* tags[4] = { "piano - drop yours here", "violin - drop yours here", "chord - drop yours here", "a place - drop yours here" };
    static const char* ids[4] = { "short", "long", "pad", "field" };
    static const char* gids[4] = { "gshort", "glong", "gpad", "gfield" };
    for (int i = 0; i < 4; ++i)
    {
        auto* pad = pads.add (new SoundPad (titles[i], tags[i]));
        pad->onDown = [this, i] { selectedPad = i; proc.preview (i); repaint(); };
        pad->onFiles = [this, i] (const juce::StringArray& files)
        {
            for (const auto& f : files) { const juce::File ff (f); if (ff.existsAsFile() || ff.hasFileExtension (kAudioExt)) { load (ff, i); return; } }
            view.showMessage ("NOTHING USABLE WAS DROPPED (A FILE FROM THE FINDER IS EXPECTED)");
        };
        pad->onRestore = [this, i] { if (proc.isCustom (i)) { proc.restoreOriginal (i); view.showMessage ("THE ORIGINAL SOUND IS BACK", 5); } };
        addAndMakeVisible (pad);
        makeKnob (amountKnobs, ids[i], "Amount", "How much of this sound there is in the piece. Click the pad to hear it. Drop your own sound on the pad to replace it (right-click the pad: the original is back).");
        makeKnob (grainKnobs, gids[i], "Grains", "Plays this sound as a cloud of small grains, so a held note is never twice the same. Low: the sound as it is. High: a shimmering texture.");
    }
    makeKnob (knobs, "silence", "Silence", "How much room there is: low is busy, high leaves long silences.");
    makeKnob (knobs, "depth", "Depth", "Light moments (the piano motif, the bells) or deep ones (violin, bass, pad).");
    makeKnob (knobs, "surprise", "Surprise", "How often something unexpected happens: a falling phrase, a drop, a cascade of bells. It always stays in the harmony.");
    makeKnob (knobs, "journey", "Journey", "The arc of the piece: it builds up, peaks in the middle and fades out. Left: it stays the same.");
    makeKnob (knobs, "bars", "Length", "The length of the piece, in bars. The note you hold in the clip decides when it really stops.");
    makeKnob (knobs, "reverb", "Reverb", "How far away it all sounds.");
    makeKnob (knobs, "level", "Level", "Output level.");
    makeKnob (knobs, "gsize", "Grain size", "The length of the grains: short is grainy and fine, long is smooth and blurred.");
    makeKnob (knobs, "scatter", "Scatter", "How far the grains wander in time and in pitch: more is more shimmering and less steady.");
    makeKnob (knobs, "move", "Movement", "A slow LFO, in time with the tempo, that moves the grains, the brightness and the stereo position of each sound, each one at its own moment.");
    makeKnob (knobs, "rate", "Speed", "How long one turn of the movement takes: from one bar to sixteen.");

    for (int i = 0; i < amb::kNumScales; ++i) scaleBox.addItem (amb::kScales[i].name, i + 1);
    for (int i = 0; i < 13; ++i) keyBox.addItem (amb::kKeyNames[i], i + 1);
    scaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "scale", scaleBox);
    keyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "key", keyBox);
    addAndMakeVisible (scaleBox); addAndMakeVisible (keyBox);
    randomButton.setComponentID ("ink"); randomButton.onClick = [this] { proc.randomise(); }; addAndMakeVisible (randomButton);

    resetButton.setComponentID ("pill"); rerollButton.setComponentID ("ink"); freeButton.setComponentID ("pill");
    resetButton.onClick = [this]
    {
        if (! resetArmed)
        {
            resetArmed = true; resetButton.setButtonText ("Sure?");
            juce::Timer::callAfterDelay (3000, [safe = juce::Component::SafePointer<PoomseAmbientEditor> (this)] { if (safe != nullptr) safe->disarmReset(); });
        }
        else { proc.resetToDefaults(); disarmReset(); }
    };
    rerollButton.onClick = [this] { proc.reroll(); };
    freeButton.onClick = [this] { proc.freeVersion(); };
    addAndMakeVisible (resetButton); addAndMakeVisible (rerollButton); addAndMakeVisible (freeButton);
    setSize (kWidth, kHeight);
    startTimerHz (10);
}

PoomseAmbientEditor::~PoomseAmbientEditor() { stopTimer(); setLookAndFeel (nullptr); }

void PoomseAmbientEditor::timerCallback()
{
    int h = -1;
    for (int i = 0; i < amountKnobs.size(); ++i) if (amountKnobs[i]->slider.isMouseOverOrDragging()) h = i * 2;
    for (int i = 0; i < grainKnobs.size(); ++i) if (grainKnobs[i]->slider.isMouseOverOrDragging()) h = i * 2 + 1;
    for (int i = 0; i < knobs.size(); ++i) if (knobs[i]->slider.isMouseOverOrDragging()) h = 8 + i;
    if (h != hovered) { hovered = h; repaint(); }
    for (int i = 0; i < pads.size(); ++i) { pads[i]->setActivity (proc.slotActivity (i)); pads[i]->setCustom (proc.isCustom (i) ? proc.customName (i) : juce::String()); }
    const int seed = proc.getSeed();
    if (seed != shownSeed) { shownSeed = seed; repaint(); }
}

void PoomseAmbientEditor::load (const juce::File& file, int slot)
{
    static const char* names[4] = { "SHORT", "LONG", "PAD", "FIELD" };
    view.setDropHover (false);
    view.showMessage ("LOADING " + file.getFileName().toUpperCase() + " AS THE " + juce::String (names[slot]) + " SOUND...", 60);
    juce::Thread::launch ([safe = juce::Component::SafePointer<PoomseAmbientEditor> (this), file, slot]
    {
        PoomseAmbientProcessor::Loaded loaded;
        const auto read = PoomseAmbientProcessor::readSound (file, slot, loaded, 10);
        juce::MessageManager::callAsync ([safe, file, loaded, read, slot]
        {
            if (safe == nullptr) return;
            const auto result = read.wasOk() ? safe->proc.installSound (slot, loaded, file) : read;
            safe->view.showMessage (result.wasOk() ? "LOADED  " + loaded.info : result.getErrorMessage(), result.wasOk() ? 8 : 15);
            if (result.wasOk()) safe->proc.preview (slot);
        });
    });
}

bool PoomseAmbientEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files) { const juce::File ff (f); if (ff.existsAsFile() || ff.hasFileExtension (kAudioExt)) return true; }
    return false;
}

void PoomseAmbientEditor::filesDropped (const juce::StringArray& files, int, int)
{
    // dropped on the pad itself, the sound goes to that pad (the pad takes the drop). Anywhere else: one file goes to the pad you clicked last,
    // several files go to the four pads in order (short, long, pad, field)
    view.setDropHover (false);
    juce::Array<juce::File> usable;
    for (const auto& f : files) { const juce::File ff (f); if (ff.existsAsFile() || ff.hasFileExtension (kAudioExt)) usable.add (ff); }
    if (usable.isEmpty()) { view.showMessage ("NOTHING USABLE WAS DROPPED (A FILE FROM THE FINDER IS EXPECTED)"); return; }
    if (usable.size() == 1) { load (usable[0], selectedPad); return; }
    for (int i = 0; i < std::min (4, usable.size()); ++i) load (usable[i], i);
}

void PoomseAmbientEditor::disarmReset() { resetArmed = false; resetButton.setButtonText ("Reset"); }

void PoomseAmbientEditor::paint (juce::Graphics& g)
{
    g.fillAll (look::paper);
    const auto word = look::caps (21.f, true, 0.28f);
    g.setFont (word);
    const int w1 = (int) std::ceil (word.getStringWidthFloat ("POOMSE"));
    g.setColour (look::ink);
    g.drawText ("POOMSE", kPad, 16, w1 + 4, 30, juce::Justification::centredLeft, false);
    g.setColour (look::accent);
    g.drawText ("AMBIENT", kPad + w1 - 4, 16, 200, 30, juce::Justification::centredLeft, false);
    g.setFont (look::caps (9.5f, true, 0.14f));
    g.setColour (look::dim);
    g.drawText (shownSeed > 0 ? "VERSION " + juce::String (shownSeed) : juce::String ("A NEW PIECE EVERY TIME"), rerollButton.getX() - 60, 50, rerollButton.getWidth() + 120, 12, juce::Justification::centred, false);
    g.setFont (look::caps (9.5f, true, 0.14f));
    g.setColour (look::dim);
    g.drawText ("KEY", keyBox.getX(), keyBox.getY() - 13, 60, 12, juce::Justification::centredLeft, false);
    g.drawText ("SCALE (THE SAME AS IN YOUR CLIP)", scaleBox.getX(), scaleBox.getY() - 13, 260, 12, juce::Justification::centredLeft, false);
    g.drawText ("THE FOUR SOUNDS  -  DROP YOUR OWN ON ANY PAD", kPad, 98, 420, 12, juce::Justification::centredLeft, false);
    g.setColour (look::ink);
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.drawText (hovered >= 0 && hovered < helps.size() ? helps[hovered] : juce::String ("Hold a note in a clip and the piece plays by itself. Choose the same scale as in your clip. Drop your own sounds on the four pads (or four files at once on the window)."),
                kPad, getHeight() - 36, getWidth() - 2 * kPad, 20, juce::Justification::centredLeft, false);
}

void PoomseAmbientEditor::resized()
{
    resetButton.setBounds (getWidth() - kPad - 74, 18, 74, 30);
    freeButton.setBounds (resetButton.getX() - 8 - 116, 18, 116, 30);
    rerollButton.setBounds (freeButton.getX() - 8 - 82, 18, 82, 30);
    randomButton.setBounds (rerollButton.getX() - 8 - 82, 18, 82, 30);
    keyBox.setBounds (kPad, 72, 130, 26);
    scaleBox.setBounds (kPad + 142, 72, 220, 26);
    const int contentW = getWidth() - 2 * kPad;
    const int pw = contentW / pads.size();
    for (int i = 0; i < pads.size(); ++i)
    {
        pads[i]->setBounds (kPad + i * pw, 116, pw, 92);
        juce::Rectangle<int> cell (kPad + i * pw, 212, pw, 112);
        auto left = cell.removeFromLeft (pw / 2);
        amountKnobs[i]->name.setBounds (left.removeFromTop (16)); amountKnobs[i]->slider.setBounds (left);
        grainKnobs[i]->name.setBounds (cell.removeFromTop (16)); grainKnobs[i]->slider.setBounds (cell);
    }
    view.setBounds (kPad, 336, contentW, 128);
    const int perRow = 7;
    const int cw = contentW / perRow;
    for (int i = 0; i < knobs.size(); ++i)
    {
        juce::Rectangle<int> cell (kPad + (i % perRow) * cw, 476 + (i / perRow) * 124, cw, 120);
        knobs[i]->name.setBounds (cell.removeFromTop (16));
        knobs[i]->slider.setBounds (cell);
    }
}
