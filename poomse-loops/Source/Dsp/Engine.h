#pragma once
#include "LoopFinder.h"
#include <array>
#include <cmath>

// The player. A held key takes the loop that is closest to the cross-hair of the map and plays it, at the pitch of the key.
// Every note plays three copies of its loop whose lengths differ by a hair: they slip against each other, and that slow
// interference is the rhythm. Nothing else: no filter, no reverb, no noise. The sound is the record.
namespace lp
{
struct Params
{
    float position = 0.08f;   // where on the record (0..1)
    float size = 0.45f;       // how long the loop is (0..1, log scale: 30 ms .. 750 ms)
    float seam = 0.4f;        // 0 = the loop point is heard (a pulse), 1 = completely smoothed (a drone)
    float moire = 0.35f;      // how much the three copies differ in length (up to 5 %)
    float scan = 0.f;         // -1..1: the cross-hair travels through the record while a key is held
    float pulse = 0.2f;       // accent at the start of each turn of the loop
    float tail = 0.5f;        // release time (0.05 s .. 6 s)
    bool places = false;      // false: the key sets the pitch.  true: the key sets the place in the record
};

class Engine
{
public:
    static constexpr int maxVoices = 12;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& v : voices) v = Voice{};
        order = 0;
    }
    void setRecord (const Record* r)
    {
        rec = r;
        for (auto& v : voices) v = Voice{};
    }

    void noteOn (int note, float velocity)
    {
        if (rec == nullptr) return;
        Voice* v = nullptr;
        for (auto& x : voices) if (! x.active) { v = &x; break; }
        if (v == nullptr) { for (auto& x : voices) if (x.released && (v == nullptr || x.order < v->order)) v = &x; }
        if (v == nullptr) { for (auto& x : voices) if (v == nullptr || x.order < v->order) v = &x; }
        *v = Voice{};
        v->active = true; v->note = note; v->vel = std::clamp (velocity, 0.05f, 1.f); v->order = ++order;
        v->checkIn = 0;   // choose the loop at once
    }
    void noteOff (int note)
    {
        for (auto& v : voices) if (v.active && ! v.released && v.note == note) v.released = true;
    }
    void allNotesOff() { for (auto& v : voices) if (v.active) v.released = true; }

    int loopOfVoice (int i) const { const auto& v = voices[(size_t) i]; return v.active ? v.cur.loop : -1; }
    int activeVoices() const { int n = 0; for (const auto& v : voices) if (v.active) ++n; return n; }

    void process (int n, const Params& p, float* L, float* R)
    {
        for (int i = 0; i < n; ++i) { L[i] = 0.f; R[i] = 0.f; }
        if (rec == nullptr || rec->x.empty()) return;
        const float releaseSec = 0.05f * std::pow (120.f, std::clamp (p.tail, 0.f, 1.f));
        const float width = 0.2f + 0.7f * p.moire;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const bool places = p.places;
            const double ratio = places ? 1.0 : std::exp2 ((double) (v.note - 60) / 12.0);
            const double place = places ? (double) (((v.note - 36) % 48 + 48) % 48) / 48.0 : 0.0;
            const double inc = ratio * rec->sampleRate / sr;
            const float pan = std::clamp ((float) (v.note - 60) / 48.f, -0.5f, 0.5f);
            float gl[3], gr[3];
            for (int c = 0; c < 3; ++c)
            {
                const float pc = std::clamp (pan + (float) (c - 1) * width, -1.f, 1.f);
                const float ang = (pc + 1.f) * 0.78539816f;
                gl[c] = std::cos (ang); gr[c] = std::sin (ang);
            }
            const float gain = std::pow (v.vel, 1.2f) * 0.55f;
            const double scanRate = (double) (p.scan < 0.f ? -1.f : 1.f) * (double) p.scan * (double) p.scan * 0.05 / sr;

            for (int i = 0; i < n; ++i)
            {
                // follow the cross-hair: every 10 ms or so, look for the loop that is closest to it now
                if (--v.checkIn <= 0)
                {
                    v.checkIn = 480;
                    if (v.xf >= 1.f)
                    {
                        double x = std::fmod (p.position + place + v.scanOffset, 1.0);
                        if (x < 0.0) x += 1.0;
                        const int idx = rec->atlas.nearest ((float) x, p.size, rec->sampleRate);
                        if (idx >= 0 && idx != v.cur.loop) switchTo (v, idx, v.cur.loop < 0);
                    }
                }
                v.scanOffset += scanRate;

                // envelope
                float env;
                if (v.released) { v.env -= 1.f / std::max (1.f, releaseSec * (float) sr); if (v.env <= 0.f) { v = Voice{}; break; } env = v.env; }
                else { v.env = std::min (1.f, v.env + 1.f / (0.006f * (float) sr)); env = v.env; }

                float sl = 0.f, sr_ = 0.f;
                if (v.cur.loop >= 0)
                {
                    float t = 1.f, to = 0.f;
                    if (v.xf < 1.f)
                    {
                        v.xf = std::min (1.f, v.xf + 1.f / (float) v.xfLen);
                        t = std::sin (v.xf * 1.5707963f); to = std::cos (v.xf * 1.5707963f);
                    }
                    for (int c = 0; c < 3; ++c)
                    {
                        float s = layerCopy (v.cur, c, p) * t;
                        if (to > 0.f && v.old.loop >= 0) s += layerCopy (v.old, c, p) * to;
                        sl += s * gl[c]; sr_ += s * gr[c];
                        advance (v.cur, c, inc, p);
                        if (v.old.loop >= 0 && to > 0.f) advance (v.old, c, inc, p);
                    }
                }
                L[i] += softLimit (sl * env * gain);
                R[i] += softLimit (sr_ * env * gain);
            }
        }
    }

private:
    struct Layer
    {
        int loop = -1;
        double a = 0.0, len = 1.0;
        double pos[3] = { 0.0, 0.0, 0.0 };
    };
    struct Voice
    {
        bool active = false, released = false;
        int note = 60; float vel = 0.f, env = 0.f, xf = 1.f;
        int xfLen = 1, checkIn = 0; long order = 0;
        double scanOffset = 0.0;
        Layer cur, old;
    };

    static float softLimit (float x)
    {
        const float a = std::abs (x);
        if (a <= 0.8f) return x;
        const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
        return x < 0.f ? -y : y;
    }

    void switchTo (Voice& v, int idx, bool first)
    {
        const auto& f = rec->atlas.loops()[(size_t) idx];
        v.old = v.cur;
        v.cur.loop = idx; v.cur.a = (double) f.start; v.cur.len = (double) f.length;
        for (auto& p : v.cur.pos) p = v.cur.a;           // the three copies start together and slip apart
        v.xf = 0.f;
        v.xfLen = std::max (1, (int) ((first ? 0.004 : 0.14) * sr));
        if (first) v.old.loop = -1;
    }

    float readSrc (double pos) const
    {
        const int n = (int) rec->x.size();
        int i0 = (int) std::floor (pos);
        const float f = (float) (pos - (double) i0);
        const int i1 = std::clamp (i0 + 1, 0, n - 1);
        i0 = std::clamp (i0, 0, n - 1);
        return rec->x[(size_t) i0] + f * (rec->x[(size_t) i1] - rec->x[(size_t) i0]);
    }

    double copyLength (const Layer& l, int c, const Params& p) const { return l.len * (1.0 + (double) (c - 1) * 0.05 * (double) p.moire); }

    float layerCopy (const Layer& l, int c, const Params& p) const
    {
        const double len = copyLength (l, c, p);
        const double fade = std::clamp ((double) p.seam * 0.5 * len, std::min (0.004 * rec->sampleRate, len * 0.5), len * 0.5);
        const double pos = l.pos[c], end = l.a + len;
        float v;
        if (pos > end - fade)
        {
            const float w = (float) ((pos - (end - fade)) / fade);
            v = readSrc (pos) * (1.f - w) + readSrc (pos - len) * w;      // the end melts into what comes before the start
        }
        else v = readSrc (pos);
        const float u = (float) ((pos - l.a) / len);
        return v * ((1.f - p.pulse) + p.pulse * std::exp (-3.5f * std::clamp (u, 0.f, 1.f)));
    }

    void advance (Layer& l, int c, double inc, const Params& p) const
    {
        const double len = copyLength (l, c, p);
        l.pos[c] += inc;
        int guard = 0;
        while (l.pos[c] >= l.a + len && guard++ < 64) l.pos[c] -= len;
    }

    double sr = 48000.0;
    const Record* rec = nullptr;
    std::array<Voice, maxVoices> voices;
    long order = 0;
};
} // namespace lp
