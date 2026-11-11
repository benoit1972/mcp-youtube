#include "../Source/Dsp/Murmur.h"
#include "../Source/Dsp/PitchDetect.h"
#include <cstdio>
#include <set>
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("FAIL: %s (line %d)\n", #c, __LINE__); ++failures; } } while (0)
using namespace mur;
static const double kSr = 48000.0;
static float peakOf (const std::vector<float>& v, size_t from = 0) { float m = 0; for (size_t i = from; i < v.size(); ++i) { if (! std::isfinite (v[i])) return 1e9f; m = std::max (m, std::abs (v[i])); } return m; }
static double rms (const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i]; return std::sqrt (s / (double) (b - a)); }
static double mag (const std::vector<float>& v, double f, size_t from, size_t to) { double re = 0, im = 0; const double w = 6.283185307 * f / kSr; for (size_t i = from; i < to; i += 2) { re += v[i] * std::cos (w * (double) i); im += v[i] * std::sin (w * (double) i); } return std::sqrt (re * re + im * im); }

struct Rendered { std::vector<float> L, R; int reversedSeen = 0; };

int main()
{
    // ---------------- the sounds ----------------
    for (int w = 0; w < 2; ++w)
    {
        auto z = makeBuiltIn (w); float pk = 0; bool fin = true;
        for (auto& ch : z->x) for (float v : ch) { if (! std::isfinite (v)) fin = false; pk = std::max (pk, std::abs (v)); }
        CHECK (fin && pk > 0.5f && pk <= 0.9f && z->loops());
        double around = 0; for (int i = z->loopEnd - 40; i < z->loopEnd - 1; ++i) around = std::max (around, (double) std::abs (z->x[0][(size_t) i + 1] - z->x[0][(size_t) i]));
        CHECK (std::abs (z->x[0][(size_t) z->loopStart] - z->x[0][(size_t) z->loopEnd - 1]) <= 4.0 * around + 0.02);
    }

    // ---------------- the flock ----------------
    // the voices that rest on a note never make a semitone or a tritone with one another, in any scale of Live, whatever the settings
    {
        long pairs = 0, bad = 0;
        for (int sc = 0; sc < kNumScales; ++sc) for (unsigned seed = 1; seed <= 3; ++seed)
        {
            FlockParams p; p.scale = sc; p.voices = 7; p.wild = 0.2f + 0.3f * (float) seed; p.journey = 0.f; p.range = 0.2f * (float) seed + 0.2f; p.key = seed == 3 ? 4 : -1;
            Flock f; f.start (0.0, seed, 48);
            for (double b = 0; b < 300; b += 0.5)
            {
                f.advance (b, p);
                for (int i = 0; i < kMaxVoices; ++i) for (int j = i + 1; j < kMaxVoices; ++j) if (f.alive (i) && f.alive (j))
                { const int d = std::abs ((int) std::lround (f.target (i)) - (int) std::lround (f.target (j))) % 12; ++pairs; if (d == 1 || d == 11 || d == 6) ++bad; }
            }
        }
        std::printf ("flock: %ld voice pairs, %ld rubbing\n", pairs, bad);
        CHECK (pairs > 500000 && bad == 0);
    }
    // every note is a note of the scale, in the window of the sample; and a fixed key is honoured
    {
        FlockParams p; p.scale = 3; p.voices = 6; p.key = 2; p.wild = 0.8f; p.range = 1.f; p.journey = 0.f; p.rootNote = 60;
        Flock f; f.start (0.0, 5, 40);
        const auto& sd = kScales[p.scale];
        for (double b = 0; b < 400; b += 0.5)
        {
            f.advance (b, p);
            for (int v = 0; v < kMaxVoices; ++v) if (f.alive (v))
            {
                const int n = (int) std::lround (f.target (v)); bool ok = false; for (int i = 0; i < sd.n; ++i) ok = ok || ((n - 2 - sd.iv[i]) % 12 + 12) % 12 == 0;
                CHECK (ok); CHECK (n >= 60 - 14 && n <= 60 + 19);
            }
        }
    }
    // the same version flies the same way, whatever the way it is looked at; another version flies another way
    {
        FlockParams p; p.voices = 6; p.journey = 0.f;
        auto trace = [&] (unsigned seed, double dt)
        {
            Flock f; f.start (0.0, seed, 48); std::vector<float> t; double b = 0.0;
            for (int k = 1; k <= 40; ++k)
            {
                const double target = 5.0 * k;
                while (b + dt < target) { b += dt; f.advance (b, p); }
                b = target; f.advance (b, p);
                for (int v = 0; v < kMaxVoices; ++v) t.push_back (f.view (v, b, p).pitch);
            }
            return t;
        };
        CHECK (trace (3, 0.5) == trace (3, 0.5)); CHECK (trace (3, 0.5) == trace (3, 7.3)); CHECK (trace (3, 0.5) != trace (4, 0.5));
    }
    // it moves: a pitch is not the same all the time, the wind carries the flock, and a bold flock moves more than a calm one
    {
        auto motion = [&] (float wild, double* centreSpan)
        {
            FlockParams p; p.voices = 5; p.wild = wild; p.journey = 0.f; p.speed = 0.5f; double total = 0; int count = 0; double lo = 1e9, hi = -1e9;
            for (unsigned seed = 1; seed <= 4; ++seed)
            {
                Flock f; f.start (0.0, seed, 48); float prev[kMaxVoices] = {};
                for (double b = 0; b < 480; b += 0.25)
                {
                    f.advance (b, p); double cs = 0; int n = 0;
                    for (int v = 0; v < kMaxVoices; ++v) if (f.alive (v)) { const float pt = f.view (v, b, p).pitch; if (b > 30) { total += std::abs (pt - prev[v]); ++count; } prev[v] = pt; cs += pt; ++n; }
                    if (n && b > 30) { lo = std::min (lo, cs / n); hi = std::max (hi, cs / n); }
                }
            }
            if (centreSpan) *centreSpan = hi - lo;
            return total / std::max (1, count);
        };
        double span = 0; const double calm = motion (0.05f, nullptr), bold = motion (0.95f, &span);
        std::printf ("motion per 0.25 beat: calm %.4f, bold %.4f semitones; the flock's centre travels %.1f semitones\n", calm, bold, span);
        CHECK (bold > calm * 1.2 && calm > 0.0);
        CHECK (span > 4.0);
    }
    // a glide is smooth: no jump of more than a semitone and a half in an eighth of a beat
    {
        FlockParams p; p.voices = 6; p.wild = 0.9f; p.journey = 0.f; Flock f; f.start (0.0, 2, 48); float prev[kMaxVoices]; bool have[kMaxVoices] = {};
        for (double b = 0; b < 400; b += 0.125)
        {
            f.advance (b, p);
            for (int v = 0; v < kMaxVoices; ++v) if (f.alive (v)) { const float pt = f.view (v, b, p).pitch; if (have[v]) CHECK (std::abs (pt - prev[v]) < 1.5f); prev[v] = pt; have[v] = true; } else have[v] = false;
        }
    }
    // the arc: it grows from one voice, peaks, thins out; journey 0 keeps them all
    {
        FlockParams p; p.voices = 8; p.pieceBars = 64; p.journey = 1.f; Flock f; f.start (0.0, 1, 48); int n[3] = {};
        for (double b = 1; b < 256; b += 1.0) { f.advance (b, p); if ((int) (b / 85.4) < 3) n[(int) (b / 85.4)] = std::max (n[(int) (b / 85.4)], f.aliveCount()); }
        std::printf ("voices at most: start %d, middle %d, end %d\n", n[0], n[1], n[2]);
        CHECK (n[1] == 8 && n[0] < 8);
        FlockParams q = p; q.journey = 0.f; Flock g; g.start (0.0, 1, 48); g.advance (2.0, q); CHECK (g.aliveCount() == 8);
        // legs sit on the half-beat grid
        for (int v = 0; v < 8; ++v) CHECK (std::abs (g.legLength (v) * 2.0 - std::round (g.legLength (v) * 2.0)) < 1e-9);
    }

    // ---------------- the player ----------------
    {
        auto glass = makeBuiltIn (1);                     // the violin, which has a clear fundamental
        auto run = [&] (unsigned seed, double seconds, double releaseAt, const FlockParams& fp, Rendered& out, Murmur* keep = nullptr)
        {
            Murmur m; m.prepare (kSr); m.setZone (glass.get());
            double beat = 0.0; bool off = false; m.noteOn (48, seed, 0.0, fp);
            std::vector<float> L (256), R (256), SL (256), SR (256);
            for (double t = 0; t < seconds; t += 256.0 / kSr)
            {
                if (! off && t >= releaseAt) { m.noteOff (48, beat); off = true; }
                std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f); std::fill (SL.begin(), SL.end(), 0.f); std::fill (SR.begin(), SR.end(), 0.f);
                m.process (256, beat, 120.0, fp, L.data(), R.data(), SL.data(), SR.data());
                beat += 256.0 / (kSr * 60.0 / 120.0);
                out.L.insert (out.L.end(), L.begin(), L.end()); out.R.insert (out.R.end(), R.begin(), R.end());
            }
            if (keep) (void) keep;
        };
        FlockParams fp; fp.voices = 5; fp.pieceBars = 32;
        Rendered a, b, c;
        run (3, 40.0, 30.0, fp, a); run (3, 40.0, 30.0, fp, b); run (4, 40.0, 30.0, fp, c);
        std::printf ("player: peak %.2f\n", peakOf (a.L));
        CHECK (peakOf (a.L) < 4.f && peakOf (a.L) > 0.05f);
        CHECK (rms (a.L, (size_t) (3 * kSr), (size_t) (6 * kSr)) > 0.003);                          // it sings while the key is down
        CHECK (rms (a.L, (size_t) (39 * kSr), a.L.size()) < 0.2 * rms (a.L, (size_t) (20 * kSr), (size_t) (28 * kSr)));      // and fades after
        CHECK (a.L == b.L && a.L != c.L && a.L != a.R);
        // one voice sings the pitch it is told: its note is heard
        FlockParams one; one.voices = 1; one.wild = 0.f; one.journey = 0.f; one.speed = 0.f; one.scale = 0;
        for (unsigned seed : { 2u, 5u, 9u })
        {
            Murmur m; m.prepare (kSr); m.setZone (glass.get()); double beat = 0.0; m.noteOn (60, seed, 0.0, one);
            Rendered o; std::vector<float> L (256), R (256), SL (256), SR (256); float pitchSeen = -1.f;
            for (double t = 0; t < 14.0; t += 256.0 / kSr)
            {
                std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
                m.process (256, beat, 120.0, one, L.data(), R.data(), SL.data(), SR.data()); beat += 256.0 / (kSr * 0.5);
                o.L.insert (o.L.end(), L.begin(), L.end());
                if (t > 11.0) pitchSeen = m.voicePitch (0);
            }
            const double f = 440.0 * std::pow (2.0, (pitchSeen - 69.0) / 12.0);
            double best = 0, bf = 0; for (double c2 = -100; c2 <= 100; c2 += 4) { const double mm = mag (o.L, f * std::pow (2.0, c2 / 1200.0), (size_t) (11.5 * kSr), (size_t) (13.5 * kSr)); if (mm > best) { best = mm; bf = c2; } }
            std::printf ("one voice, seed %u: note %.1f, %+.0f cents\n", seed, pitchSeen, bf);
            CHECK (pitchSeen > 0.f && std::abs (bf) <= 28);
        }
        // a dropped sound works the same way: a single note becomes a held tone
        std::vector<float> tone (44100); for (size_t i = 0; i < tone.size(); ++i) tone[i] = 0.5f * (float) std::sin (2.0 * 3.14159265 * 220.0 * (double) i / 44100.0) * std::exp (-(float) i / 30000.f);
        auto dz = std::make_shared<Zone> (detail::makeSustained (tone, tone, 44100.0, 57));
        CHECK (dz->loops());
        Murmur m; m.prepare (kSr); m.setZone (dz.get()); double beat = 0.0; m.noteOn (48, 1, 0.0, fp);
        std::vector<float> L (256), R (256), SL (256), SR (256); float pk = 0.f;
        for (double t = 0; t < 20.0; t += 256.0 / kSr) { std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f); m.process (256, beat, 120.0, fp, L.data(), R.data(), SL.data(), SR.data()); beat += 256.0 / (kSr * 0.5); pk = std::max (pk, peakOf (L)); }
        CHECK (pk > 0.02f && pk < 4.f);
        // hard use: the settings change all the time
        gg::Rng g (7); Murmur h; h.prepare (kSr); h.setZone (glass.get()); beat = 0.0; h.noteOn (50, 3, 0.0, fp); float hk = 0.f;
        for (int i = 0; i < 400; ++i)
        {
            FlockParams q; q.voices = 1 + (int) (g.uni() * 8); q.wild = g.uni(); q.speed = g.uni(); q.range = g.uni(); q.journey = g.uni(); q.scale = (int) (g.uni() * kNumScales); q.key = g.uni() < 0.4f ? (int) (g.uni() * 12) : -1; q.pieceBars = 16 + (int) (g.uni() * 100);
            if (g.uni() < 0.1f) h.noteOff (50, beat); if (g.uni() < 0.1f) h.noteOn (40 + (int) (g.uni() * 30), 5, beat, q);
            std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
            for (int k = 0; k < 8; ++k) { h.process (256, beat, 60.0 + 120.0 * g.uni(), q, L.data(), R.data(), SL.data(), SR.data()); beat += 256.0 / (kSr * 0.5); hk = std::max (hk, peakOf (L)); }
        }
        std::printf ("hard use: peak %.2f\n", hk); CHECK (hk < 40.f);
    }
    std::printf (failures ? "%d FAILURES\n" : "ALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
