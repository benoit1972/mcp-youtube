// JUCE-free tests for the break cutter and the loop mutator. Build: g++ -std=c++17 -O2 -fsanitize=address,undefined tests/dsp_test.cpp
#include "../Source/Dsp/Engine.h"
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <set>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("FAIL: %s (line %d)\n", #c, __LINE__); ++failures; } } while (0)

static const double kSr = 48000.0;
static const int kC = 60, kLoop = 60, kFlip = 61, kRatchet = 62, kSwap = 63, kHollow = 64, kBend = 65, kShove = 66, kGhost = 67, kMirror = 68, kStorm = 69, kStretch = 70;

struct Run
{
    sh::Engine e;
    sh::Params p;
    double bpm = 90.0, beat = 0.0;
    std::vector<float> out;
    std::vector<sh::EventLog> log;
    explicit Run (const sh::Kit* kit, double loopBeats = 8.0)
    {
        e.prepare (kSr); e.setLoopBeats (loopBeats); e.setKit (kit); e.setLog (&log);
        p.quant = 0; p.groove = 1.f;
    }
    void go (double seconds, bool playing = true)
    {
        std::vector<float> L (256), R (256);
        const int total = (int) (seconds * kSr);
        for (int done = 0; done < total; done += 256)
        {
            const int n = std::min (256, total - done);
            e.process (n, p, beat, bpm, playing, L.data(), R.data());
            beat += (double) n / (kSr * 60.0 / bpm);
            for (int i = 0; i < n; ++i) out.push_back (0.5f * (L[(size_t) i] + R[(size_t) i]));
        }
    }
};
static float peakOf (const std::vector<float>& v)
{
    float p = 0.f; for (float x : v) { if (! std::isfinite (x)) return 1e9f; p = std::max (p, std::abs (x)); }
    return p;
}
static double rms (const std::vector<float>& v, double t0, double t1)
{
    const size_t a = (size_t) (t0 * kSr), b = std::min (v.size(), (size_t) (t1 * kSr));
    double s = 0.0; for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i];
    return b > a ? std::sqrt (s / (double) (b - a)) : 0.0;
}
static std::vector<double> attacks (const std::vector<float>& v)
{
    sh::Kit k; k.sampleRate = kSr; k.x = v; k.analyse();
    std::vector<double> t; for (const auto& s : k.slices) t.push_back ((double) s.start / kSr);
    return t;
}
static bool sameAudio (const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) return false;
    return true;
}
static std::set<std::pair<long, int>> baseSet (const sh::Kit* kit)
{
    Run r (kit); r.p.mutate = 0.f; r.p.groove = 0.f; r.e.noteOn (kLoop, 0.8f, r.p); r.go (8.0 * 60.0 / 90.0 * 1.0 - 0.01);
    std::set<std::pair<long, int>> s;
    for (const auto& l : r.log) s.insert ({ std::lround (l.step * 4.0), l.slice });
    return s;
}

int main()
{
    // ---- the cutter ----
    std::vector<sh::TruthHit> truth;
    sh::Kit kit = sh::makeDefaultKit (44100.0, &truth);
    kit.analyse();
    {
        int found = 0; double mean[4] = {}; int cnt[4] = {};
        for (const auto& t : truth)
        {
            int best = -1; double bd = 1e9;
            for (int i = 0; i < (int) kit.slices.size(); ++i) { const double d = std::abs ((double) kit.slices[(size_t) i].start / kit.sampleRate - t.time); if (d < bd) { bd = d; best = i; } }
            if (bd < 0.012) { ++found; mean[t.kind] += kit.slices[(size_t) best].bright; cnt[t.kind]++; }
        }
        std::printf ("built-in break: %zu real hits, %zu pieces found, %d matched; brightness: kick %.2f, snare %.2f, hat %.2f\n", truth.size(), kit.slices.size(), found, mean[0] / cnt[0], mean[1] / cnt[1], mean[2] / cnt[2]);
        CHECK (found >= (int) truth.size() - 1);
        CHECK (kit.slices.size() <= truth.size() + 1);
        CHECK (mean[0] / cnt[0] < mean[1] / cnt[1] && mean[1] / cnt[1] < mean[2] / cnt[2]);
        std::vector<float> ys; for (const auto& s : kit.slices) ys.push_back (s.y);
        std::sort (ys.begin(), ys.end());
        CHECK (ys.front() == 0.f && ys.back() == 1.f);
        sh::Kit s; s.sampleRate = 44100.0; s.x.assign (44100 * 3, 0.f); s.analyse();
        CHECK (s.slices.empty());
        sh::Kit sh2; sh2.sampleRate = 44100.0; sh2.x.assign (500, 0.3f); sh2.analyse();
        CHECK (sh2.slices.empty());
    }
    // a longer, busier break must also be cut sensibly
    {
        sh::Kit k2 = sh::makeDefaultKit (48000.0);
        k2.analyse();
        CHECK (k2.slices.size() >= 15);
    }

    const double secPerStep = 60.0 / 90.0 / 4.0;
    // ---- the loop, untouched ----
    {
        Run r (&kit);
        r.p.mutate = 0.f;
        r.e.noteOn (kLoop, 0.8f, r.p);
        r.go (2 * 16 * secPerStep);
        const float pk = peakOf (r.out);
        auto t = attacks (r.out);
        int hit = 0;
        for (const auto& th : truth) for (double a : t) if (std::abs (a - th.time) < 0.012) { ++hit; break; }
        std::printf ("mutate 0: %zu events logged, peak %.2f, %d of %zu hits at their place\n", r.log.size(), pk, hit, truth.size());
        CHECK (pk > 0.3f && pk < 1.f);
        CHECK (r.log.size() == truth.size());
        CHECK (hit >= (int) truth.size() - 1);
        for (const auto& l : r.log) CHECK (l.kind == 0 && ! l.rev && l.r0 == 1.f);
    }
    // ---- it follows the tempo, and keeps the pitch ----
    {
        Run r (&kit); r.p.mutate = 0.f; r.bpm = 120.0;
        r.e.noteOn (kLoop, 0.8f, r.p);
        r.go (2 * 16 * 60.0 / 120.0 / 4.0);
        auto t = attacks (r.out);
        int hit = 0;
        for (const auto& th : truth) for (double a : t) if (std::abs (a - th.time * 90.0 / 120.0) < 0.012) { ++hit; break; }
        std::printf ("at 120 bpm: %d of %zu hits at their scaled place\n", hit, truth.size());
        CHECK (hit >= (int) truth.size() - 2);
        auto zc = [&] (const std::vector<float>& v) { int c = 0; for (size_t i = 100; i + 1 < (size_t) (0.05 * kSr); ++i) if ((v[i] < 0.f) != (v[i + 1] < 0.f)) ++c; return c; };
        Run q (&kit); q.p.mutate = 0.f; q.e.noteOn (kLoop, 0.8f, q.p); q.go (1.0);
        const int a = zc (q.out), b = zc (r.out);
        std::printf ("  first kick: %d zero crossings at 90 bpm, %d at 120 bpm\n", a, b);
        CHECK (a > 3 && std::abs (a - b) <= std::max (2, a / 8));
    }
    // ---- always on the grid ----
    {
        int bad = 0, total = 0; std::set<int> kinds;
        for (int seed = 1; seed <= 6; ++seed)
            for (int note : { kStorm, kRatchet, kShove, kMirror, kGhost })
            {
                Run r (&kit); r.p.mutate = 1.f; r.p.groove = 0.f; r.p.seed = seed;
                for (auto* k : { &r.p.shuffle, &r.p.reverse, &r.p.pitch, &r.p.ghosts, &r.p.holes }) *k = 1.f;
                r.p.ramp = (seed % 2) ? 1.f : -1.f;
                r.e.noteOn (note, 0.9f, r.p);
                r.go (16 * 16 * secPerStep);
                for (const auto& l : r.log) { ++total; kinds.insert (l.kind); if (std::abs (l.step * 6.0 - std::round (l.step * 6.0)) > 1e-6 && std::abs (l.step * 4.0 - std::round (l.step * 4.0)) > 1e-6) ++bad; }
            }
        std::printf ("grid: %d events over 30 runs of 16 bars, %d off the grid (1/64 notes or 1/48 triplets); kinds seen: %zu\n", total, bad, kinds.size());
        CHECK (total > 500 && bad == 0);
        CHECK (kinds.size() == 4);          // loop hits, rolls, ghosts, fills
    }
    // ---- written, not random ----
    {
        auto go = [&] (Run& r, int seed, int note, double startBeat)
        {
            r.p.mutate = 0.8f; r.p.seed = seed; r.beat = startBeat;
            r.e.noteOn (note, 0.8f, r.p);
            r.go (8 * 16 * secPerStep);
        };
        Run a (&kit), b (&kit), c (&kit), d (&kit);
        go (a, 1, kStorm, 0.0); go (b, 1, kStorm, 0.0); go (c, 2, kStorm, 0.0); go (d, 1, kRatchet, 0.0);
        CHECK (sameAudio (a.out, b.out));
        CHECK (! sameAudio (a.out, c.out));
        CHECK (! sameAudio (a.out, d.out));
        // and each bar is different from the last, even though the loop is only two bars long
        auto bar = [&] (long n) { std::vector<std::pair<long, int>> v; for (const auto& l : a.log) if (std::floor (l.step / 16.0) == (double) n) v.push_back ({ std::lround ((l.step - 16.0 * (double) n) * 4.0), l.slice }); return v; };
        CHECK (bar (0) != bar (2) || bar (1) != bar (3));
    }
    // ---- from a whisper to another loop ----
    {
        const auto base = baseSet (&kit);
        auto changed = [&] (float mutate, int note)
        {
            double sum = 0.0; int runs = 0;
            for (int seed = 1; seed <= 12; ++seed)
            {
                Run r (&kit); r.p.mutate = mutate; r.p.seed = seed; r.p.groove = 0.f;
                r.e.noteOn (note, 0.8f, r.p);
                r.go (8 * 16 * secPerStep);
                int ch = 0, n = 0;
                for (const auto& l : r.log) { ++n; if (! base.count ({ std::lround (l.step * 4.0) % 128, l.slice })) ++ch; }
                sum += (double) ch / std::max (1, n); ++runs;
            }
            return sum / runs;
        };
        const double a = changed (0.1f, kLoop), b = changed (0.35f, kLoop), c = changed (0.6f, kLoop), d = changed (1.f, kStorm), z = changed (0.f, kStorm);
        std::printf ("share of the events that are not in the original loop: mutate 0 -> %.0f %%, 0.1 -> %.0f %%, 0.35 -> %.0f %%, 0.6 -> %.0f %%, 1.0 (Storm) -> %.0f %%\n", z * 100, a * 100, b * 100, c * 100, d * 100);
        CHECK (z == 0.0);
        CHECK (a > 0.0 && a < 0.12);         // a whisper: a few things change
        CHECK (a < b && b < c);
        CHECK (d > 0.5);                     // the top of the knob: another loop
    }
    // ---- the kinds of damage ----
    {
        auto share = [&] (int note, auto&& setup, auto&& pred)
        {
            int n = 0, m = 0;
            for (int seed = 1; seed <= 10; ++seed)
            {
                Run r (&kit); r.p.mutate = 1.f; r.p.seed = seed; setup (r.p);
                r.e.noteOn (note, 0.9f, r.p);
                r.go (8 * 16 * secPerStep);
                for (const auto& l : r.log) { ++n; if (pred (l)) ++m; }
            }
            return n ? (double) m / n : 0.0;
        };
        const double revHi = share (kFlip, [] (sh::Params& p) { p.reverse = 1.f; }, [] (const sh::EventLog& l) { return l.rev; });
        const double revLo = share (kFlip, [] (sh::Params& p) { p.reverse = 0.f; }, [] (const sh::EventLog& l) { return l.rev; });
        std::printf ("backwards: Flip with Reverse 1 -> %.0f %% of the hits, Reverse 0 -> %.0f %%\n", revHi * 100, revLo * 100);
        CHECK (revHi > 0.2 && revHi > revLo * 1.8);
        const double bent = share (kBend, [] (sh::Params& p) { p.pitch = 1.f; }, [] (const sh::EventLog& l) { return l.r0 != 1.f || l.r1 != 1.f; });
        std::printf ("bent in pitch: Bend -> %.0f %% of the hits\n", bent * 100);
        CHECK (bent > 0.1);
        const double ghosts = share (kGhost, [] (sh::Params& p) { p.ghosts = 1.f; }, [] (const sh::EventLog& l) { return l.kind == 2; });
        const double ghostsLo = share (kGhost, [] (sh::Params& p) { p.ghosts = 0.f; }, [] (const sh::EventLog& l) { return l.kind == 2; });
        std::printf ("ghost hits: Ghost with Ghosts 1 -> %.0f %%, Ghosts 0 -> %.0f %%\n", ghosts * 100, ghostsLo * 100);
        CHECK (ghosts > 0.15 && ghosts > ghostsLo * 1.5);
        bool soft = true;
        for (int seed = 1; seed <= 6; ++seed)
        {
            Run r (&kit); r.p.mutate = 1.f; r.p.seed = seed; r.p.ghosts = 1.f;
            r.e.noteOn (kGhost, 0.9f, r.p); r.go (8 * 16 * secPerStep);
            for (const auto& l : r.log) if (l.kind == 2 && l.vel > 0.6f) soft = false;
        }
        CHECK (soft);
        // holes: fewer hits than the loop
        auto count = [&] (float holes)
        {
            int n = 0;
            for (int seed = 1; seed <= 10; ++seed)
            {
                Run r (&kit); r.p.mutate = 1.f; r.p.seed = seed; r.p.holes = holes; r.p.ghosts = 0.f; r.p.ramp = 0.f; r.p.shuffle = 0.f; r.p.reverse = 0.f; r.p.pitch = 0.f;
                r.e.noteOn (kHollow, 0.9f, r.p); r.go (8 * 16 * secPerStep);
                for (const auto& l : r.log) if (l.kind == 0) ++n;
            }
            return n;
        };
        const int fewer = count (1.f), more = count (0.f);
        std::printf ("hits of the loop that survive: Holes 1 -> %d, Holes 0 -> %d\n", fewer, more);
        CHECK (fewer < more);
    }
    // ---- stutter-edit ramps: the rate climbs or falls through the grid ----
    {
        auto rolls = [&] (float ramp)
        {
            std::vector<std::vector<sh::EventLog>> seqs;
            for (int seed = 1; seed <= 12; ++seed)
            {
                Run r (&kit); r.p.mutate = 1.f; r.p.seed = seed; r.p.ramp = ramp; r.p.ghosts = 0.f; r.p.holes = 0.f; r.p.shuffle = 0.f; r.p.groove = 0.f;
                r.e.noteOn (kRatchet, 0.9f, r.p); r.go (8 * 16 * secPerStep);
                std::map<int, std::vector<sh::EventLog>> groups;
                for (const auto& l : r.log) if (l.kind == 1 && l.grp > 0) groups[l.grp + seed * 100000000].push_back (l);
                for (auto& g : groups) if (g.second.size() >= 4) seqs.push_back (g.second);
            }
            return seqs;
        };
        const auto up = rolls (1.f), down = rolls (-1.f);
        int okUp = 0, okDown = 0;
        for (const auto& s : up)
        {
            bool mono = true; for (size_t i = 2; i < s.size(); ++i) if (s[i].step - s[i - 1].step > s[i - 1].step - s[i - 2].step + 1e-6) mono = false;
            if (mono && s.back().vel > s.front().vel && s.back().step - s.front().step > 0.5) ++okUp;
        }
        for (const auto& s : down)
        {
            bool mono = true; for (size_t i = 2; i < s.size(); ++i) if (s[i].step - s[i - 1].step < s[i - 1].step - s[i - 2].step - 1e-6) mono = false;
            if (mono && s.back().vel < s.front().vel) ++okDown;
        }
        std::printf ("rolls: Ramp +1 -> %zu rolls (%d speed up and swell), Ramp -1 -> %zu rolls (%d slow down and fade)\n", up.size(), okUp, down.size(), okDown);
        CHECK (up.size() >= 4 && (double) okUp >= 0.8 * (double) up.size());
        CHECK (down.size() >= 4 && (double) okDown >= 0.8 * (double) down.size());
    }
    // ---- the end of a phrase ----
    {
        int fills = 0;
        for (int seed = 1; seed <= 12; ++seed)
        {
            Run r (&kit); r.p.mutate = 0.8f; r.p.seed = seed;
            r.e.noteOn (kStorm, 0.9f, r.p); r.go (8 * 16 * secPerStep);
            bool inLast = false;
            for (const auto& l : r.log) if (l.kind == 3) { const long bar = (long) std::floor (l.step / 16.0); if (bar % 4 == 3) inLast = true; else fills = -1000; }
            if (inLast) ++fills;
        }
        std::printf ("fills at the end of a phrase: %d of 12 runs\n", fills);
        CHECK (fills >= 4);
    }
    // ---- several keys, several loops ----
    {
        Run one (&kit); one.p.mutate = 0.6f;
        one.e.noteOn (kLoop, 0.8f, one.p); one.go (8 * 16 * secPerStep);
        Run many (&kit); many.p.mutate = 0.6f;
        for (int n : { kLoop, kFlip, kRatchet, kGhost }) many.e.noteOn (n, 0.8f, many.p);
        many.go (8 * 16 * secPerStep);
        std::set<int> notes; for (const auto& l : many.log) notes.insert (l.note);
        std::printf ("4 keys held: voices %d, notes heard %zu, peak %.2f (1 key: %.2f)\n", many.e.activeVoices(), notes.size(), peakOf (many.out), peakOf (one.out));
        CHECK (many.e.activeVoices() == 4 && notes.size() == 4);
        CHECK (peakOf (many.out) < 1.0f && peakOf (many.out) < peakOf (one.out) * 2.5f);
        many.e.noteOff (kLoop); many.e.noteOff (kFlip); many.e.noteOff (kRatchet); many.e.noteOff (kGhost);
        const size_t before = many.log.size();
        many.go (2.0);
        std::printf ("  after the keys are let go: %zu more events, voices %d, hits ringing %d\n", many.log.size() - before, many.e.activeVoices(), many.e.activeHits());
        CHECK (many.log.size() - before <= 4 && many.e.activeVoices() == 0);
        many.go (1.5);
        CHECK (many.e.activeHits() == 0);
    }
    // ---- a note starts on the grid ----
    {
        Run r (&kit); r.p.mutate = 0.f; r.p.quant = 3; r.beat = 0.3;           // 1.2 steps in
        r.go (0.004);                                                           // (the engine learns where the song is)
        r.e.noteOn (kLoop, 0.8f, r.p);
        r.go (16 * secPerStep);
        double first = 1e9; for (const auto& l : r.log) first = std::min (first, l.step);
        std::printf ("note pressed 1.3 steps into the bar: first event at step %.2f with quantise 1/16\n", first);
        CHECK (first >= 2.0 - 1e-6);
    }
    // ---- the transport ----
    {
        Run r (&kit); r.p.mutate = 0.5f;
        r.e.noteOn (kLoop, 0.8f, r.p);
        r.go (1.0);
        const size_t n = r.out.size();
        r.go (1.0, false);
        CHECK (rms (r.out, (double) n / kSr, (double) r.out.size() / kSr) == 0.0);
        CHECK (r.e.activeVoices() == 0);
    }
    // ---- the fills ----
    {
        static const double lens[sh::numFills] = { 4, 8, 4, 4, 4, 4, 6, 8, 4, 8, 4, 8 };
        for (int t = 0; t < sh::numFills; ++t)
        {
            Run r (&kit); r.p.mutate = 0.f; r.p.quant = 3; r.p.groove = 0.f;
            r.go (0.004);
            r.e.noteOn (kLoop, 0.8f, r.p);                         // the loop is playing
            r.go (3 * 16 * secPerStep + 0.001);
            const double start = 4.0 * 16.0 / 16.0 * 4.0;           // the fill is pressed at the start of a beat-aligned step
            (void) start;
            r.log.clear();
            const size_t before = r.out.size();
            r.e.noteOn (sh::kFillBase + t, 0.9f, r.p);
            const double pressedAt = r.beat * 4.0;                  // in steps
            const double fillStart = std::ceil (pressedAt - 1e-9);
            r.go ((lens[t] + 6.0) * secPerStep);
            double firstFill = 1e9, lastFill = -1e9; int nFill = 0, loopInside = 0; bool onGrid = true, finite = true;
            for (const auto& l : r.log)
            {
                if (l.note == sh::kFillBase + t)
                {
                    ++nFill; firstFill = std::min (firstFill, l.step); lastFill = std::max (lastFill, l.step);
                    if (std::abs (l.step * 6.0 - std::round (l.step * 6.0)) > 1e-6 && std::abs (l.step * 4.0 - std::round (l.step * 4.0)) > 1e-6) onGrid = false;
                }
                else if (l.step >= fillStart + 1e-9 && l.step < fillStart + lens[t] - 1e-9) ++loopInside;
            }
            std::vector<float> after (r.out.begin() + (long) before, r.out.end());
            finite = peakOf (after) <= 1.0f;
            std::printf ("fill %-9s: %2d events from step %.1f to %.1f, %d loop hits inside it, ", sh::kFillNames[t], nFill, firstFill - fillStart, lastFill - fillStart, loopInside);
            std::printf ("loop back afterwards: %s\n", r.log.size() > (size_t) nFill ? "yes" : "NO");
            CHECK (nFill >= 1 && onGrid && finite);
            CHECK (firstFill >= fillStart - 1e-6 && lastFill <= fillStart + lens[t] + 1e-6);   // inside its own window (the drop's return lands on the end)
            CHECK (loopInside == 0);                                // the fill has the floor
            CHECK (r.log.size() > (size_t) nFill);                  // and the loop comes back
        }
        // what each one is
        auto fillRun = [&] (int t)
        {
            auto r = std::make_unique<Run> (&kit); r->p.mutate = 0.f; r->p.quant = 3; r->go (0.004);
            r->e.noteOn (kLoop, 0.8f, r->p); r->go (3 * 16 * secPerStep + 0.001); r->log.clear();
            r->e.noteOn (sh::kFillBase + t, 0.9f, r->p); r->go (14 * secPerStep);
            std::vector<sh::EventLog> v; for (const auto& l : r->log) if (l.note == sh::kFillBase + t) v.push_back (l);
            return v;
        };
        { auto v = fillRun (0); bool up = v.size() >= 5; for (size_t i = 2; i < v.size(); ++i) up = up && v[i].step - v[i - 1].step <= v[i - 1].step - v[i - 2].step + 1e-6; CHECK (up && v.back().vel > v.front().vel); }
        { auto v = fillRun (2); bool dn = v.size() >= 5; for (size_t i = 2; i < v.size(); ++i) dn = dn && v[i].step - v[i - 1].step >= v[i - 1].step - v[i - 2].step - 1e-6; CHECK (dn && v.back().vel < v.front().vel); }
        { auto v = fillRun (5); bool allRev = ! v.empty(); for (const auto& l : v) allRev = allRev && l.rev; CHECK (allRev); }
        { auto v = fillRun (8); bool same = v.size() >= 4; for (const auto& l : v) same = same && l.slice == v[0].slice; CHECK (same); }
        { auto v = fillRun (10); bool desc = v.size() == 8; for (size_t i = 1; i < v.size(); ++i) desc = desc && v[i].r0 < v[i - 1].r0; CHECK (desc); }
        { auto v = fillRun (6); bool falling = v.size() >= 2; for (size_t i = 1; i < v.size(); ++i) falling = falling && v[i].r0 < v[i - 1].r0 + 1e-6; CHECK (falling && v.back().r0 < 0.7f); }
        { auto v = fillRun (7); CHECK (v.size() == 1 && v[0].vel > 1.0f); }
        // the drop is silent
        {
            Run r (&kit); r.p.mutate = 0.f; r.p.quant = 3; r.go (0.004);
            r.e.noteOn (kLoop, 0.8f, r.p); r.go (3 * 16 * secPerStep + 0.001);
            const double before = r.beat * 4.0;
            const size_t n0 = r.out.size();
            r.e.noteOn (sh::kFillBase + 7, 0.9f, r.p); r.go (8 * secPerStep);
            const double a = rms (r.out, (double) n0 / kSr + 2.0 * secPerStep, (double) n0 / kSr + 7.0 * secPerStep);
            Run ref (&kit); ref.p.mutate = 0.f; ref.p.quant = 3; ref.go (0.004);
            ref.e.noteOn (kLoop, 0.8f, ref.p); ref.go (3 * 16 * secPerStep + 0.001);
            const size_t m0 = ref.out.size(); ref.go (8 * secPerStep);
            const double b = rms (ref.out, (double) m0 / kSr + 2.0 * secPerStep, (double) m0 / kSr + 7.0 * secPerStep);
            std::printf ("drop: level %.4f against %.4f for the loop (pressed at step %.1f)\n", a, b, before);
            CHECK (a < 0.2 * b);
        }
        // several fills in a row, and with the keys: no trouble
        {
            Run r (&kit); r.p.mutate = 0.7f;
            r.e.noteOn (kStorm, 0.9f, r.p);
            for (int i = 0; i < 12; ++i) { r.e.noteOn (sh::kFillBase + i, 0.9f, r.p); r.go (0.4); }
            r.go (3.0);
            CHECK (peakOf (r.out) <= 1.0f);
        }
    }
    // ---- hard use ----
    {
        gg::Rng g (9);
        Run r (&kit);
        float pk = 0.f; bool finite = true;
        for (int i = 0; i < 400; ++i)
        {
            r.p.mutate = g.uni(); r.p.shuffle = g.uni(); r.p.reverse = g.uni(); r.p.pitch = g.uni(); r.p.ghosts = g.uni(); r.p.holes = g.uni();
            r.p.ramp = g.bi(); r.p.groove = g.uni(); r.p.gate = 0.2f + 0.8f * g.uni(); r.p.tune = g.bi() * 12.f; r.p.quant = (int) (g.uni() * 4.f); r.p.seed = 1 + i % 50;
            r.bpm = 60.0 + 120.0 * g.uni();
            if (g.uni() < 0.5f) r.e.noteOn (36 + (int) (g.uni() * 60.f), g.uni(), r.p); else r.e.noteOff (36 + (int) (g.uni() * 60.f));
            const size_t n = r.out.size();
            r.go (0.12);
            std::vector<float> chunk (r.out.begin() + (long) n, r.out.end());
            pk = std::max (pk, peakOf (chunk)); finite = finite && peakOf (chunk) < 1e8f;
        }
        std::printf ("400 random gestures: peak %.3f, voices %d, hits ringing %d\n", pk, r.e.activeVoices(), r.e.activeHits());
        CHECK (finite && pk <= 1.0f);
    }
    std::printf (failures ? "%d FAILURES\n" : "ALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
