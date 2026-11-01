// JUCE-free tests of the Poomse Pad spectral engine.
#include "../Source/Dsp/SpectralPad.h"
#include "../Source/Dsp/PadReverb.h"
#include "../Source/Dsp/ModMatrix.h"
#include "../Source/Dsp/Modulators.h"
#include "../Source/Dsp/Util.h"
#include "../Source/Dsp/Presets.h"
#include <set>
#include <string>
#include <cstdio>
#include <memory>

static int fails = 0;
#define CHECK(c) do { if (! (c)) { std::printf ("FAIL: %s (line %d)\n", #c, __LINE__); ++fails; } } while (0)

static double rmsOf (const std::vector<float>& v, int a, int b)
{
    double acc = 0; for (int i = a; i < b; ++i) acc += (double) v[(size_t) i] * v[(size_t) i];
    return std::sqrt (acc / (double) std::max (1, b - a));
}
static double db (double x) { return 20.0 * std::log10 (x + 1e-12); }

// fraction of the spectral energy of v[a,b) lying within +-`semis` semitones of f0
static double energyNear (const std::vector<float>& v, int a, int b, double sr, double f0, double semis)
{
    const int n = 8192;
    pad::Fft f; f.init (n);
    std::vector<float> re ((size_t) n), im ((size_t) n, 0.f);
    double near = 0, all = 0;
    for (int off = a; off + n <= b; off += n)
    {
        for (int i = 0; i < n; ++i) { re[(size_t) i] = v[(size_t) (off + i)] * (0.5f - 0.5f * std::cos (2.f * pad::kPi * (float) i / (float) n)); im[(size_t) i] = 0.f; }
        f.transform (re.data(), im.data(), false);
        for (int k = 2; k < n / 2; ++k)
        {
            const double fr = (double) k * sr / n, e = (double) re[(size_t) k] * re[(size_t) k] + (double) im[(size_t) k] * im[(size_t) k];
            all += e;
            if (std::abs (12.0 * std::log2 (fr / f0)) <= semis) near += e;
        }
    }
    return all > 0 ? near / all : 0;
}

int main()
{
    const double sr = 48000.0;

    // ---- FFT ----
    {
        pad::Fft f; f.init (1024);
        std::vector<float> re (1024), im (1024, 0.f), orig;
        for (int i = 0; i < 1024; ++i) re[(size_t) i] = std::sin (2.f * pad::kPi * 37.f * (float) i / 1024.f) + 0.3f * std::cos (2.f * pad::kPi * 211.f * (float) i / 1024.f);
        orig = re;
        f.transform (re.data(), im.data(), false);
        int peak = 0; float best = 0;
        for (int k = 1; k < 512; ++k) { const float m = std::hypot (re[(size_t) k], im[(size_t) k]); if (m > best) { best = m; peak = k; } }
        CHECK (peak == 37);
        f.transform (re.data(), im.data(), true);
        double err = 0; for (int i = 0; i < 1024; ++i) err = std::max (err, (double) std::abs (re[(size_t) i] / 1024.f - orig[(size_t) i]));
        CHECK (err < 1e-4);
    }

    // ---- level: white noise in => about the same level out once the pad is established ----
    {
        pad::SpectralPad sp; sp.prepare (sr);
        pad::PadParams p; p.evolve = 0.f; p.width = 1.f; p.riseSec = 0.2f; p.timeSec = 2.f;
        const int n = (int) (sr * 12);
        std::vector<float> in ((size_t) n), outL ((size_t) n), outR ((size_t) n);
        gg::Rng r (5);
        for (int i = 0; i < n; ++i) in[(size_t) i] = 0.2f * r.bi() * 1.7320508f; // rms 0.2
        for (int i = 0; i < n; ++i) sp.process (in[(size_t) i], in[(size_t) i], p, outL[(size_t) i], outR[(size_t) i]);
        const double rIn = rmsOf (in, (int) (sr * 6), n), rOut = rmsOf (outL, (int) (sr * 6), n);
        std::printf ("noise: in %.3f out %.3f (%.2f dB)\n", rIn, rOut, db (rOut / rIn));
        CHECK (db (rOut / rIn) > -1.0 && db (rOut / rIn) < 3.5);
    }

    // ---- level: a steady sine in => about the same level out ----
    {
        pad::SpectralPad sp; sp.prepare (sr);
        pad::PadParams p; p.evolve = 0.f; p.width = 1.f; p.riseSec = 0.2f; p.timeSec = 2.f;
        const int n = (int) (sr * 10);
        std::vector<float> in ((size_t) n), outL ((size_t) n), outR ((size_t) n);
        for (int i = 0; i < n; ++i) in[(size_t) i] = 0.3f * (float) std::sin (2.0 * 3.14159265358979 * 523.25 * (double) i / sr);
        for (int i = 0; i < n; ++i) sp.process (in[(size_t) i], in[(size_t) i], p, outL[(size_t) i], outR[(size_t) i]);
        const double rIn = rmsOf (in, (int) (sr * 5), n), rOut = rmsOf (outL, (int) (sr * 5), n);
        std::printf ("sine: in %.3f out %.3f (%.2f dB)\n", rIn, rOut, db (rOut / rIn));
        CHECK (std::abs (db (rOut / rIn)) < 1.0);
    }

    // ---- a note becomes a long pad that stays on the same pitch ----
    {
        pad::SpectralPad sp; sp.prepare (sr);
        pad::PadParams p; p.evolve = 0.f; p.riseSec = 0.3f; p.timeSec = 8.f; p.width = 0.5f;
        const int n = (int) (sr * 14);
        std::vector<float> outL ((size_t) n), outR ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            const float x = t < 0.6 ? 0.5f * (float) std::sin (2.0 * 3.14159265358979 * 440.0 * t) * (float) std::exp (-t * 3.0) : 0.f;   // plucked, 0.6 s
            sp.process (x, x, p, outL[(size_t) i], outR[(size_t) i]);
        }
        const double early = rmsOf (outL, (int) (sr * 1.0), (int) (sr * 2.0)), late = rmsOf (outL, (int) (sr * 6.0), (int) (sr * 7.0));
        const double near = energyNear (outL, (int) (sr * 1.0), (int) (sr * 6.0), sr, 440.0, 2.0);
        std::printf ("pluck: pad rms %.4f at 1-2 s, %.4f at 6-7 s (%.1f dB), %.0f%% of the energy within +-2 semitones of 440 Hz\n", early, late, db (late / early), near * 100.0);
        CHECK (early > 0.002);                 // the pad exists although the note lasted 0.6 s
        CHECK (db (late / early) > -14.0);     // and is still there seconds later
        CHECK (near > 0.6);                    // same pitch as the input
    }

    // ---- hold freezes, no hold decays ----
    {
        pad::PadParams p; p.evolve = 0.f; p.riseSec = 0.2f; p.timeSec = 1.f;
        double held = 0, free_ = 0;
        for (int mode = 0; mode < 2; ++mode)
        {
            pad::SpectralPad sp; sp.prepare (sr);
            const int n = (int) (sr * 14);
            std::vector<float> outL ((size_t) n), outR ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double t = (double) i / sr;
                p.hold = (mode == 0) && t > 3.0;           // freeze once the tone has been heard
                const float x = t < 3.0 ? 0.4f * (float) std::sin (2.0 * 3.14159265358979 * 330.0 * t) : 0.f;
                sp.process (x, x, p, outL[(size_t) i], outR[(size_t) i]);
            }
            const double a = rmsOf (outL, (int) (sr * 2.0), (int) (sr * 3.0)), b = rmsOf (outL, (int) (sr * 11.0), (int) (sr * 13.0));
            (mode == 0 ? held : free_) = db (b / a);
        }
        std::printf ("hold: %.1f dB after 9 s with Hold, %.1f dB without\n", held, free_);
        CHECK (held > -4.0);
        CHECK (free_ < -40.0);
    }

    // ---- layers: octave up moves the energy to 2 f, sub to f/2 ----
    {
        for (int layer = 0; layer < 2; ++layer)
        {
            pad::SpectralPad sp; sp.prepare (sr);
            pad::PadParams p; p.evolve = 0.f; p.riseSec = 0.2f; p.timeSec = 4.f; p.body = 0.f;
            if (layer == 0) p.octave = 1.f; else p.sub = 1.f;
            const int n = (int) (sr * 8);
            std::vector<float> outL ((size_t) n), outR ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double t = (double) i / sr;
                const float x = 0.4f * (float) std::sin (2.0 * 3.14159265358979 * 440.0 * t);
                sp.process (x, x, p, outL[(size_t) i], outR[(size_t) i]);
            }
            const double target = layer == 0 ? 880.0 : 220.0;
            const double near = energyNear (outL, (int) (sr * 3.0), (int) (sr * 8.0), sr, target, 2.0);
            std::printf ("layer %s: %.0f%% of the energy within +-2 semitones of %.0f Hz\n", layer == 0 ? "octave" : "sub", near * 100.0, target);
            CHECK (near > 0.55);
        }
    }

    // ---- stereo width: 0 = identical channels for a mono input, 1 = decorrelated ----
    {
        for (int w = 0; w < 2; ++w)
        {
            pad::SpectralPad sp; sp.prepare (sr);
            pad::PadParams p; p.evolve = 0.f; p.width = (float) w;
            const int n = (int) (sr * 6);
            std::vector<float> outL ((size_t) n), outR ((size_t) n);
            gg::Rng r (9);
            for (int i = 0; i < n; ++i) { const float x = 0.2f * r.bi(); sp.process (x, x, p, outL[(size_t) i], outR[(size_t) i]); }
            double cross = 0, el = 0, er = 0;
            for (int i = (int) (sr * 3); i < n; ++i) { cross += (double) outL[(size_t) i] * outR[(size_t) i]; el += (double) outL[(size_t) i] * outL[(size_t) i]; er += (double) outR[(size_t) i] * outR[(size_t) i]; }
            const double corr = cross / std::sqrt (el * er + 1e-20);
            std::printf ("width %d: L/R correlation %.2f\n", w, corr);
            if (w == 0) CHECK (corr > 0.98); else CHECK (std::abs (corr) < 0.3);
        }
    }

    // ---- evolve moves the spectrum but keeps the loudness steady; everything stays finite ----
    {
        pad::SpectralPad sp; sp.prepare (sr);
        pad::PadParams p; p.evolve = 1.f; p.evolveRate = 0.5f; p.tone = 0.5f; p.sub = 0.5f; p.fifth = 0.5f; p.octave = 0.5f;
        const int n = (int) (sr * 10);
        std::vector<float> outL ((size_t) n), outR ((size_t) n);
        gg::Rng r (3);
        double lo = 1e9, hi = 0;
        for (int i = 0; i < n; ++i)
        {
            const float x = 0.5f * r.bi();
            sp.process (x, x, p, outL[(size_t) i], outR[(size_t) i]);
            CHECK (std::isfinite (outL[(size_t) i]) && std::isfinite (outR[(size_t) i]));
        }
        for (int s = 4; s < 9; ++s) { const double v = rmsOf (outL, (int) (sr * s), (int) (sr * (s + 1))); lo = std::min (lo, v); hi = std::max (hi, v); }
        std::printf ("evolve: loudness varies by %.1f dB between seconds\n", db (hi / lo));
        CHECK (db (hi / lo) < 3.0);
        CHECK (hi < 2.0);
    }

    // ---- reverb: bounded and decays ----
    {
        pad::Reverb rv; rv.prepare (sr); rv.setSize (1.f);
        double peak = 0, tail = 0;
        for (int i = 0; i < (int) (sr * 30); ++i)
        {
            const float x = i < 4800 ? 0.8f * std::sin (0.05f * (float) i) : 0.f;
            float l, r; rv.process (x, x, l, r);
            const double m = std::max (std::abs (l), std::abs (r));
            peak = std::max (peak, m);
            if (i > (int) (sr * 25)) tail = std::max (tail, m);
        }
        std::printf ("reverb: peak %.3f, tail after 25 s %.5f (%.1f dB)\n", peak, tail, db (tail / peak));
        CHECK (tail < peak * 0.01);
    }

    // ---- modulation matrix ----
    {
        gg::Matrix m;
        float src[gg::kNumSources] = {};
        float off[gg::kNumDests];
        const int dTime = gg::destIndexOf ("time"), dRise = gg::destIndexOf ("rise"), dLevel = gg::destIndexOf ("level"), dMix = gg::destIndexOf ("mix");
        CHECK (dTime == 1 && dRise == 2 && gg::destIndexOf ("nope") == 0 && dLevel == gg::kNumDests);

        gg::accumulate (src, m, off);
        for (int d = 0; d < gg::kNumDests; ++d) CHECK (off[d] == 0.f);

        m.amt[0][dTime - 1] = 0.5f;  m.amt[1][dTime - 1] = 0.25f;  m.amt[0][dRise - 1] = -0.25f;
        m.amt[gg::kSrcLfo2][dLevel - 1] = 0.1f;  m.amt[gg::kSrcSeq][dRise - 1] = 0.4f;
        src[0] = 1.f; src[1] = 0.5f; src[gg::kSrcLfo2] = -1.f; src[gg::kSrcSeq] = 0.5f;
        gg::accumulate (src, m, off);
        CHECK (std::abs (off[dTime - 1] - 0.625f) < 1e-6f);
        CHECK (std::abs (off[dRise - 1] - (-0.25f + 0.2f)) < 1e-6f);
        CHECK (std::abs (off[dLevel - 1] + 0.1f) < 1e-6f);

        const auto dm = gg::defaultMatrix();
        for (int sIdx = 0; sIdx < gg::kNumSources; ++sIdx)
            for (int d = 0; d < gg::kNumDests; ++d) CHECK (dm.amt[sIdx][d] == 0.f);

        for (uint32_t seed = 1; seed <= 300; ++seed)
        {
            gg::Rng rr (seed * 7919u);
            gg::Matrix rm;
            rm.amt[gg::kSrcLfo1][3] = 0.33f;
            gg::randomAssignMacros (rr, rm);
            CHECK (rm.amt[gg::kSrcLfo1][3] == 0.33f);
            for (int s = 0; s < gg::kNumMacros; ++s)
            {
                int n = 0;
                for (int d = 0; d < gg::kNumDests; ++d)
                {
                    const float a = rm.amt[s][d];
                    if (a == 0.f) continue;
                    ++n;
                    CHECK (d != dLevel - 1 && d != dMix - 1);
                    CHECK (std::abs (a) >= 0.01f && std::abs (a) <= 0.7f + 1e-4f);
                }
                CHECK (n >= 2 && n <= 4);
            }
        }
    }

    // ---- LFO and euclidean sequencer ----
    {
        gg::Lfo lfo; lfo.prepare (sr, 3u);
        int cross = 0; float prev = 0.f, mn = 9.f, mx = -9.f;
        for (int i = 0; i < (int) sr; ++i)
        {
            const float v = lfo.advance (1, 5.f, 0, 120.0, 0);
            if (prev < 0.f && v >= 0.f) ++cross;
            prev = v; mn = std::min (mn, v); mx = std::max (mx, v);
        }
        CHECK (cross >= 4 && cross <= 6);
        CHECK (mn >= -1.0001f && mx <= 1.0001f && mx > 0.99f && mn < -0.99f);
        for (int shape = 0; shape < 5; ++shape)
        {
            gg::Lfo l2; l2.prepare (sr, 9u);
            for (int i = 0; i < 2000; ++i) { const float v = l2.advance (64, 7.3f, 0, 120.0, shape); CHECK (v >= -1.0001f && v <= 1.0001f); }
        }
        for (int steps = 1; steps <= 16; ++steps)
            for (int hits = 0; hits <= steps; ++hits)
                for (int rot = 0; rot < steps; ++rot)
                {
                    int n = 0;
                    for (int i = 0; i < steps; ++i) n += gg::euclidHit (i, steps, hits, rot);
                    CHECK (n == hits);
                }
        gg::EuclidSeq sq; sq.prepare (sr, 5u);
        gg::SeqParams sp; sp.steps = 4; sp.hits = 1; sp.rateDiv = 2; sp.decaySec = 0.1f; sp.bpm = 120;
        float top = 0.f; int fired = 0; float pv = 0.f;
        for (int blk = 0; blk < 3000; ++blk)
        {
            const float v = sq.advance (128, sp);
            CHECK (v >= 0.f && v <= 1.0001f);
            top = std::max (top, v);
            if (v > pv + 0.3f) ++fired;
            pv = v;
        }
        CHECK (top > 0.5f && fired >= 12 && fired <= 20);
    }

    // ---- output limiter ----
    CHECK (gg::softClip (0.5f) == 0.5f && gg::softClip (-0.79f) == -0.79f);
    CHECK (gg::softClip (5.f) <= 1.0f && gg::softClip (-5.f) >= -1.0f);

    // ---- factory presets ----
    {
        const auto& list = pad::presets();
        CHECK (list.size() >= 12);
        CHECK (std::string (list[0].name) == "Init" && list[0].params.empty() && list[0].mods.empty());

        // every parameter id a preset sets must exist in the plugin, with a value inside that parameter's range
        struct Known { const char* id; float lo, hi; };
        static const Known known[] = {
            { "time", 0.1f, 60.f }, { "rise", 0.02f, 10.f }, { "sub", 0.f, 1.f }, { "body", 0.f, 1.f }, { "fifth", 0.f, 1.f },
            { "octave", 0.f, 1.f }, { "evolve", 0.f, 1.f }, { "rate", 0.01f, 1.f }, { "tone", -1.f, 1.f }, { "width", 0.f, 1.f },
            { "revSize", 0.f, 1.f }, { "revMix", 0.f, 1.f }, { "mix", 0.f, 1.f }, { "level", -48.f, 6.f },
            { "lfo1Rate", 0.02f, 20.f }, { "lfo1Sync", 0.f, 5.f }, { "lfo1Shape", 0.f, 4.f },
            { "lfo2Rate", 0.02f, 20.f }, { "lfo2Sync", 0.f, 5.f }, { "lfo2Shape", 0.f, 4.f },
            { "seqSteps", 1.f, 16.f }, { "seqHits", 0.f, 16.f }, { "seqRot", 0.f, 15.f }, { "seqRate", 0.f, 6.f },
            { "seqDecay", 0.01f, 2.f }, { "seqRandom", 0.f, 1.f },
        };
        std::set<std::string> names;
        for (const auto& pr : list)
        {
            CHECK (names.insert (pr.name).second);                      // unique names
            for (const auto& p : pr.params)
            {
                bool found = false;
                for (const auto& k : known)
                    if (std::string (k.id) == p.id) { found = true; CHECK (p.value >= k.lo && p.value <= k.hi); }
                CHECK (found);
            }
            for (const auto& m : pr.mods)
            {
                CHECK (m.src >= 0 && m.src < gg::kNumSources);
                CHECK (gg::destIndexOf (m.dest) > 0);
                CHECK (std::abs (m.amount) > 0.f && std::abs (m.amount) <= 1.f);
            }
        }
        // the six shared macros are on every preset except Init, and no preset touches hold
        for (size_t i = 1; i < list.size(); ++i)
        {
            int macroCells = 0;
            for (const auto& m : list[i].mods) macroCells += m.src < gg::kNumMacros;
            CHECK (macroCells == 11);
            for (const auto& p : list[i].params) CHECK (std::string (p.id) != "hold");
        }
    }

    std::printf (fails == 0 ? "ALL TESTS PASSED\n" : "%d FAILURES\n", fails);
    return fails == 0 ? 0 : 1;
}
