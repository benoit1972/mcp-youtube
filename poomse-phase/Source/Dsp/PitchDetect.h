#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace ph
{
// Finds the pitch of a dropped sample (as a MIDI note) so that nobody has to type a root note.
// Looks at the loudest 0.25 s of the first few seconds and takes the strongest autocorrelation peak.
// Returns `fallback` when the sound has no clear pitch (a field recording, noise...).
inline int detectRootNote (const std::vector<float>& mono, double sampleRate, int fallback)
{
    const int n = (int) mono.size();
    const int win = (int) (0.25 * sampleRate);
    if (n < win * 2) return fallback;

    // the loudest window within the first 4 seconds
    const int limit = std::min (n - win, (int) (4.0 * sampleRate));
    const int hop = std::max (1, win / 4);
    int bestStart = 0; double bestE = -1.0;
    for (int s = 0; s <= limit; s += hop)
    {
        double e = 0.0; for (int i = 0; i < win; i += 4) e += (double) mono[(size_t) (s + i)] * mono[(size_t) (s + i)];
        if (e > bestE) { bestE = e; bestStart = s; }
    }
    if (bestE < 1e-6) return fallback;
    const float* x = mono.data() + bestStart;

    const int minLag = (int) (sampleRate / 2000.0), maxLag = std::min (win / 2, (int) (sampleRate / 40.0));
    std::vector<double> r ((size_t) maxLag + 2, 0.0);
    double r0 = 0.0; for (int i = 0; i < win - maxLag; ++i) r0 += (double) x[i] * x[i];
    if (r0 < 1e-9) return fallback;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double acc = 0.0, norm = 0.0;
        for (int i = 0; i < win - maxLag; ++i) { acc += (double) x[i] * x[i + lag]; norm += (double) x[i + lag] * x[i + lag]; }
        r[(size_t) lag] = acc / std::sqrt (r0 * norm + 1e-12);
    }
    double top = 0.0; for (int lag = minLag; lag <= maxLag; ++lag) top = std::max (top, r[(size_t) lag]);
    if (top < 0.55) return fallback;                       // no clear pitch
    // the first peak that is nearly as strong as the best one (avoids picking an octave too low)
    for (int lag = minLag + 1; lag < maxLag; ++lag)
        if (r[(size_t) lag] >= 0.9 * top && r[(size_t) lag] >= r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1])
        {
            const double f = sampleRate / (double) lag;
            return (int) std::lround (69.0 + 12.0 * std::log2 (f / 440.0));
        }
    return fallback;
}
} // namespace ph
