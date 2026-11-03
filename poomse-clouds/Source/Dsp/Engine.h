#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
#include "Rng.h"
#include "Reverb.h"

// Poomse Clouds: a granular texture processor. The sound that comes in is written into a circular memory of 8 seconds; small grains
// (20 ms .. 1.5 s) are read from it, at a position, a size and a pitch of your choice, and laid on top of each other.
namespace cl
{
struct Params
{
    float position = 0.4f;   // where in the memory the grains are taken: 0 = right now, 1 = 5 seconds ago
    float size = 0.5f;       // length of a grain, 20 ms .. 1.5 s
    float pitch = 0.f;       // semitones
    float density = 0.6f;    // grains per second; below the middle they come at random times, above it regularly. On the grid: the division.
    float texture = 0.5f;    // the shape of a grain: a pluck, a bell, a flat-topped cushion
    float spread = 0.5f;     // how far grains are thrown to the sides
    float blend = 0.5f;      // dry <-> clouds
    float feedback = 0.f;    // the clouds fed back into the memory
    float reverb = 0.2f;
    bool freeze = false;     // the memory stops listening
    bool grid = false;       // grains are born on the grid of the host (when it plays)
};
inline constexpr double kGridQ[5] = { 1.0, 0.5, 0.25, 0.125, 0.0625 };

class Engine
{
public:
    static constexpr int maxGrains = 64, bins = 360, ringSize = 32;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        N = (int) (8.0 * sr);
        for (auto& b : buf) b.assign ((size_t) N, 0.f);
        wr = 0; countdown = 0.0; lastK = -1;
        for (auto& g : grains) g = Grain{};
        rng = gg::Rng (0xC10D5u);
        rev.prepare (sr); rev.setSize (0.75f);
        hpL = hpR = 0.f; fbL = fbR = 0.f;
        hpK = 1.f - std::exp (-2.f * 3.14159265f * 90.f / (float) sr);
        for (auto& p : peaks) p.store (0.f);
        count.store (0);
        makeTables();
    }

    void process (int num, const Params& p, double beat0, double bpm, bool playing,
                  const float* inL, const float* inR, float* outL, float* outR)
    {
        const double spq = sr * 60.0 / std::clamp (bpm, 20.0, 400.0);
        const float wetG = std::sin (std::clamp (p.blend, 0.f, 1.f) * 1.5707963f), dryG = std::cos (std::clamp (p.blend, 0.f, 1.f) * 1.5707963f);
        const float fb = std::clamp (p.feedback, 0.f, 1.f) * 0.9f;
        const bool gridOn = p.grid && playing;
        const int gi = std::clamp ((int) (p.density * 5.f), 0, 4);
        for (int i = 0; i < num; ++i)
        {
            // ---- when does a grain start ----
            if (gridOn)
            {
                const long k = (long) std::floor ((beat0 + (double) i / spq) / kGridQ[gi] + 1e-9);
                if (lastK != std::numeric_limits<long>::min() && k != lastK && lastK >= 0) spawn (p, 1.0 / (kGridQ[gi] * 60.0 / std::clamp (bpm, 20.0, 400.0)), true);
                lastK = k;
            }
            else
            {
                lastK = -1;
                countdown -= 1.0;
                if (countdown <= 0.0)
                {
                    const double rate = 0.7 * std::pow (140.0, (double) std::clamp (p.density, 0.f, 1.f));          // grains per second
                    const float jitter = std::max (0.f, 1.f - 2.f * p.density);
                    const float u = std::max (1e-4f, rng.uni());
                    const double interval = (1.0 / rate) * (1.0 + (double) jitter * ((double) (-std::log (u)) - 1.0));
                    countdown += std::max (8.0, interval * sr);
                    spawn (p, rate, false);
                }
            }

            // ---- the grains ----
            float wl = 0.f, wr_ = 0.f;
            for (auto& g : grains)
            {
                if (! g.on) continue;
                const float x = (float) g.age / (float) g.len;
                const float w = g.wa[(size_t) std::min (255.f, x * 256.f)] * (1.f - g.mix) + g.wb[(size_t) std::min (255.f, x * 256.f)] * g.mix;
                const int i0 = (int) g.pos;
                const float fr = (float) (g.pos - (double) i0);
                const int i1 = i0 + 1 >= N ? 0 : i0 + 1;
                const float sl = buf[0][(size_t) i0] + (buf[0][(size_t) i1] - buf[0][(size_t) i0]) * fr;
                const float sr_ = buf[1][(size_t) i0] + (buf[1][(size_t) i1] - buf[1][(size_t) i0]) * fr;
                const float s = w * g.gain;
                wl += sl * s * g.gl; wr_ += sr_ * s * g.gr;
                g.pos += g.inc; if (g.pos >= (double) N) g.pos -= (double) N;
                if (++g.age >= g.len) g.on = false;
            }
            wl = std::tanh (wl); wr_ = std::tanh (wr_);

            // ---- the memory listens (unless frozen) ----
            if (! p.freeze)
            {
                hpL += hpK * (fbL - hpL); hpR += hpK * (fbR - hpR);
                const float fl = std::tanh (fbL - hpL), fr_ = std::tanh (fbR - hpR);
                buf[0][(size_t) wr] = inL[i] + fb * fl;
                buf[1][(size_t) wr] = inR[i] + fb * fr_;
                const int bin = (int) ((long) wr * bins / N);
                if (bin != lastBin) { peaks[(size_t) bin].store (0.f); lastBin = bin; }
                peaks[(size_t) bin].store (std::max (peaks[(size_t) bin].load(), std::max (std::abs (inL[i]), std::abs (inR[i]))));
                if (++wr >= N) wr = 0;
            }
            fbL = wl; fbR = wr_;

            float rl, rr;
            rev.process (wl, wr_, rl, rr);
            wl += rl * p.reverb; wr_ += rr * p.reverb;
            outL[i] = inL[i] * dryG + wl * wetG;
            outR[i] = inR[i] * dryG + wr_ * wetG;
        }
        writeFrac.store ((float) wr / (float) N);
    }

    int hitCount() const { return count.load(); }
    int hitAt (int i) const { return ring[(size_t) (i % ringSize)].load(); }       // start (12 bits) | length (12 bits) << 12, in 1/4096 of the memory
    float peak (int i) const { return peaks[(size_t) i].load(); }
    float writePosition() const { return writeFrac.load(); }
    int activeGrains() const { int n = 0; for (const auto& g : grains) if (g.on) ++n; return n; }

private:
    struct Grain
    {
        bool on = false;
        double pos = 0.0, inc = 1.0;
        int age = 0, len = 1;
        const float* wa = nullptr; const float* wb = nullptr;
        float mix = 0.f, gain = 1.f, gl = 1.f, gr = 1.f;
    };

    void makeTables()
    {
        for (int i = 0; i < 256; ++i)
        {
            const float x = (float) i / 255.f;
            tPerc[(size_t) i] = x < 0.03f ? x / 0.03f : std::exp (-(x - 0.03f) * 5.5f) * std::min (1.f, (1.f - x) / 0.02f);
            tHann[(size_t) i] = 0.5f - 0.5f * std::cos (6.2831853f * x);
            const float e = std::min ({ 1.f, x / 0.12f, (1.f - x) / 0.12f });
            tFlat[(size_t) i] = 0.5f - 0.5f * std::cos (3.14159265f * std::max (0.f, e));
        }
        tPerc[255] = tHann[255] = tFlat[255] = 0.f;
    }

    void spawn (const Params& p, double rate, bool grid)
    {
        Grain* slot = nullptr;
        for (auto& g : grains) if (! g.on) { slot = &g; break; }
        if (slot == nullptr) return;
        const double ratio = std::exp2 (std::clamp (p.pitch, -24.f, 24.f) / 12.0);
        double lenS = 0.02 * std::pow (75.0, (double) std::clamp (p.size, 0.f, 1.f));
        lenS = std::min (lenS, 3.0 / std::max (1.0, ratio));
        const int len = std::max (32, (int) (lenS * sr));
        const double span = (double) len * ratio;
        const float jitter = grid ? 0.f : 0.03f * rng.bi();
        const double pos = std::clamp ((double) p.position + (double) jitter, 0.0, 1.0);
        const double delay = 0.02 * sr + pos * ((double) N - 3.3 * sr);
        double start = (double) wr - delay - span;
        while (start < 0.0) start += (double) N;
        slot->on = true; slot->pos = start; slot->inc = ratio; slot->age = 0; slot->len = len;
        const float t = std::clamp (p.texture, 0.f, 1.f);
        if (t < 0.5f) { slot->wa = tPerc.data(); slot->wb = tHann.data(); slot->mix = t * 2.f; }
        else          { slot->wa = tHann.data(); slot->wb = tFlat.data(); slot->mix = (t - 0.5f) * 2.f; }
        const float pan = 0.5f + 0.5f * rng.bi() * std::clamp (p.spread, 0.f, 1.f);
        slot->gl = 1.4142136f * std::cos (pan * 1.5707963f); slot->gr = 1.4142136f * std::sin (pan * 1.5707963f);
        const double overlap = rate * lenS;
        slot->gain = (float) (0.9 / std::sqrt (std::max (1.0, overlap)));
        const int c = count.load();
        ring[(size_t) (c % ringSize)].store (((int) (start * 4096.0 / N) & 4095) | ((std::min (4095, (int) (span * 4096.0 / N))) << 12));
        count.store (c + 1);
    }

    double sr = 48000.0, countdown = 0.0;
    int N = 1, wr = 0, lastBin = -1;
    long lastK = -1;
    std::array<std::vector<float>, 2> buf;
    std::array<Grain, maxGrains> grains {};
    std::array<float, 256> tPerc {}, tHann {}, tFlat {};
    float hpL = 0.f, hpR = 0.f, fbL = 0.f, fbR = 0.f, hpK = 0.01f;
    gg::Rng rng { 1 };
    Reverb rev;
    std::array<std::atomic<float>, bins> peaks {};
    std::array<std::atomic<int>, ringSize> ring {};
    std::atomic<int> count { 0 };
    std::atomic<float> writeFrac { 0.f };
};
} // namespace cl
