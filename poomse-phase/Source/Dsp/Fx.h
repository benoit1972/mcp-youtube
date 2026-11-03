#pragma once
#include "Rng.h"
#include <algorithm>
#include <cmath>
#include <vector>

// The "dusty night" side of the sound: a muffling low-pass, vinyl crackle, a little tape wobble and a long echo.
namespace ph
{
// Dark: two cascaded one-pole low-passes. amount 0 = open (18 kHz), 1 = very muffled (about 900 Hz).
class Dark
{
public:
    void prepare (double sampleRate) { sr = sampleRate; z[0] = z[1] = z[2] = z[3] = 0.f; }
    static float cutoffFor (float amount) { return 18000.f * std::pow (900.f / 18000.f, std::clamp (amount, 0.f, 1.f)); }
    void process (float& l, float& r, float amount)
    {
        const float fc = std::min (cutoffFor (amount), (float) sr * 0.45f);
        const float a = 1.f - std::exp (-2.f * 3.14159265f * fc / (float) sr);
        z[0] += a * (l - z[0]); z[1] += a * (z[0] - z[1]);
        z[2] += a * (r - z[2]); z[3] += a * (z[2] - z[3]);
        l = z[1]; r = z[3];
    }
private:
    double sr = 48000.0;
    float z[4] {};
};

// Crackle: surface noise of an old record: a faint hiss and random pops of different sizes.
class Crackle
{
public:
    void prepare (double sampleRate, uint32_t seed = 0xBEEFu) { sr = sampleRate; rng = gg::Rng (seed); popL = popR = 0.f; hissL = hissR = 0.f; }
    // The amount follows a curve that stays almost silent at the bottom of the knob: the number of pops grows
    // with the square of the amount (1 % = practically none, 25 % = a pop every second or so, 100 % = a worn record),
    // and a pop that does happen is always at least a little audible.
    void process (float amount, float& l, float& r)
    {
        amount = std::clamp (amount, 0.f, 1.f);
        l = r = 0.f;
        const float shaped = amount * amount;
        if (shaped < 1e-6f) { popL *= 0.9f; popR *= 0.9f; return; }
        const float pPop = 0.0005f * shaped * (48000.f / (float) sr);
        const float size = 0.15f + 0.7f * amount;
        if (rng.uni() < pPop) { const float a = rng.uni(); popL += (rng.bi() < 0.f ? -1.f : 1.f) * (0.3f + 0.7f * a * a) * size; }
        if (rng.uni() < pPop) { const float a = rng.uni(); popR += (rng.bi() < 0.f ? -1.f : 1.f) * (0.3f + 0.7f * a * a) * size; }
        l = popL; r = popR;
        popL *= 0.82f; popR *= 0.82f;                                  // a pop lasts about a millisecond
        hissL += 0.2f * (rng.bi() - hissL); hissR += 0.2f * (rng.bi() - hissR);
        l += hissL * 0.012f * shaped; r += hissR * 0.012f * shaped;    // the hiss only exists at higher amounts
    }
private:
    double sr = 48000.0;
    float popL = 0.f, popR = 0.f, hissL = 0.f, hissR = 0.f;
    gg::Rng rng;
};

// Tape: slow pitch wobble (a modulated short delay), soft saturation and a touch of high-frequency loss.
class Tape
{
public:
    void prepare (double sampleRate, uint32_t seed = 0x7A9Eu)
    {
        sr = sampleRate; size = (int) (0.05 * sr) + 8;
        bufL.assign ((size_t) size, 0.f); bufR.assign ((size_t) size, 0.f);
        w = 0; wowPh = flutPh = 0.f; drift = 0.f; lpL = lpR = 0.f; rng = gg::Rng (seed);
    }
    void process (float& l, float& r, float amount)
    {
        bufL[(size_t) w] = l; bufR[(size_t) w] = r;
        if (amount > 1e-4f)
        {
            wowPh += 2.f * 3.14159265f * 0.55f / (float) sr; flutPh += 2.f * 3.14159265f * 6.2f / (float) sr;
            if (wowPh > 6.2831853f) wowPh -= 6.2831853f;
            if (flutPh > 6.2831853f) flutPh -= 6.2831853f;
            drift += (rng.bi() - drift) * 0.0005f;
            const float modMs = amount * (2.2f * std::sin (wowPh) + 0.35f * std::sin (flutPh) + 1.5f * drift);
            const float d = std::clamp ((4.f + modMs) * 0.001f * (float) sr, 1.f, (float) size - 3.f);
            float rp = (float) w - d; if (rp < 0.f) rp += (float) size;
            const int i0 = std::min ((int) rp, size - 1), i1 = i0 + 1 >= size ? 0 : i0 + 1;
            const float fr = rp - (float) i0;
            float xl = bufL[(size_t) i0] + fr * (bufL[(size_t) i1] - bufL[(size_t) i0]);
            float xr = bufR[(size_t) i0] + fr * (bufR[(size_t) i1] - bufR[(size_t) i0]);
            const float drive = 1.f + 1.5f * amount, mk = 1.f / std::sqrt (drive);
            xl = std::tanh (xl * drive) * mk; xr = std::tanh (xr * drive) * mk;
            const float a = std::exp (-2.f * 3.14159265f * (16000.f - 9000.f * amount) / (float) sr);
            lpL = xl + a * (lpL - xl); lpR = xr + a * (lpR - xr);
            l = lpL; r = lpR;
        }
        w = w + 1 >= size ? 0 : w + 1;
    }
private:
    double sr = 48000.0; int size = 1, w = 0;
    std::vector<float> bufL, bufR;
    float wowPh = 0.f, flutPh = 0.f, drift = 0.f, lpL = 0.f, lpR = 0.f;
    gg::Rng rng;
};

// Echo: stereo ping-pong with a dark feedback path; the time is given in samples (the plugin sets it from the tempo).
class Echo
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate; size = (int) (3.0 * sr) + 4;
        bufL.assign ((size_t) size, 0.f); bufR.assign ((size_t) size, 0.f);
        w = 0; cur = (float) (0.5 * sr); lpL = lpR = 0.f; k = 1.f - std::exp (-2.f * 3.14159265f * 3500.f / (float) sr);
    }
    void process (float& l, float& r, float timeSamples, float feedback, float mix)
    {
        const float target = std::clamp (timeSamples, 64.f, (float) size - 4.f);
        cur += (target - cur) * 0.0004f;
        float rp = (float) w - cur; if (rp < 0.f) rp += (float) size;
        const int i0 = std::min ((int) rp, size - 1), i1 = i0 + 1 >= size ? 0 : i0 + 1;
        const float fr = rp - (float) i0;
        const float dl = bufL[(size_t) i0] + fr * (bufL[(size_t) i1] - bufL[(size_t) i0]);
        const float dr = bufR[(size_t) i0] + fr * (bufR[(size_t) i1] - bufR[(size_t) i0]);
        lpL += k * (dl - lpL); lpR += k * (dr - lpR);
        const float fb = std::clamp (feedback, 0.f, 0.85f);
        bufL[(size_t) w] = (l + r) * 0.5f + lpR * fb;
        bufR[(size_t) w] = lpL * fb;
        w = w + 1 >= size ? 0 : w + 1;
        l += dl * mix; r += dr * mix;
    }
private:
    double sr = 48000.0; int size = 1, w = 0;
    std::vector<float> bufL, bufR;
    float cur = 0.f, lpL = 0.f, lpR = 0.f, k = 0.1f;
};
} // namespace ph
