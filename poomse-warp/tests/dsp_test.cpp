#include "../Source/Dsp/Engine.h"
#include <cstdio>
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("FAIL: %s (line %d)\n", #c, __LINE__); ++failures; } } while (0)
static const double kSr = 48000.0;
static std::vector<float> render (wp::Engine& e, const wp::Params& p, double s) { std::vector<float> L ((size_t) (s * kSr)), R (L.size()); e.process ((int) L.size(), p, L.data(), R.data()); return L; }
static double mag (const std::vector<float>& v, double f, size_t from)
{ double re = 0, im = 0; const double w = 6.283185307 * f / kSr; for (size_t i = from; i < v.size(); ++i) { re += v[i] * std::cos (w * (double) i); im += v[i] * std::sin (w * (double) i); } return std::sqrt (re * re + im * im) / (double) (v.size() - from); }
static float peakOf (const std::vector<float>& v, size_t from = 0) { float m = 0; for (size_t i = from; i < v.size(); ++i) { if (! std::isfinite (v[i])) return 1e9f; m = std::max (m, std::abs (v[i])); } return m; }
int main()
{
    // silence without keys
    { wp::Engine e; e.prepare (kSr); wp::Params p; CHECK (peakOf (render (e, p, 0.5)) == 0.f); }
    // the pitch is the note (A4 = 440), also with a bent phase
    for (float warp : { 0.f, 0.5f, 1.f })
    {
        wp::Engine e; e.prepare (kSr); wp::Params p; p.warp = warp; p.pluck = 0.f; p.cutoff = 1.f; p.detune = 0.f; p.sustain = 1.f; p.attack = 0.f;
        e.noteOn (69, 1.f); const auto v = render (e, p, 1.0);
        double best = 0, bf = 0; for (double f = 200; f < 900; f += 1) { const double m = mag (v, f, 12000); if (m > best) { best = m; bf = f; } }
        std::printf ("warp %.1f: strongest at %.0f Hz\n", warp, bf);
        CHECK (std::abs (bf - 440.0) < 2.0 || std::abs (bf - 880.0) < 3.0 || std::abs (bf - 1320.0) < 4.0);
    }
    // Warp adds harmonics: more energy at 3x than a pure sine
    {
        double h3[2];
        for (int k = 0; k < 2; ++k) { wp::Engine e; e.prepare (kSr); wp::Params p; p.warp = k ? 0.9f : 0.f; p.pluck = 0.f; p.cutoff = 1.f; p.detune = 0.f; p.sustain = 1.f; p.attack = 0.f; e.noteOn (57, 1.f); const auto v = render (e, p, 1.0); h3[k] = mag (v, 3 * 220.0, 12000) / std::max (1e-9, mag (v, 220.0, 12000)); }
        std::printf ("3rd/1st harmonic: warp 0 -> %.3f, warp 0.9 -> %.3f\n", h3[0], h3[1]);
        CHECK (h3[1] > h3[0] * 5.0 + 0.02);
    }
    // Twist adds sidebands
    {
        double a[2];
        for (int k = 0; k < 2; ++k) { wp::Engine e; e.prepare (kSr); wp::Params p; p.warp = 0.f; p.twist = k ? 0.8f : 0.f; p.pluck = 0.f; p.cutoff = 1.f; p.detune = 0.f; p.sustain = 1.f; p.attack = 0.f; e.noteOn (57, 1.f); a[k] = mag (render (e, p, 1.0), 2 * 220.0, 12000); }
        CHECK (a[1] > a[0] * 5.0 + 0.005);
    }
    // Fold and Sync keep the pitch and add harmonics; Evolve makes every note different, but the same version always does the same
    {
        auto spec = [&] (float fold, float sync, float evolve, int seed, int notes)
        {
            wp::Engine e; e.prepare (kSr); e.setSeed (seed); wp::Params p; p.warp = 0.f; p.pluck = 0.f; p.cutoff = 1.f; p.detune = 0.f; p.sustain = 1.f; p.attack = 0.f; p.fold = fold; p.sync = sync; p.evolve = evolve;
            std::vector<float> all; for (int k = 0; k < notes; ++k) { e.noteOn (57, 1.f); auto v = render (e, p, 0.6); all.insert (all.end(), v.begin(), v.end()); e.noteOff (57); render (e, p, 0.4); }
            return all;
        };
        const auto plain = spec (0.f, 0.f, 0.f, 1, 1);
        for (int k = 0; k < 2; ++k)
        {
            const auto v = spec (k ? 0.f : 0.8f, k ? 0.8f : 0.f, 0.f, 1, 1);
            double best = 0, bf = 0; for (double f = 100; f < 700; f += 1) { const double m = mag (v, f, 12000); if (m > best) { best = m; bf = f; } }
            std::printf ("%s: strongest at %.0f Hz, 3rd/1st %.3f (plain %.3f)\n", k ? "sync" : "fold", bf, mag (v, 660, 12000) / mag (v, 220, 12000), mag (plain, 660, 12000) / mag (plain, 220, 12000));
            CHECK (std::abs (bf - 220.0) < 3.0 || std::abs (bf - 440.0) < 4.0 || std::abs (bf - 660.0) < 5.0);
            CHECK (peakOf (v) <= 1.0f);
        }
        const auto e0 = spec (0.f, 0.f, 0.f, 1, 2), e1 = spec (0.f, 0.f, 0.8f, 1, 2), e2 = spec (0.f, 0.f, 0.8f, 1, 2), e3 = spec (0.f, 0.f, 0.8f, 2, 2);
        CHECK (e1 == e2); CHECK (e1 != e3); CHECK (e1 != e0);
        std::vector<float> n1 (e1.begin() + (long) (0.1 * kSr), e1.begin() + (long) (0.4 * kSr)), n2 (e1.begin() + (long) (0.7 * kSr), e1.begin() + (long) (1.0 * kSr));
        CHECK (n1 != n2);
    }
    // the key lets go, and the voice ends
    { wp::Engine e; e.prepare (kSr); wp::Params p; p.release = 0.2f; e.noteOn (60, 0.8f); render (e, p, 0.5); e.noteOff (60); CHECK (e.activeVoices() == 1); const auto v = render (e, p, 4.0); CHECK (e.activeVoices() == 0); CHECK (peakOf (v, v.size() - 1000) == 0.f); }
    // polyphony beyond 8 voices steals, stays bounded; velocity changes level
    {
        wp::Engine e; e.prepare (kSr); wp::Params p; for (int n = 40; n < 56; ++n) e.noteOn (n, 1.f); const auto v = render (e, p, 0.5);
        CHECK (e.activeVoices() == 8 && peakOf (v) <= 1.0f);
        wp::Engine a, b; a.prepare (kSr); b.prepare (kSr); a.noteOn (60, 1.f); b.noteOn (60, 0.3f); CHECK (peakOf (render (a, p, 0.3)) > peakOf (render (b, p, 0.3)));
    }
    // hard use
    {
        wp::Engine e; e.prepare (kSr); wp::Params p; uint32_t s = 12345; auto u = [&] { s = s * 1664525u + 1013904223u; return (float) (s >> 8) / 16777216.f; }; float pk = 0.f;
        for (int i = 0; i < 400; ++i)
        {
            p.warp = u(); p.twist = u(); p.ratio = (int) (u() * 7); p.pluck = u(); p.cutoff = u(); p.reso = u(); p.detune = u(); p.attack = u(); p.fold = u(); p.sync = u(); p.evolve = u(); p.decay = u(); p.sustain = u(); p.release = u();
            const int n = 12 + (int) (u() * 100); if (u() < 0.6f) e.noteOn (n, u()); else e.noteOff (n); e.pitchBend ((u() - 0.5f) * 4.f); e.modWheel (u());
            pk = std::max (pk, peakOf (render (e, p, 0.05)));
        }
        std::printf ("400 random changes: peak %.2f\n", pk); CHECK (pk <= 1.0f);
    }
    std::printf (failures ? "%d FAILURES\n" : "ALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
