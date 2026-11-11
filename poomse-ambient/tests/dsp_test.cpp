#include "../Source/Dsp/Orchestra.h"
#include "../Source/Dsp/PitchDetect.h"
#include <cstdio>
#include <map>
#include <set>
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("FAIL: %s (line %d)\n", #c, __LINE__); ++failures; } } while (0)
using namespace amb;
static const double kSr = 48000.0;

static std::vector<Event> piece (uint32_t seed, double beats, const GlobalParams& g, const std::array<LayerParams, kNumLayers>& lp, int tonic = 48, double block = 1.0)
{
    Composer c; c.start (0.0, seed, tonic, g);
    std::vector<Event> ev; Event buf[Composer::maxEvents];
    for (double b = 0; b < beats; b += block) { const int n = c.advance (b, b + block, g, lp.data(), buf, Composer::maxEvents); ev.insert (ev.end(), buf, buf + n); }
    std::sort (ev.begin(), ev.end(), [] (const Event& a, const Event& b) { return a.beat < b.beat || (a.beat == b.beat && a.layer < b.layer); });
    return ev;
}
static std::array<LayerParams, kNumLayers> amounts (float a) { std::array<LayerParams, kNumLayers> l; for (auto& x : l) x.amount = a; return l; }
static bool sameEvents (const std::vector<Event>& a, const std::vector<Event>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (a[i].layer != b[i].layer || a[i].note != b[i].note || std::abs (a[i].beat - b[i].beat) > 1e-9 || std::abs (a[i].velocity - b[i].velocity) > 1e-6f) return false;
    return true;
}
static float peakOf (const std::vector<float>& v, size_t from = 0) { float m = 0; for (size_t i = from; i < v.size(); ++i) { if (! std::isfinite (v[i])) return 1e9f; m = std::max (m, std::abs (v[i])); } return m; }
static double rms (const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i]; return std::sqrt (s / (double) (b - a)); }

int main()
{
    GlobalParams g; const auto lp = amounts (0.85f);

    // ---------------- the sounds ----------------
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
                if (got >= 0) CHECK (std::abs (got - z.root) % 12 == 0);
            }
        for (auto& z : b->zones) if (z.loops())
        {
            double around = 0; for (int i = z.loopEnd - 40; i < z.loopEnd - 1; ++i) around = std::max (around, (double) std::abs (z.x[0][(size_t) i + 1] - z.x[0][(size_t) i]));
            CHECK (std::abs (z.x[0][(size_t) z.loopStart] - z.x[0][(size_t) z.loopEnd - 1]) <= 4.0 * around + 0.02);
        }
    }

    // ---------------- the music ----------------
    // everything sits on the eighth-note grid
    for (int mode = 0; mode < 4; ++mode)
    {
        g.mode = mode;
        const auto ev = piece (5, 480, g, lp);
        CHECK (ev.size() > 100);
        for (const auto& e : ev) { CHECK (std::abs (e.beat * 2.0 - std::round (e.beat * 2.0)) < 1e-9); CHECK (e.velocity > 0.f && e.velocity <= 1.f && e.pan >= -1.f && e.pan <= 1.f && e.durBeats > 0.f); }
    }
    g.mode = 0;

    // what sounds together does not rub: no semitone between two notes that sound at the same time, in any mode, key or version
    {
        int pairs = 0, bad = 0;
        for (int mode = 0; mode < 4; ++mode) for (uint32_t seed = 1; seed <= 5; ++seed)
        {
            GlobalParams q; q.mode = mode; q.surprise = 0.8f;
            const auto ev = piece (seed, 480, q, amounts (0.9f), 46 + (int) seed * 2);
            for (size_t i = 0; i < ev.size(); ++i) for (size_t j = i + 1; j < ev.size(); ++j)
            {
                const auto &a = ev[i], &b = ev[j]; if (a.note < 0 || b.note < 0) continue;
                if (b.beat >= a.beat + a.durBeats) continue;
                ++pairs; const int d = std::abs (a.note - b.note) % 12; if (d == 1 || d == 11) ++bad;
            }
        }
        std::printf ("sounding together: %d pairs, %d semitones\n", pairs, bad);
        CHECK (pairs > 3000 && bad == 0);
    }

    // a progression: the pads (and the bass) play the roots of four chords in the mode, in order, one chord every two bars
    {
        GlobalParams q; q.journey = 0.f; q.silence = 0.3f; q.depth = 0.6f;
        const auto ev = piece (9, 480, q, amounts (1.f));
        std::vector<std::pair<long, int>> pads; for (const auto& e : ev) if (e.layer == Pad && std::fmod (e.beat, 8.0) == 0.0) pads.push_back ({ (long) (e.beat / 8.0), e.note % 12 });
        std::printf ("pads: %zu chords\n", pads.size()); CHECK (pads.size() > 25);
        const int* sc = modeScale (0);
        std::set<int> inMode; for (int i = 0; i < 7; ++i) inMode.insert ((48 + sc[i]) % 12);
        for (auto& p : pads) CHECK (inMode.count (p.second) == 1);
        // the same four chords come round again before the progression changes
        int again = 0, total = 0; for (auto& a : pads) for (auto& b : pads) if (b.first == a.first + 4 && a.first / 8 == b.first / 8) { ++total; again += a.second == b.second; }
        CHECK (total > 5 && again == total);
    }

    // a motif: the piano plays almost the same rhythm with every chord, and the notes change a little each time
    {
        GlobalParams q; q.silence = 0.2f; q.journey = 0.f; q.surprise = 0.2f;
        const auto ev = piece (3, 480, q, amounts (1.f));
        std::map<long, std::set<int>> steps; std::map<long, std::vector<int>> notes;
        for (const auto& e : ev) if (e.layer == Keys) { const long c = (long) std::floor (e.beat / 8.0); steps[c].insert ((int) std::lround ((e.beat - (double) c * 8.0) * 2.0)); notes[c].push_back (e.note); }
        double same = 0; int cmp = 0, diffNotes = 0, cmpNotes = 0;
        for (auto& kv : steps) if (steps.count (kv.first + 1) && kv.first / 8 == (kv.first + 1) / 8)
        {
            const auto& a = kv.second; const auto& b = steps[kv.first + 1]; int inter = 0; for (int s : a) inter += (int) b.count (s);
            same += (double) inter / (double) std::max (a.size(), b.size()); ++cmp;
        }
        for (auto& kv : notes) if (notes.count (kv.first + 4) && kv.first / 8 == (kv.first + 4) / 8) { ++cmpNotes; diffNotes += kv.second != notes[kv.first + 4]; }
        std::printf ("motif: %.0f %% of the rhythm stays from one chord to the next (%d pairs)\n", 100.0 * same / std::max (1, cmp), cmp);
        CHECK (cmp > 20 && same / cmp > 0.55);
        CHECK (cmpNotes > 5);
        // and it is not the same notes all piece long
        std::set<std::vector<int>> distinct; for (auto& kv : notes) distinct.insert (kv.second);
        CHECK (distinct.size() > notes.size() / 3);
    }

    // the violin holds one note of the chord and moves by small steps
    {
        const auto ev = piece (4, 480, g, amounts (1.f));
        std::vector<int> v; for (const auto& e : ev) if (e.layer == Vocals && std::fmod (e.beat, 8.0) == 0.0) v.push_back (e.note);
        CHECK (v.size() > 10);
        double sum = 0; int semis = 0; for (size_t i = 1; i < v.size(); ++i) { sum += std::abs (v[i] - v[i - 1]); semis += std::abs (v[i] - v[i - 1]) == 1; }
        std::printf ("violin: %zu notes, average move %.1f semitones\n", v.size(), sum / (double) (v.size() - 1));
        CHECK (semis == 0 && sum / (double) (v.size() - 1) < 5.5);
        for (int n : v) CHECK (n >= 57 && n <= 79);
    }

    // the same version is the same piece, whatever the size of the blocks; another version is another piece
    {
        const auto a = piece (11, 200, g, lp), b = piece (11, 200, g, lp, 48, 0.37), c = piece (12, 200, g, lp);
        auto upTo = [] (std::vector<Event> v, double beat) { v.erase (std::remove_if (v.begin(), v.end(), [&] (const Event& e) { return e.beat >= beat; }), v.end()); return v; };
        CHECK (sameEvents (a, piece (11, 200, g, lp))); CHECK (sameEvents (upTo (a, 200.0), upTo (b, 200.0))); CHECK (! sameEvents (a, c));
    }
    // a layer set to zero stays silent (and the bells follow the keys)
    {
        auto l = lp; l[Keys].amount = 0.f; l[Bells].amount = 0.f; l[Vocals].amount = 0.f;
        for (const auto& e : piece (6, 480, g, l)) CHECK (e.layer != Keys && e.layer != Vocals && e.layer != Bells);
        auto m = lp; for (auto& x : m) x.amount = 0.f; CHECK (piece (6, 480, g, m).empty());
    }
    // silence: more of it, fewer notes. depth: more violin and bass in the deep moments
    {
        GlobalParams busy, airy; busy.silence = 0.f; airy.silence = 1.f;
        const auto b = piece (8, 480, busy, lp), a = piece (8, 480, airy, lp);
        std::printf ("silence 0: %zu events, silence 1: %zu\n", b.size(), a.size()); CHECK (a.size() < b.size() * 0.75);
        GlobalParams light, deep; light.depth = 0.f; deep.depth = 1.f; int lo = 0, hi = 0;
        for (uint32_t s = 1; s <= 6; ++s) { for (const auto& e : piece (s, 480, light, lp)) lo += e.layer == Vocals || e.layer == Bass; for (const auto& e : piece (s, 480, deep, lp)) hi += e.layer == Vocals || e.layer == Bass; }
        std::printf ("violin+bass: depth 0 -> %d, depth 1 -> %d\n", lo, hi); CHECK (hi > lo * 1.2);
    }
    // the arc: the middle of the piece is fuller than its start and its end
    {
        GlobalParams arc; arc.journey = 1.f; int a[3] = {};
        for (uint32_t s = 1; s <= 6; ++s) for (const auto& e : piece (s, 480, arc, lp)) a[std::min (2, (int) (e.beat / 160.0))]++;
        std::printf ("arc: %d / %d / %d events\n", a[0], a[1], a[2]); CHECK (a[1] > a[0] * 1.2 && a[1] > a[2] * 1.2);
    }
    // the key of the clip is the key of the piece: the same piece, written above another note
    {
        const auto e0 = piece (2, 240, g, lp, 48), e1 = piece (2, 240, g, lp, 53);
        CHECK (e0.size() == e1.size());
        const int* sc = modeScale (g.mode); std::set<int> inMode; for (int i = 0; i < 7; ++i) inMode.insert (sc[i]);
        for (const auto& e : e1) if (e.note >= 0) CHECK (inMode.count (((e.note - 53) % 12 + 12) % 12) == 1);
        std::set<int> p0, p1; for (const auto& e : e0) if (e.layer == Pad) p0.insert (((e.note - 48) % 12 + 12) % 12); for (const auto& e : e1) if (e.layer == Pad) p1.insert (((e.note - 53) % 12 + 12) % 12);
        CHECK (p0 == p1 && ! p0.empty());
    }

    // ---------------- the orchestra ----------------
    {
        std::shared_ptr<Bank> banks[4]; for (int s = 0; s < 4; ++s) banks[s] = makeFactory (s);
        auto run = [&] (uint32_t seed, double seconds, double releaseAt, std::vector<float>& outL, float* peak)
        {
            Orchestra o; o.prepare (kSr); for (int s = 0; s < 4; ++s) o.setBank (s, banks[s].get());
            GlobalParams q; q.pieceBars = 16; double bpm = 120.0, beat = 0.0; bool off = false;
            o.noteOn (48, seed, 0.0, q);
            std::vector<float> L (256), R (256), SL (256), SR (256);
            for (double t = 0; t < seconds; t += 256.0 / kSr)
            {
                if (! off && t >= releaseAt) { o.noteOff (48, beat); off = true; }
                std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f); std::fill (SL.begin(), SL.end(), 0.f); std::fill (SR.begin(), SR.end(), 0.f);
                o.process (256, beat, bpm, q, lp.data(), L.data(), R.data(), SL.data(), SR.data());
                beat += 256.0 / (kSr * 60.0 / bpm);
                for (int i = 0; i < 256; ++i) { outL.push_back (L[(size_t) i] + 0.3f * SL[(size_t) i]); if (peak) *peak = std::max (*peak, std::abs (L[(size_t) i])); }
            }
            return o.started();
        };
        std::vector<float> a, b, c; float pk = 0.f;
        const int started = run (3, 40.0, 30.0, a, &pk);
        std::printf ("orchestra: %d voices in 40 s, peak %.2f\n", started, pk);
        CHECK (started > 20 && peakOf (a) < 8.f && peakOf (a) > 0.05f);
        CHECK (rms (a, (size_t) (10 * kSr), (size_t) (28 * kSr)) > 0.005);                        // it plays while the key is down
        CHECK (rms (a, (size_t) (39 * kSr), a.size()) < 0.3 * rms (a, (size_t) (10 * kSr), (size_t) (28 * kSr)));      // and fades after
        run (3, 40.0, 30.0, b, nullptr); run (4, 40.0, 30.0, c, nullptr);
        CHECK (a == b); CHECK (a != c);
        // a tail of long sounds ends: no voice is left after a while
        Orchestra o; o.prepare (kSr); for (int s = 0; s < 4; ++s) o.setBank (s, banks[s].get());
        GlobalParams q; std::vector<float> L (512), R (512), SL (512), SR (512);
        o.noteOn (60, 1, 0.0, q); double beat = 0.0;
        for (int i = 0; i < 400; ++i) { std::fill (L.begin(), L.end(), 0.f); o.process (512, beat, 120.0, q, lp.data(), L.data(), R.data(), SL.data(), SR.data()); beat += 512.0 / (kSr * 0.5); if (i == 200) o.noteOff (60, beat); }
        for (int i = 0; i < 1200 && o.activeVoices() > 0; ++i) { o.process (512, beat, 120.0, q, lp.data(), L.data(), R.data(), SL.data(), SR.data()); beat += 512.0 / (kSr * 0.5); }
        CHECK (o.activeVoices() == 0);
        // the pads of the interface play an example of their sound
        Orchestra p; p.prepare (kSr); for (int s = 0; s < 4; ++s) p.setBank (s, banks[s].get());
        for (int s = 0; s < 4; ++s) { p.preview (s); std::fill (L.begin(), L.end(), 0.f); for (int k = 0; k < 40; ++k) p.process (512, 0.0, 120.0, q, lp.data(), L.data(), R.data(), SL.data(), SR.data()); }
        CHECK (p.started() == 4);
    }
    std::printf (failures ? "%d FAILURES\n" : "ALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
