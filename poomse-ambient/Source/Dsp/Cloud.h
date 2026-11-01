#pragma once
#include "Rng.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// A granular "cloud" fed by the music itself: the last seconds of the piano / bells / voices are cut into
// soft grains that come back later, an octave up, a fifth up or an octave down. It is not another sound to manage,
// just a halo that makes the notes bloom and echo in a way that stays in tune with them.
namespace amb
{
class Cloud
{
public:
    static constexpr int maxGrains = 20;

    void prepare (double sampleRate, uint32_t seed = 0xC10Du)
    {
        sr = sampleRate;
        size = (int) (6.0 * sr);
        bufL.assign ((size_t) size, 0.f); bufR.assign ((size_t) size, 0.f);
        writeIdx = 0; countdown = 0.f;
        for (auto& g : grains) g = Grain{};
        rng = gg::Rng (seed);
    }

    void process (float inL, float inR, float amount, float& outL, float& outR)
    {
        bufL[(size_t) writeIdx] = inL; bufR[(size_t) writeIdx] = inR;
        writeIdx = writeIdx + 1 >= size ? 0 : writeIdx + 1;

        amount = std::clamp (amount, 0.f, 1.f);
        if (amount > 0.001f)
        {
            countdown -= 1.f;
            if (countdown <= 0.f)
            {
                spawn();
                countdown += (float) sr / (2.0f + 6.0f * amount) * (0.7f + 0.6f * rng.uni());
            }
        }

        float l = 0.f, r = 0.f;
        for (auto& g : grains)
        {
            if (! g.active) continue;
            const float t = (float) g.age / (float) g.len;
            const float w = 0.5f - 0.5f * std::cos (2.f * 3.14159265f * t);
            int i0 = (int) g.pos; const float f = (float) (g.pos - (double) i0);
            i0 = ((i0 % size) + size) % size;
            const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
            l += (bufL[(size_t) i0] + f * (bufL[(size_t) i1] - bufL[(size_t) i0])) * w * g.gl;
            r += (bufR[(size_t) i0] + f * (bufR[(size_t) i1] - bufR[(size_t) i0])) * w * g.gr;
            g.pos += g.inc;
            if (g.pos >= (double) size) g.pos -= (double) size;
            if (++g.age >= g.len) g.active = false;
        }
        outL = l * (0.2f + 1.3f * amount) * std::min (1.f, amount * 4.f);
        outR = r * (0.2f + 1.3f * amount) * std::min (1.f, amount * 4.f);
    }

private:
    struct Grain { bool active = false; double pos = 0.0, inc = 1.0; int age = 0, len = 1; float gl = 0.f, gr = 0.f; };

    void spawn()
    {
        for (auto& g : grains)
        {
            if (g.active) continue;
            // harmonically safe transpositions: unison, octave up, octave down, fifth up, fourth down
            static const double ratios[] = { 1.0, 2.0, 0.5, 1.5, 0.75 };
            static const float cumulative[] = { 0.40f, 0.65f, 0.80f, 0.92f, 1.0f };
            const float r = rng.uni(); int k = 0;
            while (k < 4 && r > cumulative[k]) ++k;
            g.len = (int) ((0.25f + 0.45f * rng.uni()) * (float) sr);
            g.inc = ratios[k];
            const double lag = (0.4 + 4.2 * (double) rng.uni()) * sr;
            g.pos = (double) writeIdx - lag; while (g.pos < 0.0) g.pos += (double) size;
            const float pan = rng.bi(), a = (pan + 1.f) * 0.7853982f;
            g.gl = std::cos (a) * 0.9f; g.gr = std::sin (a) * 0.9f;
            g.age = 0; g.active = true;
            return;
        }
    }

    double sr = 48000.0;
    int size = 1, writeIdx = 0;
    float countdown = 0.f;
    std::vector<float> bufL, bufR;
    std::array<Grain, maxGrains> grains;
    gg::Rng rng;
};
} // namespace amb
