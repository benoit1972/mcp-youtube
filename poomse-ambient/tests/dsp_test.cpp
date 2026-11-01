// JUCE-free tests of the generative engine.
#include "../Source/Dsp/Engine.h"
#include "../Source/Dsp/Reverb.h"
#include "../Source/Dsp/Util.h"
#include <cstdio>
#include <set>
#include <string>

static int fails = 0;
#define CHECK(c) do { if (! (c)) { std::printf ("FAIL: %s (line %d)\n", #c, __LINE__); ++fails; } } while (0)

static std::array<amb::LayerParams, amb::kNumLayers> defaultLayers()
{
    std::array<amb::LayerParams, amb::kNumLayers> L;
    const float dens[] = { 0.6f, 0.5f, 0.6f, 0.45f, 0.5f, 1.f };
    const int root[] = { 60, 60, 60, 72, 60, 48 };
    for (int i = 0; i < amb::kNumLayers; ++i) { L[(size_t) i].density = dens[i]; L[(size_t) i].root = root[i]; }
    return L;
}

struct Piece { std::vector<amb::Event> events; std::vector<amb::Conductor::Scene> scenes; };

// run the conductor over a whole piece and collect every event and every scene
static Piece runPiece (uint32_t seed, int bars, const amb::GlobalParams& g, int tonic = 48)
{
    auto layers = defaultLayers();
    amb::Conductor c;
    c.start (0.0, seed, tonic, 1u << (tonic % 12), g, layers.data());
    Piece p;
    amb::Event buf[amb::Conductor::maxEvents];
    const double total = (double) bars * g.beatsPerBar, step = 0.25;
    for (double b = 0.0; b < total; b += step)
    {
        const int n = c.advance (b, b + step, g, layers.data(), buf, amb::Conductor::maxEvents);
        for (int i = 0; i < n; ++i) p.events.push_back (buf[i]);
    }
    for (int i = 0; i < c.numScenes(); ++i) p.scenes.push_back (c.scene (i));
    return p;
}
static bool inScale (int note, int tonic, int scale)
{
    const int* iv; const int nDeg = amb::scaleIntervals (scale, iv);
    const int rel = ((note - tonic) % 12 + 12) % 12;
    for (int i = 0; i < nDeg; ++i) if (iv[i] == rel) return true;
    return false;
}

int main()
{
    amb::GlobalParams g;
    g.pieceBars = 120;

    // ---- scales ----
    for (int sc = 0; sc < 8; ++sc)
        for (int d = -12; d < 25; ++d) CHECK (inScale (amb::noteAtDegree (48, sc, d), 48, sc));

    // ---- the form: arc and layer entries ----
    CHECK (amb::arcShape (0.f) > 0.15f && amb::arcShape (0.f) < 0.25f && amb::arcShape (0.5f) > 0.99f && amb::arcShape (1.f) < 0.3f);
    CHECK (amb::arcDensity (0.0f, 0.f) == 1.f && amb::arcDensity (0.3f, 0.f) == 1.f);
    for (int l = 0; l < amb::kNumLayers; ++l)
        for (float p = 0.f; p <= 1.f; p += 0.05f) { const float e = amb::layerEntry (l, p, 1.f); CHECK (e >= 0.f && e <= 1.0001f); CHECK (amb::layerEntry (l, p, 0.f) == 1.f); }
    CHECK (amb::layerEntry (amb::Beat, 0.05f, 1.f) < 0.05f && amb::layerEntry (amb::Beat, 0.55f, 1.f) > 0.95f);

    // ---- a whole 120-bar piece ----
    const auto piece = runPiece (7, 120, g);
    std::printf ("piece: %zu events, %zu scenes over 120 bars\n", piece.events.size(), piece.scenes.size());
    CHECK (piece.events.size() > 80);
    CHECK (piece.scenes.size() >= 6 && piece.scenes.size() < 40);
    CHECK (piece.scenes.front().mood == amb::Light);                       // a calm start
    for (const auto& e : piece.events) if (e.beat > 0.95 * 480.0) CHECK (e.layer != amb::Beat);   // and a calm end: no beat
    int perLayer[amb::kNumLayers] = {};
    for (const auto& e : piece.events)
    {
        ++perLayer[e.layer];
        CHECK (e.beat >= 0.0 && e.beat < 520.0 && e.velocity > 0.f && e.velocity <= 1.0f && e.pan >= -1.f && e.pan <= 1.f);
        if (e.layer == amb::Atmos) CHECK (e.note < 0);
        if (e.layer == amb::Sub) CHECK (e.note >= 24 && e.note <= 52 && inScale (e.note, 48, g.scale));
    }
    for (int l = 0; l < amb::kNumLayers; ++l) { std::printf ("  %-6s %3d events\n", amb::kLayerNames[l], perLayer[l]); CHECK (perLayer[l] > 0); }

    // no wrong notes: with surprise off, every pitched note stays in the scale
    {
        amb::GlobalParams calm = g; calm.surprise = 0.f;
        for (uint32_t seed = 1; seed <= 12; ++seed)
            for (const auto& e : runPiece (seed, 80, calm).events)
                if (e.layer == amb::Keys || e.layer == amb::Vocals || e.layer == amb::Pad || e.layer == amb::Sub) CHECK (inScale (e.note, 48, calm.scale));
    }

    // ---- the moods: depth makes deep scenes, space makes silence ----
    {
        auto deepShare = [&] (float depth)
        {
            int deep = 0, total = 0;
            for (uint32_t seed = 1; seed <= 20; ++seed)
            {
                amb::GlobalParams q = g; q.depth = depth;
                for (const auto& s : runPiece (seed, 120, q).scenes) { ++total; deep += s.mood == amb::Deep; }
            }
            return (double) deep / total;
        };
        const double lo = deepShare (0.f), hi = deepShare (1.f);
        std::printf ("share of deep scenes: depth 0 -> %.0f%%, depth 1 -> %.0f%%\n", lo * 100, hi * 100);
        CHECK (hi > lo + 0.25);

        auto events = [&] (float space)
        {
            long n = 0;
            for (uint32_t seed = 1; seed <= 12; ++seed) { amb::GlobalParams q = g; q.space = space; n += (long) runPiece (seed, 120, q).events.size(); }
            return n / 12;
        };
        const long busy = events (0.f), airy = events (1.f);
        std::printf ("events per piece: space 0 -> %ld, space 1 -> %ld\n", busy, airy);
        CHECK (airy < busy * 0.75);

        // all three moods appear in a normal piece (contrast: light, deep and pauses)
        std::set<int> moods;
        for (uint32_t seed = 1; seed <= 6; ++seed) for (const auto& s : runPiece (seed, 120, g).scenes) moods.insert (s.mood);
        CHECK (moods.size() == 3);
        // and a scene is not followed by the same mood very often
        int same = 0, total = 0;
        for (uint32_t seed = 1; seed <= 20; ++seed) { const auto sc = runPiece (seed, 120, g).scenes; for (size_t i = 1; i < sc.size(); ++i) { ++total; same += sc[i].mood == sc[i - 1].mood; } }
        std::printf ("a scene repeats the previous mood %d%% of the time\n", 100 * same / total);
        CHECK (same < total * 0.45);
    }

    // ---- surprise: the unexpected exists only when asked for ----
    {
        int foreign[2] = {}, bellsFast[2] = {};
        for (int sur = 0; sur < 2; ++sur)
            for (uint32_t seed = 1; seed <= 30; ++seed)
            {
                amb::GlobalParams q = g; q.surprise = (float) sur;
                const auto pc = runPiece (seed, 120, q);
                for (const auto& e : pc.events) if ((e.layer == amb::Keys) && ! inScale (e.note, 48, q.scale)) ++foreign[sur];
                for (size_t i = 1; i < pc.events.size(); ++i)   // a ghost-vocal phrase: voices 0.75 beat apart
                    if (pc.events[i].layer == amb::Vocals && pc.events[i - 1].layer == amb::Vocals && std::abs (pc.events[i].beat - pc.events[i - 1].beat - 0.75) < 0.02) ++bellsFast[sur];
            }
        std::printf ("surprises over 30 pieces: foreign notes %d -> %d, vocal phrases %d -> %d\n", foreign[0], foreign[1], bellsFast[0], bellsFast[1]);
        CHECK (foreign[0] == 0 && foreign[1] > 0);
        CHECK (bellsFast[1] > bellsFast[0]);
    }

    // ---- the arc: the middle of the piece is fuller than the start and the end; arc 0 is flat ----
    {
        auto thirds = [&] (float journey, long* t)
        {
            for (uint32_t seed = 1; seed <= 24; ++seed)
            {
                amb::GlobalParams q = g; q.journey = journey; q.surprise = 0.f;
                for (const auto& e : runPiece (seed, 120, q).events) if (e.layer == amb::Keys || e.layer == amb::Vocals || e.layer == amb::Pad) ++t[std::min (2, (int) (e.beat / 160.0))];
            }
        };
        long a[3] = {}, f[3] = {};
        thirds (1.f, a); thirds (0.f, f);
        std::printf ("events per third, full arc: %ld / %ld / %ld ; no arc: %ld / %ld / %ld\n", a[0], a[1], a[2], f[0], f[1], f[2]);
        CHECK (a[1] > a[0] * 1.2 && a[1] > a[2] * 1.2);
        CHECK (std::abs (f[0] - f[1]) < 0.3 * f[1] && std::abs (f[2] - f[1]) < 0.3 * f[1]);
    }

    // ---- seed: same seed = same piece, another seed = another piece ----
    {
        const auto a = runPiece (123, 60, g), b = runPiece (123, 60, g), c = runPiece (124, 60, g);
        bool same = a.events.size() == b.events.size();
        for (size_t i = 0; same && i < a.events.size(); ++i) same = a.events[i].note == b.events[i].note && a.events[i].layer == b.events[i].layer && a.events[i].beat == b.events[i].beat;
        CHECK (same);
        bool diff = a.events.size() != c.events.size();
        for (size_t i = 0; ! diff && i < std::min (a.events.size(), c.events.size()); ++i) diff = a.events[i].note != c.events[i].note || a.events[i].beat != c.events[i].beat;
        CHECK (diff);
    }

    // ---- it never repeats: no run of notes occurs twice, and the layers keep drifting apart ----
    {
        std::vector<int> seq;
        for (const auto& e : piece.events) if (e.layer == amb::Keys || e.layer == amb::Vocals) seq.push_back (e.note * 8 + e.layer);
        bool repeats = false;
        const size_t win = 6;
        for (size_t i = 0; i + win <= seq.size() && ! repeats; ++i)
            for (size_t j = i + 1; j + win <= seq.size() && ! repeats; ++j)
                repeats = std::equal (seq.begin() + (long) i, seq.begin() + (long) (i + win), seq.begin() + (long) j);
        CHECK (! repeats);
        // where the ghost voices fall inside each keys loop keeps changing
        std::set<int> phases;
        for (const auto& e : piece.events) if (e.layer == amb::Vocals) phases.insert ((int) (std::fmod (e.beat, amb::kLayerConfig[amb::Keys].periodBeats) * 4));
        std::printf ("vocal positions inside the keys loop: %zu different\n", phases.size());
        CHECK (phases.size() >= 5);
    }

    // ---- the tonic is the clip's note: another key, other notes ----
    {
        amb::GlobalParams q = g; q.surprise = 0.f;
        for (const auto& e : runPiece (9, 40, q, 53).events)
            if (e.layer == amb::Keys || e.layer == amb::Vocals || e.layer == amb::Pad || e.layer == amb::Sub) CHECK (inScale (e.note, 53, q.scale));
    }


    // ---- Clean style: the same structure, but the beat slot becomes sparse bells in the scale ----
    {
        amb::GlobalParams clean = g; clean.night = false; clean.surprise = 0.f;
        int bells = 0, grid = 0;
        for (uint32_t seed = 1; seed <= 12; ++seed)
        {
            const auto pc = runPiece (seed, 120, clean);
            for (size_t i = 0; i < pc.events.size(); ++i)
            {
                const auto& e = pc.events[i];
                if (e.layer != amb::Beat) continue;
                ++bells;
                CHECK (inScale (e.note, 48, clean.scale) && e.note >= 48);                // pitched, in the scale, above the tonic
                if (i > 0 && pc.events[i - 1].layer == amb::Beat && std::abs (e.beat - pc.events[i - 1].beat) < 0.3) ++grid;   // never a 16th-note grid
            }
        }
        std::printf ("clean style: %d bell notes over 12 pieces, %d of them on a drum grid\n", bells, grid);
        CHECK (bells > 20 && grid == 0);
        // the night style does have the grid
        amb::GlobalParams night = g; night.night = true; night.surprise = 0.f;
        int hits = 0; for (const auto& e : runPiece (3, 120, night).events) hits += e.layer == amb::Beat;
        CHECK (hits > 20);
        // clean defaults are in tune too (the choir has strong formants and no clear fundamental: it keeps its default root)
        for (int l : { amb::Keys, amb::Pad, amb::Beat })
        {
            auto d = amb::SourceBuffer::makeDefault (l, 44100.0, false);
            CHECK (d->size() > 1000);
            CHECK (amb::detectRootNote (d->l, d->sampleRate, -100) == (l == amb::Beat ? 72 : 60));
        }
    }

    // ---- granular cloud: it blooms from the music, stays bounded, and dies away ----
    {
        const double sr = 48000.0;
        amb::Cloud cl; cl.prepare (sr);
        double early = 0, late = 0, peak = 0;
        for (int i = 0; i < (int) (sr * 40); ++i)
        {
            const double t = (double) i / sr;
            const float x = t < 3.0 ? 0.4f * (float) std::sin (2.0 * 3.14159265358979 * 440.0 * t) * (float) std::exp (-std::fmod (t, 1.0) * 3.0) : 0.f;
            float l, r; cl.process (x, x, 0.8f, l, r);
            CHECK (std::isfinite (l) && std::isfinite (r));
            peak = std::max (peak, (double) std::max (std::abs (l), std::abs (r)));
            if (t > 3.0 && t < 6.0) early += (double) l * l;
            if (t > 20.0 && t < 40.0) late += (double) l * l;
        }
        std::printf ("cloud: energy 3-6 s %.4f, 20-40 s %.6f, peak %.2f\n", early, late, peak);
        CHECK (early > 1e-3);          // the halo is there after the notes
        CHECK (late < 1e-9);           // and it is gone once the 6 s memory has passed
        CHECK (peak < 3.0);
        amb::Cloud off; off.prepare (sr);
        double e0 = 0; for (int i = 0; i < (int) (sr * 6); ++i) { float l, r; off.process (0.3f, 0.3f, 0.f, l, r); e0 += (double) l * l; }
        CHECK (e0 < 1e-9);             // amount 0: nothing
    }

    // ---- pitch detection of a dropped sample ----
    {
        const double sr = 44100.0;
        auto tone = [&] (double f, int harmonics)
        {
            std::vector<float> v ((size_t) (sr * 3));
            for (size_t i = 0; i < v.size(); ++i)
            {
                double s = 0; for (int k = 1; k <= harmonics; ++k) s += std::sin (2.0 * 3.14159265358979 * f * k * (double) i / sr) / k;
                v[i] = (float) (0.3 * s * std::exp (-(double) i / sr * 1.2));
            }
            return v;
        };
        CHECK (amb::detectRootNote (tone (440.0, 1), sr, -1) == 69);
        CHECK (amb::detectRootNote (tone (261.63, 6), sr, -1) == 60);
        CHECK (amb::detectRootNote (tone (130.81, 8), sr, -1) == 48);
        CHECK (amb::detectRootNote (tone (523.25, 4), sr, -1) == 72);
        std::vector<float> noise ((size_t) (sr * 3)); gg::Rng nz (5); for (auto& x : noise) x = 0.3f * nz.bi();
        CHECK (amb::detectRootNote (noise, sr, 60) == 60);               // no pitch: keeps the default
        std::vector<float> silent ((size_t) (sr * 3), 0.f);
        CHECK (amb::detectRootNote (silent, sr, 55) == 55);
        // the built-in sounds are in tune with the keyboard
        for (int l : { amb::Keys, amb::Vocals, amb::Pad, amb::Sub })
        {
            auto d = amb::SourceBuffer::makeDefault (l, 44100.0);
            const int expected = l == amb::Sub ? 36 : 60;
            CHECK (std::abs (amb::detectRootNote (d->l, d->sampleRate, -100) - expected) <= 0);
        }
    }


    // ---- the dusty side: dark filter, crackle, tape, echo ----
    {
        const double sr = 48000.0;
        auto rmsOf = [] (const std::vector<float>& v) { double a = 0; for (float x : v) a += (double) x * x; return std::sqrt (a / (double) v.size()); };
        // Dark: a 8 kHz tone is strongly muffled at 1, untouched at 0; low notes pass
        for (int test = 0; test < 3; ++test)
        {
            amb::Dark d; d.prepare (sr);
            const double f = test == 0 ? 8000.0 : 200.0; const float amt = test == 2 ? 0.f : 1.f;
            std::vector<float> o ((size_t) sr);
            for (int i = 0; i < (int) sr; ++i) { float l = 0.5f * (float) std::sin (2.0 * 3.14159265358979 * f * (double) i / sr), r = l; d.process (l, r, amt); o[(size_t) i] = l; }
            const double g = rmsOf (o) / (0.5 / std::sqrt (2.0));
            if (test == 0) CHECK (g < 0.15);          // at least 16 dB down
            if (test == 1) CHECK (g > 0.9);           // bass is left alone
            if (test == 2) CHECK (g > 0.95);          // amount 0 = open
        }
        // Crackle: silent at 0, pops and hiss at 1, bounded
        {
            amb::Crackle c; c.prepare (sr);
            std::vector<float> o ((size_t) sr * 5); double peak = 0; int pops = 0;
            for (int i = 0; i < (int) sr * 5; ++i) { float l, r; c.process (1.f, l, r); o[(size_t) i] = l; peak = std::max (peak, (double) std::abs (l)); if (std::abs (l) > 0.15f && (i == 0 || std::abs (o[(size_t) i - 1]) <= 0.15f)) ++pops; }
            std::printf ("crackle: peak %.2f, about %d pops per second\n", peak, pops / 5);
            CHECK (peak > 0.1 && peak < 1.5 && pops / 5 >= 3);
            amb::Crackle z; z.prepare (sr); double e = 0; for (int i = 0; i < (int) sr; ++i) { float l, r; z.process (0.f, l, r); e += std::abs (l) + std::abs (r); }
            CHECK (e == 0.0);
        }
        // Tape: bounded and finite, silence stays silence
        {
            amb::Tape t; t.prepare (sr); double peak = 0, sil = 0;
            for (int i = 0; i < (int) sr * 3; ++i) { float l = 0.6f * (float) std::sin (2.0 * 3.14159265358979 * 220.0 * (double) i / sr), r = l; t.process (l, r, 1.f); CHECK (std::isfinite (l)); peak = std::max (peak, (double) std::abs (l)); }
            for (int i = 0; i < (int) sr; ++i) { float l = 0.f, r = 0.f; t.process (l, r, 1.f); if (i > 5000) sil = std::max (sil, (double) std::abs (l)); }
            CHECK (peak < 1.2 && sil < 1e-6);
        }
        // Echo: a click comes back after the delay time, quieter, and eventually dies
        {
            amb::Echo e; e.prepare (sr);
            std::vector<float> o ((size_t) (sr * 6));
            for (int i = 0; i < (int) (sr * 6); ++i) { float l = i == 0 ? 1.f : 0.f, r = l; e.process (l, r, (float) (0.5 * sr), 0.5f, 1.f); o[(size_t) i] = l; }
            double first = 0, late = 0; for (int i = (int) (0.45 * sr); i < (int) (0.55 * sr); ++i) first = std::max (first, (double) std::abs (o[(size_t) i]));
            for (int i = (int) (5.0 * sr); i < (int) (6.0 * sr); ++i) late = std::max (late, (double) std::abs (o[(size_t) i]));
            std::printf ("echo: first repeat %.3f, after 5 s %.5f\n", first, late);
            CHECK (first > 0.02 && late < first * 0.5);
        }
    }

    // ---- the whole engine renders: finite, bounded, plays while the key is held, fades after ----
    {
        const double sr = 48000.0;
        std::array<std::shared_ptr<amb::SourceBuffer>, amb::kNumLayers> defaults;
        const amb::SourceBuffer* srcs[amb::kNumLayers];
        for (int l = 0; l < amb::kNumLayers; ++l) { defaults[(size_t) l] = amb::SourceBuffer::makeDefault (l, sr); srcs[l] = defaults[(size_t) l].get(); CHECK (defaults[(size_t) l]->size() > 1000); }
        auto layers = defaultLayers();
        amb::Engine eng; eng.prepare (sr); eng.setSources (srcs);
        amb::GlobalParams gp = g; gp.pieceBars = 24;
        const int block = 512;
        std::vector<float> L ((size_t) block), R ((size_t) block), sl ((size_t) block), sr_ ((size_t) block), cl ((size_t) block), cr ((size_t) block);
        double beat = 0.0, peak = 0.0, bpm = 120.0, energyHeld = 0, energyAfter = 0; int blocksHeld = 0;
        eng.noteOn (48, 5u, beat, gp, layers.data());
        const int totalBlocks = (int) (sr * 100 / block);   // 100 s = 50 bars at 120 bpm
        for (int b = 0; b < totalBlocks; ++b)
        {
            if (b == (int) (sr * 60 / block)) eng.noteOff (48, beat);   // the clip ends at 60 s
            for (auto* v : { &L, &R, &sl, &sr_, &cl, &cr }) std::fill (v->begin(), v->end(), 0.f);
            eng.process (block, beat, bpm, gp, layers.data(), L.data(), R.data(), sl.data(), sr_.data(), cl.data(), cr.data());
            beat += (double) block / (sr * 60.0 / bpm);
            double e = 0;
            for (int i = 0; i < block; ++i)
            {
                const double v = std::max (std::abs (L[(size_t) i]), std::abs (R[(size_t) i])); peak = std::max (peak, v); e += (double) L[(size_t) i] * L[(size_t) i];
                CHECK (std::isfinite (L[(size_t) i]) && std::isfinite (R[(size_t) i]) && std::isfinite (cl[(size_t) i]));
            }
            if (b > (int) (sr * 10 / block) && b < (int) (sr * 60 / block)) { energyHeld += e; ++blocksHeld; }
            if (b > (int) (sr * 90 / block)) energyAfter += e;
        }
        std::printf ("audio: peak %.2f, mean power while held %.5f, 90-100 s %.8f, voices left %d, seed %u\n", peak, energyHeld / blocksHeld / block, energyAfter, eng.activeVoices(), eng.seedInUse());
        CHECK (peak < 12.0);
        CHECK (energyHeld / blocksHeld / block > 1e-4);
        CHECK (energyAfter < 1e-6 && eng.activeVoices() == 0);
    }

    // ---- reverb stays bounded ----
    {
        amb::Reverb rv; rv.prepare (48000.0); rv.setSize (1.f);
        double pk = 0, tail = 0;
        for (int i = 0; i < 48000 * 30; ++i)
        {
            const float x = i < 4800 ? 0.8f * std::sin (0.05f * (float) i) : 0.f; float l, r; rv.process (x, x, l, r);
            const double m = std::max (std::abs (l), std::abs (r)); pk = std::max (pk, m); if (i > 48000 * 25) tail = std::max (tail, m);
        }
        CHECK (tail < pk * 0.01);
    }

    std::printf (fails == 0 ? "ALL TESTS PASSED\n" : "%d FAILURES\n", fails);
    return fails == 0 ? 0 : 1;
}
