#pragma once
#include "Kit.h"
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

// A loop player that you take apart. The break plays in time with the song, each hit where it belongs in the bar.
// PLAYBACK pads (white keys from C2 up): which version of the loop plays. One at a time and it stays selected (Latch), or stacked while held.
//   Original   the loop as it is                      Break    taken apart at random, bar after bar (Mutate says how far)
//   Ratchet    snares and hats turn into rolls         Flip     beats 2 and 4 played backwards
//   Swap       kicks and snares change places          Shove    pushed an eighth note late
//   Ghost      soft hits in every gap
// FILL pads (white keys below C2): they take the floor for exactly as long as the key is down, and the playback is back the moment it is let go.
//   Roll up    a stutter-edit ramp: the rate climbs through the grid, the pitch rises, the level swells; held, it stays at full speed
//   Roll down  the ramp that stretches: slower, lower, fading        Stutter  the last hit, again and again
//   Reverse    the beat you have just heard, backwards               Stop     the loop slows down and stops like a tape
//   Hats       a flurry of hats                                       Drop     silence; let go and the loop comes back with a hard hit
// Everything falls on the grid. The dice are loaded by the song position and the seed: the same bar breaks the same way.
namespace sh
{
enum Play { PlayOriginal = 0, PlayBreak, PlayRatchet, PlayFlip, PlaySwap, PlayShove, PlayGhost, numPlays };
inline constexpr const char* kPlayNames[numPlays] = { "Original", "Break", "Ratchet", "Flip", "Swap", "Shove", "Ghost" };
inline constexpr const char* kPlayTags[numPlays] = { "the loop, as it is", "taken apart", "snares and hats roll", "beats 2 and 4 backwards", "kicks and snares trade", "an eighth late", "soft hits in the gaps" };
inline constexpr const char* kPlayHelp[numPlays] = {
    "ORIGINAL - the loop exactly as it is.",
    "BREAK - the loop taken apart, differently every bar: hits swapped, pushed, backwards, ghosts, rolls. Mutate says how far.",
    "RATCHET - the snares and hats turn into rolls that speed up along the grid.",
    "FLIP - beats 2 and 4 are played backwards.",
    "SWAP - the kicks and the snares change places.",
    "SHOVE - the whole loop is pushed an eighth note late: everything lands off the beat.",
    "GHOST - soft hats and ghost hits fill every gap.",
};
enum Fill { FillRollUp = 0, FillRollDown, FillStutter, FillReverse, FillStop, FillHats, FillDrop, numFills };
inline constexpr const char* kFillNames[numFills] = { "Roll up", "Roll down", "Stutter", "Reverse", "Stop", "Hats", "Drop" };
inline constexpr const char* kFillTags[numFills] = { "speeds up, rises", "slows, stretches", "the last hit again", "backwards", "like a tape", "a flurry of hats", "silence, then back" };
inline constexpr const char* kFillHelp[numFills] = {
    "ROLL UP - a stutter-edit ramp: it speeds up along the grid, rises in pitch and swells. Held, it stays at full speed.",
    "ROLL DOWN - the ramp that stretches out: slower, lower, fading away.",
    "STUTTER - the last hit, again and again.",
    "REVERSE - the beat you just heard, backwards, again and again.",
    "STOP - the loop slows down and stops like a tape. Held, it stays stopped.",
    "HATS - a flurry of hats.",
    "DROP - silence while you hold it. Let go: the loop comes back with a hard hit.",
};
// the white keys C D E F G A B; each black key is the same as the white key below it. From C2 (48) up: playback pads. Below C2: fills.
inline int whiteIndex (int note) { static const int m[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 }; return m[((note % 12) + 12) % 12]; }
inline bool isFillNote (int note) { return note < 48; }

struct Params
{
    float mutate = 0.6f;    // how much of the loop the selected playback takes hold of (a gentle curve: low = small refined changes, high = another loop)
    float ramp = 0.6f;      // how far the rolls go: how fast they get, and how far the pitch travels
    float groove = 1.f;     // 1 = the hits keep their feel, 0 = everything snapped to the grid
    float tune = 0.f;       // semitones
    float gate = 0.9f;      // how much of its step a roll repeat fills
    int quant = 3;          // a playback key starts on the next: 0 at once, 1 = 1/4, 2 = 1/8, 3 = 1/16
    bool latch = true;      // a playback pad stays selected when it is let go (otherwise it plays while held, and several can be stacked)
    int seed = 1;
};

struct EventLog { double step; int note, slice, kind; float vel, r0, r1; bool rev; int grp; };   // for tests

inline uint32_t mixHash (uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint32_t h = a * 2654435761u ^ (b + 0x9e3779b9u) * 2246822519u ^ (c + 0x7f4a7c15u) * 3266489917u ^ (d + 0x85ebca6bu) * 668265263u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
    return h;
}

class Engine
{
public:
    static constexpr int maxVoices = 8, maxHits = 96, maxEvents = 320, ringSize = 32;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& v : vc) v = Vc{};
        for (auto& h : hit) h = Hit{};
        fill = Fill{};
        order = 0; curStep = 0.0; synced = false;
        hits.reserve (256); fullLen.reserve (256);
    }
    void setKit (const Kit* k) { kit = k; build(); killAll(); }
    void setLoopBeats (double b) { loopBeats = std::max (4.0, b); build(); }
    void setLog (std::vector<EventLog>* l) { log = l; }

    void noteOn (int note, float velocity, const Params& p)
    {
        if (isFillNote (note)) { startFill (whiteIndex (note), note, velocity, p); return; }
        Vc* v = nullptr;
        for (auto& x : vc) if (x.on && x.note == note && ! x.released) { v = &x; break; }       // the same pad again: start over
        if (v == nullptr) for (auto& x : vc) if (! x.on) { v = &x; break; }
        if (v == nullptr) for (auto& x : vc) if (v == nullptr || x.order < v->order) v = &x;
        const double startStep = quantised (p);
        if (p.latch)       // the pad is selected: the one that was playing gives way at the very step the new one starts
            for (auto& x : vc) if (x.on && &x != v && ! x.released) { x.released = true; x.relStep = std::max (x.startStep, startStep); }
        *v = Vc{};
        v->on = true; v->note = note; v->vel = std::clamp (velocity, 0.05f, 1.f); v->order = ++order;
        v->startStep = startStep; v->relStep = 1e18; v->latched = p.latch;
    }
    void noteOff (int note)
    {
        if (isFillNote (note)) { if (fill.on && fill.note == note) releaseFill(); return; }
        for (auto& v : vc)
            if (v.on && v.note == note && ! v.released && ! v.latched) { v.released = true; v.relStep = std::max (v.startStep, std::ceil (curStep - 1e-9)); }
    }
    void allNotesOff()
    {
        for (auto& v : vc) if (v.on && ! v.released) { v.released = true; v.relStep = std::max (v.startStep, std::ceil (curStep - 1e-9)); }
        if (fill.on) releaseFill();
    }
    void killAll() { for (auto& v : vc) v = Vc{}; for (auto& h : hit) h = Hit{}; fill = Fill{}; synced = false; }

    void process (int num, const Params& p, double beat0, double bpm, bool playing, float* L, float* R)
    {
        const double spq = sr * 60.0 / std::clamp (bpm, 20.0, 400.0);
        const double tune = std::exp2 ((double) p.tune / 12.0);
        for (int i = 0; i < num; ++i)
        {
            const double step = (beat0 + (double) i / spq) * 4.0;
            if (! playing || kit == nullptr || hits.empty())
            {
                L[i] = R[i] = 0.f;
                if (synced) killAll();
                continue;
            }
            if (! synced || std::abs (step - curStep) > 8.0 / (spq / 4.0)) { synced = true; for (auto& h : hit) h = Hit{}; for (auto& v : vc) if (v.on) v.planned = -1; }
            curStep = step;
            const long absBar = (long) std::floor (step / 16.0);
            const double stepSamples = spq / 4.0;

            int nOn = 0;
            for (const auto& v : vc) if (v.on && step >= v.startStep) ++nOn;
            if (fill.on)
            {
                if (! fill.started && step >= fill.start - 1e-9)
                {
                    fill.started = true;
                    for (auto& h : hit) if (h.on && h.chokeLeft == 0) { h.chokeLeft = std::max (1, (int) (0.003 * sr)); h.chokeLen = h.chokeLeft; }   // the loop stops dead
                }
                if (fill.started && ! fill.released && step >= fill.end - 1e-9) extendFill();          // the key is still down: go on
                while (fill.started && fill.iEv < fill.nEv && fill.ev[(size_t) fill.iEv].t <= step + 1e-9)
                {
                    const Ev& e = fill.ev[(size_t) fill.iEv++];
                    if (e.t < fill.end - 1e-9) trigger (maxVoices, fill.note, 1.f, e, e.t, stepSamples, tune, 1);
                }
                if (fill.started && fill.released)             // the key is up: the playback is back at once
                {
                    for (auto& h : hit) if (h.on && h.owner == maxVoices && h.chokeLeft == 0) { h.chokeLeft = std::max (1, (int) (0.003 * sr)); h.chokeLen = h.chokeLeft; }
                    if (fill.type == FillDrop && fill.accentSlice >= 0 && ! fill.accentDone)     // the drop ends with a hard hit
                    {
                        fill.accentDone = true;
                        const Ev e { step, fill.accentSlice, 1.3f * fill.vel, 1.f, 1.f, 0.f, 0.f, false, 3, 0 };
                        trigger (maxVoices, fill.note, 1.f, e, step, stepSamples, tune, 1);
                    }
                    fill.on = false;
                }
            }
            for (auto& v : vc)
            {
                if (! v.on) continue;
                if (v.latched && ! p.latch && ! v.released) { v.released = true; v.relStep = std::ceil (step - 1e-9); }   // Latch switched off: the pad plays only while held
                if (step >= v.relStep) { v.on = false; continue; }
                if (step < v.startStep) continue;
                if (v.planned != absBar) { plan (v, absBar, p); v.planned = absBar; v.iEv = 0; while (v.iEv < v.nEv && (double) absBar * 16.0 + v.ev[(size_t) v.iEv].t < v.startStep - 1e-9) ++v.iEv; }
                while (v.iEv < v.nEv && (double) absBar * 16.0 + v.ev[(size_t) v.iEv].t <= step + 1e-9)
                {
                    const Ev& e = v.ev[(size_t) v.iEv++];
                    const double at = (double) absBar * 16.0 + e.t;
                    if (at >= v.relStep - 1e-9) continue;
                    if (fill.on && at >= fill.start - 1e-9 && at < fill.end - 1e-9) continue;      // the gesture has the floor
                    trigger ((int) (&v - vc.data()), v.note, v.vel, e, at, stepSamples, tune, nOn);
                }
            }
            float l = 0.f, r = 0.f;
            for (auto& h : hit)
            {
                if (! h.on) continue;
                const float a = std::min (1.f, (float) h.age / std::max (1.f, 0.001f * (float) sr));
                float rel = 1.f;
                const int left = h.len - h.age;
                if (left < h.relLen) rel = std::max (0.f, (float) left / (float) std::max (1, h.relLen));
                if (h.chokeLeft > 0) { rel *= (float) h.chokeLeft / (float) h.chokeLen; if (--h.chokeLeft == 0) { h = Hit{}; continue; } }
                const float s = readHit (h) * a * rel * h.gain;
                l += s * h.gl; r += s * h.gr;
                h.pos += h.rev ? -h.inc : h.inc;
                h.inc *= h.mult;
                if (h.pos < 0.0 || h.pos >= (double) h.length || ++h.age >= h.len) h = Hit{};
            }
            L[i] = softLimit (l); R[i] = softLimit (r);
        }
        dispPhase.store ((float) (std::fmod (curStep, loopBeats * 4.0) / (loopBeats * 4.0)));
        int mask = 0;
        for (const auto& v : vc) if (v.on && curStep >= v.startStep && ! v.released) mask |= 1 << whiteIndex (v.note);
        if (fill.on && fill.started) mask |= 1 << (8 + fill.type);
        dispMask.store (mask);
    }

    // what the interface shows: the last hits played (slice index, bit 16 = a roll, bit 17 = backwards, bit 18 = a ghost or a gesture)
    int hitCount() const { return count.load(); }
    int hitAt (int i) const { return ring[(size_t) (i % ringSize)].load(); }
    float loopPhase() const { return dispPhase.load(); }
    int activeKeys() const { return dispMask.load(); }          // bits 0..6: playback pads, bits 8..14: fills
    int activeVoices() const { int n = 0; for (const auto& v : vc) if (v.on) ++n; return n; }
    bool gestureActive() const { return fill.on; }
    int activeHits() const { int n = 0; for (const auto& h : hit) if (h.on) ++n; return n; }
    int numLoopHits() const { return (int) hits.size(); }

private:
    struct LoopHit { int slice; float pos; };                // pos in steps (1/16 notes) from the start of the loop
    struct Ev { double t; int slice; float vel, r0, r1, len, pan; bool rev; int kind; int grp; };    // t in steps from the start of the bar; len in steps, 0 = natural
    struct Vc
    {
        bool on = false, released = false, latched = false;
        int note = 0, nEv = 0, iEv = 0;
        float vel = 0.f;
        double startStep = 0.0, relStep = 1e18;
        long planned = -1, order = 0;
        std::array<Ev, maxEvents> ev {};
    };
    struct Hit
    {
        bool on = false, rev = false;
        int owner = -1, kind = 0, slice = 0, start = 0, length = 0, age = 0, len = 0, relLen = 1, chokeLeft = 0, chokeLen = 1;
        double pos = 0.0, inc = 1.0, mult = 1.0;
        float gain = 0.f, gl = 0.f, gr = 0.f;
        long order = 0;
    };
    struct Fill
    {
        bool on = false, started = false, released = false, accentDone = false;
        int type = 0, note = 0, nEv = 0, iEv = 0, block = 0, accentSlice = -1;
        double start = 0.0, end = 0.0, nominalEnd = 0.0;
        float vel = 1.f;
        Params p;
        std::array<Ev, 200> ev {};
    };

    double quantised (const Params& p) const
    {
        static const double q[4] = { 0.0, 4.0, 2.0, 1.0 };
        const double qs = q[std::clamp (p.quant, 0, 3)];
        return qs > 0.0 ? std::ceil (curStep / qs - 1e-9) * qs : curStep;
    }

    // the loop as a list of hits placed on the grid
    void build()
    {
        hits.clear(); fullLen.clear();
        if (kit == nullptr || kit->slices.empty() || kit->x.empty()) return;
        const int origin = kit->slices.front().start;
        const double span = std::max (1, (int) kit->x.size() - origin);
        for (size_t i = 0; i < kit->slices.size(); ++i)
        {
            hits.push_back ({ (int) i, (float) ((double) (kit->slices[i].start - origin) / span * loopBeats * 4.0) });
            const int end = i + 1 < kit->slices.size() ? kit->slices[i + 1].start : (int) kit->x.size();
            fullLen.push_back (std::max (1, end - kit->slices[i].start));
        }
        c = kit->sampleRate / sr;
    }

    // The gaps (in steps) of a stutter-edit ramp. It walks along the grid, rung by rung: 1/8, 1/16, 1/32, 1/64 (or the triplet
    // rungs), a few repeats on each. Climbing goes from slow to fast; falling goes from fast to slow, so the sound is drawn out.
    static int rollGaps (bool up, double span, bool triplets, double* out, int maxOut)
    {
        static const double bin[4] = { 2.0, 1.0, 0.5, 0.25 }, tri[4] = { 4.0 / 3.0, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0 };
        static const int count[4] = { 1, 2, 3, 6 };
        const double* g = triplets ? tri : bin;
        const int first = span >= 6.0 ? 0 : span >= 3.0 ? 1 : 2;                       // a short roll starts on a faster rung
        int n = 0; double t = 0.0;
        for (int k = 0; k < 4 && t < span - 1e-6; ++k)
        {
            const int rung = up ? first + k : 3 - k;
            if (rung > 3 || rung < first) { if (up) break; else continue; }
            for (int c2 = 0; c2 < count[rung] && t < span - 1e-6 && n < maxOut; ++c2) { out[n++] = g[rung]; t += g[rung]; }
        }
        while (t < span - 1e-6 && n < maxOut) { out[n] = up ? g[3] : g[first]; t += out[n]; ++n; }       // the rest of the span at the end rate
        return n;
    }

    // ---------------------------------------------------------------------------------------------------------------
    // BREAK: the writing of one bar for one key
    void plan (Vc& v, long absBar, const Params& p)
    {
        gg::Rng r (mixHash ((uint32_t) p.seed, (uint32_t) v.note, (uint32_t) absBar, 77u) | 1u);
        for (int i = 0; i < 4; ++i) r.nextU32();
        const int mode = whiteIndex (v.note);
        const bool isBreak = mode == PlayBreak;
        const int loopBars = std::max (1, (int) std::lround (loopBeats / 4.0));
        const int lb = (int) (((absBar % loopBars) + loopBars) % loopBars);
        const int phrase = (int) (((absBar % 4) + 4) % 4);
        const float heat = 0.7f + 0.1f * (float) phrase;                                     // each bar of a phrase is a little wilder
        // the Mutate knob is a curve: the bottom of its travel is for small, refined changes, the top takes the loop apart
        const float mut = std::clamp (p.mutate, 0.f, 1.f);
        const float D = isBreak ? std::clamp ((std::pow (mut, 1.8f) * 1.25f + (v.vel - 0.6f) * 0.2f * mut) * heat, 0.f, 1.3f) : 0.f;
        auto prob = [&] (float weight, float base) { return std::clamp (D * weight * base, 0.f, 0.9f); };
        const float pShuffle = prob (0.9f, 0.6f), pRev = prob (0.7f, 0.5f), pPitch = prob (0.6f, 0.5f), pGhost = prob (0.9f, 0.5f), pHole = prob (0.6f, 0.4f);
        const float rampAmt = std::clamp (p.ramp, 0.f, 1.f);
        const float pRamp = std::clamp (D * (0.2f + 0.8f * rampAmt) * 0.5f, 0.f, 0.9f);

        // 1. the loop as written
        int n = 0;
        auto add = [&] (const Ev& e) { if (n < maxEvents) v.ev[(size_t) n++] = e; };
        for (const auto& h : hits)
        {
            const float local = h.pos - 16.f * (float) lb;
            const float q = std::round (local);                         // the slot of the grid the hit belongs to
            if (q < 0.f || q >= 16.f) continue;
            Ev e { (double) (q + p.groove * (local - q)), h.slice, 1.f, 1.f, 1.f, 0.f, 0.f, false, 0, 0 };
            e.t = std::clamp (e.t, 0.0, 15.99);
            add (e);
        }
        auto sortEvents = [&] { std::sort (v.ev.begin(), v.ev.begin() + n, [] (const Ev& a, const Ev& b) { return a.t < b.t; }); };
        sortEvents();

        // 1b. the character of the pad: a clear, recognisable thing done to the loop, as much of it as Mutate and the velocity say
        const float sig = (mode == PlayOriginal || isBreak) ? 0.f : std::clamp (std::pow (mut, 0.8f) * (0.55f + 0.45f * v.vel) * 1.1f, 0.f, 1.f);
        auto takes = [&] { return r.uni() < sig; };
        auto yOf = [&] (const Ev& e) { return kit->slices[(size_t) e.slice].y; };
        auto pickBand = [&] (float lo, float hi, int fallback)
        {
            int best = -1, seen = 0;
            for (int i = 0; i < (int) kit->slices.size(); ++i)
            {
                const float y = kit->slices[(size_t) i].y;
                if (y >= lo && y <= hi && ++seen && r.uni() < 1.f / (float) seen) best = i;
            }
            return best >= 0 ? best : fallback;
        };
        bool rolls = false;
        switch (mode)
        {
            case PlayFlip:      for (int i = 0; i < n; ++i) { Ev& e = v.ev[(size_t) i]; if (((e.t >= 4.0 && e.t < 8.0) || e.t >= 12.0) && takes()) e.rev = true; } break;
            case PlayRatchet:   rolls = true; break;
            case PlaySwap:
                for (int i = 0; i < n; ++i)
                {
                    Ev& e = v.ev[(size_t) i]; const float y = yOf (e);
                    if (y < 0.2f) { if (takes()) e.slice = pickBand (0.3f, 0.8f, e.slice); }
                    else if (y > 0.3f && y < 0.8f) { if (takes()) e.slice = pickBand (0.f, 0.2f, e.slice); }
                }
                break;
            case PlayShove:     for (int i = 0; i < n; ++i) { Ev& e = v.ev[(size_t) i]; if (takes()) e.t = std::fmod (e.t + 2.0, 16.0); } break;
            case PlayGhost:
                for (int st = 0; st < 16; ++st)
                {
                    bool free = true;
                    for (int i = 0; i < n; ++i) if (std::abs (v.ev[(size_t) i].t - (double) st) < 0.45) free = false;
                    if (free && takes()) add ({ (double) st, pickBand (0.8f, 1.f, 0), 0.2f + 0.25f * r.uni(), 1.f, 1.f, 0.9f, 0.f, false, 2, 0 });
                }
                break;
            default: break;      // Original (nothing) and Break (random damage, below)
        }
        sortEvents();
        if (isBreak)
        {
            // 2. the beat runs backwards for a beat
            if (r.uni() < pShuffle * 0.5f)
            {
                const float w0 = (float) (4 * (int) (r.uni() * 4.f) % 16);
                for (int i = 0; i < n; ++i) if (v.ev[(size_t) i].t >= w0 && v.ev[(size_t) i].t < w0 + 4.f) v.ev[(size_t) i].t = std::clamp (2.0 * w0 + 3.0 - v.ev[(size_t) i].t, (double) w0, (double) w0 + 3.75);
                sortEvents();
            }
            // 3. hits pushed along the grid or swapped for others
            for (int i = 0; i < n; ++i)
            {
                Ev& e = v.ev[(size_t) i];
                if (r.uni() < pShuffle * 0.5f) { const double s = std::round (e.t) + (r.uni() < 0.5f ? -1.0 : 1.0); e.t = std::clamp (s, 0.0, 15.0); }
                if (r.uni() < pShuffle * 0.7f)
                {
                    const float y0 = kit->slices[(size_t) e.slice].y, reach = 0.12f + 0.55f * D;
                    int pick = -1, seen = 0;
                    for (int s = 0; s < (int) kit->slices.size(); ++s)
                        if (s != e.slice && std::abs (kit->slices[(size_t) s].y - y0) <= reach && ++seen && r.uni() < 1.f / (float) seen) pick = s;   // reservoir pick
                    if (pick >= 0) e.slice = pick;
                }
            }
            sortEvents();
            // 4. holes: a few steps of nothing, and the way back in
            for (int hole = 0; hole < 2; ++hole) if (r.uni() < pHole * (hole == 0 ? 1.1f : 0.5f))
            {
                const double s0 = 2.0 * std::floor (r.uni() * 7.f) + (D > 0.75f ? 0.0 : 1.0), w = r.uni() < 0.5f ? 2.0 : 4.0;
                int m = 0;
                for (int i = 0; i < n; ++i) { const double t = v.ev[(size_t) i].t; if (! (t >= s0 && t < s0 + w)) v.ev[(size_t) m++] = v.ev[(size_t) i]; }
                n = m;
                for (int i = 0; i < n; ++i) if (v.ev[(size_t) i].t >= s0 + w) { v.ev[(size_t) i].vel = 1.25f; v.ev[(size_t) i].rev = false; break; }   // the re-entry hits harder
            }
        }
        // 5. one by one: backwards, bent, rolled
        const int base = n;
        int rollCounter = (int) ((absBar % 100000) * 100);
        std::array<Ev, 160> extra {}; int nx = 0;
        auto addX = [&] (const Ev& e) { if (nx < 160) extra[(size_t) nx++] = e; };
        for (int i = 0; i < base && (isBreak || rolls); ++i)
        {
            Ev& e = v.ev[(size_t) i];
            if (isBreak && r.uni() < pRev) e.rev = true;
            if (isBreak && r.uni() < pPitch)
            {
                static const float jump[6] = { 0.5f, 0.75f, 1.5f, 2.f, 0.5f, 1.335f };
                e.r0 = jump[(int) (r.uni() * 6.f) % 6];
                const float u = r.uni();
                e.r1 = u < 0.35f ? e.r0 * 0.5f : u < 0.6f ? e.r0 * 2.f : e.r0;                 // sometimes it glides
                if (r.uni() < 0.15f) { e.r0 = 1.f; e.r1 = 0.25f; e.len = 1.5f; }                // a tape stop
            }
            // a roll: the hit repeats along the grid, the rate climbing or falling, with the pitch following
            const bool tonal = kit->slices[(size_t) e.slice].y > 0.15f;
            const bool ratchet = rolls && tonal && takes();
            if ((ratchet || r.uni() < pRamp * (tonal ? 1.f : 0.5f)) && e.t < 15.0)
            {
                const double span = std::min (ratchet ? (kit->slices[(size_t) e.slice].y > 0.7f ? 2.0 : (rampAmt > 0.6f ? 4.0 : 3.0)) : (rampAmt > 0.6f && r.uni() < 0.5f ? 6.0 : (D > 0.5f ? 4.0 : 3.0)), 16.0 - e.t);
                const bool up = ratchet || r.uni() < 0.65f;
                const bool triplets = r.uni() < 0.2f * D;
                double gaps[64];
                const int ng = rollGaps (up, span, triplets, gaps, 64);
                const int rollId = ++rollCounter;
                e.grp = rollId;
                const float amount = std::max (0.35f, rampAmt);                            // how far the ramp goes in pitch
                double t = e.t;
                const float ratio0 = e.r0, ratio1 = e.r1;
                for (int rep = 0; rep < ng; ++rep)
                {
                    const double gap = gaps[rep];
                    const float prog = (float) ((t - e.t) / span);
                    const float semis = up ? 12.f * amount * prog : -9.f * amount * prog;      // climbing: the pitch rises; falling: it sags
                    const float ratio = std::exp2 (semis / 12.f);
                    if (rep == 0) { e.kind = 1; e.len = (float) (gap * p.gate); e.vel = up ? 0.4f : 1.f; }
                    else
                    {
                        Ev x = e;
                        x.t = t; x.kind = 1; x.len = (float) (gap * p.gate); x.grp = rollId;
                        x.r0 = ratio0 * ratio; x.r1 = ratio1 * ratio;
                        x.vel = up ? 0.4f + 0.8f * prog : 1.f - 0.75f * prog;              // swells while it climbs, fades while it falls
                        x.rev = r.uni() < pRev * 0.3f;
                        if (r.uni() < 0.25f * D && nNeighbour (e.slice) >= 0) x.slice = nNeighbour (e.slice, r.nextU32());
                        addX (x);
                    }
                    t += gap;
                }
                // whatever the roll covers is replaced by it
                for (int j = 0; j < base; ++j) if (j != i && v.ev[(size_t) j].t > e.t + 1e-9 && v.ev[(size_t) j].t < e.t + span - 1e-9) v.ev[(size_t) j].vel = 0.f;
            }
        }
        // 6. ghosts: soft hits in the gaps (the off-beats first)
        for (int st = 1; st < 16 && isBreak; ++st)
        {
            const float w = (st % 2) ? 1.f : (st % 4 == 2 ? 0.45f : 0.2f);
            if (r.uni() >= std::min (0.9f, pGhost * 1.1f * w)) continue;
            double t = (double) st + (r.uni() < 0.2f * D ? 0.5 : 0.0);
            bool free = true;
            for (int i = 0; i < base; ++i) if (v.ev[(size_t) i].vel > 0.f && std::abs (v.ev[(size_t) i].t - t) < 0.45) free = false;
            for (int i = 0; i < nx; ++i) if (std::abs (extra[(size_t) i].t - t) < 0.45) free = false;
            if (! free) continue;
            const bool kick = r.uni() < 0.2f;
            int pick = -1, seen = 0;
            for (int q = 0; q < (int) kit->slices.size(); ++q)
            {
                const float y = kit->slices[(size_t) q].y;
                if ((kick ? y < 0.2f : y > 0.3f) && ++seen && r.uni() < 1.f / (float) seen) pick = q;
            }
            if (pick < 0) continue;
            addX ({ t, pick, 0.22f + 0.28f * r.uni(), 1.f, 1.f, 0.9f, 0.f, false, 2, 0 });
        }
        // 7. the end of a phrase: a roll that swells into the next bar
        if (isBreak && phrase == 3 && D > 0.25f && r.uni() < 0.35f + 0.5f * D)
        {
            int pick = -1, seen = 0;
            for (int q = 0; q < (int) kit->slices.size(); ++q)
            {
                const float y = kit->slices[(size_t) q].y;
                if (y > 0.3f && y < 0.8f && ++seen && r.uni() < 1.f / (float) seen) pick = q;
            }
            if (pick >= 0)
            {
                for (int i = 0; i < base; ++i) if (v.ev[(size_t) i].t >= 12.0) v.ev[(size_t) i].vel = 0.f;
                double gaps[64];
                const int ng = rollGaps (true, 4.0, false, gaps, 64);
                double t = 12.0;
                for (int rep = 0; rep < ng; ++rep)
                {
                    const float prog = (float) ((t - 12.0) / 4.0), ratio = std::exp2 (12.f * std::max (0.35f, rampAmt) * prog / 12.f);
                    addX ({ t, pick, 0.35f + 0.8f * prog, ratio, ratio, (float) (gaps[rep] * p.gate), 0.f, false, 3, 0 });
                    t += gaps[rep];
                }
            }
        }
        // merge, drop the silenced, sort, and work out how long each hit may ring
        int m = 0;
        for (int i = 0; i < base; ++i) if (v.ev[(size_t) i].vel > 0.f) v.ev[(size_t) m++] = v.ev[(size_t) i];
        for (int i = 0; i < nx && m < maxEvents; ++i) v.ev[(size_t) m++] = extra[(size_t) i];
        n = m;
        sortEvents();
        for (int i = 0; i < n; ++i)
        {
            Ev& e = v.ev[(size_t) i];
            e.pan = std::clamp ((kit->slices[(size_t) e.slice].y - 0.4f) * 0.6f + (e.kind >= 2 ? 0.25f * (unit (mixHash ((uint32_t) i, (uint32_t) absBar, (uint32_t) v.note, 5u)) - 0.5f) : 0.f), -0.7f, 0.7f);
            if (e.kind == 0 && e.len <= 0.f)
                for (int j = i + 1; j < n; ++j) if (v.ev[(size_t) j].kind != 2 && v.ev[(size_t) j].t > e.t + 1e-9) { e.len = (float) (v.ev[(size_t) j].t - e.t); break; }   // a hit rings until the next one
        }
        v.nEv = n;
    }
    static float unit (uint32_t h) { return (float) (h >> 8) * (1.0f / 16777216.0f); }
    int nNeighbour (int slice, uint32_t rnd = 0) const
    {
        const auto& a = kit->slices[(size_t) slice];
        int best[3] = { -1, -1, -1 }; float cost[3] = { 1e9f, 1e9f, 1e9f };
        for (int i = 0; i < (int) kit->slices.size(); ++i)
        {
            if (i == slice) continue;
            const auto& b = kit->slices[(size_t) i];
            const float cc = (a.x - b.x) * (a.x - b.x) * 0.3f + (a.y - b.y) * (a.y - b.y);
            for (int j = 0; j < 3; ++j) if (cc < cost[j]) { for (int k = 2; k > j; --k) { cost[k] = cost[k - 1]; best[k] = best[k - 1]; } cost[j] = cc; best[j] = i; break; }
        }
        int cnt = 0; for (int j = 0; j < 3; ++j) if (best[j] >= 0) ++cnt;
        return cnt == 0 ? -1 : best[rnd % (uint32_t) cnt];
    }

    // ---------------------------------------------------------------------------------------------------------------
    // the fills: they take the place of the playback for exactly as long as the key is down, and give it back the moment it is up
    static double blockSteps (int type, int block)
    {
        static const double first[numFills] = { 8, 8, 4, 4, 6, 4, 4 };       // the plan of a first block (the key can be let go at any moment)
        return block == 0 ? first[type] : 4.0;                                // and it goes on block after block while the key is down
    }

    void startFill (int type, int note, float vel, const Params& p)
    {
        fill = Fill{};
        fill.on = true; fill.type = type; fill.note = note; fill.p = p; fill.vel = std::clamp (vel, 0.4f, 1.f);
        fill.start = quantised (p);
        fill.end = fill.nominalEnd = fill.start + blockSteps (type, 0);
        planFill (fill, p, fill.vel);
    }
    void extendFill()          // the key is still down at the end of a block: the next block starts at once
    {
        fill.block++;
        fill.start = fill.end;
        fill.end = fill.start + blockSteps (fill.type, fill.block);
        fill.nEv = 0; fill.iEv = 0;
        planFill (fill, fill.p, fill.vel);
    }
    void releaseFill()         // the key is let go: that is the end, now (a key let go before the fill began cancels it)
    {
        if (fill.released) return;
        if (! fill.started && curStep < fill.start - 1e-9) { fill.on = false; return; }
        fill.released = true;
        fill.end = std::min (fill.end, curStep);
    }

    void planFill (Fill& f, const Params& p, float vel)
    {
        if (kit == nullptr || kit->slices.empty()) { f.on = false; return; }
        gg::Rng r (mixHash ((uint32_t) p.seed, (uint32_t) f.type, (uint32_t) std::floor (f.start), 99u + (uint32_t) f.block) | 1u);
        for (int i = 0; i < 4; ++i) r.nextU32();
        const double loopSteps = loopBeats * 4.0, t0 = f.start;
        const float amount = std::max (0.35f, std::clamp (p.ramp, 0.f, 1.f));           // how far the roll goes in pitch
        int n = 0;
        auto add = [&] (double t, int slice, float v, float r0, float r1, float len, bool rev)
        {
            if (n >= 200 || slice < 0) return;
            const float pan = std::clamp ((kit->slices[(size_t) slice].y - 0.4f) * 0.6f + 0.2f * (r.uni() - 0.5f), -0.7f, 0.7f);
            f.ev[(size_t) n++] = { t, slice, v * vel, r0, r1, len, pan, rev, 3, 0 };
        };
        auto pick = [&] (float lo, float hi)
        {
            int best = -1, seen = 0;
            for (int i = 0; i < (int) kit->slices.size(); ++i)
            {
                const float y = kit->slices[(size_t) i].y;
                if (y >= lo && y <= hi && ++seen && r.uni() < 1.f / (float) seen) best = i;
            }
            if (best < 0) best = (int) (r.uni() * (float) kit->slices.size()) % (int) kit->slices.size();
            return best;
        };
        auto hitsFrom = [&] (double from, double span, auto&& fn)      // the hits of the loop that fall in [from, from + span), by position in the loop
        {
            for (const auto& h : hits)
            {
                double d = std::fmod ((double) h.pos - std::fmod (from, loopSteps) + loopSteps, loopSteps);
                const double q = std::round (d);
                d = q + (double) p.groove * (d - q);                    // (the hits keep their feel as much as Groove says)
                if (d >= 0.0 && d < span) fn (h, d);
            }
        };
        const bool rollUp = f.type == FillRollUp, rollDown = f.type == FillRollDown;
        if ((rollUp || rollDown) && f.block == 0)
        {
            // a stutter-edit ramp: a snare hit repeated along the grid, 1/8 -> 1/64 (rising in pitch, swelling) or 1/64 -> 1/8 (sagging, fading)
            const int s0 = pick (0.3f, 0.8f);
            double gaps[64];
            const double span = 8.0;
            const int ng = rollGaps (rollUp, span, false, gaps, 64);
            double t = 0.0;
            for (int rep = 0; rep < ng; ++rep)
            {
                const float prog = (float) (t / span);
                const float semis = rollUp ? 12.f * amount * prog : -9.f * amount * prog;
                const float ratio = std::exp2 (semis / 12.f);
                add (t0 + t, s0, rollUp ? 0.35f + 0.8f * prog : 1.f - 0.75f * prog, ratio, ratio, (float) (gaps[rep] * p.gate), false);
                t += gaps[rep];
            }
        }
        else if ((rollUp || rollDown))        // held: it stays where the ramp ended (full speed and high, or slow and low and nearly gone)
        {
            const int s0 = pick (0.3f, 0.8f);
            if (rollDown) { const float rr = std::exp2 (-9.f * amount / 12.f); for (int k = 0; k < 2; ++k) add (t0 + 1.0 + 2.0 * k, s0, 0.22f, rr, rr, (float) (2.0 * p.gate), false); }   // (the ramp's last gap was 2 steps: the next one is 2 steps on)
            else { const float rr = std::exp2 (12.f * amount / 12.f); for (int k = 0; k < 16; ++k) add (t0 + 0.25 * k, s0, 1.15f, rr, rr, 0.25f * (float) p.gate, false); }
        }
        else if (f.type == FillStutter)       // the last hit, again and again
        {
            int last = -1; double best = 1e9;
            hitsFrom (t0 - 16.0, 16.0, [&] (const LoopHit& h, double d) { const double behind = 16.0 - d; if (behind > 1e-9 && behind < best) { best = behind; last = h.slice; } });
            if (last < 0) last = pick (0.f, 1.f);
            if (f.block == 0)
            {
                double t = 0.0; int rep = 0;
                while (t < 4.0 - 1e-6 && rep < 20) { const double gap = rep < 2 ? 1.0 : 0.5; add (t0 + t, last, 1.f - 0.06f * (float) rep, 1.f, 1.f, (float) (gap * p.gate), false); t += gap; ++rep; }
            }
            else for (int k = 0; k < 8; ++k) add (t0 + 0.5 * k, last, 0.85f, 1.f, 1.f, 0.5f * (float) p.gate, false);      // held: at the fast rate
        }
        else if (f.type == FillHats)          // a flurry of hats
        {
            const int hat = pick (0.8f, 1.f);
            for (int k = 0; k < 8; ++k) add (t0 + 0.5 * k, hat, (k % 2 ? 0.6f : 1.f) * (0.55f + 0.45f * (float) k / 7.f), k == 7 ? 1.19f : 1.f, 1.f, 0.45f, false);
        }
        else if (f.type == FillReverse)       // the beat before, in the opposite order, backwards (held: the one before that, and so on)
        {
            hitsFrom (t0 - 4.0, 4.0, [&] (const LoopHit& h, double d) { add (std::min (t0 + 4.0 - d, t0 + 3.75), h.slice, 0.95f, 1.f, 1.f, 0.f, true); });
        }
        else if (f.type == FillStop)          // tape stop: the loop goes on, slower and lower, and stops (held: it stays stopped)
        {
            if (f.block == 0)
                hitsFrom (t0, 6.0, [&] (const LoopHit& h, double d) { const float prog = (float) (d / 6.0); const float r0 = 1.f - 0.8f * prog; add (t0 + d, h.slice, 1.f - 0.3f * prog, r0, r0 * 0.55f, 0.f, false); });
        }
        else if (f.type == FillDrop)          // nothing; the hard return comes when the key is let go
        {
            if (f.block == 0) f.accentSlice = pick (0.f, 0.2f);
        }
        f.nEv = n;
        std::sort (f.ev.begin(), f.ev.begin() + n, [] (const Ev& a, const Ev& b) { return a.t < b.t; });
    }

    // ---------------------------------------------------------------------------------------------------------------
    void trigger (int owner, int note, float noteVel, const Ev& e, double at, double stepSamples, double tune, int nVoices)
    {
        Hit* h = nullptr;
        for (auto& x : hit) if (! x.on) { h = &x; break; }
        if (h == nullptr) for (auto& x : hit) if (h == nullptr || x.order < h->order) h = &x;
        // a main hit cuts the one before it, like a sampler voice; ghosts and fills ring over it
        if (e.kind <= 1) for (auto& x : hit) if (x.on && x.owner == owner && x.kind <= 1 && x.chokeLeft == 0) { x.chokeLeft = std::max (1, (int) (0.003 * sr)); x.chokeLen = x.chokeLeft; }
        const auto& s = kit->slices[(size_t) e.slice];
        *h = Hit{};
        h->on = true; h->owner = owner; h->kind = e.kind; h->slice = e.slice; h->start = s.start; h->rev = e.rev; h->order = ++order;
        h->length = fullLen[(size_t) e.slice];
        h->pos = e.rev ? (double) (h->length - 1) : 0.0;
        const double r0 = (double) e.r0 * tune, r1 = (double) e.r1 * tune;
        h->inc = r0 * c;
        double natural = (double) h->length / std::max (1e-6, std::min (r0, r1) * c);
        double lenSamples = e.len > 0.f ? (double) e.len * stepSamples : natural;
        h->len = std::max (16, (int) std::min (lenSamples, natural));
        h->mult = r1 != r0 ? std::pow (r1 / r0, 1.0 / (double) h->len) : 1.0;
        h->relLen = std::max (1, std::min (h->len / 2, (int) (0.004 * sr)));
        h->gain = std::pow (std::clamp (e.vel, 0.f, 1.6f), 1.2f) * (0.6f + 0.4f * noteVel) / std::pow ((float) std::max (1, nVoices), 0.65f) * 0.9f;
        const float ang = (e.pan + 1.f) * 0.78539816f;
        h->gl = std::cos (ang) * 1.4142f; h->gr = std::sin (ang) * 1.4142f;
        const int cnt = count.load();
        ring[(size_t) (cnt % ringSize)].store (e.slice | (e.kind == 1 ? 0x10000 : 0) | (e.rev ? 0x20000 : 0) | (e.kind >= 2 ? 0x40000 : 0));
        count.store (cnt + 1);
        if (log != nullptr) log->push_back ({ at, note, e.slice, e.kind, e.vel, e.r0, e.r1, e.rev, e.grp });
    }

    float readHit (const Hit& h) const
    {
        const int i0 = (int) h.pos;
        const float f = (float) (h.pos - (double) i0);
        const int n = (int) kit->x.size();
        const int a = std::clamp (h.start + i0, 0, n - 1), b = std::clamp (h.start + i0 + 1, 0, n - 1);
        return kit->x[(size_t) a] + f * (kit->x[(size_t) b] - kit->x[(size_t) a]);
    }

    static float softLimit (float x)
    {
        const float a = std::abs (x);
        if (a <= 0.8f) return x;
        const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
        return x < 0.f ? -y : y;
    }

    double sr = 48000.0, loopBeats = 16.0, curStep = 0.0, c = 1.0;
    bool synced = false;
    const Kit* kit = nullptr;
    std::vector<LoopHit> hits;
    std::vector<int> fullLen;
    std::array<Vc, maxVoices> vc;
    std::array<Hit, maxHits> hit;
    Fill fill;
    long order = 0;
    std::vector<EventLog>* log = nullptr;
    std::array<std::atomic<int>, ringSize> ring {};
    std::atomic<int> count { 0 };
    std::atomic<float> dispPhase { 0.f };
    std::atomic<int> dispMask { 0 };
};
} // namespace sh
