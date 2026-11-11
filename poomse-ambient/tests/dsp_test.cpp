#include "../Source/Dsp/Sampler.h"
#include "../Source/Dsp/PitchDetect.h"
#include <cstdio>
#include <map>
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("FAIL: %s (line %d)\n", #c, __LINE__); ++failures; } } while (0)
static const double kSr = 48000.0;


using namespace amb;
struct Run
{
    Sampler e; SamplerParams p; double bpm = 120.0, beat = 0.0; std::vector<float> out, outR; std::shared_ptr<Bank> bank;
    explicit Run (int sound) { bank = makeFactory (sound); e.prepare (kSr); e.setBank (bank.get()); }
    void go (double seconds)
    {
        const int total = (int) (seconds * kSr); std::vector<float> L (256), R (256);
        for (int d = 0; d < total; d += 256)
        {
            const int n = std::min (256, total - d);
            e.process (n, p, beat, bpm, L.data(), R.data());
            beat += (double) n / (kSr * 60.0 / bpm);
            for (int i = 0; i < n; ++i) { out.push_back (L[(size_t) i]); outR.push_back (R[(size_t) i]); }
        }
    }
};
static float peakOf (const std::vector<float>& v, size_t from = 0, size_t to = 0) { if (to == 0) to = v.size(); float m = 0; for (size_t i = from; i < to; ++i) { if (! std::isfinite (v[i])) return 1e9f; m = std::max (m, std::abs (v[i])); } return m; }
static double mag (const std::vector<float>& v, double f, size_t from, size_t to)
{ double re = 0, im = 0; const double w = 6.283185307 * f / kSr; for (size_t i = from; i < to; i += 2) { re += v[i] * std::cos (w * (double) i); im += v[i] * std::sin (w * (double) i); } return std::sqrt (re * re + im * im); }
static double rms (const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i]; return std::sqrt (s / (double) (b - a)); }
// energy of a band, by crude filtering: difference signal for the high part
static double hfShare (const Zone& z) { double lo = 0, hi = 0; for (size_t i = 2; i < z.x[0].size(); ++i) { const double x = z.x[0][i], d = z.x[0][i] - z.x[0][i - 1]; lo += x * x; hi += d * d; } return hi / (lo + 1e-12); }

int main()
{
    // every sound: finite, not silent, not clipped; the pitched ones are in tune with the key they are built for
    for (int s = 0; s < numFactory; ++s)
    {
        auto b = makeFactory (s); float pk = 0; bool fin = true;
        for (auto& z : b->zones) for (auto& ch : z.x) for (float v : ch) { if (! std::isfinite (v)) fin = false; pk = std::max (pk, std::abs (v)); }
        CHECK (fin && pk > 0.5f && pk <= 0.9f);
        if (b->pitched && s != SPad)
            for (auto& z : b->zones)
            {
                std::vector<float> mono (z.x[0].size()); for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (z.x[0][i] + z.x[1][i]);
                const int got = detectRootNote (mono, z.sampleRate, -1);
                if (got < 0) continue;
                if (got != z.root && got != z.root + 12 && got != z.root - 12) std::printf ("  %s root %d detected %d\n", kSoundNames[s], z.root, got);
                CHECK (std::abs (got - z.root) % 12 == 0 || std::abs (got - z.root) <= 0);
            }
        for (auto& z : b->zones) if (z.loops())
        {
            // the seam: the step at the loop point is no bigger than the steps around it
            double around = 0; int c = 0; for (int i = z.loopEnd - 40; i < z.loopEnd - 1; ++i) { around = std::max (around, (double) std::abs (z.x[0][(size_t) i + 1] - z.x[0][(size_t) i])); ++c; }
            const double seam = std::abs (z.x[0][(size_t) z.loopStart] - z.x[0][(size_t) z.loopEnd - 1]);
            CHECK (seam <= 4.0 * around + 0.02);
        }
    }
    // a key plays its note: the pitch follows the key
    for (int s : { (int) SPiano, (int) SViolin })
    {
        for (int note : { 48, 55, 62, 69 })
        {
            Run r (s); r.p.tone = 1.f; r.e.noteOn (note, 1.f, r.p, 0.0); r.go (1.6);
            const double f = 440.0 * std::pow (2.0, (note - 69) / 12.0);
            double best = 0, bf = 0; for (double c = -60; c <= 60; c += 2) { const double m = mag (r.out, f * std::pow (2.0, c / 1200.0), (size_t) (0.4 * kSr), (size_t) (1.4 * kSr)); if (m > best) { best = m; bf = c; } }
            if (std::abs (bf) > 30) std::printf ("  %s note %d is %+.0f cents off\n", kSoundNames[s], note, bf);
            CHECK (std::abs (bf) <= 30);
        }
    }
    // short sounds die by themselves; held sounds stay while the key is down and fade after
    {
        Run r (SPiano); r.e.noteOn (60, 1.f, r.p, 0.0); r.go (12.0); CHECK (r.e.activeVoices() == 0);
        Run v (SViolin); v.p.tail = 0.3f; v.e.noteOn (60, 1.f, v.p, 0.0); v.go (10.0);
        CHECK (v.e.activeVoices() == 1 && rms (v.out, (size_t) (9 * kSr), (size_t) (10 * kSr)) > 0.01);
        v.e.noteOff (60); v.go (6.0); CHECK (v.e.activeVoices() == 0 && peakOf (v.out, v.out.size() - 1000) == 0.f);
    }
    // bloom: the note swells in
    {
        Run a (SViolin), b (SViolin); b.p.bloom = 1.f; a.e.noteOn (60, 1.f, a.p, 0.0); b.e.noteOn (60, 1.f, b.p, 0.0); a.go (1.0); b.go (1.0);
        CHECK (rms (b.out, 0, (size_t) (0.4 * kSr)) < 0.3 * rms (a.out, 0, (size_t) (0.4 * kSr)));
    }
    // chords: a key makes three notes
    {
        Run r (SPiano); r.p.chord = 3; r.e.noteOn (60, 1.f, r.p, 0.0); r.go (1.0);
        const double c4 = 261.6256, e4 = c4 * std::pow (2.0, 4.0 / 12), g4 = c4 * std::pow (2.0, 7.0 / 12), d4 = c4 * std::pow (2.0, 2.0 / 12);
        CHECK (mag (r.out, e4, 4000, 40000) > 3.0 * mag (r.out, d4, 4000, 40000)); CHECK (mag (r.out, g4, 4000, 40000) > 3.0 * mag (r.out, d4, 4000, 40000));
    }
    // loops: a held key says its note again
    {
        Run a (SPiano), b (SPiano); b.p.loops = 1.f; a.e.noteOn (60, 1.f, a.p, 0.0); b.e.noteOn (60, 1.f, b.p, 0.0); a.go (30.0); b.go (30.0);
        std::printf ("voices started in 30 s: %d without loops, %d with\n", a.e.started(), b.e.started());
        CHECK (a.e.started() == 1 && b.e.started() >= 10 && b.e.started() <= 13);
        // two keys do not repeat in step with each other
        Run c (SPiano); c.p.loops = 1.f; c.e.noteOn (60, 1.f, c.p, 0.0); c.e.noteOn (62, 1.f, c.p, 0.0); c.go (40.0); CHECK (c.e.started() >= 8);
        // let go of the key: the repeats stop
        Run d (SPiano); d.p.loops = 1.f; d.e.noteOn (60, 1.f, d.p, 0.0); d.go (2.0); d.e.noteOff (60); d.go (1.0); const int n = d.e.started(); d.go (30.0); CHECK (d.e.started() == n);
    }
    // latch
    {
        Run r (SViolin); r.p.latch = true; r.e.noteOn (60, 1.f, r.p, 0.0); r.e.noteOff (60); r.go (5.0); CHECK (r.e.heldKeys() == 1 && r.e.activeVoices() == 1);
        r.e.noteOn (60, 1.f, r.p, r.beat); r.go (8.0); CHECK (r.e.heldKeys() == 0 && r.e.activeVoices() == 0);
    }
    // a place: the key lets it in whatever the key is, and it does not stop by itself
    {
        Run r (SForest); r.e.noteOn (40, 1.f, r.p, 0.0); r.go (3.0); const double a = rms (r.out, (size_t) (2 * kSr), (size_t) (3 * kSr));
        Run q (SForest); q.e.noteOn (80, 1.f, q.p, 0.0); q.go (3.0); const double b = rms (q.out, (size_t) (2 * kSr), (size_t) (3 * kSr));
        CHECK (a > 0.005 && std::abs (a - b) < 0.4 * std::max (a, b));
        Run l (SForest); l.e.noteOn (60, 1.f, l.p, 0.0); l.go (45.0); CHECK (l.e.activeVoices() == 1 && rms (l.out, l.out.size() - (size_t) kSr, l.out.size()) > 0.005);
    }
    // hard use
    {
        gg::Rng g (11); float pk = 0.f;
        for (int s : { (int) SPiano, (int) SPad, (int) SForest })
        {
            Run r (s);
            for (int i = 0; i < 300; ++i)
            {
                r.p.tone = g.uni(); r.p.bloom = g.uni(); r.p.tail = g.uni(); r.p.loops = g.uni(); r.p.chord = (int) (g.uni() * 6); r.p.latch = g.uni() < 0.3f; r.bpm = 50 + 150 * g.uni();
                const int note = 24 + (int) (g.uni() * 80);
                if (g.uni() < 0.6f) r.e.noteOn (note, g.uni(), r.p, r.beat); else r.e.noteOff (note);
                const size_t from = r.out.size(); r.go (0.08); pk = std::max (pk, peakOf (r.out, from));
            }
            CHECK (r.e.activeVoices() <= Sampler::maxVoices);
        }
        std::printf ("hard use: peak %.2f\n", pk); CHECK (pk <= 1.0f);
    }
    std::printf (failures ? "%d FAILURES\n" : "ALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
