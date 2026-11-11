#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "Rng.h"

// The sounds that are built in. Nothing is recorded: they are made here, note by note, with the models of the instruments themselves
// (a plucked string, a struck piano string with its beating and inharmonicity, a bowed string with its body resonances, a detuned lead,
// a chord of detuned saws), and with layers of small events for the places (a street, a brook, a forest).
namespace amb
{
struct Zone                                  // one recording, for the keys around its root note
{
    std::vector<float> x[2];
    double sampleRate = 44100.0;
    int root = 60;
    int loopStart = -1, loopEnd = -1;        // the part that is repeated while the key is held (-1 = one shot)
    int length() const { return (int) x[0].size(); }
    bool loops() const { return loopEnd > loopStart && loopStart >= 0; }
};

enum Category { CatShort = 0, CatSustained, CatPad, CatField, numCategories };
struct Bank
{
    std::vector<Zone> zones;
    bool pitched = true;                     // false: a place. The keys do not change its pitch, they let it in.
    int category = CatShort;
    const Zone& zoneFor (int note) const
    {
        const Zone* best = &zones.front(); int bd = 1 << 20;
        for (const auto& z : zones) { const int d = std::abs (z.root - note); if (d < bd) { bd = d; best = &z; } }
        return *best;
    }
};

enum Sound { SPiano = 0, SViolin, SPad, SForest, numFactory };
inline constexpr const char* kSoundNames[numFactory] = { "Piano", "Violin", "Pad", "Forest" };
inline constexpr const char* kSoundTags[numFactory] = { "short, a piano note", "long, a bowed note", "a held open chord", "a place: birds and wind" };
inline constexpr int kSoundCategory[numFactory] = { CatShort, CatSustained, CatPad, CatField };
inline constexpr const char* kCategoryNames[numCategories] = { "SHORT", "SUSTAINED", "PAD", "FIELD" };

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

// ---- piano: modal synthesis of the strings: inharmonic partials, two or three strings per note that beat, a hammer thud ----
inline Zone makePiano (int note, double sr)
{
    Zone z; z.sampleRate = sr; z.root = note;
    const double f0 = hz (note);
    const double Tf = std::clamp (14.0 * std::pow (2.0, -(note - 36) / 17.0), 1.8, 14.0);
    const int len = (int) (sr * std::clamp (Tf * 0.8, 2.5, 7.0));
    allocate (z, len);
    const double B = 1.2e-4 * std::pow (2.0, (note - 36) / 12.0 * 0.85);
    const int strings = note < 40 ? 1 : note < 52 ? 2 : 3;
    const int nPart = std::max (4, std::min (64, (int) (16000.0 / f0)));
    gg::Rng rng (0x91A40u + (uint32_t) note);
    for (int n = 1; n <= nPart; ++n)
    {
        const double fn = n * f0 * std::sqrt (1.0 + B * n * n);
        if (fn > 0.45 * sr) break;
        const double hammer = std::fabs (std::sin (3.14159265 * n * 0.115));                     // where the hammer strikes
        const double bright = 1.0 / (1.0 + std::pow (fn / 3800.0, 2.2));
        const double amp = (hammer * bright / std::pow ((double) n, 0.75)) * 0.55;
        if (amp < 1e-4) continue;
        const double Tn = std::max (0.06, Tf / (1.0 + 0.55 * std::pow ((double) n - 1.0, 1.05)));
        const float pan = 0.5f + 0.28f * std::sin ((float) n * 1.7f + 0.3f * (float) note);
        for (int s = 0; s < strings; ++s)
        {
            const double cents = strings == 1 ? 0.0 : (s - 0.5 * (strings - 1)) * (0.45 + 0.1 * (n % 3));
            const double fs = fn * std::pow (2.0, cents / 1200.0), w = 2.0 * 3.14159265358979 * fs / sr;
            const double r = std::exp (-1.0 / (Tn * (s == 0 ? 1.0 : 0.8) * sr));              // the sympathetic strings die sooner
            const double c1 = 2.0 * r * std::cos (w), c2 = r * r, a = amp / strings * (0.9 + 0.2 * rng.uni());
            double y2 = 0.0, y1 = a * r * std::sin (w);
            const float gl = std::cos (pan * 1.5707963f), gr = std::sin (pan * 1.5707963f);
            for (int i = 0; i < len; ++i)
            {
                z.x[0][(size_t) i] += (float) y1 * gl; z.x[1][(size_t) i] += (float) y1 * gr;
                const double y = c1 * y1 - c2 * y2; y2 = y1; y1 = y;
            }
        }
    }
    // the hammer: a short dull knock, and a softly rising attack
    float lp = 0.f;
    for (int i = 0; i < (int) (0.05 * sr); ++i)
    {
        lp += 0.06f * (rng.bi() - lp);
        const float e = std::exp (-(float) i / (0.009f * (float) sr)) * 0.5f;
        z.x[0][(size_t) i] += lp * e; z.x[1][(size_t) i] += lp * e;
    }
    for (auto& ch : z.x) for (int i = 0; i < (int) (0.02 * sr); ++i) ch[(size_t) i] *= 1.f - std::exp (-(float) i / (0.0018f * (float) sr));
    fadeEnd (z, (int) (0.5 * sr));
    normalise (z, 0.85f);
    return z;
}

// ---- bowed string: a sawtooth (the stick-slip of the bow) through the resonances of the body, with a vibrato that comes in late ----
inline Zone makeViolin (int note, double sr)
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

// ---- pad: a chord of detuned saws, a different set of detunings on each side, a filter that moves once round the loop ----
inline Zone makePad (int note, double sr)
{
    Zone z; z.sampleRate = sr; z.root = note;
    const double f0 = hz (note);
    const double loopLen = 10.0;
    const int a = (int) (2.6 * sr), loopN = (int) (loopLen * sr), b = a + loopN, len = b;
    allocate (z, len);
    static const int chord[5] = { 0, 7, 12, 14, 19 };                // open: no third, so it suits a major or a minor key
    gg::Rng rng (0xADD00u + (uint32_t) note);
    for (int c = 0; c < 2; ++c)
    {
        std::vector<Saw> saws; std::vector<double> inc;
        for (int k = 0; k < 5; ++k)
            for (int d = 0; d < 3; ++d)
            {
                const double cents = (d - 1) * (11.0 + 3.0 * c + 2.0 * k) + (c ? 2.5 : -2.5);
                Saw s; s.ph = rng.uni(); saws.push_back (s);
                inc.push_back (f0 * std::pow (2.0, (chord[k] + cents / 100.0) / 12.0) / sr);
            }
        Svf f1, f2;
        for (int i = 0; i < len; ++i)
        {
            const double t = (double) i / sr;
            const float lfo = 0.5f + 0.5f * std::sin (2.f * 3.14159265f * (float) ((double) (i - a) / sr / loopLen) + 0.6f * (float) c);
            float mix = 0.f;
            for (size_t k = 0; k < saws.size(); ++k) mix += saws[k].next (inc[k]) * (k < 3 ? 1.1f : 0.8f);
            const float fc = 650.f * (1.f + 2.2f * lfo);
            f1.set (fc, 0.8f, sr); f2.set (fc * 1.3f, 0.7f, sr);
            float lp, bp, hp; f1.run (mix * 0.07f, lp, bp, hp); float lp2; f2.run (lp, lp2, bp, hp);
            z.x[c][(size_t) i] = lp2 * smooth01 ((float) (t / 1.8));
        }
    }
    makeLoop (z, a, b, (int) (2.0 * sr));
    normalise (z, 0.85f);
    return z;
}

// ---- places ----
inline void addEvent (std::vector<float>& L, std::vector<float>& R, int at, const std::vector<float>& ev, float gl, float gr)
{
    const int n = (int) L.size();
    for (size_t i = 0; i < ev.size(); ++i) { const int j = at + (int) i; if (j < 0) continue; if (j >= n) break; L[(size_t) j] += ev[i] * gl; R[(size_t) j] += ev[i] * gr; }
}
inline void panGains (float pan, float& gl, float& gr) { gl = std::cos (pan * 1.5707963f); gr = std::sin (pan * 1.5707963f); }
inline void noiseBand (gg::Rng& rng, std::vector<float>& out, float fc, float q, double sr, float amp, bool bandpass = true)
{
    Svf f; f.set (fc, q, sr);
    for (auto& v : out) { float lp, bp, hp; f.run (rng.bi(), lp, bp, hp); v = (bandpass ? bp : lp) * amp; }
}

inline void bird (std::vector<float>& L, std::vector<float>& R, gg::Rng& rng, int at, int kind, float amp, float pan, double sr)
{
    auto sweep = [&] (std::vector<float>& ev, size_t off, double dur, float f0, float f1, float a, float curve)
    {
        const size_t n = (size_t) (dur * sr); double ph = 0.0;
        if (ev.size() < off + n) ev.resize (off + n, 0.f);
        for (size_t i = 0; i < n; ++i)
        {
            const float u = (float) i / (float) n, f = f0 + (f1 - f0) * std::pow (u, curve);
            ph += f / sr; const float e = std::sin (3.14159265f * u);
            ev[off + i] += a * e * e * ((float) std::sin (6.2831853 * ph) + 0.18f * (float) std::sin (12.5663706 * ph));
        }
    };
    std::vector<float> ev;
    if (kind == 0) { sweep (ev, 0, 0.14, 2700.f, 3600.f, 1.f, 1.2f); sweep (ev, (size_t) (0.2 * sr), 0.2, 3300.f, 2400.f, 0.9f, 0.8f); }
    else if (kind == 1) { const int k = 6 + (int) (rng.uni() * 5); for (int i = 0; i < k; ++i) sweep (ev, (size_t) (i * 0.062 * sr), 0.04, 4600.f, 3700.f, 0.8f, 1.f); }
    else if (kind == 2) { sweep (ev, 0, 0.3, 760.f, 700.f, 1.f, 1.f); sweep (ev, (size_t) (0.4 * sr), 0.34, 600.f, 540.f, 0.9f, 1.f); }
    else { sweep (ev, 0, 0.09, 5200.f, 6400.f, 0.8f, 1.f); sweep (ev, (size_t) (0.12 * sr), 0.09, 6200.f, 5000.f, 0.7f, 1.f); sweep (ev, (size_t) (0.24 * sr), 0.12, 5000.f, 6600.f, 0.8f, 1.f); }
    // distance: darker and a short reflection
    const size_t d = (size_t) (0.09 * sr); const size_t n0 = ev.size(); ev.resize (n0 + d + (size_t) (0.2 * sr), 0.f);
    for (size_t i = n0 + d; i-- > d;) ev[i] += 0.3f * ev[i - d];
    for (auto& v : ev) v *= amp;
    float gl, gr; panGains (pan, gl, gr);
    addEvent (L, R, at, ev, gl, gr);
}

inline Zone makeForest (double sr)
{
    Zone z; z.sampleRate = sr; z.root = 60;
    const int N = (int) (36.0 * sr), xf = (int) (3.0 * sr), len = N + xf;
    allocate (z, len);
    gg::Rng rng (0xF0AE57u);
    // wind in the trees: a band of noise whose centre breathes, and the leaves that answer it
    for (int c = 0; c < 2; ++c)
    {
        Svf wind, leaves; float lpn = 0.f;
        for (int i = 0; i < len; ++i)
        {
            const float t = (float) i / (float) sr;
            const float gust = 0.5f + 0.5f * std::sin (6.28318f * (t / 11.f + 0.31f * (float) c)) * std::sin (6.28318f * (t / 17.f + 0.2f));
            wind.set (260.f + 380.f * gust, 0.9f, sr); leaves.set (4200.f, 0.6f, sr);
            const float x = rng.bi(); float l, bp, h, l2, bp2, h2;
            wind.run (x, l, bp, h); leaves.run (x, l2, bp2, h2);
            lpn += 0.02f * (rng.bi() - lpn);
            z.x[c][(size_t) i] += bp * (0.25f + 0.75f * gust) * 0.5f + h2 * gust * gust * 0.05f + lpn * 0.1f;
        }
    }
    // birds
    const int nCalls = 26;
    for (int e = 0; e < nCalls; ++e)
    {
        const int kind = (int) (rng.uni() * 4.f) % 4;
        const float amp = (kind == 2 ? 0.13f : 0.07f) * (0.35f + 0.65f * rng.uni());
        const int at = (int) (rng.uni() * (N - 2.f * sr));
        bird (z.x[0], z.x[1], rng, at, kind, amp, rng.uni(), sr);
        if (rng.uni() < 0.5f) bird (z.x[0], z.x[1], rng, at + (int) ((0.9 + 0.5 * rng.uni()) * sr), kind, amp * 0.85f, rng.uni() < 0.5f ? 0.3f : 0.7f, sr);
    }
    wrapLoop (z, N, xf);
    normalise (z, 0.8f);
    return z;
}

} // namespace detail

inline std::shared_ptr<Bank> makeFactory (int sound, double sr = 44100.0)
{
    auto b = std::make_shared<Bank>();
    b->category = kSoundCategory[std::clamp (sound, 0, (int) numFactory - 1)];
    switch (sound)
    {
        case SPiano:  for (int r : { 36, 48, 60, 72, 84, 96 }) b->zones.push_back (detail::makePiano (r, sr)); break;
        case SViolin: for (int r : { 43, 55, 67, 79 }) b->zones.push_back (detail::makeViolin (r, sr)); break;
        case SPad:    for (int r : { 36, 48, 60, 72 }) b->zones.push_back (detail::makePad (r, sr)); break;
        default:     b->pitched = false; b->zones.push_back (detail::makeForest (32000.0)); break;
    }
    return b;
}
} // namespace amb
