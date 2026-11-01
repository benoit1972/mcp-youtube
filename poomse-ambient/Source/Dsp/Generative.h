#pragma once
#include "Rng.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

// Generative engine: six independent layers (piano, voices, pad, bells, field, drone), each one a "tape loop" of
// its own length, so they phase against each other and the combination practically never repeats.
// The held MIDI note of the clip is the tonic; the scale, the arc of the whole piece and the seed are parameters.
namespace amb
{
constexpr int kNumLayers = 6;
enum LayerId { Keys = 0, Vocals, Pad, Beat, Atmos, Sub };
inline constexpr const char* kLayerNames[kNumLayers] = { "Keys", "Vocals", "Pad", "Beat", "Atmos", "Sub" };

constexpr float kPi = 3.14159265358979323846f;

// What the user can set for each layer. Everything else about a layer (its loop length, register, spread)
// is a fixed musical choice, see kLayerConfig.
struct LayerParams
{
    float level = 0.8f;        // 0..1 volume
    float density = 0.5f;      // 0..1 "amount": how often the layer plays
    float space = 0.5f;        // send to the reverb
    int root = 60;             // MIDI note the sample was recorded at (auto-detected when a sample is dropped)
};

struct LayerConfig
{
    float periodBeats;   // the length of the layer's "tape loop": one event per period. Different for every layer.
    float centreOffset;  // register centre above the tonic, in semitones
    float spread;        // register half range, in octaves
    float pan;           // stereo spread of its events
    float cloud;         // how much of this layer feeds the granular cloud
};
// periods are not multiples of each other (7.3, 17.9, 24.7, 41.3, 9.7 beats): the layers keep drifting apart.
// The beat is the exception: it follows a 16th-note grid (with a loose swing) and decides step by step.
inline constexpr LayerConfig kLayerConfig[kNumLayers] = {
    {  7.3f, 12.f, 1.5f, 0.5f, 0.8f },   // keys: sparse melancholic notes
    { 17.9f, 12.f, 1.0f, 0.6f, 0.9f },   // vocals: ghost voices, chopped and pitched
    { 24.7f,  0.f, 1.0f, 0.5f, 0.25f },  // pad: dark chords
    {  0.25f, 0.f, 0.0f, 0.4f, 0.0f },   // beat: one drum hit sample, played in a broken, ghostly shuffle
    { 41.3f,  0.f, 0.0f, 0.5f, 0.0f },   // atmos: rain, street, vinyl
    {  9.7f, -12.f, 0.0f, 0.0f, 0.0f },  // sub: slow low notes, mono
};

// The beat slot is a ghost beat in Night style and a set of sparse bells in Clean style.
inline LayerConfig layerConfig (int l, bool night)
{
    if (l == Beat && ! night) return { 11.1f, 24.f, 1.5f, 0.7f, 0.9f };
    return kLayerConfig[l];
}

// The three moods a piece moves through. Light: sparse keys and vocals, a hint of beat. Deep: pad, sub, the beat
// comes in, voices go lower. Still: a pause, only rain and crackle and the tails of the reverb (no beat at all).
enum Mood { Light = 0, Deep = 1, Still = 2 };
inline constexpr float kMoodProb[3][kNumLayers] = {
    { 1.30f, 0.70f, 0.30f, 0.45f, 1.00f, 0.40f },   // light
    { 0.60f, 1.20f, 1.40f, 1.20f, 1.00f, 1.30f },   // deep
    { 0.15f, 0.10f, 0.40f, 0.00f, 1.00f, 0.30f },   // still
};
inline constexpr float kMoodProbBells[3] = { 1.30f, 0.25f, 0.10f };   // bells: mostly light moments
inline float moodProb (bool night, int mood, int layer) { return (layer == Beat && ! night) ? kMoodProbBells[mood] : kMoodProb[mood][layer]; }
inline constexpr float kMoodRegister[3][kNumLayers] = {   // octaves
    { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f },
    { -1.f, -0.5f, 0.f, 0.f, 0.f, 0.f },
    { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f },
};

struct GlobalParams
{
    int scale = 2;             // see scaleIntervals()
    float journey = 0.7f;      // 0 = stationary, 1 = full arc (builds up, peaks, fades)
    float space = 0.5f;        // 0 = busy, 1 = lots of silence
    float depth = 0.5f;        // 0 = mostly light moments, 1 = mostly deep moments
    float surprise = 0.3f;     // chance and size of the unexpected
    float chords = 0.35f;      // how often the harmony moves (always by a fifth or a fourth, never abruptly)
    bool night = true;         // true: dusty night (ghost beat);  false: clean ambient (the beat slot becomes sparse bells)
    int pieceBars = 120;
    int beatsPerBar = 4;
};

// ---------------------------------------------------------------------------------------------------------------
inline int scaleIntervals (int scale, const int*& iv)
{
    static const int major[]    = { 0, 2, 4, 5, 7, 9, 11 };
    static const int minor[]    = { 0, 2, 3, 5, 7, 8, 10 };
    static const int pent[]     = { 0, 2, 4, 7, 9 };
    static const int minpent[]  = { 0, 3, 5, 7, 10 };
    static const int dorian[]   = { 0, 2, 3, 5, 7, 9, 10 };
    static const int lydian[]   = { 0, 2, 4, 6, 7, 9, 11 };
    static const int mixo[]     = { 0, 2, 4, 5, 7, 9, 10 };
    static const int whole[]    = { 0, 2, 4, 6, 8, 10 };
    switch (std::clamp (scale, 0, 7))
    {
        case 0: iv = major;   return 7;
        case 1: iv = minor;   return 7;
        case 2: iv = pent;    return 5;
        case 3: iv = minpent; return 5;
        case 4: iv = dorian;  return 7;
        case 5: iv = lydian;  return 7;
        case 6: iv = mixo;    return 7;
        default: iv = whole;  return 6;
    }
}
inline constexpr const char* kScaleNames[8] = { "Major", "Minor", "Pentatonic", "Minor pent.", "Dorian", "Lydian", "Mixolydian", "Whole tone" };

// The five notes a piece is made of: the scale reduced to tones that never rub against each other
// (no semitone, no tritone between any two of them). Every voice plays one of these, always the same one.
inline void consonantSet (int scale, int* out5)
{
    static const int sets[8][5] = {
        { 0, 2, 4, 7, 9 },    // major        -> major pentatonic
        { 0, 3, 5, 7, 10 },   // minor        -> minor pentatonic
        { 0, 2, 4, 7, 9 },    // pentatonic
        { 0, 3, 5, 7, 10 },   // minor pent.
        { 0, 3, 5, 7, 9 },    // dorian       -> the bright minor
        { 0, 2, 4, 6, 9 },    // lydian       -> with the raised fourth, no semitone
        { 0, 2, 4, 7, 10 },   // mixolydian
        { 0, 2, 4, 6, 8 },    // whole tone
    };
    for (int i = 0; i < 5; ++i) out5[i] = sets[std::clamp (scale, 0, 7)][i];
}

// MIDI note of scale degree `deg` (any integer, wraps over octaves) above the tonic
inline int noteAtDegree (int tonic, int scale, int deg)
{
    const int* iv; const int n = scaleIntervals (scale, iv);
    const int oct = (int) std::floor ((double) deg / n);
    return tonic + 12 * oct + iv[((deg % n) + n) % n];
}

// ---------------------------------------------------------------------------------------------------------------
// The form of the piece. progress 0..1 (clamped). Returns the overall density multiplier 0.2..1.
inline float smooth (float x) { x = std::clamp (x, 0.f, 1.f); return x * x * (3.f - 2.f * x); }
inline float arcShape (float p)
{
    p = std::clamp (p, 0.f, 1.f);
    return p < 0.5f ? 0.2f + 0.8f * smooth (p / 0.5f) : 1.f - 0.75f * smooth ((p - 0.5f) / 0.5f);
}
inline float arcDensity (float p, float amount) { return 1.f + std::clamp (amount, 0.f, 1.f) * (arcShape (p) - 1.f); }

// When each layer is allowed to be present (progress range), with soft edges. `amount` 0 = always present.
inline float layerEntry (int layer, float p, float amount)
{
    static const float enter[kNumLayers] = { 0.03f, 0.15f, 0.06f, 0.30f, 0.00f, 0.05f };
    static const float exit_[kNumLayers] = { 0.97f, 0.90f, 0.95f, 0.85f, 1.00f, 0.95f };
    const float e = smooth ((p - enter[layer]) / 0.08f) * (1.f - smooth ((p - exit_[layer] + 0.1f) / 0.1f));
    return 1.f + std::clamp (amount, 0.f, 1.f) * (std::clamp (e, 0.f, 1.f) - 1.f);
}

// ---------------------------------------------------------------------------------------------------------------
// A voice is one tape loop: always the same note of the set (a degree 0..4 and an octave), its own period, its own chance.
// Notes never wander: the music only changes through the *offsets* between the loops, and through slow harmony shifts.
struct VoiceDef { int layer; int degree; int octave; float period; float prob; };
inline constexpr VoiceDef kVoices[] = {
    // keys: five notes, five unrelated loop lengths
    { Keys, 0, 0,  7.3f, 0.9f }, { Keys, 2, 0,  9.1f, 0.8f }, { Keys, 4, 0, 11.7f, 0.8f }, { Keys, 1, 1, 14.3f, 0.6f }, { Keys, 3, 0, 18.9f, 0.7f },
    // vocals: three long, soft notes
    { Vocals, 0, 1, 17.9f, 0.8f }, { Vocals, 4, 0, 23.3f, 0.7f }, { Vocals, 2, 1, 29.7f, 0.6f },
    // chords: ONE slow voice. Each event is a whole chord held for 40-70 beats; a new one only every ~60 beats.
    { Pad, 0, 0, 61.7f, 0.9f },
    // bells (the beat slot in the Clean style)
    { Beat, 0, 2, 11.1f, 0.8f }, { Beat, 4, 1, 13.7f, 0.7f }, { Beat, 2, 2, 19.1f, 0.7f }, { Beat, 1, 2, 22.3f, 0.5f },
    // sub: the root, and now and then the fifth
    { Sub, 0, -1, 16.3f, 0.9f }, { Sub, 4, -1, 26.9f, 0.5f },
    // atmosphere
    { Atmos, 0, 0, 41.3f, 1.0f },
    // the ghost beat (Night style): a 16th-note grid, decided step by step
    { Beat, -1, 0, 0.25f, 1.0f },
};
constexpr int kNumVoices = (int) (sizeof (kVoices) / sizeof (kVoices[0]));
inline constexpr int kLayerBaseOffset[kNumLayers] = { 12, 12, 0, 12, 0, 0 };   // semitones above the tonic for octave 0

struct Event
{
    int layer = 0;
    int note = 60;          // MIDI note, or -1 for an unpitched event (the field recording)
    float velocity = 0.6f;
    float pan = 0.f;        // -1..1
    double beat = 0.0;
    float durBeats = 4.f;
};

class Conductor
{
public:
    static constexpr int maxEvents = 96;
    static constexpr int maxScenes = 128;
    struct Scene { double startBeat = 0.0; int mood = Light; };

    bool running() const { return active; }
    float progress() const { return active ? lastProgress : -1.f; }
    int tonicNote() const { return tonic; }
    int rootShift() const { return shift; }
    int mood() const { return curMood; }
    int numScenes() const { return nScenes; }
    const Scene& scene (int i) const { return scenes[(size_t) i]; }
    double startBeatOfPiece() const { return startBeat; }
    float layerActivity (int l) const { return activity[(size_t) l]; }

    // The note a voice plays now: the tonic of the clip, moved by the current harmony shift, plus its fixed degree and octave.
    int noteFor (int layer, int degree, int octave, const GlobalParams& g) const
    {
        int set[5]; consonantSet (g.scale, set);
        int note = tonic + shift + kLayerBaseOffset[layer] + set[std::clamp (degree, 0, 4)] + 12 * (octave + (int) std::lround (kMoodRegister[curMood][layer]));
        while (note > 100) note -= 12;
        while (note < 24) note += 12;
        return note;
    }

    // piece start: seed 0 is resolved by the caller (a fresh random number for a new piece)
    void start (double startBeat_, uint32_t seed, int tonicNote_, uint32_t /*heldPitchClasses*/, const GlobalParams& g, const LayerParams*)
    {
        active = true;
        startBeat = startBeat_;
        rng = gg::Rng (seed * 2654435761u + 12345u);
        tonic = tonicNote_;
        shift = 0;
        nextChordBeat = startBeat_ + 32.0;
        nextBarBeat = startBeat_;
        lastProgress = 0.f;
        nScenes = 0; curMood = Light;
        sceneEnd = startBeat_;   // the first scene begins at once (always light: a calm start)
        firstScene = true;
        for (int i = 0; i < kNumVoices; ++i)
        {
            const auto& v = kVoices[i];
            const bool grid = v.degree < 0;
            const double phase = (v.layer == Atmos || grid) ? 0.0 : (0.05 + 0.55 * (double) rng.uni());
            nextBeat[(size_t) i] = startBeat_ + phase * (double) v.period;
        }
        for (int l = 0; l < kNumLayers; ++l) { walkDens[(size_t) l] = 1.f; activity[(size_t) l] = 0.f; }
        (void) g;
    }
    void stop() { active = false; }
    void setTonic (int tonicNote_, uint32_t /*heldPitchClasses*/) { tonic = tonicNote_; }

    // events of the beat range [b0, b1)
    int advance (double b0, double b1, const GlobalParams& g, const LayerParams* layers, Event* out, int maxOut)
    {
        if (! active) return 0;
        int count = 0;
        const double pieceBeats = std::max (1.0, (double) g.pieceBars * g.beatsPerBar);

        for (auto& a : activity) a *= 0.9995f;

        // scenes: light / deep / still, each a few bars long, with the occasional surprise at the change
        while (sceneEnd < b1)
        {
            const double at = sceneEnd;
            const float p = (float) ((at - startBeat) / pieceBeats);
            curMood = pickMood (curMood, p, g);
            if (nScenes < maxScenes) { scenes[(size_t) nScenes].startBeat = at; scenes[(size_t) nScenes].mood = curMood; ++nScenes; }
            const int bars = curMood == Still ? 3 + (int) (rng.uni() * 3.f) : curMood == Light ? 6 + (int) (rng.uni() * 8.f) : 8 + (int) (rng.uni() * 10.f);
            sceneEnd = at + (double) bars * g.beatsPerBar;
            if (! firstScene && p < 0.9f && rng.uni() < 0.8f * g.surprise) gesture (std::max (at, b0), g, layers, out, count, maxOut);
            firstScene = false;
        }

        // slow drift of how busy each layer is, once per bar
        while (nextBarBeat < b1)
        {
            nextBarBeat += g.beatsPerBar;
            for (int l = 0; l < kNumLayers; ++l) walkDens[(size_t) l] = std::clamp (walkDens[(size_t) l] + 0.12f * rng.bi(), 0.6f, 1.4f);
        }

        // harmony: rarely, the whole set moves a fourth or a fifth (so four of its five notes stay), and later comes back
        const double chordEvery = std::max (24.0, pieceBeats / 8.0);
        while (nextChordBeat < b1)
        {
            nextChordBeat += chordEvery;
            const float p = (float) ((nextChordBeat - startBeat) / pieceBeats);
            if (shift != 0) { if (rng.uni() < 0.7f) shift = 0; }                     // home again, most of the time
            else if (p < 0.9f && rng.uni() < g.chords) shift = rng.uni() < 0.5f ? 7 : 5;
        }

        const float spaceFactor = 1.15f - 0.9f * std::clamp (g.space, 0.f, 1.f);
        for (int i = 0; i < kNumVoices; ++i)
        {
            const auto& v = kVoices[i];
            const bool grid = v.degree < 0;
            if (v.layer == Beat && grid != g.night) continue;      // Night = the grid, Clean = the bells
            const auto& lp = layers[v.layer];
            const int l = v.layer;
            if (nextBeat[(size_t) i] < b0) nextBeat[(size_t) i] = b0;
            while (nextBeat[(size_t) i] < b1)
            {
                const double t = nextBeat[(size_t) i];
                const float p = (float) ((t - startBeat) / pieceBeats);
                lastProgress = std::clamp (p, 0.f, 1.f);
                nextBeat[(size_t) i] = grid ? t + (double) v.period : t + (double) v.period * (1.0 + 0.02 * (double) rng.bi());   // loops are not machine-exact

                const int savedMood = curMood;
                if (p > 0.94f) curMood = Still;               // the last 6 %: a long calm, whatever the scene was
                const float entry = layerEntry (l, p, g.journey);
                const float arcD = arcDensity (p, g.journey);
                float prob = v.prob * lp.density * spaceFactor * arcD * entry * walkDens[(size_t) l] * moodProb (g.night, curMood, l);
                if (l == Atmos) prob = std::clamp (lp.density * 1.1f * moodProb (g.night, curMood, l), 0.f, 1.f);
                if (grid) prob = std::clamp (lp.density * arcD * entry * moodProb (g.night, curMood, l), 0.f, 1.f);
                if (rng.uni() >= std::clamp (prob, 0.f, 1.f) || count >= maxOut - 6) { curMood = savedMood; continue; }

                activity[(size_t) l] = 1.f;
                makeEvent (v, t, lp, g, arcD, out, count, maxOut);
                curMood = savedMood;
            }
        }
        return count;
    }

private:
    void push (Event* out, int& count, int maxOut, const Event& e) { if (count < maxOut) out[count++] = e; }

    int pickMood (int prev, float progress, const GlobalParams& g)
    {
        if (firstScene) return Light;
        if (progress > 0.94f) return Still;                       // the end: a long calm
        float w[3] = { 0.7f - 0.55f * g.depth, 0.12f + 0.75f * g.depth, 0.10f + 0.28f * g.space };
        w[prev] *= 0.35f;                                          // a scene rarely repeats itself: contrast
        float r = rng.uni() * (w[0] + w[1] + w[2]);
        for (int m = 0; m < 3; ++m) { r -= w[m]; if (r <= 0.f) return m; }
        return Light;
    }

    // the unexpected: always made of notes of the set, so it surprises without ever rubbing
    void gesture (double t, const GlobalParams& g, const LayerParams* layers, Event* out, int& count, int maxOut)
    {
        const float r = rng.uni();
        auto ev = [&] (int layer, int note, float vel, double beat, float dur)
        {
            Event e; e.layer = layer; e.note = std::clamp (note, 12, 120); e.velocity = vel; e.pan = 0.5f * rng.bi(); e.beat = beat; e.durBeats = dur;
            push (out, count, maxOut, e);
            activity[(size_t) layer] = 1.f;
        };
        if (r < 0.30f)            // a ghost vocal chopped into a quick falling phrase through the set
        {
            int deg = 4;
            for (int k = 0; k < 4; ++k) { ev (Vocals, noteFor (Vocals, deg, 1, g), 0.6f - 0.08f * (float) k, t + 1.0 + 0.75 * k, 1.6f); deg = std::max (0, deg - 1 - (int) (rng.uni() * 1.4f)); }
        }
        else if (r < 0.55f)       // the floor drops: a deep sub note under the root
        {
            ev (Sub, noteFor (Sub, 0, -1, g), 0.85f, t + 0.5, 12.f);
            ev (Pad, noteFor (Pad, 0, 0, g), 0.7f, t + 0.5, 40.f);
        }
        else if (r < 0.80f && g.night)   // a short broken fill of the beat, slipping off the grid
        {
            const int root = layers[Beat].root;
            for (int k = 0; k < 6; ++k)
            {
                Event e; e.layer = Beat; e.note = root + (k % 2 ? 2 : 0); e.velocity = 0.7f - 0.08f * (float) k; e.pan = 0.3f * rng.bi();
                e.beat = t + 1.0 + 0.21 * k + 0.03 * rng.bi(); e.durBeats = 0.3f;
                push (out, count, maxOut, e);
            }
            activity[Beat] = 1.f;
        }
        else if (r < 0.80f)       // clean style: a cascade of bells falling through the set
        {
            int deg = 4;
            for (int k = 0; k < 5; ++k) { ev (Beat, noteFor (Beat, deg, 2, g), 0.55f - 0.07f * (float) k, t + 1.0 + 1.6 * k, 3.f); deg = (deg + 4) % 5; }
        }
        else                      // a single high, soft note from the set: a twinkle
            ev (Keys, noteFor (Keys, (int) (rng.uni() * 5.f) % 5, 2, g), 0.35f, t + 1.0, 4.f);
    }

    void makeEvent (const VoiceDef& v, double t, const LayerParams& lp, const GlobalParams& g, float arcD, Event* out, int& count, int maxOut)
    {
        const int l = v.layer;
        const float gain = 0.55f + 0.45f * arcD;
        auto vel = [&] (float lo, float hi) { return (lo + (hi - lo) * rng.uni() * rng.uni()) * gain; }; // biased towards soft
        const auto cfg = layerConfig (l, g.night);
        auto pan = [&] { return std::clamp (cfg.pan * rng.bi(), -1.f, 1.f); };

        Event e; e.layer = l; e.beat = t; e.pan = pan();
        if (l == Atmos)       { e.note = -1; e.velocity = vel (0.6f, 1.0f); e.durBeats = 24.f + 40.f * rng.uni(); }
        else if (v.degree < 0) // the ghost beat: kick on the 1, snare on the 3 (the 2-step), the rest are rare ghost notes
        {
            const int steps = g.beatsPerBar * 4;
            const int step = (((int) std::lround ((t - startBeat) / 0.25)) % steps + steps) % steps;
            float w = 0.06f; int semis = 0; float vl = 0.f;
            if (step == 0)                           { w = 0.75f; semis = -7; vl = 0.85f; }
            else if (step == steps / 2)              { w = 0.80f; semis = 0;  vl = 0.80f; }
            else if (step % 4 == 3 || step % 8 == 6) { w = 0.20f; semis = rng.uni() < 0.5f ? 0 : 2; vl = 0.35f; }
            else if (step % 2 == 1)                  { w = 0.10f; semis = 2;  vl = 0.25f; }
            if (rng.uni() >= w) return;
            e.note = lp.root + semis; e.velocity = std::clamp (vl * (0.8f + 0.4f * rng.uni()) * gain, 0.05f, 1.f);
            e.beat = t + ((step % 2) ? 0.06 + 0.04 * rng.uni() : 0.015 * rng.bi()); e.durBeats = 0.3f;
        }
        else if (l == Pad)     // a held chord: three notes of the set started together, very long, never short repeats
        {
            static const int chords[3][3] = { { 0, 2, 3 }, { 0, 3, 4 }, { 1, 3, 4 } };   // no two notes a semitone apart
            const int* c = chords[(int) (rng.uni() * 3.f) % 3];
            const float dur = 40.f + 30.f * rng.uni();
            const float vl = vel (0.5f, 0.85f);
            for (int k = 0; k < 3; ++k)
            {
                Event n = e; n.note = noteFor (Pad, c[k], 0, g); n.velocity = vl; n.durBeats = dur + 2.f * (float) k; n.pan = pan();
                push (out, count, maxOut, n);
            }
            return;
        }
        else
        {
            e.note = noteFor (l, v.degree, v.octave, g);   // always the same note of the set: this is what keeps it harmonious
            switch (l)
            {
                case Keys:   e.velocity = vel (0.3f, 0.85f); e.durBeats = 3.f + 4.f * rng.uni(); break;
                case Vocals: e.velocity = vel (0.3f, 0.7f);  e.durBeats = 2.f + 3.f * rng.uni(); break;
                case Pad:    e.velocity = vel (0.5f, 0.85f); e.durBeats = 18.f + 14.f * rng.uni(); break;
                case Beat:   e.velocity = vel (0.25f, 0.65f); e.durBeats = 2.f + 2.f * rng.uni(); break;   // bells
                default:     e.velocity = vel (0.6f, 0.9f);  e.durBeats = 6.f + 8.f * rng.uni(); e.pan = 0.f; break;   // sub
            }
        }
        push (out, count, maxOut, e);
    }

    bool active = false, firstScene = true;
    double startBeat = 0.0, nextChordBeat = 0.0, nextBarBeat = 0.0, sceneEnd = 0.0;
    float lastProgress = 0.f;
    int tonic = 48, shift = 0, curMood = Light, nScenes = 0;
    gg::Rng rng;
    std::array<double, kNumVoices> nextBeat {};
    std::array<float, kNumLayers> walkDens {}, activity {};
    std::array<Scene, maxScenes> scenes {};
};
} // namespace amb
