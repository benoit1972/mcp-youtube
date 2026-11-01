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
    int mood() const { return curMood; }
    int numScenes() const { return nScenes; }
    const Scene& scene (int i) const { return scenes[(size_t) i]; }
    double startBeatOfPiece() const { return startBeat; }
    float layerActivity (int l) const { return activity[(size_t) l]; }

    // piece start: seed 0 is resolved by the caller (a fresh random number for a new piece)
    void start (double startBeat_, uint32_t seed, int tonicNote_, uint32_t heldPitchClasses, const GlobalParams& g, const LayerParams* layers)
    {
        active = true;
        startBeat = startBeat_;
        rng = gg::Rng (seed * 2654435761u + 12345u);
        tonic = tonicNote_;
        heldMask = heldPitchClasses;
        centerDeg = 0;
        nextChordBeat = startBeat_ + 32.0;
        nextBarBeat = startBeat_;
        lastProgress = 0.f;
        nScenes = 0; curMood = Light;
        sceneEnd = startBeat_;   // the first scene begins at once (always light: a calm start)
        firstScene = true;
        for (int l = 0; l < kNumLayers; ++l)
        {
            const bool grid = l == Beat && g.night;
            const double phase = (l == Atmos || grid) ? 0.0 : (0.05 + 0.55 * (double) rng.uni());
            nextBeat[(size_t) l] = startBeat_ + phase * (double) layerConfig (l, g.night).periodBeats;
            walkDens[(size_t) l] = 1.f; walkReg[(size_t) l] = 0.f;
            lastNote[(size_t) l] = -1; activity[(size_t) l] = 0.f;
        }
        (void) layers;
    }
    void stop() { active = false; }
    void setTonic (int tonicNote_, uint32_t heldPitchClasses) { tonic = tonicNote_; heldMask = heldPitchClasses; }

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
            const int prev = curMood;
            curMood = pickMood (prev, p, g);
            if (nScenes < maxScenes) { scenes[(size_t) nScenes].startBeat = at; scenes[(size_t) nScenes].mood = curMood; ++nScenes; }
            const int bars = curMood == Still ? 3 + (int) (rng.uni() * 3.f) : curMood == Light ? 6 + (int) (rng.uni() * 8.f) : 8 + (int) (rng.uni() * 10.f);
            sceneEnd = at + (double) bars * g.beatsPerBar;
            if (! firstScene && p < 0.9f && rng.uni() < 0.8f * g.surprise) gesture (std::max (at, b0), g, layers, out, count, maxOut);
            firstScene = false;
        }

        // slow drifts, once per bar
        while (nextBarBeat < b1)
        {
            nextBarBeat += g.beatsPerBar;
            for (int l = 0; l < kNumLayers; ++l)
            {
                walkDens[(size_t) l] = std::clamp (walkDens[(size_t) l] + 0.12f * rng.bi(), 0.6f, 1.4f);
                walkReg[(size_t) l] = std::clamp (walkReg[(size_t) l] + 0.15f * rng.bi(), -0.6f, 0.6f);
            }
        }
        // harmony: every ~pieceBeats/10 beats the chord may move
        const double chordEvery = std::max (16.0, pieceBeats / 10.0);
        while (nextChordBeat < b1)
        {
            nextChordBeat += chordEvery;
            if (rng.uni() < 0.35f)
            {
                static const int options[] = { 0, 0, 3, 4, 5, 1 };
                const int* iv; const int nDeg = scaleIntervals (g.scale, iv);
                centerDeg = options[(size_t) (rng.uni() * 6.f) % 6] % nDeg;
            }
        }

        const float spaceFactor = 1.15f - 0.9f * std::clamp (g.space, 0.f, 1.f);
        for (int l = 0; l < kNumLayers; ++l)
        {
            const auto& lp = layers[l];
            const bool grid = l == Beat && g.night;
            const double period = (double) layerConfig (l, g.night).periodBeats;
            if (nextBeat[(size_t) l] < b0) nextBeat[(size_t) l] = b0;
            while (nextBeat[(size_t) l] < b1)
            {
                const double t = nextBeat[(size_t) l];
                const float p = (float) ((t - startBeat) / pieceBeats);
                lastProgress = std::clamp (p, 0.f, 1.f);
                nextBeat[(size_t) l] = grid ? t + period : t + period * (1.0 + 0.02 * (double) rng.bi()); // loops are not machine-exact

                const int savedMood = curMood;
                if (p > 0.94f) curMood = Still;               // the last 6 %: a long calm, whatever the scene was
                const float entry = layerEntry (l, p, g.journey);
                const float arcD = arcDensity (p, g.journey);
                float prob = lp.density * spaceFactor * arcD * entry * walkDens[(size_t) l] * moodProb (g.night, curMood, l);
                if (l == Atmos)  // rain and crackle: almost continuous, only the mood and the layer's amount shape it
                    prob = std::clamp (lp.density * 1.1f * moodProb (g.night, curMood, l), 0.f, 1.f);
                if (grid)   // the beat is gated per 16th step, so here it only follows the mood, the arc and the layer's amount
                    prob = std::clamp (lp.density * arcD * entry * moodProb (g.night, curMood, l), 0.f, 1.f);
                if (rng.uni() >= std::clamp (prob, 0.f, 1.f) || count >= maxOut - 6) { curMood = savedMood; continue; }

                activity[(size_t) l] = 1.f;
                makeEvents (l, t, lp, g, arcD, out, count, maxOut);
                curMood = savedMood;
            }
        }
        return count;
    }

    int candidates (const GlobalParams& g, float centreNote, float spreadOct, int* notes, int maxNotes) const
    {
        const int* iv; const int nDeg = scaleIntervals (g.scale, iv);
        const int lo = (int) std::floor (centreNote - spreadOct * 12.f), hi = (int) std::ceil (centreNote + spreadOct * 12.f);
        int n = 0;
        for (int note = std::max (12, lo); note <= std::min (120, hi) && n < maxNotes; ++note)
        {
            const int rel = ((note - tonic) % 12 + 12) % 12;
            for (int i = 0; i < nDeg; ++i) if (iv[i] == rel) { notes[n++] = note; break; }
        }
        return n;
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

    // the unexpected: a short gesture that does not belong to the regular loops
    void gesture (double t, const GlobalParams& g, const LayerParams* layers, Event* out, int& count, int maxOut)
    {
        const float r = rng.uni();
        const int* iv; const int nDeg = scaleIntervals (g.scale, iv);
        auto ev = [&] (int layer, int note, float vel, double beat, float dur)
        {
            Event e; e.layer = layer; e.note = std::clamp (note, 12, 120); e.velocity = vel; e.pan = 0.5f * rng.bi(); e.beat = beat; e.durBeats = dur;
            push (out, count, maxOut, e);
            activity[(size_t) layer] = 1.f;
        };
        if (r < 0.30f)            // a ghost vocal chopped into a quick falling phrase
        {
            int deg = nDeg + 1 + (int) (rng.uni() * 4.f);
            for (int k = 0; k < 4; ++k) ev (Vocals, noteAtDegree (tonic + 12, g.scale, deg - k * (1 + (int) (rng.uni() * 1.4f))), 0.6f - 0.08f * k, t + 1.0 + 0.75 * k, 1.6f);
        }
        else if (r < 0.55f)       // the floor drops: a deep sub note under a dark chord
        {
            ev (Sub, tonic - 12, 0.85f, t + 0.5, 12.f);
            ev (Pad, tonic, 0.7f, t + 0.5, 28.f);
        }
        else if (r < 0.80f && ! g.night)   // clean style: a cascade of bells falling down the scale
        {
            int deg = (int) (rng.uni() * 4.f) + nDeg + 2;
            for (int k = 0; k < 5; ++k) ev (Beat, noteAtDegree (tonic + 12, g.scale, deg - k * (1 + (int) (rng.uni() * 1.5f))), 0.55f - 0.07f * k, t + 1.0 + 1.6 * k, 3.f);
        }
        else if (r < 0.80f)       // a short broken fill of the beat, slipping off the grid
        {
            const int root = layers[Beat].root;
            for (int k = 0; k < 6; ++k)
            {
                Event e; e.layer = Beat; e.note = root + (int) (rng.uni() * 9.f) - 4; e.velocity = 0.7f - 0.08f * k; e.pan = 0.3f * rng.bi();
                e.beat = t + 1.0 + 0.21 * k + 0.03 * rng.bi(); e.durBeats = 0.3f;
                push (out, count, maxOut, e);
            }
            activity[Beat] = 1.f;
        }
        else                      // one colour note from outside the scale, very soft
        {
            int note;
            do { note = tonic + 12 + (int) (rng.uni() * 14.f); const int rel = ((note - tonic) % 12 + 12) % 12; bool in = false; for (int i = 0; i < nDeg; ++i) in |= iv[i] == rel; if (! in) break; } while (true);
            ev (Keys, note, 0.35f, t + 1.0, 4.f);
        }
    }

    int chordNoteNear (const GlobalParams& g, int degOffset, float centreNote) const
    {
        int best = noteAtDegree (tonic, g.scale, centerDeg + degOffset), bestD = 100000;
        for (int oct = -4; oct <= 4; ++oct)
        {
            const int n = noteAtDegree (tonic, g.scale, centerDeg + degOffset) + 12 * oct;
            const int d = (int) std::abs ((float) n - centreNote);
            if (d < bestD) { bestD = d; best = n; }
        }
        return best;
    }

    void makeEvents (int l, double t, const LayerParams& lp, const GlobalParams& g, float arcD, Event* out, int& count, int maxOut)
    {
        const auto cfg = layerConfig (l, g.night);
        const float gain = 0.55f + 0.45f * arcD;
        auto vel = [&] (float lo, float hi) { return (lo + (hi - lo) * rng.uni() * rng.uni()) * gain; }; // biased towards soft
        auto pan = [&] { return std::clamp (cfg.pan * rng.bi(), -1.f, 1.f); };
        const float centre = (float) tonic + cfg.centreOffset + 12.f * (kMoodRegister[curMood][l] + walkReg[(size_t) l]);

        if (l == Atmos)
        {
            Event e; e.layer = l; e.note = -1; e.velocity = vel (0.6f, 1.0f); e.pan = pan(); e.beat = t;
            e.durBeats = 24.f + 40.f * rng.uni();
            push (out, count, maxOut, e);
            return;
        }
        if (l == Beat && g.night)
        {
            // 16 steps per bar, the odd ones a little late (swing). Kick on the 1, snare on the 3 (the 2-step),
            // everything else is a rare ghost note that slips between them.
            const int steps = g.beatsPerBar * 4;
            const int step = (((int) std::lround ((t - startBeat) / 0.25)) % steps + steps) % steps;
            float w = 0.06f; int semis = 0; float v = 0.f;
            if (step == 0)                     { w = 0.75f; semis = -7; v = 0.85f; }          // low "kick"
            else if (step == steps / 2)        { w = 0.80f; semis = 0;  v = 0.80f; }          // "snare"
            else if (step % 4 == 3 || step % 8 == 6) { w = 0.20f; semis = 4 + (int) (rng.uni() * 5.f); v = 0.35f; }  // ghost notes
            else if (step % 2 == 1)            { w = 0.10f; semis = 7; v = 0.25f; }           // tiny ticks
            if (rng.uni() >= w) return;
            Event e; e.layer = l; e.note = lp.root + semis; e.velocity = std::clamp (v * (0.8f + 0.4f * rng.uni()) * gain, 0.05f, 1.f);
            e.pan = pan(); e.beat = t + ((step % 2) ? 0.06 + 0.04 * rng.uni() : 0.015 * rng.bi());
            e.durBeats = 0.3f;
            push (out, count, maxOut, e);
            return;
        }
        if (l == Sub)
        {
            // slow low notes on the root of the current chord
            const int note = std::clamp (chordNoteNear (g, 0, (float) tonic - 12.f + 12.f * kMoodRegister[curMood][l]), 24, 52);
            Event e; e.layer = l; e.note = note; e.velocity = vel (0.6f, 0.9f); e.pan = 0.f; e.beat = t;
            e.durBeats = 5.f + 8.f * rng.uni();
            push (out, count, maxOut, e);
            return;
        }
        if (l == Pad)
        {
            // stacked chord tones of the current harmony: root + third + fifth
            const int offs[3] = { 0, 2, 4 };
            const int first = chordNoteNear (g, 0, centre);
            for (int k = 0; k < 3; ++k)
            {
                int note = first;
                if (k > 0)
                {
                    note = noteAtDegree (tonic, g.scale, centerDeg + offs[k]) + 12 * ((first - noteAtDegree (tonic, g.scale, centerDeg)) / 12);
                    while (note <= first) note += 12;
                }
                Event e; e.layer = l; e.note = std::clamp (note, 12, 120); e.velocity = vel (0.55f, 0.9f);
                e.pan = pan(); e.beat = t + 0.05 * k;
                e.durBeats = 16.f + 16.f * rng.uni();
                push (out, count, maxOut, e);
            }
            return;
        }

        // keys, vocals: a single note among the scale tones, favouring chord tones and small leaps
        int pool[80];
        const int n = candidates (g, centre, cfg.spread, pool, 80);
        if (n == 0) return;
        float w[80], total = 0.f;
        const int* iv; const int nDeg = scaleIntervals (g.scale, iv);
        const float tension = 0.3f;
        for (int i = 0; i < n; ++i)
        {
            const int rel = ((pool[i] - tonic) % 12 + 12) % 12;
            int degree = 0;
            for (int d = 0; d < nDeg; ++d) if (iv[d] == rel) degree = d;
            const int fromCentre = ((degree - centerDeg) % nDeg + nDeg) % nDeg;
            const bool chordTone = fromCentre == 0 || fromCentre == 2 % nDeg || fromCentre == 4 % nDeg;
            float wt = chordTone ? 3.f : (0.15f + 1.35f * tension);
            if (heldMask & (1u << (unsigned) (((pool[i] % 12) + 12) % 12))) wt *= 2.5f;
            if (lastNote[(size_t) l] >= 0) wt *= std::exp (-std::abs ((float) (pool[i] - lastNote[(size_t) l])) / (4.f + 3.f * cfg.spread));
            w[i] = wt; total += wt;
        }
        float r = rng.uni() * total; int pick = n - 1;
        for (int i = 0; i < n; ++i) { r -= w[i]; if (r <= 0.f) { pick = i; break; } }
        lastNote[(size_t) l] = pool[pick];

        Event e; e.layer = l; e.note = pool[pick]; e.pan = pan(); e.beat = t;
        e.velocity = l == Keys ? vel (0.3f, 0.85f) : vel (0.3f, 0.75f);
        e.durBeats = l == Keys ? 2.f + 4.f * rng.uni() : (l == Beat ? 2.f + 2.f * rng.uni() : 1.5f + 3.f * rng.uni());
        push (out, count, maxOut, e);
        // the ghost vocal sometimes drags an octave-down shadow behind it
        if (l == Vocals && rng.uni() < 0.25f)
        {
            Event s2 = e; s2.note = std::clamp (e.note - 12, 12, 120); s2.velocity *= 0.55f; s2.beat += 0.5; s2.pan = -e.pan;
            push (out, count, maxOut, s2);
        }
    }

    bool active = false, firstScene = true;
    double startBeat = 0.0, nextChordBeat = 0.0, nextBarBeat = 0.0, sceneEnd = 0.0;
    float lastProgress = 0.f;
    int tonic = 48, centerDeg = 0, curMood = Light, nScenes = 0;
    uint32_t heldMask = 0;
    gg::Rng rng;
    std::array<double, kNumLayers> nextBeat {};
    std::array<float, kNumLayers> walkDens {}, walkReg {}, activity {};
    std::array<int, kNumLayers> lastNote {};
    std::array<Scene, maxScenes> scenes {};
};
} // namespace amb
