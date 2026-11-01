// JUCE-free sanity test of the granular engine + glitch/stutter FX.
#include "../Source/Dsp/AmbientFx.h"
#include "../Source/Dsp/GlitchFx.h"
#include "../Source/Dsp/NoteGen.h"
#include <set>
#include <cstdio>
#include <cstdlib>

static int fails = 0;
#define CHECK(c) do { if (! (c)) { std::printf ("FAIL: %s (line %d)\n", #c, __LINE__); ++fails; } } while (0)

static double rms (const std::vector<float>& v)
{
    double s = 0; for (float x : v) s += (double) x * x;
    return std::sqrt (s / (double) v.size());
}
static bool finite (const std::vector<float>& v)
{
    for (float x : v) if (! std::isfinite (x) || std::abs (x) > 50.f) return false;
    return true;
}

int main()
{
    const double sr = 48000.0;
    const int n = (int) (sr * 4);
    auto src = gg::SourceBuffer::makeDefault (sr);
    CHECK (src->size() == (int) (sr * 4));

    gg::GranularEngine eng; eng.prepare (sr);
    gg::GrainParams gp;
    std::vector<float> L ((size_t) n, 0.f), R ((size_t) n, 0.f);

    // silence without notes
    eng.render (L.data(), R.data(), n, *src, gp);
    CHECK (rms (L) == 0.0);

    // chord, then release; also reverse + pitch spray + spray extremes
    gp.reverse = 0.5f; gp.pitchSpray = 12.f; gp.spray = 1.f; gp.density = 200.f; gp.sizeMs = 500.f; gp.shape = 1.f;
    eng.noteOn (48, 1.f); eng.noteOn (60, 0.8f); eng.noteOn (67, 0.8f);
    eng.render (L.data(), R.data(), n / 2, *src, gp);
    eng.noteOff (48); eng.noteOff (60); eng.noteOff (67);
    eng.render (L.data() + n / 2, R.data() + n / 2, n / 2, *src, gp);
    CHECK (finite (L) && finite (R));
    CHECK (rms (L) > 0.001);

    // voice stealing: 20 note-ons on 8 voices must not crash
    for (int i = 0; i < 20; ++i) eng.noteOn (40 + i, 1.f);
    std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
    eng.render (L.data(), R.data(), n, *src, gp);
    CHECK (finite (L) && finite (R));

    // envelope releases to silence
    eng.allNotesOff();
    std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
    eng.render (L.data(), R.data(), 1000, *src, gp);
    CHECK (rms (L) == 0.0);

    // tiny source must not crash
    auto tiny = std::make_shared<gg::SourceBuffer>();
    tiny->l = { 0.f, 0.5f }; tiny->r = { 0.f, 0.5f };
    gg::GranularEngine e2; e2.prepare (sr); e2.noteOn (60, 1.f);
    e2.render (L.data(), R.data(), 5000, *tiny, gp);
    CHECK (finite (L));

    // FX: bypass with zero amounts is transparent
    gg::GlitchFx fx; fx.prepare (sr);
    gg::FxParams fp;
    std::vector<float> dryL ((size_t) n), dryR ((size_t) n);
    eng.noteOn (60, 1.f); gp = gg::GrainParams{};
    std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
    eng.render (L.data(), R.data(), n, *src, gp);
    dryL = L; dryR = R;
    for (int i = 0; i < n; ++i) fx.process (L[(size_t) i], R[(size_t) i], fp);
    double maxDiff = 0; for (int i = 0; i < n; ++i) maxDiff = std::max (maxDiff, (double) std::abs (L[(size_t) i] - dryL[(size_t) i]));
    CHECK (maxDiff < 1e-6);

    // FX: full stutter + glitch at every rate stays finite and actually changes the signal
    for (int div = 0; div <= 6; ++div)
        for (double bpm : { 40.0, 120.0, 300.0 })
        {
            gg::GlitchFx f; f.prepare (sr, 7u + (uint32_t) div);
            gg::FxParams q; q.stutterAmount = 1.f; q.glitchAmount = 1.f; q.stutterDiv = div; q.glitchDiv = div; q.bpm = bpm; q.stutterPitch = 7.f;
            std::vector<float> a = dryL, b = dryR;
            for (int i = 0; i < n; ++i) f.process (a[(size_t) i], b[(size_t) i], q);
            CHECK (finite (a) && finite (b));
            double diff = 0; for (int i = 0; i < n; ++i) diff += std::abs (a[(size_t) i] - dryL[(size_t) i]);
            CHECK (diff > 1.0);
        }


    // ---- ambient FX ----
    {
        gg::AmbientFx a; a.prepare (sr);
        gg::AmbientParams ap; ap.tape = 1.f; ap.delayMix = 0.5f; ap.delayFeedback = 0.9f; ap.reverbMix = 1.f; ap.reverbSize = 1.f; ap.shimmer = 1.f;
        std::vector<float> xl ((size_t) n), xr ((size_t) n);
        xl[0] = 1.f; xr[0] = 1.f; // impulse, then silence: the tail must stay bounded and decay
        for (int i = 0; i < n; ++i) a.process (xl[(size_t) i], xr[(size_t) i], ap);
        CHECK (finite (xl) && finite (xr));
        CHECK (rms (xl) > 1e-5);
        double early = 0, late = 0;
        for (int i = 0; i < n / 4; ++i) early += std::abs (xl[(size_t) i]);
        for (int i = 3 * n / 4; i < n; ++i) late += std::abs (xl[(size_t) i]);
        CHECK (late < early);

        // sustained loud input with every ambient control maxed must stay bounded
        gg::AmbientFx hot; hot.prepare (sr);
        double peak = 0;
        for (int i = 0; i < (int) sr * 10; ++i)
        {
            float l = 0.8f * std::sin (0.03f * (float) i) + 0.2f * std::sin (0.17f * (float) i), r = l;
            hot.process (l, r, ap);
            peak = std::max (peak, (double) std::max (std::abs (l), std::abs (r)));
        }
        std::printf ("ambient max-settings peak: %.2f\n", peak);
        CHECK (peak < 20.0);

        // everything at zero => transparent
        gg::AmbientFx b; b.prepare (sr);
        gg::AmbientParams z; z.reverbMix = 0.f; z.shimmer = 0.f;
        double md = 0;
        for (int i = 0; i < 20000; ++i)
        {
            float l = std::sin (0.05f * (float) i), r = l, l0 = l;
            b.process (l, r, z);
            md = std::max (md, (double) std::abs (l - l0));
        }
        CHECK (md < 1e-6);

        // octave-up shifter: a 500 Hz sine should come out near 1000 Hz (zero-crossing rate)
        gg::OctaveUp sh; sh.prepare (sr);
        int crossings = 0; float prev = 0;
        const int m = (int) sr;
        for (int i = 0; i < m; ++i)
        {
            float y = sh.process (std::sin (2.f * gg::kPi * 500.f * (float) i / (float) sr));
            if (i > 4000 && prev < 0.f && y >= 0.f) ++crossings;
            prev = y;
        }
        const double hz = crossings * (double) m / (double) (m - 4000);
        std::printf ("octave-up: 500 Hz -> ~%.0f Hz\n", hz);
        CHECK (hz > 900 && hz < 1100);
    }

    // ---- note generator ----
    {
        gg::NoteGen g; g.prepare (sr);
        gg::GenParams gp2; gp2.probability = 1.f; gp2.scale = 2; gp2.root = 2; gp2.rateDiv = 3; gp2.length = 1.5f; gp2.bpm = 120;
        const int* iv = nullptr; const int nd = gg::scaleIntervals (gp2.scale, iv);
        std::set<int> allowed; for (int o = 0; o < 4; ++o) for (int i = 0; i < nd; ++i) allowed.insert (48 + gp2.root + o * 12 + iv[i]);
        int ons = 0, offs = 0, held[128] = {};
        gg::NoteEvent ev[256];
        for (int blk = 0; blk < 4000; ++blk)
        {
            const int c = g.generate (512, gp2, ev, 256);
            for (int i = 0; i < c; ++i)
            {
                CHECK (ev[i].offset >= 0 && ev[i].offset < 512);
                if (ev[i].on) { ++ons; ++held[ev[i].note]; CHECK (allowed.count (ev[i].note) == 1); CHECK (ev[i].velocity > 0.f && ev[i].velocity <= 1.f); }
                else ++offs, --held[ev[i].note];
            }
        }
        // 4000*512 samples at 120 bpm, 1/8 steps (0.25 s) => ~170 notes
        std::printf ("notegen: %d on, %d off\n", ons, offs);
        CHECK (ons > 150 && ons < 190);
        CHECK (ons - offs >= 0 && ons - offs <= gg::NoteGen::maxPending);

        // scale taken from MIDI: only the played pitch classes (C, E, G) may come out
        gg::NoteGen gm; gm.prepare (sr);
        gg::GenParams mp; mp.probability = 1.f; mp.scale = 6; mp.scaleMask = (1u << 0) | (1u << 4) | (1u << 7); mp.rateDiv = 3; mp.bpm = 120;
        int mOn = 0;
        for (int blk = 0; blk < 2000; ++blk)
        {
            const int c = gm.generate (512, mp, ev, 256);
            for (int i = 0; i < c; ++i)
                if (ev[i].on) { ++mOn; const int pc = ev[i].note % 12; CHECK (pc == 0 || pc == 4 || pc == 7); }
        }
        CHECK (mOn > 50);
        // nothing played yet (mask 0): falls back to a normal scale instead of staying silent
        gg::NoteGen gf; gf.prepare (sr);
        mp.scaleMask = 0;
        int fOn = 0;
        for (int blk = 0; blk < 2000; ++blk) { const int c = gf.generate (512, mp, ev, 256); for (int i = 0; i < c; ++i) fOn += ev[i].on; }
        CHECK (fOn > 50);

        gg::NoteGen g0; g0.prepare (sr);
        gp2.probability = 0.f;
        int silent = 0;
        for (int blk = 0; blk < 200; ++blk) silent += g0.generate (512, gp2, ev, 256);
        CHECK (silent == 0);
    }

    // ---- anti-residual-sound guarantees ----
    {
        // 1) stutter/glitch fully off while no key is held, even at 100 %
        gg::GlitchFx f; f.prepare (sr, 99u);
        gg::FxParams q; q.stutterAmount = 1.f; q.glitchAmount = 1.f; q.glitchDiv = 3; q.stutterDiv = 2; q.active = false;
        double md2 = 0;
        for (int i = 0; i < (int) sr * 3; ++i)
        {
            float l = std::sin (0.04f * (float) i), r = l, l0 = l;
            f.process (l, r, q);
            md2 = std::max (md2, (double) std::abs (l - l0));
        }
        CHECK (md2 < 1e-6);

        // 2) active for 2 s, then keys released: dry again within a few ms
        gg::GlitchFx g2; g2.prepare (sr, 5u);
        q.active = true;
        for (int i = 0; i < (int) sr * 2; ++i) { float l = std::sin (0.04f * (float) i), r = l; g2.process (l, r, q); }
        q.active = false;
        double after = 0;
        for (int i = 0; i < 2000; ++i)
        {
            float l = std::sin (0.04f * (float) i), r = l, l0 = l;
            g2.process (l, r, q);
            if (i > 1500) after = std::max (after, (double) std::abs (l - l0));
        }
        CHECK (after < 1e-3);

        // 3) tape with silence in must give exact silence out (no hiss)
        gg::AmbientFx a; a.prepare (sr);
        gg::AmbientParams tp; tp.tape = 1.f; tp.delayMix = 0.f; tp.reverbMix = 0.f; tp.shimmer = 0.f;
        double hiss = 0;
        for (int i = 0; i < (int) sr; ++i) { float l = 0.f, r = 0.f; a.process (l, r, tp); hiss = std::max (hiss, (double) std::max (std::abs (l), std::abs (r))); }
        CHECK (hiss < 1e-9);

        // 4) longest reverb (+ shimmer): after a burst, the tail is inaudible well within 12 s
        gg::AmbientFx rv; rv.prepare (sr);
        gg::AmbientParams rp2; rp2.reverbSize = 1.f; rp2.reverbMix = 1.f; rp2.shimmer = 1.f; rp2.delayMix = 0.f;
        double pk = 0, tailPk = 0;
        for (int i = 0; i < (int) sr * 12; ++i)
        {
            float l = i < 4800 ? std::sin (0.05f * (float) i) * 0.8f : 0.f, r = l;
            rv.process (l, r, rp2);
            const double m = std::max (std::abs (l), std::abs (r));
            pk = std::max (pk, m);
            if (i > (int) sr * 10) tailPk = std::max (tailPk, m);
        }
        std::printf ("reverb: peak %.3f, tail after 10 s %.6f (%.1f dB)\n", pk, tailPk, 20.0 * std::log10 (tailPk / pk + 1e-12));
        CHECK (tailPk < pk * 0.003); // below -50 dB
    }

    std::printf (fails == 0 ? "ALL TESTS PASSED\n" : "%d FAILURES\n", fails);
    return fails == 0 ? 0 : 1;
}
