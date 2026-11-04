#include "../Source/Dsp/Engine.h"
#include <cstdio>
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("FAIL: %s (line %d)\n", #c, __LINE__); ++failures; } } while (0)
static const double kSr = 48000.0;
struct Run
{
    sp::Engine e; sp::Params p; double bpm = 120.0, beat = 0.0; std::vector<float> in, out, outR;
    Run() { e.prepare (kSr); }
    template <class F> void go (double s, F gen)
    {
        const int n = (int) (s * kSr); std::vector<float> a ((size_t) n), l ((size_t) n), r ((size_t) n);
        for (int i = 0; i < n; ++i) a[(size_t) i] = gen ((long) in.size() + i);
        e.process (n, p, beat, bpm, a.data(), a.data(), l.data(), r.data());
        beat += (double) n / (kSr * 60.0 / bpm);
        in.insert (in.end(), a.begin(), a.end()); out.insert (out.end(), l.begin(), l.end()); outR.insert (outR.end(), r.begin(), r.end());
    }
};
static double mag (const std::vector<float>& v, double f, size_t from, size_t to)
{ double re = 0, im = 0; const double w = 6.283185307 * f / kSr; for (size_t i = from; i < to; ++i) { re += v[i] * std::cos (w * (double) i); im += v[i] * std::sin (w * (double) i); } return std::sqrt (re * re + im * im); }
static double peakFreq (const std::vector<float>& v, size_t from, size_t to) { double b = 0, bf = 0; for (double f = 100; f < 2000; f += 1) { const double m = mag (v, f, from, to); if (m > b) { b = m; bf = f; } } return bf; }
static float peakOf (const std::vector<float>& v, size_t from = 0) { float m = 0; for (size_t i = from; i < v.size(); ++i) { if (! std::isfinite (v[i])) return 1e9f; m = std::max (m, std::abs (v[i])); } return m; }
static auto sine (double hz) { return [hz] (long i) { return 0.4f * (float) std::sin (6.283185307 * hz * (double) i / kSr); }; }
int main()
{
    // mix 0: the input untouched
    { Run r; r.p.mix = 0.f; r.go (1.0, sine (330)); bool same = true; for (size_t i = 0; i < r.out.size(); ++i) same = same && r.out[i] == r.in[i]; CHECK (same); }
    // the first echo comes back one time later (1/8. at 120 bpm = 0.375 s)
    {
        Run r; r.p.mix = 1.f; r.p.feedback = 0.f; r.p.tone = 1.f; r.p.time = 2;
        r.go (0.05, [] (long i) { return i < 48 ? 0.8f : 0.f; }); r.go (1.0, [] (long) { return 0.f; });
        size_t at = 0; float best = 0; for (size_t i = 0; i < r.out.size(); ++i) if (std::abs (r.out[i]) > best) { best = std::abs (r.out[i]); at = i; }
        std::printf ("echo at %.3f s (expected 0.375)\n", (double) at / kSr); CHECK (std::abs ((double) at / kSr - 0.375) < 0.012 && best > 0.1f);
    }
    // the pitch of the echo: one pass through the shifter
    for (float st : { 0.f, 12.f, -12.f, 7.f })
    {
        Run r; r.p.mix = 1.f; r.p.feedback = 0.f; r.p.tone = 1.f; r.p.pitchL = st; r.p.pitchR = st; r.p.time = 1;
        r.go (3.0, sine (440));
        const double f = peakFreq (r.out, (size_t) (1.5 * kSr), (size_t) (2.5 * kSr)), expect = 440.0 * std::pow (2.0, st / 12.0);
        std::printf ("pitch %+5.1f st: %.0f Hz (expected %.0f)\n", st, f, expect); CHECK (std::abs (f - expect) < expect * 0.03);
    }
    // two sides, two pitches
    {
        Run r; r.p.mix = 1.f; r.p.feedback = 0.f; r.p.tone = 1.f; r.p.pitchL = 12.f; r.p.pitchR = -12.f; r.p.time = 1; r.go (3.0, sine (440));
        const double fl = peakFreq (r.out, (size_t) (1.5 * kSr), (size_t) (2.5 * kSr)), fr = peakFreq (r.outR, (size_t) (1.5 * kSr), (size_t) (2.5 * kSr));
        CHECK (std::abs (fl - 880) < 25 && std::abs (fr - 220) < 8);
    }
    // feedback: the echoes stay and fade; hold: they stay forever
    {
        Run r; r.p.mix = 1.f; r.p.feedback = 0.6f; r.p.time = 1; r.go (0.3, sine (440)); r.go (8.0, [] (long) { return 0.f; });
        CHECK (peakOf (r.out, r.out.size() - 4800) < 0.02f);
        Run h; h.p.mix = 1.f; h.p.time = 1; h.go (0.3, sine (440)); h.p.hold = true; h.go (20.0, [] (long) { return 0.f; });
        const float tail = peakOf (h.out, h.out.size() - 4800); std::printf ("hold after 20 s: %.3f\n", tail); CHECK (tail > 0.05f && tail <= 1.f);
        // and let go of hold: the echoes die again
        h.p.hold = false; h.go (20.0, [] (long) { return 0.f; }); CHECK (peakOf (h.out, h.out.size() - 4800) < 0.02f);
    }
    // character: drive keeps the loop bounded, drift moves the echoes, smear changes the window
    {
        Run a, b; a.p.mix = b.p.mix = 1.f; a.p.feedback = b.p.feedback = 0.5f; b.p.drift = 1.f; a.go (2.0, sine (330)); b.go (2.0, sine (330));
        double d = 0; for (size_t i = 0; i < a.out.size(); ++i) d += std::abs (a.out[i] - b.out[i]); CHECK (d > 1.0);
        Run c; c.p.mix = 1.f; c.p.feedback = 1.f; c.p.drive = 1.f; c.p.pitchL = 7.f; c.p.pitchR = -5.f; c.go (6.0, [] (long i) { return i < 4800 ? 0.9f : 0.f; });
        CHECK (peakOf (c.out) <= 1.0f && peakOf (c.outR) <= 1.0f);
        for (float sm : { 0.f, 1.f }) { Run r; r.p.mix = 1.f; r.p.feedback = 0.f; r.p.tone = 1.f; r.p.pitchL = r.p.pitchR = 12.f; r.p.smear = sm; r.p.time = 1; r.go (3.0, sine (440));
            const double f = peakFreq (r.out, (size_t) (1.5 * kSr), (size_t) (2.5 * kSr)); CHECK (std::abs (f - 880.0) < (sm == 0.f ? 56.0 : 26.0)); }
    }
    // Chaos: written, not random. Without it nothing happens; with it the same version always plays the same way
    {
        auto run = [&] (float chaos, int seed) { Run r; r.p.mix = 1.f; r.p.chaos = chaos; r.p.seed = seed; r.p.feedback = 0.6f; r.go (12.0, sine (330)); return r.out; };
        const auto a = run (0.f, 1), b = run (1.f, 1), c = run (1.f, 1), d = run (1.f, 2);
        CHECK (a != b); CHECK (b == c); CHECK (b != d); CHECK (a == run (0.f, 2));
    }
    // Rise: the echoes climb
    {
        Run r; r.p.mix = 1.f; r.p.feedback = 0.9f; r.p.pitchL = r.p.pitchR = 0.f; r.p.time = 1; r.p.tone = 1.f;
        r.go (1.0, sine (330)); r.p.rise = true; r.go (4.0, sine (330));
        const double f0 = peakFreq (r.out, (size_t) (0.3 * kSr), (size_t) (0.9 * kSr)), f1 = peakFreq (r.out, (size_t) (4.0 * kSr), (size_t) (5.0 * kSr));
        std::printf ("rise: %.0f Hz -> %.0f Hz\n", f0, f1); CHECK (f1 > f0 * 1.5);
        r.p.rise = false; r.p.feedback = 0.f; r.go (8.0, [] (long) { return 0.f; }); CHECK (peakOf (r.out, r.out.size() - 4800) < 0.02f);
    }
    // hard use: spirals up with feedback at the top stay finite and bounded
    {
        Run r; uint32_t s = 99; auto u = [&] { s = s * 1664525u + 1013904223u; return (float) (s >> 8) / 16777216.f; }; float pk = 0.f;
        for (int i = 0; i < 300; ++i)
        {
            r.p.time = (int) (u() * 6); r.p.pitchL = (u() - 0.5f) * 48.f; r.p.pitchR = (u() - 0.5f) * 48.f; r.p.feedback = u() < 0.4f ? 1.f : u(); r.p.tone = u(); r.p.mix = u(); r.p.hold = u() < 0.3f; r.p.drive = u(); r.p.drift = u(); r.p.smear = u(); r.p.chaos = u(); r.p.rise = u() < 0.2f; r.p.seed = 1 + (int) (u() * 50); r.bpm = 40.0 + 200.0 * u();
            const size_t from = r.out.size(); r.go (0.1, [&] (long) { return 0.9f * (u() * 2.f - 1.f); });
            pk = std::max (pk, peakOf (r.out, from));
        }
        std::printf ("300 random changes: peak %.2f\n", pk); CHECK (pk < 2.2f);
    }
    std::printf (failures ? "%d FAILURES\n" : "ALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
