// JUCE-free tests for the loop finder and the player. Build: g++ -std=c++17 -O2 -fsanitize=address,undefined tests/dsp_test.cpp
#include "../Source/Dsp/Engine.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <set>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("FAIL: %s (line %d)\n", #c, __LINE__); ++failures; } } while (0)

static lp::Record sineRecord (double freq, double seconds, double sr = 44100.0)
{
    lp::Record r; r.sampleRate = sr;
    r.x.resize ((size_t) (seconds * sr));
    for (size_t i = 0; i < r.x.size(); ++i) r.x[i] = (float) (0.6 * std::sin (6.283185307179586 * freq * (double) i / sr));
    r.analyse();
    return r;
}

struct Rendered { std::vector<float> l, r; };
static Rendered render (lp::Engine& e, const lp::Params& p, double seconds, double sr = 48000.0)
{
    Rendered o;
    const int total = (int) (seconds * sr), block = 256;
    std::vector<float> L ((size_t) block), R ((size_t) block);
    for (int done = 0; done < total; done += block)
    {
        const int n = std::min (block, total - done);
        e.process (n, p, L.data(), R.data());
        o.l.insert (o.l.end(), L.begin(), L.begin() + n);
        o.r.insert (o.r.end(), R.begin(), R.begin() + n);
    }
    return o;
}
static float peakOf (const std::vector<float>& v, size_t from = 0)
{
    float p = 0.f; for (size_t i = from; i < v.size(); ++i) { if (! std::isfinite (v[i])) return 1e9f; p = std::max (p, std::abs (v[i])); }
    return p;
}
static double rmsOf (const std::vector<float>& v, size_t a, size_t b)
{
    double s = 0.0; for (size_t i = a; i < b; ++i) s += (double) v[i] * (double) v[i];
    return std::sqrt (s / (double) std::max<size_t> (1, b - a));
}

int main()
{
    // ---- the loop finder ----
    lp::Record rec = lp::makeDefaultRecord();
    {
        const auto t0 = std::chrono::steady_clock::now();
        rec.analyse();
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        const auto& L = rec.atlas.loops();
        std::printf ("default record: %.1f s, %zu loops found in %.0f ms\n", (double) rec.x.size() / rec.sampleRate, L.size(), ms);
        CHECK (L.size() >= 150);
        bool ok = true; float lo = 1.f;
        for (const auto& f : L)
        {
            ok = ok && f.start >= 0 && f.length > 0 && f.start + f.length < (int) rec.x.size() && f.score >= 0.6f && f.score <= 1.f && f.level >= 0.08f;
            lo = std::min (lo, f.score);
        }
        CHECK (ok);
        std::printf ("  worst score %.2f\n", lo);
        // there are loops of every size
        int bySize[5] = {};
        for (const auto& f : L) bySize[std::clamp ((int) (lp::Atlas::sizeToY ((double) f.length / rec.sampleRate) * 4.99f), 0, 4)]++;
        std::printf ("  loops per size class: %d %d %d %d %d\n", bySize[0], bySize[1], bySize[2], bySize[3], bySize[4]);
        for (int c : bySize) CHECK (c > 0);
    }
    {   // silence, noise, too short
        lp::Record s; s.sampleRate = 44100.0; s.x.assign (44100 * 3, 0.f); s.analyse();
        CHECK (s.atlas.loops().empty());
        lp::Record n; n.sampleRate = 44100.0; gg::Rng g (7); for (int i = 0; i < 44100 * 3; ++i) n.x.push_back (0.5f * g.bi());
        n.analyse();
        std::printf ("white noise: %zu loops\n", n.atlas.loops().size());
        CHECK (n.atlas.loops().size() < 10);
        lp::Record t; t.sampleRate = 44100.0; t.x.assign (2000, 0.3f); t.analyse();
        CHECK (t.atlas.loops().empty());
    }
    {   // a pure tone: the found loops are whole numbers of periods
        const double sr = 44100.0, f0 = 220.0, period = sr / f0;
        auto s = sineRecord (f0, 4.0, sr);
        int bad = 0;
        for (const auto& f : s.atlas.loops())
        {
            const double k = (double) f.length / period, off = std::abs (k - std::round (k));
            if (off > 0.06 || f.score < 0.9f) ++bad;
        }
        std::printf ("sine: %zu loops, %d not a whole number of periods\n", s.atlas.loops().size(), bad);
        CHECK (s.atlas.loops().size() > 100 && bad == 0);
    }
    {   // the map query
        int a = rec.atlas.nearest (0.1f, 0.2f, rec.sampleRate), b = rec.atlas.nearest (0.8f, 0.2f, rec.sampleRate);
        CHECK (a >= 0 && b >= 0 && a != b);
        const auto& L = rec.atlas.loops();
        CHECK (L[(size_t) a].start < L[(size_t) b].start);
        const int small = rec.atlas.nearest (0.5f, 0.0f, rec.sampleRate), big = rec.atlas.nearest (0.5f, 1.0f, rec.sampleRate);
        CHECK (L[(size_t) small].length < L[(size_t) big].length);
        CHECK (lp::Atlas{}.nearest (0.5f, 0.5f, 44100.0) == -1);
    }

    // ---- the player ----
    auto sine = sineRecord (220.0, 4.0);
    const double srOut = 48000.0;
    {   // nothing without a note; sound with a note; no click at the loop points
        lp::Engine e; e.prepare (srOut); e.setRecord (&sine);
        lp::Params p; p.pulse = 0.f; p.moire = 0.f; p.seam = 0.4f;
        auto silent = render (e, p, 0.5);
        CHECK (peakOf (silent.l) == 0.f);
        e.noteOn (60, 0.8f);
        auto o = render (e, p, 3.0);
        const float pk = peakOf (o.l, 4800);
        std::printf ("sine, key 60: peak %.3f\n", pk);
        CHECK (pk > 0.1f && pk < 1.0f);
        // the largest step between two samples must stay close to that of a plain sine of the same size
        float maxStep = 0.f; for (size_t i = 24000; i + 1 < o.l.size(); ++i) maxStep = std::max (maxStep, std::abs (o.l[i + 1] - o.l[i]));
        const float sineStep = pk * 6.2832f * 220.f / (float) srOut;
        std::printf ("  largest step %.4f, a plain sine of that size would be %.4f\n", maxStep, sineStep);
        CHECK (maxStep < sineStep * 1.5f);
    }
    {   // pitch: key 72 sounds an octave above key 60
        auto zc = [&] (int note)
        {
            lp::Engine e; e.prepare (srOut); e.setRecord (&sine);
            lp::Params p; p.pulse = 0.f; p.moire = 0.f;
            e.noteOn (note, 0.8f);
            auto o = render (e, p, 2.0);
            int c = 0; for (size_t i = 24000; i + 1 < o.l.size(); ++i) if ((o.l[i] < 0.f) != (o.l[i + 1] < 0.f)) ++c;
            return c;
        };
        const int a = zc (60), b = zc (72);
        std::printf ("zero crossings: key 60 -> %d, key 72 -> %d\n", a, b);
        CHECK (a > 100 && std::abs ((double) b / a - 2.0) < 0.1);
    }
    {   // release
        for (int t = 0; t < 2; ++t)
        {
            lp::Engine e; e.prepare (srOut); e.setRecord (&sine);
            lp::Params p; p.tail = t == 0 ? 0.f : 1.f;
            e.noteOn (60, 0.8f);
            render (e, p, 1.0);
            e.noteOff (60);
            auto o = render (e, p, 2.0);
            const double early = rmsOf (o.l, 4800, 9600), late = rmsOf (o.l, o.l.size() - 4800, o.l.size());
            std::printf ("tail %s: rms 0.1-0.2 s after release %.4f, last 0.1 s %.5f, voices %d\n", t == 0 ? "short" : "long", early, late, e.activeVoices());
            if (t == 0) CHECK (late == 0.0 && e.activeVoices() == 0);
            else CHECK (late > 0.01);
        }
    }
    {   // the moire: three copies of slightly different lengths beat against each other
        auto beating = [&] (float moire)
        {
            lp::Engine e; e.prepare (srOut); e.setRecord (&sine);
            lp::Params p; p.pulse = 0.f; p.moire = moire; p.seam = 0.2f; p.size = 0.2f;
            e.noteOn (60, 0.8f);
            auto o = render (e, p, 6.0);
            std::vector<double> w;
            for (size_t i = 48000; i + 960 < o.l.size(); i += 960) w.push_back (rmsOf (o.l, i, i + 960));
            double m = 0.0; for (double x : w) m += x; m /= (double) w.size();
            double v = 0.0; for (double x : w) v += (x - m) * (x - m);
            return std::sqrt (v / (double) w.size()) / std::max (1e-9, m);
        };
        const double flat = beating (0.f), slip = beating (0.8f);
        std::printf ("level variation over 20 ms windows: moire 0 -> %.3f, moire 0.8 -> %.3f\n", flat, slip);
        CHECK (slip > flat + 0.03);
    }
    {   // the pulse: the start of each turn of the loop is accented
        auto variation = [&] (float pulse)
        {
            lp::Engine e; e.prepare (srOut); e.setRecord (&sine);
            lp::Params p; p.pulse = pulse; p.moire = 0.f; p.size = 0.3f;
            e.noteOn (60, 0.8f);
            auto o = render (e, p, 4.0);
            std::vector<double> w;
            for (size_t i = 24000; i + 480 < o.l.size(); i += 480) w.push_back (rmsOf (o.l, i, i + 480));
            double mx = 0, mn = 1e9; for (double x : w) { mx = std::max (mx, x); mn = std::min (mn, x); }
            return mx / std::max (1e-9, mn);
        };
        const double flat = variation (0.f), acc = variation (1.f);
        std::printf ("loudest/quietest 10 ms window: pulse 0 -> %.2f, pulse 1 -> %.2f\n", flat, acc);
        CHECK (acc > flat * 2.0);
    }
    {   // keys: pitch mode plays the same loop, places mode a different place for each key
        lp::Params p;
        lp::Engine a; a.prepare (srOut); a.setRecord (&rec);
        a.noteOn (40, 0.8f); a.noteOn (64, 0.8f); render (a, p, 0.2);
        std::vector<int> pitch; for (int i = 0; i < lp::Engine::maxVoices; ++i) if (a.loopOfVoice (i) >= 0) pitch.push_back (a.loopOfVoice (i));
        CHECK (pitch.size() == 2 && pitch[0] == pitch[1]);
        p.places = true;
        lp::Engine b; b.prepare (srOut); b.setRecord (&rec);
        b.noteOn (40, 0.8f); b.noteOn (64, 0.8f); render (b, p, 0.2);
        std::vector<int> places; for (int i = 0; i < lp::Engine::maxVoices; ++i) if (b.loopOfVoice (i) >= 0) places.push_back (b.loopOfVoice (i));
        CHECK (places.size() == 2 && places[0] != places[1]);
    }
    {   // position and scan move the loop through the record
        lp::Params p;
        auto loopAt = [&] (float pos)
        {
            lp::Engine e; e.prepare (srOut); e.setRecord (&rec); p.position = pos;
            e.noteOn (60, 0.8f); render (e, p, 0.1);
            for (int i = 0; i < lp::Engine::maxVoices; ++i) if (e.loopOfVoice (i) >= 0) return rec.atlas.loops()[(size_t) e.loopOfVoice (i)].start;
            return -1;
        };
        const int s1 = loopAt (0.1f), s2 = loopAt (0.8f);
        CHECK (s1 >= 0 && s2 > s1);
        p.position = 0.1f; p.scan = 1.f;
        lp::Engine e; e.prepare (srOut); e.setRecord (&rec);
        e.noteOn (60, 0.8f);
        std::set<int> seen;
        for (int k = 0; k < 40; ++k)
        {
            render (e, p, 0.25);
            for (int i = 0; i < lp::Engine::maxVoices; ++i) if (e.loopOfVoice (i) >= 0) seen.insert (e.loopOfVoice (i));
        }
        std::printf ("scan at full speed for 10 s: %zu different loops\n", seen.size());
        CHECK (seen.size() >= 8);
    }
    {   // many keys, hard use
        lp::Engine e; e.prepare (srOut); e.setRecord (&rec);
        lp::Params p; p.moire = 1.f; p.seam = 1.f; p.pulse = 1.f; p.scan = -1.f; p.tail = 1.f;
        gg::Rng g (3);
        float pk = 0.f; bool finite = true;
        for (int k = 0; k < 80; ++k)
        {
            e.noteOn (24 + (int) (g.uni() * 80.f), g.uni());
            if (k % 3 == 0) e.noteOff (24 + (int) (g.uni() * 80.f));
            auto o = render (e, p, 0.15);
            pk = std::max ({ pk, peakOf (o.l), peakOf (o.r) });
            finite = finite && peakOf (o.l) < 1e8f;
        }
        e.allNotesOff();
        std::printf ("80 notes, extreme settings: peak %.3f, voices %d\n", pk, e.activeVoices());
        CHECK (finite && pk <= 1.0f && e.activeVoices() <= lp::Engine::maxVoices);
    }
    std::printf (failures ? "%d FAILURES\n" : "ALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
