// JUCE-free tests for Phase. Build: g++ -std=c++17 -O2 -fsanitize=address,undefined tests/dsp_test.cpp
#include "../Source/Dsp/Engine.h"
#include <cstdio>
#include <map>
#include <memory>
#include <set>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("FAIL: %s (line %d)\n", #c, __LINE__); ++failures; } } while (0)
static const double kSr = 48000.0;

struct Run
{
    ph::Engine e; ph::Params p; double bpm = 120.0, beat = 0.0; std::vector<float> out; std::vector<ph::EventLog> log;
    explicit Run (const ph::Kit* kit) { e.prepare (kSr); e.setKit (kit, ph::kBuiltInRoot); e.setLog (&log); p.quant = 0; }
    void go (double seconds, bool playing = true)
    {
        std::vector<float> L (256), R (256);
        const int total = (int) (seconds * kSr);
        for (int d = 0; d < total; d += 256)
        {
            const int n = std::min (256, total - d);
            e.process (n, p, beat, bpm, playing, L.data(), R.data());
            beat += (double) n / (kSr * 60.0 / bpm);
            for (int i = 0; i < n; ++i) out.push_back (0.5f * (L[(size_t) i] + R[(size_t) i]));
        }
    }
    void steps (double n) { go (n * ph::kGridQ[p.rate] * 60.0 / bpm); }
};
static float peakOf (const std::vector<float>& v) { float p = 0.f; for (float x : v) { if (! std::isfinite (x)) return 1e9f; p = std::max (p, std::abs (x)); } return p; }
static bool sameAudio (const std::vector<float>& a, const std::vector<float>& b) { if (a.size() != b.size()) return false; for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) return false; return true; }

int main()
{
    ph::Kit kit = ph::makeDefaultKit (44100.0);
    kit.analyse();
    std::printf ("built-in sound: %zu notes found\n", kit.slices.size());
    CHECK (kit.slices.size() == 8);
    {   // the cutter on a sound without attacks still gives pieces
        ph::Kit pad; pad.sampleRate = 44100.0; for (int i = 0; i < 44100 * 6; ++i) pad.x.push_back (0.3f * (float) std::sin (0.03 * i));
        pad.analyse(); CHECK (pad.slices.size() == 16);
    }
    // ---- nothing without a key; a note starts its cycles on the grid ----
    {
        Run r (&kit);
        r.steps (8); CHECK (peakOf (r.out) == 0.f);
        r.e.noteOn (60, 0.9f, r.p);
        r.steps (64);
        CHECK (peakOf (r.out) > 0.05f && peakOf (r.out) <= 1.f);
        CHECK (! r.log.empty());
        // every event falls on a step of the grid by construction (k is the step); and the first one is the anchor of a cycle
        CHECK (r.log.front().layer == 0);
    }
    // ---- a note makes two cycles, and their lengths are the ones of the set ----
    {
        for (int set = 0; set < ph::numSets; ++set)
        {
            Run r (&kit); r.p.set = set; r.p.density = 1.f; r.p.variation = 0.f;
            r.e.noteOn (60, 0.9f, r.p); r.steps (200);
            int len0 = r.e.layerLength (0), len1 = r.e.layerLength (1);
            std::printf ("set %-5s: one key -> cycles of %d and %d steps\n", ph::kSetNames[set], len0, len1);
            CHECK (len0 == ph::kCycle[set][0] && len1 == ph::kCycle[set][1]);
        }
    }
    // ---- a cycle repeats exactly with the length it announces (no variation); a chord's cycles slip against each other ----
    {
        Run r (&kit); r.p.set = ph::SetReich; r.p.density = 0.6f; r.p.variation = 0.f;
        for (int n : { 60, 64, 67 }) r.e.noteOn (n, 0.9f, r.p);
        r.steps (800);
        std::map<int, std::vector<long>> byLayer; for (const auto& l : r.log) byLayer[l.layer].push_back (l.k);
        CHECK (byLayer.size() == 6);
        bool repeats = true;
        for (int layer = 0; layer < 6; ++layer)
        {
            const int L = ph::kCycle[ph::SetReich][layer];
            std::set<long> ks (byLayer[layer].begin(), byLayer[layer].end());
            long start = *ks.begin();
            for (long k : ks) if (k + L < start + 700) if (! ks.count (k + L)) repeats = false;       // a hit comes back one cycle later
        }
        CHECK (repeats);
        // the first step of every layer falls on the same step, then they drift: nothing but the start lines up again for a long time
        std::set<long> anchors;
        for (const auto& l : r.log) if (l.vel > 0.89f) anchors.insert (l.k);
        std::printf ("chord of 3, Reich: the six anchors fall together %d times in 800 steps\n", (int) std::count_if (anchors.begin(), anchors.end(), [&] (long k) { int c = 0; for (const auto& l : r.log) if (l.k == k && l.vel > 0.89f) ++c; return c >= 4; }));
        CHECK (std::count_if (anchors.begin(), anchors.end(), [&] (long k) { int c = 0; for (const auto& l : r.log) if (l.k == k && l.vel > 0.89f) ++c; return c >= 6; }) <= 1);
    }
    // ---- the pitch is the key ----
    {
        Run r (&kit); r.p.density = 1.f; r.e.noteOn (72, 0.9f, r.p); r.steps (60);
        bool ok = ! r.log.empty();
        for (const auto& l : r.log) { const double expect = std::exp2 ((l.layer == 0 ? 72 - 60 : 84 - 60) / 12.0); ok = ok && std::abs (l.ratio - expect) < 1e-6; }
        CHECK (ok);
        Run t (&kit); t.p.tune = -12.f; t.p.density = 1.f; t.e.noteOn (60, 0.9f, t.p); t.steps (20);
        CHECK (! t.log.empty() && std::abs (t.log.front().ratio - 0.5f) < 1e-6);
    }
    // ---- density ----
    {
        auto count = [&] (float dens) { Run r (&kit); r.p.density = dens; r.p.variation = 0.f; r.e.noteOn (60, 0.9f, r.p); r.steps (400); return (int) r.log.size(); };
        const int a = count (0.f), b = count (0.3f), c = count (0.7f), d = count (1.f);
        std::printf ("hits in 400 steps: density 0 -> %d, 0.3 -> %d, 0.7 -> %d, 1 -> %d\n", a, b, c, d);
        CHECK (a == 0 && b > 0 && b < c && c < d);
    }
    // ---- variation: none = the cycle is the same every time; some = it is written again ----
    {
        auto distinct = [&] (float var)
        {
            Run r (&kit); r.p.set = ph::SetEven; r.p.density = 0.6f; r.p.variation = var; r.e.noteOn (60, 0.9f, r.p); r.steps (16 * 12);
            std::set<std::vector<long>> bars;
            for (int bar = 0; bar < 12; ++bar) { std::vector<long> v; for (const auto& l : r.log) if (l.layer == 0 && l.k / 16 == bar + 0 && l.k >= 0) v.push_back ((l.k % 16) * 100 + l.slice); bars.insert (v); }
            return (int) bars.size();
        };
        const int none = distinct (0.f), some = distinct (0.6f);
        std::printf ("distinct bars out of 12: variation 0 -> %d, 0.6 -> %d\n", none, some);
        CHECK (none <= 2 && some > none + 3);
    }
    // ---- written, not random ----
    {
        auto go = [&] (int seed, double beat0) { Run r (&kit); r.p.seed = seed; r.beat = beat0; r.e.noteOn (60, 0.9f, r.p); r.e.noteOn (67, 0.9f, r.p); r.steps (200); return r.out; };
        const auto a = go (1, 0.0), b = go (1, 0.0), c = go (2, 0.0);
        CHECK (sameAudio (a, b));
        CHECK (! sameAudio (a, c));
    }
    // ---- the Tight set lines up again every 8 bars ----
    {
        Run r (&kit); r.p.set = ph::SetTight; r.p.density = 0.7f; r.p.variation = 0.f; r.p.rate = 1;
        r.e.noteOn (60, 0.9f, r.p); r.e.noteOn (62, 0.9f, r.p);
        r.steps (16 * 8 * 3);
        // the anchors of the first layer and of the others fall together at the start of every 8 bars
        std::map<long, int> anchorsAt; for (const auto& l : r.log) if (l.vel > 0.89f) anchorsAt[l.k]++;
        int together0 = anchorsAt.count (0) ? anchorsAt[0] : 0;
        CHECK (together0 >= 2);
        long second = 16 * 8, third = 16 * 16;
        CHECK (anchorsAt[second] >= together0 && anchorsAt[third] >= together0);
    }
    // ---- latch: the keys stay down, and pressing again lets go ----
    {
        Run r (&kit); r.p.latch = true; r.p.density = 1.f;
        r.e.noteOn (60, 0.9f, r.p); r.e.noteOff (60); r.steps (40);
        CHECK (r.e.activeNotes() == 1);
        const size_t n = r.log.size(); r.steps (20); CHECK (r.log.size() > n);
        r.e.noteOn (60, 0.9f, r.p);                       // again: it lets go
        r.steps (4);
        const size_t m = r.log.size(); r.steps (40); CHECK (r.log.size() == m && r.e.activeNotes() == 0);
        // without latch a key ends with its release
        Run q (&kit); q.p.latch = false; q.p.density = 1.f; q.e.noteOn (60, 0.9f, q.p); q.steps (20); q.e.noteOff (60); q.steps (3);
        const size_t z = q.log.size(); q.steps (40); CHECK (q.log.size() == z);
    }
    // ---- a key starts on its quantise boundary ----
    {
        Run r (&kit); r.p.quant = 3; r.p.rate = 1; r.beat = 0.3; r.steps (0.1);        // a quarter note is 4 steps of 1/16: quant 3 = every 1/4 of a quarter
        r.e.noteOn (60, 0.9f, r.p); r.steps (16);
        long first = 1L << 40; for (const auto& l : r.log) first = std::min (first, l.k);
        CHECK (std::fmod ((double) first * 0.25, 0.25) < 1e-9 && first * 0.25 >= 0.3);
    }
    // ---- killAll lets go of every key, even latched ones ----
    {
        Run r (&kit); r.p.latch = true; r.p.density = 1.f; r.e.noteOn (60, 0.9f, r.p); r.e.noteOn (64, 0.9f, r.p); r.steps (20);
        CHECK (r.e.activeNotes() == 2);
        r.e.killAll(); r.steps (2); r.out.clear(); r.log.clear(); r.steps (40);
        CHECK (r.e.activeNotes() == 0 && r.log.empty() && peakOf (r.out) == 0.f);
        r.e.noteOn (67, 0.9f, r.p); r.steps (20); CHECK (! r.log.empty());
    }
    // ---- tails, transport, hard use ----
    {
        Run r (&kit); r.p.density = 1.f; r.p.ring = 1.f; r.e.noteOn (60, 1.f, r.p); r.steps (20); r.e.noteOff (60); r.steps (4); r.go (12.0);
        float tail = 0.f; for (size_t i = r.out.size() - (size_t) (2 * kSr); i < r.out.size(); ++i) tail = std::max (tail, std::abs (r.out[i]));
        CHECK (tail == 0.f && r.e.activeHits() == 0);
        Run t (&kit); t.e.noteOn (60, 0.9f, t.p); t.go (2.0); const size_t n = t.out.size(); t.go (1.0, false);
        float after = 0.f; for (size_t i = n; i < t.out.size(); ++i) after = std::max (after, std::abs (t.out[i]));
        CHECK (after == 0.f && t.e.activeNotes() == 0);
        gg::Rng g (5); Run h (&kit); float pk = 0.f;
        for (int i = 0; i < 600; ++i)
        {
            h.p.set = (int) (g.uni() * 4.f); h.p.rate = (int) (g.uni() * 4.f); h.p.density = g.uni(); h.p.variation = g.uni(); h.p.ring = g.uni(); h.p.tune = g.bi() * 12.f; h.p.quant = (int) (g.uni() * 4.f); h.p.seed = 1 + i % 40; h.p.latch = g.uni() < 0.3f;
            h.bpm = 60.0 + 120.0 * g.uni();
            const int note = 24 + (int) (g.uni() * 80.f);
            if (g.uni() < 0.6f) h.e.noteOn (note, g.uni(), h.p); else h.e.noteOff (note);
            const size_t n0 = h.out.size(); h.go (0.1);
            std::vector<float> chunk (h.out.begin() + (long) n0, h.out.end()); pk = std::max (pk, peakOf (chunk));
        }
        std::printf ("600 random key presses: peak %.3f, ringing %d\n", pk, h.e.activeHits());
        CHECK (pk <= 1.0f);
    }
    std::printf (failures ? "%d FAILURES\n" : "ALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
