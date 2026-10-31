// JUCE-free sanity test of the granular engine + glitch/stutter FX.
#include "../Source/Dsp/GlitchFx.h"
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

    std::printf (fails == 0 ? "ALL TESTS PASSED\n" : "%d FAILURES\n", fails);
    return fails == 0 ? 0 : 1;
}
