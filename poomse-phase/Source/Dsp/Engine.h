#pragma once
#include "Kit.h"
#include "Sounds.h"
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

// Phase: an instrument made of cycles of different lengths. Hold a chord: every note starts two cycles (the note and its octave),
// each a loop of a few steps (5, 7, 9...) over the grid of the song. The cycles have different lengths, so they slip against each other
// and the pattern of the whole never repeats for a long time: the phasing of Steve Reich, the micro-loops of Jan Jelinek, the tape loops
// of Brian Eno. Every note falls on the grid; the dice are loaded by the song position and the seed (the same bar always sounds the same).
// Any sound can be dropped on it: it is cut into its notes, and the notes are played at the pitch of the keys.
namespace ph
{
enum CycleSet { SetReich = 0, SetTight, SetLong, SetEven, numSets };
inline constexpr const char* kSetNames[numSets] = { "Reich", "Tight", "Long", "Even" };
inline constexpr const char* kSetTags[numSets] = { "5, 7, 9, 11...", "3, 4, 5, 6...", "11, 13, 17, 19...", "4, 8, 16: no phasing" };
inline constexpr const char* kSetHelp[numSets] = {
    "REICH - cycles of 5, 7, 9, 11, 13... steps: they slip against each other and the whole takes a very long time to come back.",
    "TIGHT - short cycles (3, 4, 5, 6...): the pattern shifts fast, and everything lines up again every 8 bars.",
    "LONG - long prime cycles (11, 13, 17, 19...): slow, open, never the same twice.",
    "EVEN - cycles of 4, 8 and 16 steps: no phasing, a steady groove that only varies.",
};
inline constexpr int kCycle[numSets][16] = {
    { 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35 },
    { 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18 },
    { 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 37, 41 },
    { 4, 8, 16, 8, 4, 16, 8, 4, 16, 12, 8, 4, 16, 8, 12, 4 },
};
inline constexpr int kRealignBars[numSets] = { 0, 8, 0, 0 };               // every how many bars all the cycles start again together (0 = never)

inline constexpr int kRates = 4;
inline constexpr double kGridQ[kRates] = { 0.5, 0.25, 0.125, 1.0 / 6.0 };     // a step, in quarter notes: 1/8, 1/16, 1/32, 1/16 triplet
inline constexpr const char* kRateNames[kRates] = { "1/8", "1/16", "1/32", "1/16T" };

struct Params
{
    int set = SetReich;
    int rate = 1;            // index into kGridQ
    float density = 0.55f;   // how many of the steps of a cycle sound
    float variation = 0.25f; // how much of each cycle is written again every time round
    float ring = 0.5f;       // how long a note rings (0.15 s .. 3 s)
    float tune = 0.f;        // semitones
    int quant = 2;           // a key starts on the next: 0 step, 1 = 1/4, 2 = 1/8, 3 = 1/16 of a bar... (see kQuant)
    int seed = 1;
    bool latch = false;      // the keys stay down by themselves; press a key again to let it go
};
inline constexpr double kQuantQ[4] = { 0.0, 1.0, 0.5, 0.25 };

struct EventLog { long k; int layer, note, slice, len; float ratio, vel; };    // for tests

inline uint32_t mixHash (uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint32_t h = a * 2654435761u ^ (b + 0x9e3779b9u) * 2246822519u ^ (c + 0x7f4a7c15u) * 3266489917u ^ (d + 0x85ebca6bu) * 668265263u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
    return h;
}
inline float unitOf (uint32_t h) { return (float) (h >> 8) * (1.0f / 16777216.0f); }

class Engine
{
public:
    static constexpr int maxNotes = 8, maxLayers = 16, maxHits = 96, ringSize = 32, maskBits = 64;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& h : held) h = Held{};
        for (auto& h : hit) h = Hit{};
        order = 0; lastK = -1; synced = false;
        fullLen.reserve (256); sortedSlices.reserve (256);
    }
    void setKit (const Kit* k, int rootNote)
    {
        kit = k; root = rootNote;
        fullLen.clear(); sortedSlices.clear();
        if (k != nullptr && ! k->slices.empty())
        {
            for (size_t i = 0; i < k->slices.size(); ++i)
            {
                const int end = i + 1 < k->slices.size() ? k->slices[i + 1].start : (int) k->x.size();
                fullLen.push_back (std::max (1, std::min (end - k->slices[i].start, (int) (4.0 * k->sampleRate))));
                sortedSlices.push_back ((int) i);
            }
            std::sort (sortedSlices.begin(), sortedSlices.end(), [&] (int a, int b) { return k->slices[(size_t) a].y < k->slices[(size_t) b].y; });
        }
        killAll();
    }
    void setLog (std::vector<EventLog>* l) { log = l; }

    void noteOn (int note, float velocity, const Params& p)
    {
        Held* h = nullptr;
        for (auto& x : held) if (x.on && x.note == note) { h = &x; break; }
        if (h != nullptr && p.latch && ! h->released) { h->released = true; h->relK = nextStepK(); return; }      // pressed again: let go
        if (h == nullptr) for (auto& x : held) if (! x.on) { h = &x; break; }
        if (h == nullptr) for (auto& x : held) if (h == nullptr || x.order < h->order) h = &x;
        *h = Held{};
        h->on = true; h->note = note; h->vel = std::clamp (velocity, 0.2f, 1.f); h->order = ++order; h->latched = p.latch;
        const double q = kQuantQ[std::clamp (p.quant, 0, 3)], g = kGridQ[std::clamp (p.rate, 0, kRates - 1)];
        long k = nextStepK();
        if (q > 0.0) k = std::max (k, (long) std::ceil (std::ceil (curBeat / q - 1e-9) * q / g - 1e-9));
        h->startK = k;
    }
    void noteOff (int note)
    {
        for (auto& h : held) if (h.on && h.note == note && ! h.released && ! h.latched) { h.released = true; h.relK = nextStepK(); }
    }
    void allNotesOff() { for (auto& h : held) if (h.on && ! h.released) { h.released = true; h.relK = nextStepK(); } }
    void killAll() { for (auto& h : held) h = Held{}; for (auto& h : hit) h = Hit{}; synced = false; lastK = -1; clearDisplay(); }

    void process (int num, const Params& p, double beat0, double bpm, bool playing, float* L, float* R)
    {
        const double spq = sr * 60.0 / std::clamp (bpm, 20.0, 400.0);
        const double g = kGridQ[std::clamp (p.rate, 0, kRates - 1)];
        for (int i = 0; i < num; ++i)
        {
            const double beat = beat0 + (double) i / spq;
            if (! playing || kit == nullptr || fullLen.empty())
            {
                L[i] = R[i] = 0.f;
                if (synced) killAll();
                continue;
            }
            if (! synced || std::abs (beat - curBeat) > 8.0 / spq) { synced = true; for (auto& h : hit) h = Hit{}; lastK = (long) std::floor (beat / g + 1e-9) - 1; }
            curBeat = beat;
            const long k = (long) std::floor (beat / g + 1e-9);
            if (k != lastK) { lastK = k; onStep (k, p, g, spq); }

            float l = 0.f, r = 0.f;
            for (auto& h : hit)
            {
                if (! h.on) continue;
                const int left = h.len - h.age;
                const float a = std::min (1.f, (float) h.age / std::max (1.f, 0.002f * (float) sr));
                const float rel = left < h.relLen ? std::max (0.f, (float) left / (float) std::max (1, h.relLen)) : 1.f;
                const float s = readHit (h) * a * rel * h.gain;
                l += s * h.gl; r += s * h.gr;
                h.pos += h.inc;
                if (h.pos >= (double) h.length || ++h.age >= h.len) h = Hit{};
            }
            L[i] = softLimit (l); R[i] = softLimit (r);
        }
        int n = 0; for (const auto& h : held) if (h.on) ++n;
        dispNotes.store (n);
    }

    // what the interface shows
    int hitCount() const { return count.load(); }
    int hitAt (int i) const { return ring[(size_t) (i % ringSize)].load(); }
    int layerLength (int i) const { return dispLen[(size_t) i].load(); }
    int layerPosition (int i) const { return dispPos[(size_t) i].load(); }
    uint64_t layerMask (int i) const { return dispMask[(size_t) i].load(); }
    int activeNotes() const { return dispNotes.load(); }
    int activeHits() const { int n = 0; for (const auto& h : hit) if (h.on) ++n; return n; }

private:
    struct Held { bool on = false, released = false, latched = false; int note = 0; float vel = 0.f; long startK = 0, relK = 0, order = 0; };
    struct Hit
    {
        bool on = false;
        int slice = 0, start = 0, length = 0, age = 0, len = 0, relLen = 1;
        double pos = 0.0, inc = 1.0;
        float gain = 0.f, gl = 0.f, gr = 0.f;
        long order = 0;
    };
    struct Layer { int note = -1, octave = 0, key = 0, length = 0, rank = 0; float vel = 0.f; long startK = 0; };

    long nextStepK() const { return lastK < 0 ? 0 : lastK + 1; }

    // a note of one cycle: does it sound, which piece, how hard, where
    struct Step { bool hit; int slice; float vel; float pan; };
    Step stepOf (const Params& p, int key, int len, int pos, long cycle, int octave) const
    {
        const uint32_t base = (uint32_t) p.seed * 7919u + (uint32_t) key * 104729u;
        uint32_t c = 0;                                                  // which writing of this step is used: the first one, or one made for this cycle
        if (unitOf (mixHash (base, (uint32_t) pos, (uint32_t) cycle, 11u)) < p.variation * 0.6f) c = 1u + (uint32_t) (cycle & 0xffff);
        const float u = unitOf (mixHash (base, (uint32_t) pos, c, 21u));
        Step s { false, 0, 0.f, 0.f };
        const float dens = std::clamp (p.density, 0.f, 1.f);
        s.hit = (pos == 0) ? dens > 0.02f : u < dens * 0.9f;              // the first step of a cycle is its anchor
        const float z = unitOf (mixHash (base, (uint32_t) pos, c, 31u));
        const int n = (int) sortedSlices.size();
        const float bias = octave > 0 ? 0.35f + 0.65f * z : 0.65f * z;     // the upper cycle speaks with the brighter notes
        s.slice = sortedSlices[(size_t) std::clamp ((int) (bias * (float) n), 0, n - 1)];
        s.vel = pos == 0 ? 1.f : 0.35f + 0.45f * unitOf (mixHash (base, (uint32_t) pos, c, 41u));
        s.pan = (unitOf (mixHash (base, 0u, 0u, 51u)) - 0.5f) * 1.5f;
        (void) len;
        return s;
    }

    void layersNow (const Params& p, long k, Layer* out, int& n) const
    {
        n = 0;
        std::array<int, maxNotes> idx {}; int m = 0;
        for (int i = 0; i < maxNotes; ++i) if (held[(size_t) i].on && k >= held[(size_t) i].startK && ! (held[(size_t) i].released && k >= held[(size_t) i].relK)) idx[(size_t) m++] = i;
        std::sort (idx.begin(), idx.begin() + m, [&] (int a, int b) { return held[(size_t) a].note < held[(size_t) b].note; });
        for (int r = 0; r < m; ++r)
            for (int j = 0; j < 2; ++j)
            {
                const auto& h = held[(size_t) idx[(size_t) r]];
                Layer& l = out[n++];
                l.note = h.note; l.octave = j; l.rank = r * 2 + j; l.vel = h.vel; l.startK = h.startK;
                l.key = h.note * 2 + j;
                l.length = kCycle[std::clamp (p.set, 0, numSets - 1)][std::min (15, r * 2 + j)];
            }
    }

    void onStep (long k, const Params& p, double g, double spq)
    {
        // notes that were let go are forgotten once their last step has gone
        for (auto& h : held) if (h.on && h.released && k >= h.relK) h = Held{};
        Layer layers[maxLayers]; int n = 0;
        layersNow (p, k, layers, n);
        const int realignBars = kRealignBars[std::clamp (p.set, 0, numSets - 1)];
        const long realignSteps = realignBars > 0 ? (long) std::llround ((double) realignBars * 4.0 / g) : 0;
        const int nl = std::max (1, n);
        for (int li = 0; li < n; ++li)
        {
            const Layer& l = layers[li];
            long rel = k - l.startK;
            if (realignSteps > 0) rel %= realignSteps;
            const int pos = (int) (rel % l.length);
            const long cycle = rel / l.length;
            // the display: the pattern of this cycle
            uint64_t mask = 0;
            for (int s = 0; s < l.length && s < maskBits; ++s) if (stepOf (p, l.key, l.length, s, cycle, l.octave).hit) mask |= (uint64_t) 1 << s;
            dispLen[(size_t) li].store (l.length); dispPos[(size_t) li].store (pos); dispMask[(size_t) li].store (mask);

            const Step st = stepOf (p, l.key, l.length, pos, cycle, l.octave);
            if (! st.hit) continue;
            trigger (li, l, st, p, nl, spq, k);
        }
        for (int li = n; li < maxLayers; ++li) { dispLen[(size_t) li].store (0); dispPos[(size_t) li].store (0); dispMask[(size_t) li].store (0); }
    }

    void trigger (int layerIndex, const Layer& l, const Step& st, const Params& p, int nLayers, double /*spq*/, long k)
    {
        Hit* h = nullptr;
        for (auto& x : hit) if (! x.on) { h = &x; break; }
        if (h == nullptr) for (auto& x : hit) if (h == nullptr || x.order < h->order) h = &x;
        const auto& s = kit->slices[(size_t) st.slice];
        *h = Hit{};
        const double semis = (double) (l.note + 12 * l.octave) + (double) p.tune - (double) root;           // each note, and its octave above
        const double ratio = std::clamp (std::exp2 (semis / 12.0), 0.2, 6.0);
        h->on = true; h->slice = st.slice; h->start = s.start; h->length = fullLen[(size_t) st.slice]; h->order = ++order;
        h->inc = ratio * kit->sampleRate / sr;
        const double ringSec = 0.15 * std::pow (20.0, (double) std::clamp (p.ring, 0.f, 1.f));          // 0.15 s .. 3 s
        const double natural = (double) h->length / h->inc;
        h->len = std::max (32, (int) std::min (natural, ringSec * sr));
        h->relLen = std::max (1, std::min (h->len / 2, (int) (0.012 * sr)));
        h->gain = st.vel * l.vel * 0.7f / std::sqrt ((float) nLayers);
        const float ang = (std::clamp (st.pan, -0.8f, 0.8f) + 1.f) * 0.78539816f;
        h->gl = std::cos (ang) * 1.4142f; h->gr = std::sin (ang) * 1.4142f;
        const int c = count.load();
        ring[(size_t) (c % ringSize)].store (layerIndex);
        count.store (c + 1);
        if (log != nullptr) log->push_back ({ k, layerIndex, l.note, st.slice, h->len, (float) ratio, st.vel * l.vel });
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
    void clearDisplay()
    {
        for (int i = 0; i < maxLayers; ++i) { dispLen[(size_t) i].store (0); dispPos[(size_t) i].store (0); dispMask[(size_t) i].store (0); }
    }

    double sr = 48000.0, curBeat = 0.0;
    long lastK = -1;
    bool synced = false;
    int root = 60;
    const Kit* kit = nullptr;
    std::vector<int> fullLen, sortedSlices;
    std::array<Held, maxNotes> held;
    std::array<Hit, maxHits> hit;
    long order = 0;
    std::vector<EventLog>* log = nullptr;
    std::array<std::atomic<int>, ringSize> ring {};
    std::atomic<int> count { 0 }, dispNotes { 0 };
    std::array<std::atomic<int>, maxLayers> dispLen {}, dispPos {};
    std::array<std::atomic<uint64_t>, maxLayers> dispMask {};
};
} // namespace ph
