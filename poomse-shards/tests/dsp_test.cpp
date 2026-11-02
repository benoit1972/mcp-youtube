// JUCE-free tests for the break cutter and the loop player. Build: g++ -std=c++17 -O2 -fsanitize=address,undefined tests/dsp_test.cpp
#include "../Source/Dsp/Engine.h"
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <tuple>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("FAIL: %s (line %d)\n", #c, __LINE__); ++failures; } } while (0)

static const double kSr = 48000.0;
// the playback pads: the white keys from C3 (MIDI 60); the fills: the white keys from C1 (MIDI 36)
static const int kLoop = 60, kBreak = 62, kRatchet = 64, kFlip = 65, kSwap = 67, kShove = 69, kGhost = 71;
static const int fRollUp = 36, fRollDown = 38, fStutter = 40, fReverse = 41, fStop = 43, fHats = 45, fDrop = 47;

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
        p.quant = 0; p.groove = 1.f; p.latch = false;
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
        for (int oct = 0; oct < 9; ++oct) for (int i = 0; i < 12; ++i) ok = ok && sh::whiteIndex (oct * 12 + i) == expect[i];
        CHECK (ok);
        CHECK (sh::isFillNote (36) && sh::isFillNote (47) && ! sh::isFillNote (48) && ! sh::isFillNote (60));
        CHECK (sh::whiteIndex (kLoop) == sh::PlayOriginal && sh::whiteIndex (kBreak) == sh::PlayBreak && sh::whiteIndex (kRatchet) == sh::PlayRatchet && sh::whiteIndex (kFlip) == sh::PlayFlip
               && sh::whiteIndex (kSwap) == sh::PlaySwap && sh::whiteIndex (kShove) == sh::PlayShove && sh::whiteIndex (kGhost) == sh::PlayGhost);
        CHECK (sh::whiteIndex (fRollUp) == sh::FillRollUp && sh::whiteIndex (fRollDown) == sh::FillRollDown && sh::whiteIndex (fStutter) == sh::FillStutter && sh::whiteIndex (fReverse) == sh::FillReverse
               && sh::whiteIndex (fStop) == sh::FillStop && sh::whiteIndex (fHats) == sh::FillHats && sh::whiteIndex (fDrop) == sh::FillDrop);
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

    // ---- the pads of the playback zone: each one a clear character ----
    {
        auto run = [&] (int note, float mutate)
        {
            auto r = std::make_unique<Run> (&kit); r->p.mutate = mutate; r->p.groove = 0.f;
            r->e.noteOn (note, 0.95f, r->p); r->steps (8 * 16);
            return r;
        };
        // Flip: beats 2 and 4 backwards, nothing else
        {   auto r = run (kFlip, 1.f); int in = 0, inRev = 0, out = 0, outRev = 0;
            for (const auto& l : r->log) { const double t = std::fmod (l.step, 16.0); const bool b24 = (t >= 4.0 && t < 8.0) || t >= 12.0; (b24 ? in : out)++; if (l.rev) (b24 ? inRev : outRev)++; }
            std::printf ("FLIP: %d of %d hits on beats 2 and 4 are backwards, %d of %d elsewhere\n", inRev, in, outRev, out);
            CHECK (in > 0 && inRev == in && outRev == 0); }
        // Swap: the kicks and the snares trade places
        {   auto base = run (kLoop, 1.f); auto r = run (kSwap, 1.f); int kicksNow = 0, nKick = 0, snaresNow = 0, nSnare = 0;
            for (size_t i = 0; i < r->log.size() && i < base->log.size(); ++i)
            {
                const float y0 = kit.slices[(size_t) base->log[i].slice].y, y1 = kit.slices[(size_t) r->log[i].slice].y;
                if (y0 < 0.2f) { ++nKick; if (y1 > 0.3f && y1 < 0.8f) ++kicksNow; }
                else if (y0 > 0.3f && y0 < 0.8f) { ++nSnare; if (y1 < 0.2f) ++snaresNow; }
            }
            std::printf ("SWAP: %d of %d kicks now sound as snares, %d of %d snares as kicks\n", kicksNow, nKick, snaresNow, nSnare);
            CHECK (nKick > 0 && nSnare > 0 && kicksNow == nKick && snaresNow == nSnare); }
        // Shove: everything an eighth note late
        {   auto base = run (kLoop, 1.f); auto r = run (kShove, 1.f); std::multiset<long> a, b;
            for (const auto& l : base->log) a.insert (std::lround (std::fmod (std::fmod (l.step, 16.0) + 2.0, 16.0) * 4.0));
            for (const auto& l : r->log) b.insert (std::lround (std::fmod (l.step, 16.0) * 4.0));
            CHECK (a == b); }
        // Ratchet: the snares and the hats turn into rolls
        {   auto r = run (kRatchet, 1.f); int rolls = 0, total = 0; for (const auto& l : r->log) { ++total; if (l.kind == 1) ++rolls; }
            std::printf ("RATCHET: %d of %d hits are part of a roll\n", rolls, total);
            CHECK (rolls > total / 2); }
        // Ghost: soft hits in the gaps
        {   auto r = run (kGhost, 1.f); int ghosts = 0, loopHits = 0; for (const auto& l : r->log) { if (l.kind == 2) { ++ghosts; CHECK (l.vel < 0.5f); } else ++loopHits; }
            std::printf ("GHOST: %d ghost hits added to %d hits of the loop\n", ghosts, loopHits);
            CHECK (ghosts >= loopHits / 2 && ghosts >= 40); }
        // Mutate: it says how much of the loop the pad takes hold of
        {   auto lo = run (kFlip, 0.15f), hi = run (kFlip, 1.f); int a = 0, b = 0; for (const auto& l : lo->log) a += l.rev; for (const auto& l : hi->log) b += l.rev;
            std::printf ("FLIP backwards hits: Mutate 0.15 -> %d, Mutate 1 -> %d\n", a, b);
            CHECK (a < b / 2 + 1 && b > 0); }
    }
    // ---- Latch: the pad stays selected ----
    {
        Run r (&kit); r.p.latch = true; r.p.mutate = 0.5f;
        r.e.noteOn (kLoop, 0.8f, r.p); r.steps (16);
        r.e.noteOff (kLoop);                                    // let go: it keeps playing
        const size_t mark = r.log.size(); r.steps (32);
        CHECK (r.log.size() - mark >= 10);
        r.e.noteOn (kFlip, 0.9f, r.p);                          // another pad: the first gives way at once
        const double switchStep = r.step(); r.steps (32);
        bool onlyFlip = true; int flips = 0;
        for (const auto& l : r.log) if (l.step >= switchStep + 1.5) { if (l.note != kFlip) onlyFlip = false; else ++flips; }
        std::printf ("Latch: after another pad is chosen, %d hits, all from the new pad: %s\n", flips, onlyFlip ? "yes" : "NO");
        CHECK (onlyFlip && flips >= 10);
        Run q (&kit); q.p.latch = false; q.e.noteOn (kLoop, 0.8f, q.p); q.steps (16); q.e.noteOff (kLoop); q.steps (2);
        const size_t m2 = q.log.size(); q.steps (32);
        CHECK (q.log.size() == m2);                             // without Latch it stops when the key does
    }
    // ---- the fills: they last exactly as long as the key is down ----
    struct F { int note; const char* name; };
    const F fills[7] = { { fRollUp, "Roll up" }, { fRollDown, "Roll down" }, { fStutter, "Stutter" }, { fReverse, "Reverse" }, { fStop, "Stop" }, { fHats, "Hats" }, { fDrop, "Drop" } };
    for (const auto& f : fills)
        for (double held : { 1.5, 6.0, 20.0 })
        {
            Run r (&kit); r.p.mutate = 0.f; r.p.quant = 3; r.p.groove = 0.f; r.p.latch = true;
            r.steps (0.1);
            r.e.noteOn (kLoop, 0.8f, r.p);
            r.steps (3 * 16 + 0.05);
            r.log.clear();
            r.e.noteOn (f.note, 0.9f, r.p);
            const double start = std::ceil (r.step() - 1e-9);
            r.steps (held);
            r.e.noteOff (f.note);
            const double released = r.step();
            r.steps (24);
            int nF = 0, loopInside = 0, loopAfter = 0; bool grid = true; double lastF = -1.0, firstLoopAfter = 1e9;
            for (const auto& l : r.log)
            {
                if (l.note == f.note) { ++nF; lastF = std::max (lastF, l.step); if (! onGrid (l.step)) grid = false; }
                else if (l.step >= start + 1e-9 && l.step < released - 0.02) ++loopInside;
                else if (l.step >= released - 0.02) { ++loopAfter; firstLoopAfter = std::min (firstLoopAfter, l.step); }
            }
            std::printf ("%-9s held %4.1f steps: %3d events, last at +%.2f after the key went up, %d loop hits inside, first loop hit back %.2f after\n", f.name, held, nF, lastF - released, loopInside, firstLoopAfter - released);
            CHECK ((grid || f.note == fDrop) && loopInside == 0 && loopAfter > 0);     // (the hard hit of the Drop falls when the key goes up)
            CHECK (peakOf (r.out) <= 1.0f);
            if (f.note == fDrop) { CHECK (nF == 1 && std::abs (lastF - released) < 0.02); }     // one hard hit, at the very moment the key goes up
            else { CHECK ((nF >= 1 || held < 5.0) && lastF <= released + 1e-6); }                  // nothing after the key is up
            CHECK (firstLoopAfter - released < 8.0);                                               // and the playback is back
            if (held >= 20.0 && f.note != fDrop && f.note != fStop) CHECK (lastF > start + 12.0);   // held: it went on
        }
    {   // a key let go before the fill had even begun cancels it
        Run r (&kit); r.p.mutate = 0.f; r.p.quant = 3; r.beat = 0.3;
        r.steps (0.1);
        r.e.noteOn (fHats, 0.9f, r.p);
        r.e.noteOff (fHats);
        r.steps (16);
        int nF = 0; for (const auto& l : r.log) if (l.note == fHats) ++nF;
        CHECK (nF == 0);
    }
    {   // what each one is
        auto run = [&] (int note, double held)
        {
            auto r = std::make_unique<Run> (&kit); r->p.mutate = 0.f; r->p.quant = 3; r->p.groove = 0.f; r->p.ramp = 1.f; r->p.latch = true;
            r->steps (0.1); r->e.noteOn (kLoop, 0.8f, r->p); r->steps (3 * 16 + 0.05); r->log.clear();
            r->e.noteOn (note, 0.9f, r->p); r->steps (held); r->e.noteOff (note); r->steps (14);
            std::vector<sh::EventLog> v; for (const auto& l : r->log) if (l.note == note) v.push_back (l);
            return v;
        };
        {   auto v = run (fRollUp, 9.0);     // climbing: faster, higher, louder
            bool gaps = v.size() >= 8, pitch = true;
            for (size_t i = 2; i < v.size(); ++i) { gaps = gaps && v[i].step - v[i - 1].step <= v[i - 1].step - v[i - 2].step + 1e-6; pitch = pitch && v[i].r0 >= v[i - 1].r0 - 1e-5f; }
            std::printf ("Roll up: %zu repeats, pitch from %.2f to %.2f, level from %.2f to %.2f\n", v.size(), v.front().r0, v.back().r0, v.front().vel, v.back().vel);
            CHECK (gaps && pitch && v.back().vel > v.front().vel * 2.f && v.back().r0 > 1.5f);
            auto h = run (fRollUp, 20.0);    // held: it stays at full speed, high
            CHECK (h.size() > v.size() + 20);
            CHECK (std::abs ((h.back().step - h[h.size() - 2].step) - 0.25) < 1e-6 && h.back().r0 > 1.5f);
        }
        {   auto v = run (fRollDown, 9.0);   // stretching: slower, lower, quieter
            bool gaps = v.size() >= 8, pitch = true;
            for (size_t i = 2; i < v.size(); ++i) { gaps = gaps && v[i].step - v[i - 1].step >= v[i - 1].step - v[i - 2].step - 1e-6; pitch = pitch && v[i].r0 <= v[i - 1].r0 + 1e-5f; }
            std::printf ("Roll down: %zu repeats, pitch from %.2f to %.2f, level from %.2f to %.2f, last gap %.2f steps\n", v.size(), v.front().r0, v.back().r0, v.front().vel, v.back().vel, v.back().step - v[v.size() - 2].step);
            CHECK (gaps && pitch && v.back().vel < v.front().vel * 0.5f && v.back().r0 < 0.8f);
            CHECK (v.back().step - v[v.size() - 2].step >= 1.0);                  // the last repeats are drawn out
        }
        {   auto v = run (fStutter, 4.0); bool same = v.size() >= 4; for (const auto& l : v) same = same && l.slice == v[0].slice; CHECK (same); }
        {   auto v = run (fReverse, 4.0); bool allRev = ! v.empty(); for (const auto& l : v) allRev = allRev && l.rev; CHECK (allRev); }
        {   auto v = run (fStop, 7.0); bool falling = v.size() >= 2; for (size_t i = 1; i < v.size(); ++i) falling = falling && v[i].r0 < v[i - 1].r0 + 1e-6f; CHECK (falling && v.back().r0 < 0.7f); }
        {   auto v = run (fHats, 4.0); bool hats = v.size() >= 6; for (const auto& l : v) hats = hats && kit.slices[(size_t) l.slice].y > 0.7f; CHECK (hats); }
        {   // Drop is silent while it is held, and the hit that ends it is hard
            auto measure = [&] (bool drop)
            {
                Run r (&kit); r.p.mutate = 0.f; r.p.quant = 3; r.steps (0.1);
                r.e.noteOn (kLoop, 0.8f, r.p); r.steps (3 * 16 + 0.05);
                const size_t n0 = r.out.size();
                if (drop) r.e.noteOn (fDrop, 0.9f, r.p);
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
