#pragma once
#include "Rng.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// A kit is a drum break cut into its hits. The cutter listens for attacks (a sudden rise in loudness), cuts the break
// there, and describes every piece: how loud it is and how bright it sounds (a kick is dark, a hat is bright).
namespace sh
{
struct Slice
{
    int start = 0, length = 0;   // in samples of the break
    float level = 0.f;           // 0..1 relative to the loudest hit
    float bright = 0.f;          // 0..1 raw brightness (low-band vs high-band energy)
    float x = 0.f;               // place in the break, 0..1
    float y = 0.f;               // brightness RANK, 0 (darkest hit) .. 1 (brightest): spread evenly, whatever the break
};

struct Kit
{
    std::vector<float> x;
    double sampleRate = 44100.0;
    std::vector<Slice> slices;

    void analyse()
    {
        slices.clear();
        const int n = (int) x.size();
        if (n < (int) (sampleRate * 0.2) || sampleRate <= 0.0) return;

        const int hop = std::max (16, (int) (sampleRate * 0.0029));
        const int frames = n / hop;
        if (frames < 8) return;
        // Loudness as a peak envelope (instant attack, 40 ms release): steady even on a 50 Hz kick, unlike frame energy.
        // Two ears: the whole sound, and only the highs. A hat falling on the tail of a kick is invisible to the first
        // one but plain to the second.
        const float hpA = 1.f - std::exp (-6.2831853f * 2000.f / (float) sampleRate);
        const double rel = std::exp (-1.0 / (0.04 * sampleRate));
        std::vector<double> E[2], L[2];
        for (int b = 0; b < 2; ++b) { E[b].assign ((size_t) frames, 0.0); L[b].assign ((size_t) frames, 0.0); }
        double env[2] = { 0.0, 0.0 }, peak[2] = { 1e-12, 1e-12 };
        float lp = 0.f;
        for (int f = 0; f < frames; ++f)
        {
            for (int i = 0; i < hop; ++i)
            {
                const float v = x[(size_t) (f * hop + i)];
                lp += hpA * (v - lp);
                env[0] = std::max ((double) std::abs (v), env[0] * rel);
                env[1] = std::max ((double) std::abs (v - lp), env[1] * rel);
            }
            for (int b = 0; b < 2; ++b)
            {
                E[b][(size_t) f] = env[b]; peak[b] = std::max (peak[b], env[b]);
                L[b][(size_t) f] = 20.0 * std::log10 (env[b] + 1e-7);
            }
        }
        if (peak[0] < 1e-4) return;                     // silence
        const int minGap = std::max (2, (int) (0.035 * sampleRate / hop));

        std::vector<int> found;
        for (int b = 0; b < 2; ++b)
        {
            const double floorE = peak[b] * (b == 0 ? 0.01 : 0.03);
            double prev = -140.0;
            for (int f = 0; f < frames; ++f)
            {
                const double jump = L[b][(size_t) f] - prev;
                prev = L[b][(size_t) f];
                if (E[b][(size_t) f] >= floorE && jump >= 6.0) found.push_back (f);   // a hit lifts the level by 6 dB in 3 ms
            }
        }
        std::sort (found.begin(), found.end());
        std::vector<int> onsetFrames;
        for (int f : found) if (onsetFrames.empty() || f - onsetFrames.back() >= minGap) onsetFrames.push_back (f);

        // exact start: the first sample of that frame region that is clearly audible
        for (size_t k = 0; k < onsetFrames.size(); ++k)
        {
            const int f = onsetFrames[k];
            const int a = std::max (0, (f - 1) * hop), b = std::min (n, (f + 3) * hop);
            float pk = 0.f;
            for (int i = a; i < b; ++i) pk = std::max (pk, std::abs (x[(size_t) i]));
            int s = a;
            for (int i = a; i < b; ++i) if (std::abs (x[(size_t) i]) > 0.12f * pk) { s = i; break; }
            Slice sl;
            sl.start = std::max (0, s - (int) (0.0008 * sampleRate));
            slices.push_back (sl);
        }
        // a sound with no clear attacks (a pad, a voice, a field recording) is cut into 16 equal pieces instead: any sound can be played
        if (slices.size() < 4)
        {
            slices.clear();
            for (int i = 0; i < 16; ++i) { Slice sl; sl.start = (int) ((long long) i * n / 16); slices.push_back (sl); }
        }

        // lengths, loudness, brightness
        const int maxLen = (int) (0.7 * sampleRate), minLen = (int) (0.03 * sampleRate);
        double loudest = 1e-12;
        std::vector<double> rmsOf (slices.size());
        for (size_t k = 0; k < slices.size(); ++k)
        {
            auto& s = slices[k];
            const int next = k + 1 < slices.size() ? slices[k + 1].start : n;
            s.length = std::clamp (next - s.start, minLen, maxLen);
            if (s.start + s.length > n) s.length = n - s.start;

            const int an = std::min (s.length, (int) (0.02 * sampleRate));
            const float a1 = 1.f - std::exp (-6.2831853f * 250.f / (float) sampleRate), a2 = 1.f - std::exp (-6.2831853f * 2500.f / (float) sampleRate);
            // energy of the dark / middle / bright parts of the sound between two points
            auto bands = [&] (int from, int count, double out[3])
            {
                out[0] = out[1] = out[2] = 0.0;
                float lp1 = 0.f, lp2 = 0.f;
                for (int i = 0; i < count; ++i)
                {
                    const float v = from + i >= 0 ? x[(size_t) (from + i)] : 0.f;
                    lp1 += a1 * (v - lp1); lp2 += a2 * (v - lp2);
                    const float low = lp1, mid = lp2 - lp1, high = v - lp2;
                    out[0] += (double) low * low; out[1] += (double) mid * mid; out[2] += (double) high * high;
                }
            };
            double now[3], before[3];
            bands (s.start, an, now);
            bands (s.start - an, an, before);
            // what the hit adds: the tail of the sound before it (a kick still ringing under a hat) does not count
            double el = std::max (0.0, now[0] - 0.9 * before[0]), em = std::max (0.0, now[1] - 0.9 * before[1]), eh = std::max (0.0, now[2] - 0.9 * before[2]);
            const double e = now[0] + now[1] + now[2];
            const double tot = el + em + eh + 1e-12;
            s.bright = (float) ((0.5 * em + 1.0 * eh) / tot);
            rmsOf[k] = std::sqrt (e / std::max (1, an));
            loudest = std::max (loudest, rmsOf[k]);
            s.x = (float) s.start / (float) n;
        }
        for (size_t k = 0; k < slices.size(); ++k) slices[k].level = (float) std::min (1.0, rmsOf[k] / loudest);

        // keep the 200 strongest if there are more
        if (slices.size() > 200)
        {
            std::vector<Slice> v = slices;
            std::sort (v.begin(), v.end(), [] (const Slice& a, const Slice& b) { return a.level > b.level; });
            v.resize (200);
            std::sort (v.begin(), v.end(), [] (const Slice& a, const Slice& b) { return a.start < b.start; });
            slices = v;
        }
        rank();
    }

    // y = rank of the brightness: the map is evenly filled whatever the break (a break with few hats still has a top row)
    void rank()
    {
        std::vector<int> idx (slices.size());
        for (size_t i = 0; i < idx.size(); ++i) idx[i] = (int) i;
        std::sort (idx.begin(), idx.end(), [&] (int a, int b) { return slices[(size_t) a].bright < slices[(size_t) b].bright; });
        const float d = (float) std::max<size_t> (1, slices.size() - 1);
        for (size_t r = 0; r < idx.size(); ++r) slices[(size_t) idx[r]].y = (float) r / d;
    }

    // The slice a key plays: the one closest to (place, brightness of the key). Brightness counts most, so a key keeps its
    // role (low keys = dark hits) while the place chooses *which* dark hit.
    int nearest (float place, float y) const
    {
        int best = -1; float bestCost = 1e9f;
        for (int i = 0; i < (int) slices.size(); ++i)
        {
            const auto& s = slices[(size_t) i];
            const float dx = s.x - place, dy = s.y - y;
            const float cost = dy * dy + 0.25f * dx * dx + (s.level < 0.15f ? 0.05f : 0.f);
            if (cost < bestCost) { bestCost = cost; best = i; }
        }
        return best;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// The break you get before dropping your own: a plain "boom, tchak" with a hat in the air. Two bars at 90 bpm: a soft round kick,
// a cracked snare on 2 and 4, hats on the off-beats (the last one open). Made on the spot. Simple on purpose: the plugin is
// there to make something of it.
struct TruthHit { double time; int kind; };   // kind: 0 kick, 1 snare, 2 hat, 3 other

namespace detail
{
inline void kick (std::vector<float>& b, size_t at, double sr, gg::Rng& r, float amp)
{
    const double f0 = 52.0 + 3.0 * r.uni(), f1 = f0 * 2.7, decay = 0.11 + 0.01 * r.uni();
    double ph = 0.0;
    const int len = (int) (0.5 * sr);
    for (int i = 0; i < len && at + (size_t) i < b.size(); ++i)
    {
        const double t = i / sr;
        ph += 6.283185307179586 * (f0 + (f1 - f0) * std::exp (-t / 0.03)) / sr;
        double s = std::tanh (1.5 * std::sin (ph) * std::exp (-t / decay)) * 0.8;
        if (i < 90) s += 0.35 * r.bi() * std::exp (-i / 14.0);
        b[at + (size_t) i] += (float) (amp * s);
    }
}
inline void snare (std::vector<float>& b, size_t at, double sr, gg::Rng& r, float amp)
{
    const double f = 180.0 + 6.0 * r.uni(), body = 0.08, tail = 0.12;
    float lp = 0.f;
    const int len = (int) (0.4 * sr);
    for (int i = 0; i < len && at + (size_t) i < b.size(); ++i)
    {
        const double t = i / sr;
        const float n = r.bi();
        lp += 0.25f * (n - lp);
        const double noise = (n - lp) * std::exp (-t / tail) * 0.8;
        const double tone = std::sin (6.283185307179586 * f * t) * std::exp (-t / body) * 0.5;
        b[at + (size_t) i] += (float) (amp * (noise + tone));
    }
}
inline void hat (std::vector<float>& b, size_t at, double sr, gg::Rng& r, float amp, bool open)
{
    float lp = 0.f;
    const double dec = open ? 0.11 : 0.02;
    const int len = (int) ((open ? 0.45 : 0.1) * sr);
    for (int i = 0; i < len && at + (size_t) i < b.size(); ++i)
    {
        const double t = i / sr;
        const float n = r.bi();
        lp += 0.5f * (n - lp);
        double s = (n - lp) * std::exp (-t / dec);
        if (open) s += 0.08 * (std::sin (6.28318 * 7100.0 * t) + std::sin (6.28318 * 9300.0 * t)) * std::exp (-t / dec);
        b[at + (size_t) i] += (float) (amp * 0.55 * s);
    }
}
} // namespace detail

inline Kit makeDefaultKit (double sr = 44100.0, std::vector<TruthHit>* truth = nullptr)
{
    Kit k;
    k.sampleRate = sr;
    const double step = 60.0 / 90.0 / 4.0;
    constexpr int bars = 2, steps = 16;
    k.x.assign ((size_t) ((bars * steps * step + 0.5) * sr), 0.f);
    static const char* kick[bars]  = { "k......k........", "k......k...k...." };
    static const char* snare[bars] = { "....s.......s...", "....s.......s..." };
    static const char* hats[bars]  = { "..h...h...h...o.", "..h...h...h...o." };
    gg::Rng rng (0xB00Du);
    for (int bar = 0; bar < bars; ++bar)
        for (int st = 0; st < steps; ++st)
        {
            const double t = ((double) bar * steps + st) * step;
            const size_t at = (size_t) (t * sr);
            int kind = -1;
            if (kick[bar][st] == 'k')       { detail::kick (k.x, at, sr, rng, 0.9f); kind = 0; }
            else if (snare[bar][st] == 's') { detail::snare (k.x, at, sr, rng, 0.8f); kind = 1; }
            else if (hats[bar][st] == 'h')  { detail::hat (k.x, at, sr, rng, 0.7f, false); kind = 2; }
            else if (hats[bar][st] == 'o')  { detail::hat (k.x, at, sr, rng, 0.8f, true); kind = 2; }
            if (kind >= 0 && truth != nullptr) truth->push_back ({ t, kind });
        }
    // a loop is a circle: what rings on after the end comes back in at the start
    const size_t loopLen = (size_t) (bars * steps * step * sr);
    for (size_t i = loopLen; i < k.x.size(); ++i) k.x[i - loopLen] += k.x[i];
    k.x.resize (loopLen);
    float peak = 1e-6f;
    for (float v : k.x) peak = std::max (peak, std::abs (v));
    for (float& v : k.x) v *= 0.9f / peak;
    return k;
}
} // namespace sh
