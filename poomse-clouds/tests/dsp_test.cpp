#include "../Source/Dsp/Engine.h"
#include <cstdio>
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("FAIL: %s (line %d)\n", #c, __LINE__); ++failures; } } while (0)
static const double kSr = 48000.0;

struct Run
{
    cl::Engine e; cl::Params p; double bpm = 120.0, beat = 0.0; std::vector<float> out, in;
    Run() { e.prepare (kSr); }
    template <class F> void go (double seconds, F gen, bool playing = true)
    {
        std::vector<float> iL (256), oL (256), oR (256);
        const int total = (int) (seconds * kSr);
        for (int d = 0; d < total; d += 256)
        {
            const int n = std::min (256, total - d);
            for (int i = 0; i < n; ++i) iL[(size_t) i] = gen ((long) in.size() + i);
            e.process (n, p, beat, bpm, playing, iL.data(), iL.data(), oL.data(), oR.data());
            beat += (double) n / (kSr * 60.0 / bpm);
            for (int i = 0; i < n; ++i) { in.push_back (iL[(size_t) i]); out.push_back (oL[(size_t) i]); }
        }
    }
};
static auto sine (double hz) { return [hz] (long i) { return 0.5f * (float) std::sin (6.283185307 * hz * (double) i / kSr); }; }
static auto silence() { return [] (long) { return 0.f; }; }
static double freqOf (const std::vector<float>& v, size_t from, size_t to)
{
    double best = 0, bf = 0;
    for (double f = 100.0; f < 2000.0; f += 1.0)
    {
        double re = 0, im = 0; const double w = 6.283185307 * f / kSr;
        for (size_t i = from; i < to; i += 2) { re += v[i] * std::cos (w * (double) i); im += v[i] * std::sin (w * (double) i); }
        const double m = re * re + im * im; if (m > best) { best = m; bf = f; }
    }
    return bf;
}
static float peakOf (const std::vector<float>& v, size_t from = 0) { float m = 0; for (size_t i = from; i < v.size(); ++i) { if (! std::isfinite (v[i])) return 1e9f; m = std::max (m, std::abs (v[i])); } return m; }

int main()
{
    // blend 0 = the input, untouched
    { Run r; r.p.blend = 0.f; r.go (1.0, sine (440)); bool same = true; for (size_t i = 0; i < r.out.size(); ++i) same = same && r.out[i] == r.in[i]; CHECK (same); }
    // silence in, silence out
    { Run r; r.go (2.0, silence()); CHECK (peakOf (r.out) == 0.f); }
    // the clouds keep the pitch of what they were taken from, and move it with Pitch
    {
        for (float st : { 0.f, 12.f, -12.f, 7.f })
        {
            Run r; r.p.blend = 1.f; r.p.reverb = 0.f; r.p.density = 0.8f; r.p.size = 0.6f; r.p.pitch = st; r.p.texture = 0.5f; r.p.spread = 0.f;
            r.go (3.0, sine (440));
            const double f = freqOf (r.out, (size_t) (1.5 * kSr), r.out.size()), expect = 440.0 * std::pow (2.0, st / 12.0);
            std::printf ("pitch %+5.1f st: %.1f Hz (expected %.1f)\n", st, f, expect);
            CHECK (std::abs (f - expect) < expect * 0.03);
        }
    }
    // freeze: the memory keeps playing after the input has gone; without freeze it empties
    {
        Run r; r.p.blend = 1.f; r.p.reverb = 0.f; r.p.density = 0.8f; r.p.position = 0.2f; r.p.size = 0.5f;
        r.go (2.0, sine (330)); r.p.freeze = true; r.go (6.0, silence());
        CHECK (peakOf (std::vector<float> (r.out.end() - (long) kSr, r.out.end())) > 0.05f);
        Run q; q.p = r.p; q.p.freeze = false; q.go (2.0, sine (330)); q.go (14.0, silence());
        CHECK (peakOf (std::vector<float> (q.out.end() - (long) kSr, q.out.end())) < 0.001f);
    }
    // grid: the grains start on the steps, so the same pass twice is the same; free is not locked
    {
        Run r; r.p.blend = 1.f; r.p.reverb = 0.f; r.p.grid = true; r.p.density = 0.5f; r.p.spread = 0.f; r.p.size = 0.2f;
        r.go (4.0, sine (220));
        CHECK (r.e.hitCount() > 10);
        // density 0.5 -> 1/16 on the grid at 120 bpm = 8 grains per second
        const int a = r.e.hitCount(); r.go (2.0, sine (220)); const int b = r.e.hitCount();
        std::printf ("grid 1/16 @120: %d grains in 2 s\n", b - a);
        CHECK (b - a >= 15 && b - a <= 17);
    }
    // density: more of it, more grains; always bounded
    {
        int last = -1;
        for (float d : { 0.1f, 0.4f, 0.7f, 1.0f })
        {
            Run r; r.p.density = d; r.p.blend = 1.f; r.go (4.0, sine (440)); const int c = r.e.hitCount();
            std::printf ("density %.1f: %d grains in 4 s, peak %.2f\n", d, c, peakOf (r.out));
            CHECK (c > last); last = c; CHECK (peakOf (r.out) <= 1.8f);
        }
    }
    // hard use: random settings, feedback at the top, noise in: it must stay finite and bounded
    {
        gg::Rng g (7); Run r; float pk = 0.f;
        for (int i = 0; i < 400; ++i)
        {
            r.p.position = g.uni(); r.p.size = g.uni(); r.p.pitch = g.bi() * 24.f; r.p.density = g.uni(); r.p.texture = g.uni(); r.p.spread = g.uni();
            r.p.blend = g.uni(); r.p.feedback = g.uni() < 0.5f ? 1.f : g.uni(); r.p.reverb = g.uni(); r.p.freeze = g.uni() < 0.2f; r.p.grid = g.uni() < 0.3f;
            r.bpm = 40.0 + 160.0 * g.uni();
            const size_t from = r.out.size();
            r.go (0.15, [&] (long) { return 0.8f * g.bi(); }, g.uni() < 0.8f);
            pk = std::max (pk, peakOf (r.out, from));
        }
        std::printf ("400 random settings, feedback up: peak %.2f\n", pk);
        CHECK (pk < 4.f);
    }
    std::printf (failures ? "%d FAILURES\n" : "ALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
