#pragma once
#include "Kit.h"

// The sound you get before dropping your own: eight soft, glassy, pitched notes (all a C4), made on the spot: a glass bell, a
// kalimba, a marimba, a celeste, a plucked string, a slightly detuned bell, a shimmer, a dull wooden knock. They are cut into
// eight pieces like any other sound, and every piece is a note of the same pitch, so a chord can be built from them.
namespace ph
{
constexpr int kBuiltInRoot = 60;
constexpr double kC4 = 261.6255653005986;

namespace detail
{
inline void add (std::vector<float>& b, size_t at, double sr, double seconds, gg::Rng& r, int kind)
{
    const double f = kC4, twoPi = 6.283185307179586;
    const int len = (int) (seconds * sr);
    float lp = 0.f;
    std::vector<float> ks;                                   // for the plucked string
    int ksLen = 0, ksIdx = 0;
    if (kind == 4) { ksLen = std::max (2, (int) std::round (sr / f)); ks.resize ((size_t) ksLen); for (auto& x : ks) x = r.bi(); }
    for (int i = 0; i < len && at + (size_t) i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        double s = 0.0;
        switch (kind)
        {
            case 0: { const double idx = 0.15 + 2.2 * std::exp (-t / 0.30); s = std::sin (twoPi * f * t + idx * std::sin (twoPi * f * 3.5 * t)) * std::exp (-t / 0.9); break; }
            case 1: { s = (std::sin (twoPi * f * t) + 0.35 * std::sin (twoPi * f * 5.4 * t) * std::exp (-t / 0.08)) * std::exp (-t / 0.45); break; }
            case 2: { s = (std::sin (twoPi * f * t) + 0.5 * std::sin (twoPi * f * 4.0 * t) * std::exp (-t / 0.12)) * std::exp (-t / 0.4); break; }
            case 3: { const double idx = 0.5 * std::exp (-t / 0.8); s = std::sin (twoPi * f * t + idx * std::sin (twoPi * f * 2.0 * t)) * std::exp (-t / 1.4); break; }
            case 4: { const float x = ks[(size_t) ksIdx]; const float y = ks[(size_t) ((ksIdx + 1) % ksLen)]; ks[(size_t) ksIdx] = 0.497f * (x + y); ksIdx = (ksIdx + 1) % ksLen; s = (double) x * 0.9; break; }
            case 5: { const double a = 1.0 - std::exp (-t / 0.03); s = a * (std::sin (twoPi * f * t) + std::sin (twoPi * f * 1.004 * t)) * 0.5 * std::exp (-t / 1.5); break; }
            case 6: { s = (std::sin (twoPi * f * t) + 0.45 * std::sin (twoPi * f * 3.0 * t) + 0.25 * std::sin (twoPi * f * 5.0 * t)) * std::exp (-t / 1.1) * (1.0 - std::exp (-t / 0.02)); break; }
            default: { const float n = r.bi(); lp += 0.2f * (n - lp); s = (std::sin (twoPi * f * t) * std::exp (-t / 0.12) + 0.5 * lp * std::exp (-t / 0.02)); break; }
        }
        const double taper = std::min (1.0, (double) (len - i) / (0.2 * sr));          // every note dies away: nothing is cut off
        b[at + (size_t) i] += (float) (0.7 * s * taper);
    }
}
} // namespace detail

inline Kit makeDefaultKit (double sr = 44100.0)
{
    Kit k;
    k.sampleRate = sr;
    constexpr int hits = 8;
    const double gap = 1.6;
    k.x.assign ((size_t) ((hits * gap + 0.5) * sr), 0.f);
    gg::Rng rng (0x61A55u);
    for (int i = 0; i < hits; ++i) detail::add (k.x, (size_t) ((i * gap + 0.05) * sr), sr, 1.5, rng, i);
    float peak = 1e-6f;
    for (float v : k.x) peak = std::max (peak, std::abs (v));
    for (float& v : k.x) v *= 0.85f / peak;
    return k;
}
} // namespace ph
