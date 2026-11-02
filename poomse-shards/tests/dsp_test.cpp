// JUCE-free tests for the break cutter and the loop player. Build: g++ -std=c++17 -O2 -fsanitize=address,undefined tests/dsp_test.cpp
#include "../Source/Dsp/Engine.h"
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <set>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("FAIL: %s (line %d)\n", #c, __LINE__); ++failures; } } while (0)

static const double kSr = 48000.0;
// the seven keys, on the white keys of the octave starting at C3 (MIDI 60)
static const int kLoop = 60, kBreak = 62, kRollUp = 64, kRollDown = 65, kReverse = 67, kStop = 69, kDrop = 71;

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
    double step() const { return beat * 4.0; }
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
    void steps (double n) { go (n * 60.0 / bpm / 4.0); }
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
static bool onGrid (double step) { return std::abs (step * 6.0 - std::round (step * 6.0)) < 1e-6 || std::abs (step * 4.0 - std::round (step * 4.0)) < 1e-6; }

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
        // any sound can be dropped: one without attacks is cut into equal pieces, and plays
        sh::Kit pad; pad.sampleRate = 44100.0;
        for (int i = 0; i < 44100 * 6; ++i) pad.x.push_back (0.3f * (float) (std::sin (0.031 * i) + 0.6 * std::sin (0.047 * i)));
        pad.analyse();
        CHECK (pad.slices.size() == 16);
        Run r (&pad, 8.0); r.p.mutate = 0.5f;
        r.e.noteOn (kBreak, 0.8f, r.p); r.steps (32);
        CHECK (peakOf (r.out) > 0.05f && peakOf (r.out) <= 1.0f);
    }

    const double secPerStep = 60.0 / 90.0 / 4.0;
    // ---- the key map ----
    {
        int expect[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
        bool ok = true;
        for (int oct = 0; oct < 9; ++oct) for (int i = 0; i < 12; ++i) ok = ok && sh::keyOfNote (oct * 12 + i) == expect[i];
        CHECK (ok);
        CHECK (sh::keyOfNote (kLoop) == sh::KeyLoop && sh::keyOfNote (kBreak) == sh::KeyBreak && sh::keyOfNote (kRollUp) == sh::KeyRollUp && sh::keyOfNote (kRollDown) == sh::KeyRollDown
               && sh::keyOfNote (kReverse) == sh::KeyReverse && sh::keyOfNote (kStop) == sh::KeyStop && sh::keyOfNote (kDrop) == sh::KeyDrop);
    }
    // ---- LOOP: the loop untouched, whatever the knobs say ----
    std::vector<float> loopAudio;
    {
        Run r (&kit);
        r.p.mutate = 1.f; r.p.ramp = 1.f;
        r.e.noteOn (kLoop, 0.8f, r.p);
        r.steps (32);
        auto t = attacks (r.out);
        int hit = 0;
        for (const auto& th : truth) for (double a : t) if (std::abs (a - th.time) < 0.012) { ++hit; break; }
        std::printf ("LOOP with Mutate at the maximum: %zu events, peak %.2f, %d of %zu hits at their place\n", r.log.size(), peakOf (r.out), hit, truth.size());
        CHECK (peakOf (r.out) > 0.3f && peakOf (r.out) < 1.f);
        CHECK (r.log.size() == truth.size() && hit >= (int) truth.size() - 1);
        for (const auto& l : r.log) CHECK (l.kind == 0 && ! l.rev && l.r0 == 1.f && l.r1 == 1.f);
        loopAudio = r.out;
        Run z (&kit); z.p.mutate = 0.f; z.e.noteOn (kLoop, 0.8f, z.p); z.steps (32);
        CHECK (sameAudio (z.out, loopAudio));
    }
    // ---- it follows the tempo, and keeps the pitch ----
    {
        Run r (&kit); r.bpm = 120.0;
        r.e.noteOn (kLoop, 0.8f, r.p);
        r.go (2 * 16 * 60.0 / 120.0 / 4.0);
        auto t = attacks (r.out);
        int hit = 0;
        for (const auto& th : truth) for (double a : t) if (std::abs (a - th.time * 90.0 / 120.0) < 0.012) { ++hit; break; }
        std::printf ("at 120 bpm: %d of %zu hits at their scaled place\n", hit, truth.size());
        CHECK (hit >= (int) truth.size() - 2);
        auto zc = [&] (const std::vector<float>& v) { int c = 0; for (size_t i = 100; i + 1 < (size_t) (0.05 * kSr); ++i) if ((v[i] < 0.f) != (v[i + 1] < 0.f)) ++c; return c; };
        const int a = zc (loopAudio), b = zc (r.out);
        CHECK (a > 3 && std::abs (a - b) <= std::max (2, a / 8));
    }
    // ---- BREAK: always on the grid ----
    {
        int bad = 0, total = 0; std::set<int> kinds;
        for (int seed = 1; seed <= 12; ++seed)
        {
            Run r (&kit); r.p.mutate = 1.f; r.p.groove = 0.f; r.p.seed = seed; r.p.ramp = (seed % 2) ? 1.f : 0.3f;
            r.e.noteOn (kBreak, 0.9f, r.p);
            r.steps (16 * 16);
            for (const auto& l : r.log) { ++total; kinds.insert (l.kind); if (! onGrid (l.step)) ++bad; }
        }
        std::printf ("BREAK on the grid: %d events over 12 runs of 16 bars, %d off the grid (1/64 notes or 1/48 triplets); kinds seen: %zu\n", total, bad, kinds.size());
        CHECK (total > 500 && bad == 0);
        CHECK (kinds.size() == 4);          // loop hits, rolls, ghosts, fills
    }
    // ---- written, not random ----
    {
        auto go = [&] (Run& r, int seed, int note)
        {
            r.p.mutate = 0.8f; r.p.seed = seed;
            r.e.noteOn (note, 0.8f, r.p);
            r.steps (8 * 16);
        };
        Run a (&kit), b (&kit), c (&kit), d (&kit);
        go (a, 1, kBreak); go (b, 1, kBreak); go (c, 2, kBreak); go (d, 1, kLoop);
        CHECK (sameAudio (a.out, b.out));
        CHECK (! sameAudio (a.out, c.out));
        CHECK (! sameAudio (a.out, d.out));
        auto bar = [&] (long n) { std::vector<std::pair<long, int>> v; for (const auto& l : a.log) if (std::floor (l.step / 16.0) == (double) n) v.push_back ({ std::lround ((l.step - 16.0 * (double) n) * 4.0), l.slice }); return v; };
        CHECK (bar (0) != bar (2) || bar (1) != bar (3));       // each bar is different from the last, though the loop is only two bars long
    }
    // ---- from a whisper to another loop ----
    {
        std::set<std::tuple<long, int, bool, long>> base;
        { Run r (&kit); r.p.groove = 0.f; r.e.noteOn (kLoop, 0.8f, r.p); r.steps (32); for (const auto& l : r.log) base.insert ({ std::lround (l.step * 4.0), l.slice, l.rev, std::lround (l.r0 * 100) }); }
        auto changed = [&] (float mutate)
        {
            double sum = 0.0;
            for (int seed = 1; seed <= 12; ++seed)
            {
                Run r (&kit); r.p.mutate = mutate; r.p.seed = seed; r.p.groove = 0.f;
                r.e.noteOn (kBreak, 0.8f, r.p); r.steps (8 * 16);
                int ch = 0, n = 0;
                for (const auto& l : r.log) { ++n; if (! base.count ({ std::lround (l.step * 4.0) % 128, l.slice, l.rev, std::lround (l.r0 * 100) })) ++ch; }
                sum += (double) ch / std::max (1, n);
            }
            return sum / 12.0;
        };
        const double z = changed (0.f), a = changed (0.1f), b = changed (0.3f), c = changed (0.6f), d = changed (1.f);
        std::printf ("share of the hits that are not in the original loop: Mutate 0 -> %.0f %%, 0.1 -> %.0f %%, 0.3 -> %.0f %%, 0.6 -> %.0f %%, 1.0 -> %.0f %%\n", z * 100, a * 100, b * 100, c * 100, d * 100);
        CHECK (z == 0.0);
        CHECK (a > 0.0 && a < 0.12);         // a whisper: a few things change
        CHECK (a < b && b < c && c < d);
        CHECK (d > 0.5);                     // the top of the knob: another loop
    }
    // ---- the rolls of BREAK: a stutter-edit ramp ----
    {
        int up = 0, down = 0, okUp = 0, okDown = 0;
        for (int seed = 1; seed <= 20; ++seed)
        {
            Run r (&kit); r.p.mutate = 1.f; r.p.seed = seed; r.p.ramp = 1.f; r.p.groove = 0.f;
            r.e.noteOn (kBreak, 0.9f, r.p); r.steps (8 * 16);
            std::map<int, std::vector<sh::EventLog>> groups;
            for (const auto& l : r.log) if (l.kind == 1 && l.grp > 0) groups[l.grp].push_back (l);
            for (auto& g : groups)
            {
                const auto& s = g.second;
                if (s.size() < 4) continue;
                const bool isUp = s.back().step - s[s.size() - 2].step < s[1].step - s[0].step;
                bool gapsOk = true, pitchOk = true;
                for (size_t i = 2; i < s.size(); ++i)
                {
                    const double g1 = s[i].step - s[i - 1].step, g0 = s[i - 1].step - s[i - 2].step;
                    if (isUp ? g1 > g0 + 1e-6 : g1 < g0 - 1e-6) gapsOk = false;
                }
                for (size_t i = 2; i < s.size(); ++i) if (isUp ? s[i].r0 < s[i - 1].r0 - 1e-5f : s[i].r0 > s[i - 1].r0 + 1e-5f) pitchOk = false;
                const bool velOk = isUp ? s.back().vel > s.front().vel : s.back().vel < s.front().vel;
                const bool ok = gapsOk && pitchOk && velOk;
                if (isUp) { ++up; if (ok) ++okUp; } else { ++down; if (ok) ++okDown; }
            }
        }
        std::printf ("rolls inside BREAK: %d climbing (%d speed up, rise and swell), %d falling (%d slow down, sag and fade)\n", up, okUp, down, okDown);
        CHECK (up >= 8 && okUp >= up * 9 / 10);
        CHECK (down >= 4 && okDown >= down * 9 / 10);
    }
    // ---- several keys, several loops ----
    {
        Run one (&kit); one.p.mutate = 0.6f;
        one.e.noteOn (kLoop, 0.8f, one.p); one.steps (8 * 16);
        Run many (&kit); many.p.mutate = 0.6f;
        for (int n : { kLoop, kBreak, kLoop + 12, kBreak + 12 }) many.e.noteOn (n, 0.8f, many.p);
        many.steps (8 * 16);
        std::set<int> notes; for (const auto& l : many.log) notes.insert (l.note);
        std::printf ("4 keys held: voices %d, keys heard %zu, peak %.2f (1 key: %.2f)\n", many.e.activeVoices(), notes.size(), peakOf (many.out), peakOf (one.out));
        CHECK (many.e.activeVoices() == 4 && notes.size() == 4);
        CHECK (peakOf (many.out) < 1.0f);
        many.e.allNotesOff();
        const size_t before = many.log.size();
        many.steps (12);
        CHECK (many.log.size() - before <= 4 && many.e.activeVoices() == 0);
        many.steps (10);
        CHECK (many.e.activeHits() == 0);
    }

    // ---- the five gestures ----
    // A Loop key is held; a gesture is played on top of it. For each: it takes the floor (no loop hit inside its window), stays on the grid,
    // lasts as long as the key is held, and gives the loop back.
    struct G { int note; const char* name; double nominal; };
    const G gestures[5] = { { kRollUp, "Roll up", 8 }, { kRollDown, "Roll down", 8 }, { kReverse, "Reverse", 4 }, { kStop, "Stop", 6 }, { kDrop, "Drop", 4 } };
    for (const auto& g : gestures)
    {
        for (int mode = 0; mode < 2; ++mode)               // 0: a tap, 1: held for 20 steps
        {
            Run r (&kit); r.p.mutate = 0.f; r.p.quant = 3; r.p.groove = 0.f;
            r.steps (0.1);
            r.e.noteOn (kLoop, 0.8f, r.p);
            r.steps (3 * 16 + 0.05);
            r.log.clear();
            r.e.noteOn (g.note, 0.9f, r.p);
            const double pressed = r.step(), start = std::ceil (pressed - 1e-9);
            double held = mode == 0 ? 0.0 : 20.0;
            r.steps (held);
            r.e.noteOff (g.note);
            const double released = r.step();
            r.steps (g.nominal + 10.0);
            const double end = mode == 0 ? start + g.nominal : std::max (start + g.nominal, std::ceil (released - 1e-9));
            int nG = 0, loopInside = 0; bool grid = true; double firstG = 1e9, lastG = -1e9; int loopAfter = 0;
            double accentAt = -1.0;
            for (const auto& l : r.log)
            {
                if (l.note == g.note)
                {
                    ++nG; firstG = std::min (firstG, l.step); lastG = std::max (lastG, l.step);
                    if (! onGrid (l.step)) grid = false;
                    if (g.note == kDrop) accentAt = l.step;
                }
                else if (l.step >= start + 1e-9 && l.step < end - 1e-9) ++loopInside;
                else if (l.step >= end - 1e-9) ++loopAfter;
            }
            std::printf ("%-9s %s: %3d events, from step %.1f to %.1f (window %.0f to %.0f), %d loop hits inside, %d after\n", g.name, mode ? "held " : "tapped", nG, firstG - start, lastG - start, 0.0, end - start, loopInside, loopAfter);
            CHECK (grid && loopInside == 0 && loopAfter > 0);
            CHECK (peakOf (r.out) <= 1.0f);
            if (g.note == kDrop) CHECK (nG == 1 && std::abs (accentAt - end) < 0.51);        // one hard hit, as the key is let go
            else CHECK (nG >= 1 && firstG >= start - 1e-6 && lastG <= end + 1e-6);
            if (mode == 1 && g.note != kDrop && g.note != kStop) CHECK (lastG > start + g.nominal + 4.0);   // it went on beyond its first block
            if (g.note == kDrop && mode == 1) CHECK (accentAt >= start + 20.0 - 0.01);
        }
    }
    {   // what each one is
        auto run = [&] (int note, double held)
        {
            auto r = std::make_unique<Run> (&kit); r->p.mutate = 0.f; r->p.quant = 3; r->p.groove = 0.f; r->p.ramp = 1.f;
            r->steps (0.1); r->e.noteOn (kLoop, 0.8f, r->p); r->steps (3 * 16 + 0.05); r->log.clear();
            r->e.noteOn (note, 0.9f, r->p); r->steps (held); r->e.noteOff (note); r->steps (14);
            std::vector<sh::EventLog> v; for (const auto& l : r->log) if (l.note == note) v.push_back (l);
            return v;
        };
        {   auto v = run (kRollUp, 0.0);     // climbing: faster, higher, louder
            bool gaps = v.size() >= 8, pitch = true;
            for (size_t i = 2; i < v.size(); ++i) { gaps = gaps && v[i].step - v[i - 1].step <= v[i - 1].step - v[i - 2].step + 1e-6; pitch = pitch && v[i].r0 >= v[i - 1].r0 - 1e-5f; }
            std::printf ("Roll up: %zu repeats, pitch from %.2f to %.2f, level from %.2f to %.2f\n", v.size(), v.front().r0, v.back().r0, v.front().vel, v.back().vel);
            CHECK (gaps && pitch && v.back().vel > v.front().vel * 2.f && v.back().r0 > 1.5f);
            auto h = run (kRollUp, 20.0);    // held: it stays at full speed, high
            CHECK (h.size() > v.size() + 20);
            CHECK (std::abs ((h.back().step - h[h.size() - 2].step) - 0.25) < 1e-6 && h.back().r0 > 1.5f);
        }
        {   auto v = run (kRollDown, 0.0);   // stretching: slower, lower, quieter
            bool gaps = v.size() >= 8, pitch = true;
            for (size_t i = 2; i < v.size(); ++i) { gaps = gaps && v[i].step - v[i - 1].step >= v[i - 1].step - v[i - 2].step - 1e-6; pitch = pitch && v[i].r0 <= v[i - 1].r0 + 1e-5f; }
            std::printf ("Roll down: %zu repeats, pitch from %.2f to %.2f, level from %.2f to %.2f, last gap %.2f steps\n", v.size(), v.front().r0, v.back().r0, v.front().vel, v.back().vel, v.back().step - v[v.size() - 2].step);
            CHECK (gaps && pitch && v.back().vel < v.front().vel * 0.5f && v.back().r0 < 0.8f);
            CHECK (v.back().step - v[v.size() - 2].step >= 1.0);                  // the last repeats are drawn out
        }
        {   auto v = run (kReverse, 0.0); bool allRev = ! v.empty(); for (const auto& l : v) allRev = allRev && l.rev; CHECK (allRev); }
        {   auto v = run (kStop, 0.0); bool falling = v.size() >= 2; for (size_t i = 1; i < v.size(); ++i) falling = falling && v[i].r0 < v[i - 1].r0 + 1e-6f; CHECK (falling && v.back().r0 < 0.7f); }
        {   // Drop is silent while it is held, and the hit that ends it is hard
            auto measure = [&] (bool drop)
            {
                Run r (&kit); r.p.mutate = 0.f; r.p.quant = 3; r.steps (0.1);
                r.e.noteOn (kLoop, 0.8f, r.p); r.steps (3 * 16 + 0.05);
                const size_t n0 = r.out.size();
                if (drop) r.e.noteOn (kDrop, 0.9f, r.p);
                r.steps (12);
                return rms (r.out, (double) n0 / kSr + 2.0 * secPerStep, (double) n0 / kSr + 11.0 * secPerStep);
            };
            const double a = measure (true), b = measure (false);
            std::printf ("Drop held: level %.4f against %.4f for the loop\n", a, b);
            CHECK (a < 0.05 * b + 1e-9);
        }
    }
    // ---- a key starts on the grid ----
    {
        Run r (&kit); r.p.mutate = 0.f; r.p.quant = 3; r.beat = 0.3;           // 1.2 steps in
        r.steps (0.1);
        r.e.noteOn (kLoop, 0.8f, r.p);
        r.steps (16);
        double first = 1e9; for (const auto& l : r.log) first = std::min (first, l.step);
        CHECK (first >= 2.0 - 1e-6);
    }
    // ---- the transport ----
    {
        Run r (&kit); r.p.mutate = 0.5f;
        r.e.noteOn (kBreak, 0.8f, r.p);
        r.go (1.0);
        const size_t n = r.out.size();
        r.go (1.0, false);
        CHECK (rms (r.out, (double) n / kSr, (double) r.out.size() / kSr) == 0.0);
        CHECK (r.e.activeVoices() == 0 && ! r.e.gestureActive());
    }
    // ---- hard use ----
    {
        gg::Rng g (9);
        Run r (&kit);
        float pk = 0.f; bool finite = true;
        for (int i = 0; i < 500; ++i)
        {
            r.p.mutate = g.uni(); r.p.ramp = g.uni(); r.p.groove = g.uni(); r.p.tune = g.bi() * 12.f; r.p.quant = (int) (g.uni() * 4.f); r.p.seed = 1 + i % 50;
            r.bpm = 60.0 + 120.0 * g.uni();
            const int note = 36 + (int) (g.uni() * 60.f);
            if (g.uni() < 0.55f) r.e.noteOn (note, g.uni(), r.p); else r.e.noteOff (note);
            const size_t n = r.out.size();
            r.go (0.12);
            std::vector<float> chunk (r.out.begin() + (long) n, r.out.end());
            pk = std::max (pk, peakOf (chunk)); finite = finite && peakOf (chunk) < 1e8f;
        }
        std::printf ("500 random key presses: peak %.3f, voices %d, hits ringing %d\n", pk, r.e.activeVoices(), r.e.activeHits());
        CHECK (finite && pk <= 1.0f);
    }
    std::printf (failures ? "%d FAILURES\n" : "ALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
