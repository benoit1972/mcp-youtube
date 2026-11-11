#include "PluginEditor.h"

namespace
{
constexpr int kWidth = 780, kHeight = 640, kPad = 28;
constexpr float kInset = 14.f;
const char* const kAudioExt = "wav;aif;aiff;mp3;flac;m4a;ogg;caf";
}

//==============================================================================
SkyView::SkyView (PoomseMurmurProcessor& p) : proc (p)
{
    for (auto& a : pitch) a.fill (-1.f);
    for (auto& a : level) a.fill (0.f);
    startTimerHz (30);
}

void SkyView::timerCallback()
{
    head = (head + 1) % history;
    for (int v = 0; v < mur::kMaxVoices; ++v) { pitch[(size_t) v][(size_t) head] = proc.voicePitch (v); level[(size_t) v][(size_t) head] = proc.voiceLevel (v); }
    if (messageTicks > 0 && --messageTicks == 0) message = {};
    repaint();
}

void SkyView::paint (juce::Graphics& g)
{
    const auto all = getLocalBounds().toFloat();
    g.setColour (look::paper2);
    g.fillRoundedRectangle (all, 6.f);
    const auto m = all.reduced (kInset, kInset + 8.f);
    const int root = proc.rootNote();
    const float lo = (float) root - 14.f, hi = (float) root + 19.f;
    auto yOf = [&] (float pt) { return m.getBottom() - (pt - lo) / (hi - lo) * m.getHeight(); };

    // the notes of the scale
    const int scale = (int) *proc.apvts.getRawParameterValue ("scale"), key = (int) *proc.apvts.getRawParameterValue ("key") - 1;
    const auto& sd = mur::kScales[std::clamp (scale, 0, mur::kNumScales - 1)];
    const int tonicPc = key >= 0 ? key : root % 12;
    for (int note = root - 14; note <= root + 19; ++note)
    {
        const int rel = (((note - tonicPc) % 12) + 12) % 12;
        bool in = false; for (int i = 0; i < sd.n; ++i) in = in || sd.iv[i] == rel;
        if (! in) continue;
        g.setColour (rel == 0 ? look::dim.withAlpha (0.55f) : look::rule);
        g.fillRect (m.getX(), yOf ((float) note) - 0.5f, m.getWidth(), rel == 0 ? 1.4f : 1.f);
    }

    // the ribbons
    for (int v = 0; v < mur::kMaxVoices; ++v)
    {
        for (int k = history - 1; k > 0; --k)
        {
            const size_t i1 = (size_t) ((head - history + 1 + k + history) % history), i0 = (size_t) ((head - history + 1 + k - 1 + history) % history);
            const float p0 = pitch[(size_t) v][i0], p1 = pitch[(size_t) v][i1], l1 = level[(size_t) v][i1];
            if (p0 < 0.f || p1 < 0.f) continue;
            const float x0 = m.getX() + m.getWidth() * (float) (k - 1) / (float) (history - 1), x1 = m.getX() + m.getWidth() * (float) k / (float) (history - 1);
            const float age = (float) k / (float) history;
            g.setColour (look::accent.withAlpha (std::clamp (l1 * 1.1f, 0.f, 1.f) * (0.12f + 0.88f * age)));
            g.drawLine (x0, yOf (p0), x1, yOf (p1), 1.2f + 2.6f * l1);
        }
        const float pn = pitch[(size_t) v][(size_t) head], ln = level[(size_t) v][(size_t) head];
        if (pn >= 0.f) { g.setColour (look::accent); const float r = 2.5f + 4.f * ln; g.fillEllipse (m.getRight() - r, yOf (pn) - r, r * 2.f, r * 2.f); }
    }

    g.setFont (look::caps (9.5f, true, 0.14f));
    juce::String headText;
    if (message.isNotEmpty()) headText = message;
    else if (proc.isBuilding()) headText = "MAKING THE SOUND...";
    else if (! proc.pieceRunning()) headText = "HOLD A NOTE IN A CLIP: THE FLOCK GATHERS AROUND IT";
    else headText = "VERSION " + juce::String (proc.versionInUse()) + "   -   EACH RIBBON IS A VOICE";
    g.setColour (message.isNotEmpty() || proc.isBuilding() ? look::accent : look::dim);
    g.drawText (headText, all.reduced (kInset, 8.f), juce::Justification::topLeft, true);

    if (dropHover)
    {
        g.setColour (look::paper.withAlpha (0.85f)); g.fillRoundedRectangle (all, 6.f);
        g.setColour (look::accent); g.drawRoundedRectangle (all.reduced (1.f), 6.f, 2.f);
        g.setFont (look::caps (14.f, true, 0.28f));
        g.drawText (dropName.isNotEmpty() ? "LET GO: THE FLOCK WILL SING WITH " + dropName.toUpperCase() : juce::String ("DROP A SOUND"), getLocalBounds().reduced (20), juce::Justification::centred, true);
    }
}

//==============================================================================
void PoomseMurmurEditor::makeKnob (const juce::String& id, const juce::String& text, const juce::String& help)
{
    auto* k = knobs.add (new Knob());
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 18);
    k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k->name.setText (text.toUpperCase(), juce::dontSendNotification);
    k->name.setJustificationType (juce::Justification::centred);
    k->name.setFont (look::caps (10.5f));
    k->name.setColour (juce::Label::textColourId, look::dim);
    k->name.setInterceptsMouseClicks (false, false);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    helps.add (help);
    addAndMakeVisible (k->slider); addAndMakeVisible (k->name);
}

PoomseMurmurEditor::PoomseMurmurEditor (PoomseMurmurProcessor& p) : AudioProcessorEditor (&p), proc (p), sky (p)
{
    setLookAndFeel (&laf);
    addAndMakeVisible (sky);

    makeKnob ("swarm", "Swarm", "How many voices at the most. They arrive one by one, and leave one by one.");
    makeKnob ("wild", "Wildness", "Calm: small steps, always consonant, a voice stays on a note. Bold: big leaps, long slow slides, and voices that play backwards.");
    makeKnob ("speed", "Speed", "How long a voice stays on a note before it chooses the next one.");
    makeKnob ("range", "Range", "How far the flock spreads: close together, or from the deepest to the highest note.");
    makeKnob ("journey", "Journey", "The shape of the piece: it starts with one voice, grows, peaks, and thins out. Left: all the voices from the start.");
    makeKnob ("bars", "Length", "The length of the piece, in bars.");
    makeKnob ("space", "Space", "How far away the flock sounds.");
    makeKnob ("level", "Level", "Output level.");

    for (int i = 0; i < mur::kNumScales; ++i) scaleBox.addItem (mur::kScales[i].name, i + 1);
    for (int i = 0; i < 13; ++i) keyBox.addItem (mur::kKeyNames[i], i + 1);
    scaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "scale", scaleBox);
    keyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "key", keyBox);
    for (auto* c : { (juce::Component*) &scaleBox, (juce::Component*) &keyBox }) addAndMakeVisible (c);

    glassButton.setComponentID ("toggle"); violinButton.setComponentID ("toggle"); originalButton.setComponentID ("pill");
    glassButton.onClick = [this] { setSound (0); };
    violinButton.onClick = [this] { setSound (1); };
    originalButton.onClick = [this] { proc.restoreOriginal(); sky.showMessage ("THE BUILT-IN SOUND IS BACK", 5); };
    randomButton.setComponentID ("ink"); rerollButton.setComponentID ("pill"); freeButton.setComponentID ("pill"); resetButton.setComponentID ("pill");
    randomButton.onClick = [this] { proc.randomise(); };
    rerollButton.onClick = [this] { proc.reroll(); };
    freeButton.onClick = [this] { proc.freeVersion(); };
    resetButton.onClick = [this]
    {
        if (! resetArmed)
        {
            resetArmed = true; resetButton.setButtonText ("Sure?");
            juce::Timer::callAfterDelay (3000, [safe = juce::Component::SafePointer<PoomseMurmurEditor> (this)] { if (safe != nullptr) safe->disarmReset(); });
        }
        else { proc.resetToDefaults(); disarmReset(); }
    };
    for (auto* c : { (juce::Component*) &glassButton, (juce::Component*) &violinButton, (juce::Component*) &originalButton, (juce::Component*) &randomButton,
                     (juce::Component*) &rerollButton, (juce::Component*) &freeButton, (juce::Component*) &resetButton })
        addAndMakeVisible (c);
    originalButton.setVisible (false);
    setSize (kWidth, kHeight);
    startTimerHz (10);
}

PoomseMurmurEditor::~PoomseMurmurEditor() { stopTimer(); setLookAndFeel (nullptr); }

void PoomseMurmurEditor::setSound (int index)
{
    if (auto* q = proc.apvts.getParameter ("sound")) { q->beginChangeGesture(); q->setValueNotifyingHost (q->convertTo0to1 ((float) index)); q->endChangeGesture(); }
    if (proc.isCustom()) proc.restoreOriginal();
}

void PoomseMurmurEditor::timerCallback()
{
    int h = -1;
    for (int i = 0; i < knobs.size(); ++i) if (knobs[i]->slider.isMouseOverOrDragging()) h = i;
    if (h != hovered) { hovered = h; repaint(); }
    const int sound = (int) *proc.apvts.getRawParameterValue ("sound");
    const bool custom = proc.isCustom();
    if (sound != shownSound || custom != shownCustom)
    {
        shownSound = sound; shownCustom = custom;
        glassButton.setToggleState (! custom && sound == 0, juce::dontSendNotification);
        violinButton.setToggleState (! custom && sound == 1, juce::dontSendNotification);
        originalButton.setVisible (custom);
        repaint();
    }
    const int seed = proc.getSeed();
    if (seed != shownSeed) { shownSeed = seed; repaint(); }
}

void PoomseMurmurEditor::load (const juce::File& file)
{
    sky.setDropHover (false);
    sky.showMessage ("LOADING " + file.getFileName().toUpperCase() + "...", 60);
    juce::Thread::launch ([safe = juce::Component::SafePointer<PoomseMurmurEditor> (this), file]
    {
        PoomseMurmurProcessor::Loaded loaded;
        const auto read = PoomseMurmurProcessor::readSound (file, loaded, 10);
        juce::MessageManager::callAsync ([safe, file, loaded, read]
        {
            if (safe == nullptr) return;
            const auto result = read.wasOk() ? safe->proc.installSound (loaded, file) : read;
            safe->sky.showMessage (result.wasOk() ? "LOADED  " + loaded.info : result.getErrorMessage(), result.wasOk() ? 8 : 15);
        });
    });
}

bool PoomseMurmurEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files) { const juce::File ff (f); if (ff.existsAsFile() || ff.hasFileExtension (kAudioExt)) return true; }
    return false;
}

void PoomseMurmurEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files) { const juce::File ff (f); if (ff.existsAsFile() || ff.hasFileExtension (kAudioExt)) { load (ff); return; } }
    sky.setDropHover (false);
    sky.showMessage ("NOTHING USABLE WAS DROPPED (A FILE FROM THE FINDER IS EXPECTED)");
}

void PoomseMurmurEditor::disarmReset() { resetArmed = false; resetButton.setButtonText ("Reset"); }

void PoomseMurmurEditor::paint (juce::Graphics& g)
{
    g.fillAll (look::paper);
    const auto word = look::caps (21.f, true, 0.28f);
    g.setFont (word);
    const int w1 = (int) std::ceil (word.getStringWidthFloat ("POOMSE"));
    g.setColour (look::ink);
    g.drawText ("POOMSE", kPad, 16, w1 + 4, 30, juce::Justification::centredLeft, false);
    g.setColour (look::accent);
    g.drawText ("MURMUR", kPad + w1 - 4, 16, 200, 30, juce::Justification::centredLeft, false);
    g.setFont (look::caps (9.5f, true, 0.14f));
    g.setColour (look::dim);
    g.drawText ("KEY", keyBox.getX(), keyBox.getY() - 13, 60, 12, juce::Justification::centredLeft, false);
    g.drawText ("SCALE (THE SAME AS IN YOUR CLIP)", scaleBox.getX(), scaleBox.getY() - 13, 260, 12, juce::Justification::centredLeft, false);
    g.drawText (proc.isCustom() ? "YOUR SOUND: " + proc.customName().toUpperCase() : juce::String ("THE SOUND  -  OR DROP YOUR OWN ON THE WINDOW"), glassButton.getX(), glassButton.getY() - 13, 420, 12, juce::Justification::centredLeft, false);
    g.drawText (shownSeed > 0 ? "VERSION " + juce::String (shownSeed) : juce::String ("A NEW FLIGHT EVERY TIME"), rerollButton.getX() - 60, 50, rerollButton.getWidth() + 120, 12, juce::Justification::centred, false);
    g.setColour (look::ink);
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    g.drawText (hovered >= 0 && hovered < helps.size() ? helps[hovered] : juce::String ("Hold a note in a clip: the flock gathers around it and sings, with no one telling it what to play. Drop a sound on the window and it sings with that."),
                kPad, getHeight() - 36, getWidth() - 2 * kPad, 20, juce::Justification::centredLeft, false);
}

void PoomseMurmurEditor::resized()
{
    resetButton.setBounds (getWidth() - kPad - 74, 18, 74, 30);
    freeButton.setBounds (resetButton.getX() - 8 - 116, 18, 116, 30);
    rerollButton.setBounds (freeButton.getX() - 8 - 82, 18, 82, 30);
    randomButton.setBounds (rerollButton.getX() - 8 - 82, 18, 82, 30);
    keyBox.setBounds (kPad, 82, 130, 26);
    scaleBox.setBounds (kPad + 142, 82, 220, 26);
    glassButton.setBounds (kPad + 392, 82, 80, 26);
    violinButton.setBounds (glassButton.getRight() + 6, 82, 80, 26);
    originalButton.setBounds (glassButton.getX(), 82, 170, 26);
    const int contentW = getWidth() - 2 * kPad;
    sky.setBounds (kPad, 124, contentW, 310);
    const int cw = contentW / knobs.size();
    for (int i = 0; i < knobs.size(); ++i)
    {
        juce::Rectangle<int> cell (kPad + i * cw, 450, cw, 130);
        knobs[i]->name.setBounds (cell.removeFromTop (16));
        knobs[i]->slider.setBounds (cell);
    }
}
