// JUCE-free tests for the break cutter and the player. Build: g++ -std=c++17 -O2 -fsanitize=address,undefined tests/dsp_test.cpp
#include "../Source/Dsp/Engine.h"
#include <chrono>
#include <cstdio>
#include <set>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("FAIL: %s (line %d)\n", #c, __LINE__); ++failures; } } while (0)

static const double kSr = 48000.0;

static std::vector<float> render (sh::Engine& e, double seconds)
{
    std::vector<float> out, L (256), R (256);
    const int total = (int) (seconds * kSr);
    for (int done = 0; done < total; done += 256)
    {
        const int n = std::min (256, total - done);
        e.process (n, L.data(), R.data());
        for (int i = 0; i < n; ++i) out.push_back (0.5f * (L[(size_t) i] + R[(size_t) i]));
    }
    return out;
}
static float peakOf (const std::vector<float>& v, size_t from = 0)
{
    float p = 0.f; for (size_t i = from; i < v.size(); ++i) { if (! std::isfinite (v[i])) return 1e9f; p = std::max (p, std::abs (v[i])); }
    return p;
}
static double rms (const std::vector<float>& v, double t0, double t1)
{
    const size_t a = (size_t) (t0 * kSr), b = std::min (v.size(), (size_t) (t1 * kSr));
    double s = 0.0; for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i];
    return b > a ? std::sqrt (s / (double) (b - a)) : 0.0;
}
static int zeroCrossings (const std::vector<float>& v, double t0, double t1)
{
    int c = 0;
    for (size_t i = (size_t) (t0 * kSr); i + 1 < std::min (v.size(), (size_t) (t1 * kSr)); ++i) if ((v[i] < 0.f) != (v[i + 1] < 0.f)) ++c;
    return c;
}
// the times at which the rendered sound has an attack (the cutter listens to its own output)
static std::vector<double> attacks (const std::vector<float>& v)
{
    sh::Kit k; k.sampleRate = kSr; k.x = v; k.analyse();
    std::vector<double> t; for (const auto& s : k.slices) t.push_back ((double) s.start / kSr);
    return t;
}
// a kit with one slice: a 440 Hz tone, to measure pitch and timing exactly
static sh::Kit toneKit()
{
    sh::Kit k; k.sampleRate = kSr;
    k.x.resize ((size_t) kSr);
    for (size_t i = 0; i < k.x.size(); ++i) k.x[i] = (float) (0.7 * std::sin (6.283185307179586 * 440.0 * (double) i / kSr));
    sh::Slice s; s.start = 0; s.length = (int) (0.6 * kSr); s.level = 1.f; s.bright = 0.5f; s.x = 0.5f; s.y = 0.5f;
    k.slices.push_back (s);
    return k;
}

int main()
{
    // ---- the cutter ----
    std::vector<sh::TruthHit> truth;
    sh::Kit kit = sh::makeDefaultKit (44100.0, &truth);
    {
        const auto t0 = std::chrono::steady_clock::now();
        kit.analyse();
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("default break: %.1f s, %zu real hits, %zu pieces found in %.0f ms\n", (double) kit.x.size() / kit.sampleRate, truth.size(), kit.slices.size(), ms);
        int found = 0; double mean[4] = {}; int cnt[4] = {};
        for (const auto& t : truth)
        {
            int best = -1; double bd = 1e9;
            for (int i = 0; i < (int) kit.slices.size(); ++i) { const double d = std::abs ((double) kit.slices[(size_t) i].start / kit.sampleRate - t.time); if (d < bd) { bd = d; best = i; } }
            if (bd < 0.012) { ++found; mean[t.kind] += kit.slices[(size_t) best].bright; cnt[t.kind]++; }
        }
        std::printf ("  found %d of %zu hits; brightness: kicks %.2f, snares %.2f, hats %.2f\n", found, truth.size(), mean[0] / cnt[0], mean[1] / cnt[1], mean[2] / cnt[2]);
        CHECK ((double) found >= 0.85 * (double) truth.size());                      // nearly every hit is found
        CHECK (kit.slices.size() <= truth.size() + 3);                               // and nothing is invented
        CHECK (mean[0] / cnt[0] < mean[1] / cnt[1] && mean[1] / cnt[1] < mean[2] / cnt[2]);   // dark < middle < bright
        bool ok = true;
        for (size_t i = 0; i < kit.slices.size(); ++i)
        {
            const auto& s = kit.slices[i];
            ok = ok && s.start >= 0 && s.length >= 1000 && s.start + s.length <= (int) kit.x.size() && s.y >= 0.f && s.y <= 1.f && s.level > 0.f && s.level <= 1.f;
            if (i > 0) ok = ok && s.start > kit.slices[i - 1].start;
        }
        CHECK (ok);
        // y is a rank: the map is evenly filled
        std::vector<float> ys; for (const auto& s : kit.slices) ys.push_back (s.y);
        std::sort (ys.begin(), ys.end());
        CHECK (ys.front() == 0.f && ys.back() == 1.f);
    }
    {   // nothing to cut
        sh::Kit s; s.sampleRate = 44100.0; s.x.assign (44100 * 3, 0.f); s.analyse();
        CHECK (s.slices.empty());
        sh::Kit t; t.sampleRate = 44100.0; for (int i = 0; i < 44100 * 3; ++i) t.x.push_back (0.5f * (float) std::sin (0.05 * i)); t.analyse();
        CHECK (t.slices.size() <= 1);                    // a steady tone has one attack at most
        sh::Kit sh2; sh2.sampleRate = 44100.0; sh2.x.assign (500, 0.3f); sh2.analyse();
        CHECK (sh2.slices.empty());
    }
    {   // the map query: a low key finds a dark hit, a high key a bright one; the place picks which
        const int lo = kit.nearest (0.1f, 0.f), hi = kit.nearest (0.1f, 1.f);
        CHECK (lo >= 0 && hi >= 0 && kit.slices[(size_t) lo].bright < kit.slices[(size_t) hi].bright);
        const int a = kit.nearest (0.05f, 0.1f), b = kit.nearest (0.9f, 0.1f);
        std::printf ("dark hit near the start: slice %d (x %.2f), near the end: slice %d (x %.2f)\n", a, kit.slices[(size_t) a].x, b, kit.slices[(size_t) b].x);
        CHECK (a != b && kit.slices[(size_t) a].x < kit.slices[(size_t) b].x);
    }

    // ---- the player ----
    const sh::Kit* K = &kit;
    sh::Params base; base.shatter = 0.f; base.snap = 1.f;
    {   // a low key is a dark sound, a high key a bright one
        auto zc = [&] (int note)
        {
            sh::Engine e; e.prepare (kSr); e.setKit (K);
            e.noteOn (note, 0.9f, base, 0.0);
            auto o = render (e, 0.4);
            const float pk = peakOf (o);
            CHECK (pk > 0.05f && pk < 1.f);
            return zeroCrossings (o, 0.0, 0.1) / 0.1;
        };
        const double lo = zc (36), hi = zc (84);
        std::printf ("zero crossings per second: key 36 -> %.0f, key 84 -> %.0f\n", lo, hi);
        CHECK (hi > lo * 3.0);
    }
    {   // without shatter: one hit, one attack
        sh::Engine e; e.prepare (kSr); e.setKit (K);
        e.noteOn (84, 0.9f, base, 0.0);
        auto o = render (e, 1.0);
        CHECK (attacks (o).size() == 1);
    }
    {   // tune: +12 semitones doubles the pitch (a pure tone, so it can be measured)
        auto tk = toneKit();
        auto z = [&] (float tune)
        {
            sh::Engine e; e.prepare (kSr); e.setKit (&tk);
            sh::Params p = base; p.tune = tune;
            e.noteOn (60, 0.9f, p, 0.0);
            return zeroCrossings (render (e, 0.3), 0.02, 0.2);
        };
        const int a = z (0.f), b = z (12.f);
        std::printf ("tone: %d crossings, tuned up an octave: %d\n", a, b);
        CHECK (a > 100 && std::abs ((double) b / a - 2.0) < 0.05);
    }
    {   // snap: a short length cuts the hit
        auto level = [&] (float snap)
        {
            sh::Engine e; e.prepare (kSr); e.setKit (K);
            sh::Params p = base; p.snap = snap;
            e.noteOn (36, 0.9f, p, 0.0);
            return rms (render (e, 0.6), 0.12, 0.3);
        };
        const double shortHit = level (0.f), longHit = level (1.f);
        std::printf ("level 120-300 ms after a kick: snap 0 -> %.4f, snap 1 -> %.4f\n", shortHit, longHit);
        CHECK (shortHit < 1e-4 && longHit > 5.0 * shortHit);
    }
    {   // shatter, locked to the tempo: 1/16 notes at 120 bpm = a fragment every 125 ms
        sh::Params p = base; p.shatter = 1.f; p.grid = 2; p.ramp = 0.f; p.scatter = 0.f; p.bpm = 120.0;
        sh::Engine e; e.prepare (kSr); e.setKit (K);
        e.noteOn (84, 0.9f, p, 0.0);
        auto o = render (e, 2.5);
        auto t = attacks (o);
        std::printf ("shattered hit: %zu attacks at", t.size());
        for (double x : t) std::printf (" %.3f", x);
        std::printf ("\n");
        CHECK (t.size() >= 2);
        bool onGrid = true;
        for (size_t i = 1; i < t.size(); ++i) onGrid = onGrid && std::abs ((t[i] - t[i - 1]) - 0.125) < 0.012;
        CHECK (onGrid);
        // another tempo: the burst follows
        sh::Params q = p; q.bpm = 90.0;
        sh::Engine e2; e2.prepare (kSr); e2.setKit (K);
        e2.noteOn (84, 0.9f, q, 0.0);
        auto t2 = attacks (render (e2, 3.0));
        CHECK (t2.size() >= 2 && std::abs ((t2[1] - t2[0]) - 60.0 / 90.0 / 4.0 * 1.0) < 0.012);
    }
    {   // ramp: a positive ramp speeds the burst up, a negative one slows it down
        auto gaps = [&] (float ramp)
        {
            sh::Params p = base; p.shatter = 1.f; p.grid = 2; p.ramp = ramp; p.scatter = 0.f;
            sh::Engine e; e.prepare (kSr); e.setKit (K);
            e.noteOn (84, 0.9f, p, 0.0);
            auto t = attacks (render (e, 4.0));
            std::vector<double> g; for (size_t i = 1; i < t.size(); ++i) g.push_back (t[i] - t[i - 1]);
            return g;
        };
        const auto fast = gaps (1.f), slow = gaps (-1.f);
        std::printf ("ramp +1: %zu gaps, first %.3f last %.3f;  ramp -1: %zu gaps, first %.3f last %.3f\n", fast.size(), fast.empty() ? 0 : fast.front(), fast.empty() ? 0 : fast.back(), slow.size(), slow.empty() ? 0 : slow.front(), slow.empty() ? 0 : slow.back());
        CHECK (fast.size() >= 1 && slow.size() >= 1);
        if (fast.size() >= 2) CHECK (fast.back() < fast.front());
        if (slow.size() >= 2) CHECK (slow.back() > slow.front());
    }
    {   // fall: the burst falls by an octave (a pure tone again)
        auto tk = toneKit();
        sh::Params p = base; p.shatter = 1.f; p.grid = 2; p.ramp = 0.f; p.scatter = 0.f; p.fall = -1.f;
        sh::Engine e; e.prepare (kSr); e.setKit (&tk);
        e.noteOn (84, 0.9f, p, 0.0);
        auto o = render (e, 2.5);
        const int n = e.hitCount();
        const double lastAt = (n - 1) * 0.125;
        const int first = zeroCrossings (o, 0.02, 0.07), last = zeroCrossings (o, lastAt + 0.02, lastAt + 0.07);
        std::printf ("fall -1 over %d fragments: crossings in 50 ms: first %d, last %d\n", n, first, last);
        CHECK (n >= 2 && first > 10 && std::abs ((double) last / first - 0.5) < 0.12);
    }
    {   // the edit is written, not random: same note, same place, same seed = the same sound; another seed = another edit
        auto go = [&] (int seed, double beat)
        {
            sh::Params p = base; p.shatter = 1.f; p.scatter = 0.7f; p.seed = seed;
            sh::Engine e; e.prepare (kSr); e.setKit (K);
            e.noteOn (84, 0.9f, p, beat);
            return render (e, 2.0);
        };
        const auto a = go (1, 4.0), b = go (1, 4.0), c = go (2, 4.0), d = go (1, 4.25);
        auto same = [&] (const std::vector<float>& u, const std::vector<float>& v) { for (size_t i = 0; i < u.size(); ++i) if (u[i] != v[i]) return false; return true; };
        CHECK (same (a, b));
        CHECK (! same (a, c));
        CHECK (! same (a, d));
    }
    {   // kicks stay whole more often than hats
        int kickBreaks = 0, hatBreaks = 0;
        for (int i = 0; i < 200; ++i)
        {
            sh::Params p = base; p.shatter = 0.5f; p.seed = 7;
            for (int pass = 0; pass < 2; ++pass)
            {
                sh::Engine e; e.prepare (kSr); e.setKit (K);
                e.noteOn (pass == 0 ? 36 : 84, 0.9f, p, (double) i * 0.25);
                render (e, 1.5);
                if (e.hitCount() > 1) (pass == 0 ? kickBreaks : hatBreaks)++;
            }
        }
        std::printf ("hits that broke in 200 tries: low key %d, high key %d\n", kickBreaks, hatBreaks);
        CHECK (hatBreaks > kickBreaks * 2);
    }
    {   // the same key again cuts the previous burst
        sh::Params p = base; p.shatter = 1.f; p.grid = 0; p.ramp = 0.f;     // 1/8 notes: slow, so the cut is clear
        sh::Engine e; e.prepare (kSr); e.setKit (K);
        e.noteOn (84, 0.9f, p, 0.0);
        render (e, 0.1);
        const int before = e.hitCount();
        sh::Params q = p; q.shatter = 0.f;
        e.noteOn (84, 0.9f, q, 1.0);
        render (e, 3.0);
        std::printf ("hits: %d at the first key, %d in total after the same key again\n", before, e.hitCount());
        CHECK (e.hitCount() == before + 1);
    }
    {   // hard use: many keys, extreme settings, all the time
        sh::Engine e; e.prepare (kSr); e.setKit (K);
        gg::Rng g (5);
        float pk = 0.f;
        for (int i = 0; i < 300; ++i)
        {
            sh::Params p; p.shatter = 1.f; p.grid = (int) (g.uni() * 7.f); p.ramp = g.bi(); p.scatter = 1.f; p.fall = g.bi(); p.snap = g.uni(); p.tune = g.bi() * 12.f;
            p.place = g.uni(); p.seed = i; p.bpm = 60.0 + 120.0 * g.uni();
            e.noteOn (24 + (int) (g.uni() * 80.f), g.uni(), p, (double) i * 0.25);
            auto o = render (e, 0.06);
            pk = std::max (pk, peakOf (o));
        }
        auto o = render (e, 3.0);
        std::printf ("300 keys, extreme settings: peak %.3f, voices left %d\n", pk, e.activeVoices());
        CHECK (pk <= 1.0f && peakOf (o) < 1.f && e.activeVoices() == 0);
    }
    std::printf (failures ? "%d FAILURES\n" : "ALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
