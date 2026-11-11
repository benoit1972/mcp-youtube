#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include "Rng.h"

// The sound the flock sings with: one sample, looped. Built in: a bowed glass (partials that are not harmonic, as in a glass harmonica) and a bowed string.
namespace mur
{
struct Zone
{
    std::vector<float> x[2];
    double sampleRate = 44100.0;
    int root = 60;
    int loopStart = -1, loopEnd = -1;
    int length() const { return (int) x[0].size(); }
    bool loops() const { return loopEnd > loopStart && loopStart >= 0; }
};

// The scales of Ableton Live (same names).
struct ScaleDef { const char* name; int n; int iv[8]; };
inline constexpr ScaleDef kScales[] = {
    { "Major", 7, { 0, 2, 4, 5, 7, 9, 11, 0 } },          { "Minor", 7, { 0, 2, 3, 5, 7, 8, 10, 0 } },
    { "Dorian", 7, { 0, 2, 3, 5, 7, 9, 10, 0 } },         { "Mixolydian", 7, { 0, 2, 4, 5, 7, 9, 10, 0 } },
    { "Lydian", 7, { 0, 2, 4, 6, 7, 9, 11, 0 } },         { "Phrygian", 7, { 0, 1, 3, 5, 7, 8, 10, 0 } },
    { "Locrian", 7, { 0, 1, 3, 5, 6, 8, 10, 0 } },        { "Whole Tone", 6, { 0, 2, 4, 6, 8, 10, 0, 0 } },
    { "Half-whole Dim.", 8, { 0, 1, 3, 4, 6, 7, 9, 10 } }, { "Whole-half Dim.", 8, { 0, 2, 3, 5, 6, 8, 9, 11 } },
    { "Minor Blues", 6, { 0, 3, 5, 6, 7, 10, 0, 0 } },    { "Minor Pentatonic", 5, { 0, 3, 5, 7, 10, 0, 0, 0 } },
    { "Major Pentatonic", 5, { 0, 2, 4, 7, 9, 0, 0, 0 } }, { "Harmonic Minor", 7, { 0, 2, 3, 5, 7, 8, 11, 0 } },
    { "Harmonic Major", 7, { 0, 2, 4, 5, 7, 8, 11, 0 } }, { "Dorian #4", 7, { 0, 2, 3, 6, 7, 9, 10, 0 } },
    { "Phrygian Dominant", 7, { 0, 1, 4, 5, 7, 8, 10, 0 } }, { "Melodic Minor", 7, { 0, 2, 3, 5, 7, 9, 11, 0 } },
    { "Lydian Augmented", 7, { 0, 2, 4, 6, 8, 9, 11, 0 } }, { "Lydian Dominant", 7, { 0, 2, 4, 6, 7, 9, 10, 0 } },
    { "Super Locrian", 7, { 0, 1, 3, 4, 6, 8, 10, 0 } },  { "8-Tone Spanish", 8, { 0, 1, 3, 4, 5, 6, 8, 10 } },
    { "Bhairav", 7, { 0, 1, 4, 5, 7, 8, 11, 0 } },        { "Hungarian Minor", 7, { 0, 2, 3, 6, 7, 8, 11, 0 } },
    { "Hirajoshi", 5, { 0, 2, 3, 7, 8, 0, 0, 0 } },       { "In-Sen", 5, { 0, 1, 5, 7, 10, 0, 0, 0 } },
    { "Iwato", 5, { 0, 1, 5, 6, 10, 0, 0, 0 } },          { "Kumoi", 5, { 0, 2, 3, 7, 9, 0, 0, 0 } },
    { "Pelog Selisir", 5, { 0, 1, 3, 7, 8, 0, 0, 0 } },   { "Pelog Tembung", 5, { 0, 1, 5, 7, 8, 0, 0, 0 } },
};
inline constexpr int kNumScales = (int) (sizeof (kScales) / sizeof (kScales[0]));
inline constexpr const char* kKeyNames[13] = { "From the note", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

namespace detail
{
inline double hz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }
inline float smooth01 (float x) { x = std::clamp (x, 0.f, 1.f); return x * x * (3.f - 2.f * x); }
inline float smooth01ish (double t) { return smooth01 ((float) ((t - 0.45) / 0.9)); }

struct Svf                                   // a state-variable filter
{
    float ic1 = 0.f, ic2 = 0.f, g = 0.f, k = 1.f, a1 = 0.f, a2 = 0.f, a3 = 0.f;
    void set (float fc, float q, double sr)
    {
        g = std::tan (3.14159265f * std::min (std::max (fc, 10.f), 0.45f * (float) sr) / (float) sr); k = 1.f / q;
        a1 = 1.f / (1.f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void run (float x, float& lp, float& bp, float& hp)
    {
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1; ic2 = 2.f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
};

struct Saw                                   // a band-limited sawtooth
{
    double ph = 0.0;
    static float blep (double t, double dt)
    {
        if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.f;
    }
    float next (double dt)
    {
        float v = (float) (2.0 * ph - 1.0) - blep (ph, dt);
        ph += dt; if (ph >= 1.0) ph -= 1.0;
        return v;
    }
};

inline void normalise (Zone& z, float peak)
{
    float m = 1e-9f;
    for (auto& ch : z.x) for (float v : ch) m = std::max (m, std::abs (v));
    const float g = peak / m;
    for (auto& ch : z.x) for (float& v : ch) v *= g;
}

// seamless loop [a, b): the last xf samples before b melt into the xf samples before a
inline void makeLoop (Zone& z, int a, int b, int xf)
{
    xf = std::min (xf, a);
    for (auto& ch : z.x)
    {
        for (int i = 0; i < xf; ++i)
        {
            const float w = (float) (i + 1) / (float) xf;
            ch[(size_t) (b - xf + i)] = ch[(size_t) (b - xf + i)] * (1.f - w) + ch[(size_t) (a - xf + i)] * w;
        }
        ch.resize ((size_t) b);
    }
    z.loopStart = a; z.loopEnd = b;
}

// a sound that is made of n + xf samples becomes an endless loop of n samples
inline void wrapLoop (Zone& z, int n, int xf)
{
    for (auto& ch : z.x)
    {
        for (int i = 0; i < xf; ++i)
        {
            const float w = (float) (i + 1) / (float) xf;
            ch[(size_t) i] = ch[(size_t) i] * w + ch[(size_t) (n + i)] * (1.f - w);
        }
        ch.resize ((size_t) n);
    }
    z.loopStart = 0; z.loopEnd = n;
}

inline void fadeEnd (Zone& z, int n)
{
    for (auto& ch : z.x) for (int i = 0; i < n; ++i) { const size_t j = ch.size() - 1 - (size_t) i; ch[j] *= (float) i / (float) n; }
}
inline void allocate (Zone& z, int len) { for (auto& ch : z.x) ch.assign ((size_t) len, 0.f); }


// ---- bowed string: a sawtooth (the stick-slip of the bow) through the resonances of the body, with a vibrato that comes in late ----
inline Zone makeViolinZone (int note, double sr)
{
    Zone z; z.sampleRate = sr; z.root = note;
    const double f0 = hz (note);
    const double loopLen = std::round (2.0 * f0) / f0;                                  // a whole number of periods
    const double fv = std::round (5.4 * loopLen) / loopLen;                             // and of vibrato cycles
    const int a = (int) (1.6 * sr), loopN = (int) std::round (loopLen * sr), b = a + loopN, len = b;
    allocate (z, len);
    static const float form[7][3] = { { 290.f, 1.0f, 5.f }, { 460.f, 0.8f, 5.f }, { 700.f, 1.0f, 6.f }, { 1150.f, 0.7f, 6.f }, { 2300.f, 0.9f, 6.f }, { 3300.f, 0.6f, 6.f }, { 5200.f, 0.3f, 4.f } };
    gg::Rng rng (0xB0Eu + (uint32_t) note);
    for (int c = 0; c < 2; ++c)
    {
        Saw saw; saw.ph = c ? 0.37 : 0.0;
        Svf body[7], noiseBp; float pre = 0.f;
        noiseBp.set (3600.f, 1.2f, sr);
        for (int k = 0; k < 7; ++k) body[k].set (form[k][0], form[k][2], sr);
        double vph = c ? 0.13 : 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double t = (double) i / sr;
            const float depth = 0.23f * smooth01ish (t);
            const double vib = std::pow (2.0, depth * std::sin (2.0 * 3.14159265358979 * vph) / 12.0);
            vph += fv / sr; if (vph >= 1.0) vph -= 1.0;
            const float bow = 0.5f * (saw.next (f0 * vib / sr) + 0.0f);
            pre += 0.45f * (bow - pre);
            float sum = 0.28f * pre;
            for (int k = 0; k < 7; ++k) { float lp, bp, hp; body[k].run (pre, lp, bp, hp); sum += bp * form[k][1] * 0.55f; }
            float lp, bp, hp; noiseBp.run (rng.bi(), lp, bp, hp);
            const float wob = 1.f + 0.08f * std::sin (2.f * 3.14159265f * (float) ((double) (i - a) / sr / loopLen));
            const float env = (1.f - std::exp (-(float) t / 0.11f)) * wob;
            z.x[c][(size_t) i] = (sum + 0.05f * bp * (1.f + 3.f * std::exp (-(float) t / 0.08f))) * env;
        }
    }
    makeLoop (z, a, b, (int) (0.12 * sr));
    normalise (z, 0.85f);
    return z;
}


// ---- bowed glass: partials that are not harmonic (1, 2.32, 4.25, 6.63, 9.38), each one a pair that beats slowly, a breath of noise.
//      Every frequency is a whole number of cycles over the loop, so the loop has no seam at all.
inline Zone makeGlassZone (int note, double sr)
{
    Zone z; z.sampleRate = sr; z.root = note;
    const double f0 = hz (note), loopLen = 8.0;
    const int a = (int) (1.2 * sr), loopN = (int) (loopLen * sr), b = a + loopN, len = b;
    allocate (z, len);
    static const double ratio[6] = { 1.0, 2.32, 4.25, 6.63, 9.38, 12.7 }, amp[6] = { 1.0, 0.55, 0.32, 0.2, 0.12, 0.07 };
    gg::Rng rng (0x6A55u + (uint32_t) note);
    Svf bp; bp.set (3200.f, 1.1f, sr);
    for (int c = 0; c < 2; ++c)
    {
        for (int k = 0; k < 6; ++k)
            for (int pair = 0; pair < 2; ++pair)
            {
                double f = f0 * ratio[k] * (pair ? 1.0013 : 0.9987) * (c ? 1.0004 : 1.0);
                f = std::round (f * loopLen) / loopLen;                                     // whole cycles over the loop
                if (f > 0.45 * sr) continue;
                const double shimmerF = std::round ((0.25 + 0.12 * k) * loopLen) / loopLen;   // a slow swell of this partial
                const double w = 2.0 * 3.14159265358979 * f / sr, ws = 2.0 * 3.14159265358979 * shimmerF / sr;
                const double ph0 = (double) rng.uni() * 6.2831853;
                for (int i = 0; i < len; ++i)
                    z.x[c][(size_t) i] += (float) (amp[k] * 0.5 * std::sin (w * (double) i + ph0) * (0.78 + 0.22 * std::sin (ws * (double) (i - a) + (double) k + (double) pair)));
            }
        for (int i = 0; i < len; ++i)
        {
            float lp, bpv, hp; bp.run (rng.bi(), lp, bpv, hp);
            const float t = (float) i / (float) sr;
            z.x[c][(size_t) i] = (z.x[c][(size_t) i] + 0.02f * bpv * (0.6f + 0.4f * std::sin (6.2831853f * t / 8.f))) * smooth01 (t / 1.0f);
        }
    }
    makeLoop (z, a, b, (int) (0.05 * sr));
    normalise (z, 0.85f);
    return z;
}

// a dropped sound becomes a sustained note: its second half, looped
inline Zone makeSustained (std::vector<float> l, std::vector<float> r, double sr, int root)
{
    Zone z; z.sampleRate = sr; z.root = root; z.x[0] = std::move (l); z.x[1] = std::move (r);
    const int n = z.length();
    const int a = std::max (64, (int) (0.4 * n)), xf = std::max (16, std::min (a, (int) std::min (0.45 * (n - a), 1.5 * sr)));
    makeLoop (z, a, n, xf);
    normalise (z, 0.85f);
    return z;
}
} // namespace detail

inline std::shared_ptr<Zone> makeBuiltIn (int which, double sr = 44100.0)
{
    return std::make_shared<Zone> (which == 0 ? detail::makeGlassZone (60, sr) : detail::makeViolinZone (60, sr));
}
} // namespace mur
