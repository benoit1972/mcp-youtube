#pragma once
#include "Kit.h"
#include <array>
#include <atomic>
#include <cmath>

// The player. A key plays the piece of the break that sits at its height on the map (low key = dark hit, high key = bright).
// A hit can shatter: it is cut into a short burst of fragments, locked to the tempo, that speeds up or slows down, wanders
// to neighbouring pieces, falls in pitch, turns backwards. The edits are written, not random: the same note at the same
// place in the bar always breaks the same way (change the seed for another edit).
namespace sh
{
inline constexpr int kGridDiv[7] = { 8, 12, 16, 24, 32, 48, 64 };   // fragments per bar: 1/8, 1/12, 1/16, 1/24, 1/32, 1/48, 1/64

struct Params
{
    float place = 0.f;       // where in the break the hits are taken from (0..1)
    float shatter = 0.35f;   // how often (and into how many pieces) a hit breaks
    int grid = 4;            // index into kGridDiv: how fast the fragments follow each other
    float ramp = 0.4f;       // >0 the burst speeds up and swells, <0 it slows down and fades
    float scatter = 0.3f;    // 0 = the same piece repeated, 1 = pieces from all around (and some backwards)
    float fall = 0.f;        // -1..1: the burst falls or rises in pitch (one octave over the burst)
    float snap = 0.8f;       // length of the hits (30 ms .. 750 ms)
    float tune = 0.f;        // semitones
    int seed = 1;
    double bpm = 120.0;
};

inline double snapSeconds (float s) { return 0.03 * std::pow (25.0, (double) std::clamp (s, 0.f, 1.f)); }
inline float noteToY (int note) { return std::clamp ((float) (note - 36) / 48.f, 0.f, 1.f); }

class Engine
{
public:
    static constexpr int maxVoices = 48, maxPending = 96, ringSize = 32;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& v : voices) v = Voice{};
        for (auto& p : pending) p = Pending{};
        order = 0;
    }
    void setKit (const Kit* k)
    {
        kit = k;
        for (auto& v : voices) v = Voice{};
        for (auto& p : pending) p = Pending{};
    }

    // beatNow: the position of this note in the song, in quarter notes (it seeds the edit, so a bar repeats exactly)
    void noteOn (int note, float velocity, const Params& p, double beatNow)
    {
        if (kit == nullptr || kit->slices.empty()) return;
        // the same key again: the previous hit and its unplayed fragments are cut (tight, like a gated drum)
        for (auto& v : voices) if (v.on && v.group == note && v.chokeLeft == 0) { v.chokeLeft = std::max (1, (int) (0.005 * sr)); v.chokeLen = v.chokeLeft; }
        for (auto& q : pending) if (q.on && q.group == note) q.on = false;

        const float y = noteToY (note);
        const int s0 = kit->nearest (p.place, y);
        if (s0 < 0) return;

        uint32_t h = (uint32_t) p.seed * 2654435761u ^ (uint32_t) note * 40503u ^ (uint32_t) std::llround (beatNow * 4.0) * 2246822519u;
        gg::Rng rng (h | 1u);
        for (int i = 0; i < 4; ++i) rng.nextU32();

        // kicks stay whole more often than snares and hats
        const bool breaks = rng.uni() < p.shatter * (0.25f + 0.75f * y);
        const int n = breaks ? 2 + (int) (rng.uni() * (1.f + p.shatter * 6.f)) : 1;
        const double baseBeats = 4.0 / (double) kGridDiv[std::clamp (p.grid, 0, 6)];
        const double beatSamples = sr * 60.0 / std::max (20.0, p.bpm);
        const double ratioStep = 1.0 - 0.28 * (double) p.ramp;      // each gap is 28 % shorter (ramp 1) or longer (ramp -1) than the last
        const double capOut = snapSeconds (p.snap) * sr;

        // neighbours of the first piece, for the scatter
        std::array<int, 8> near {}; int nNear = 0;
        if (p.scatter > 0.001f && n > 1)
        {
            const int m = 1 + (int) (p.scatter * 7.f);
            std::array<float, 8> cost {};
            const auto& a = kit->slices[(size_t) s0];
            for (int i = 0; i < (int) kit->slices.size(); ++i)
            {
                if (i == s0) continue;
                const auto& b = kit->slices[(size_t) i];
                const float c = (a.x - b.x) * (a.x - b.x) * 0.5f + (a.y - b.y) * (a.y - b.y);
                int pos = nNear;
                while (pos > 0 && c < cost[(size_t) pos - 1]) --pos;
                if (pos >= m) continue;
                for (int j = std::min (nNear, m - 1); j > pos; --j) { cost[(size_t) j] = cost[(size_t) j - 1]; near[(size_t) j] = near[(size_t) j - 1]; }
                cost[(size_t) pos] = c; near[(size_t) pos] = i;
                nNear = std::min (m, nNear + 1);
            }
        }

        double t = 0.0, gap = baseBeats * beatSamples;
        for (int k = 0; k < n; ++k)
        {
            Pending q;
            q.on = true; q.group = note; q.delay = (int) t;
            q.slice = s0;
            if (k > 0 && nNear > 0 && rng.uni() < p.scatter) q.slice = near[(size_t) (rng.uni() * (float) nNear) % (size_t) nNear];
            q.rev = k > 0 && rng.uni() < p.scatter * 0.35f;
            q.semis = p.tune + (n > 1 ? p.fall * 12.f * (float) k / (float) (n - 1) : 0.f);
            float g;
            if (n == 1) g = 1.f;
            else if (p.ramp > 0.f) g = 0.55f + 0.45f * (float) k / (float) (n - 1);      // a swelling burst
            else g = 1.f - 0.65f * (float) k / (float) (n - 1);                           // a dying one
            q.vel = std::clamp (velocity, 0.05f, 1.f) * (n == 1 ? 1.f : (p.ramp > 0.f ? g : 0.4f + 0.6f * g));
            q.len = k + 1 < n ? (int) std::min (capOut, gap * 1.1) : (int) capOut;       // a fragment is cut short by the next one
            q.pan = std::clamp ((kit->slices[(size_t) q.slice].y - 0.35f) * 0.5f + (k % 2 ? 1.f : -1.f) * 0.12f * p.scatter * (n > 1 ? 1.f : 0.f), -0.8f, 0.8f);
            q.first = k == 0;
            push (q);
            t += gap;
            gap = std::max (sr * 0.004, gap * ratioStep);
        }
    }

    void allNotesOff() { for (auto& p : pending) p.on = false; }

    void process (int num, float* L, float* R)
    {
        for (int i = 0; i < num; ++i) { L[i] = 0.f; R[i] = 0.f; }
        if (kit == nullptr) return;
        for (int i = 0; i < num; ++i)
        {
            for (auto& q : pending)
            {
                if (! q.on) continue;
                if (q.delay > 0) { --q.delay; continue; }
                spawn (q);
                q.on = false;
            }
            float l = 0.f, r = 0.f;
            for (auto& v : voices)
            {
                if (! v.on) continue;
                const float a = std::min (1.f, (float) v.age / std::max (1.f, 0.001f * (float) sr));
                float rel = 1.f;
                const int left = v.len - v.age;
                if (left < v.relLen) rel = std::max (0.f, (float) left / (float) std::max (1, v.relLen));
                if (v.chokeLeft > 0) { rel *= (float) v.chokeLeft / (float) v.chokeLen; if (--v.chokeLeft == 0) { v = Voice{}; continue; } }
                const float s = read (v.start, v.length, v.pos) * a * rel * v.gain;
                l += s * v.gl; r += s * v.gr;
                v.pos += v.rev ? -v.inc : v.inc;
                if (++v.age >= v.len || v.pos < 0.0 || v.pos >= (double) v.length) v = Voice{};
            }
            L[i] = softLimit (l); R[i] = softLimit (r);
        }
    }

    // what the interface shows: the last fragments played (slice index, bit 16 = a fragment of a burst)
    int hitCount() const { return hits.load(); }
    int hitAt (int i) const { return ring[(size_t) (i % ringSize)].load(); }
    int activeVoices() const { int n = 0; for (const auto& v : voices) if (v.on) ++n; return n; }

private:
    struct Pending
    {
        bool on = false, rev = false, first = true;
        int delay = 0, slice = 0, group = 0, len = 0;
        float vel = 0.f, semis = 0.f, pan = 0.f;
    };
    struct Voice
    {
        bool on = false, rev = false;
        int start = 0, length = 0, age = 0, len = 0, relLen = 1, group = -1, chokeLeft = 0, chokeLen = 1;
        double pos = 0.0, inc = 1.0;
        float gain = 0.f, gl = 0.f, gr = 0.f;
        long order = 0;
    };

    void push (const Pending& q)
    {
        for (auto& p : pending) if (! p.on) { p = q; return; }
    }

    void spawn (const Pending& q)
    {
        Voice* v = nullptr;
        for (auto& x : voices) if (! x.on) { v = &x; break; }
        if (v == nullptr) { for (auto& x : voices) if (v == nullptr || x.order < v->order) v = &x; }
        const auto& s = kit->slices[(size_t) q.slice];
        *v = Voice{};
        v->on = true; v->start = s.start; v->length = s.length; v->rev = q.rev; v->group = q.group; v->order = ++order;
        v->inc = std::exp2 ((double) q.semis / 12.0) * kit->sampleRate / sr;
        v->pos = q.rev ? (double) (s.length - 1) : 0.0;
        const double outLen = (double) s.length / v->inc;
        v->len = std::max (8, (int) std::min ((double) q.len, outLen));
        v->relLen = std::max (1, std::min (v->len / 2, (int) (0.004 * sr)));
        v->gain = std::pow (q.vel, 1.3f) * 0.9f;
        const float ang = (q.pan + 1.f) * 0.78539816f;
        v->gl = std::cos (ang) * 1.4142f; v->gr = std::sin (ang) * 1.4142f;
        const int c = hits.load();
        ring[(size_t) (c % ringSize)].store(q.slice | (q.first ? 0 : 0x10000));
        hits.store(c + 1);
    }

    float read (int start, int length, double pos) const
    {
        const int i0 = (int) pos;
        const float f = (float) (pos - (double) i0);
        const int a = std::clamp (i0, 0, length - 1), b = std::clamp (i0 + 1, 0, length - 1);
        const auto& d = kit->x;
        return d[(size_t) (start + a)] + f * (d[(size_t) (start + b)] - d[(size_t) (start + a)]);
    }

    static float softLimit (float x)
    {
        const float a = std::abs (x);
        if (a <= 0.8f) return x;
        const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
        return x < 0.f ? -y : y;
    }

    double sr = 48000.0;
    const Kit* kit = nullptr;
    std::array<Voice, maxVoices> voices;
    std::array<Pending, maxPending> pending;
    long order = 0;
    std::array<std::atomic<int>, ringSize> ring {};
    std::atomic<int> hits { 0 };
};
} // namespace sh
