#pragma once
#include "Rng.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// The loop finder: reads a record and looks for the tiny places where the sound can close on itself without a click.
// For a start point s and a length L it compares what happens around s with what happens around s+L (normalised
// cross-correlation of a short window on both sides). A high score means "the end flows into the start": a loop.
namespace lp
{
struct FoundLoop
{
    int start = 0;       // in samples of the record
    int length = 0;
    float score = 0.f;   // 0..1, how seamless the loop is
    float level = 0.f;   // loudness relative to the loudest part of the record
};

class Atlas
{
public:
    const std::vector<FoundLoop>& loops() const { return found; }
    int sourceLength() const { return n; }

    // mono: the record, sr: its sample rate. About 300 M multiply-adds for a long record: run it off the audio thread.
    void analyse (const float* mono, int num, double sr)
    {
        found.clear();
        n = num;
        if (num < (int) (sr * 0.25) || sr <= 0.0) return;

        const int d = std::max (1, (int) std::lround (sr / 12000.0));   // analyse at about 12 kHz
        const int m = num / d;
        const double sd = sr / (double) d;
        std::vector<float> x ((size_t) m);
        for (int i = 0; i < m; ++i)
        {
            float s = 0.f;
            for (int k = 0; k < d; ++k) s += mono[(size_t) i * (size_t) d + (size_t) k];
            x[(size_t) i] = s / (float) d;
        }
        std::vector<double> cum ((size_t) m + 1, 0.0);
        for (int i = 0; i < m; ++i) cum[(size_t) i + 1] = cum[(size_t) i] + (double) x[(size_t) i] * (double) x[(size_t) i];
        auto rmsOf = [&] (int s, int e)
        {
            s = std::clamp (s, 0, m); e = std::clamp (e, s + 1, m);
            return std::sqrt (std::max (0.0, (cum[(size_t) e] - cum[(size_t) s]) / (double) std::max (1, e - s)));
        };
        const int win = std::max (8, (int) (0.1 * sd));
        double peak = 1e-9;
        for (int i = 0; i + win <= m; i += win / 2) peak = std::max (peak, rmsOf (i, i + win));
        if (peak < 1e-5) return;   // silence: nothing to find

        static const double lens[5] = { 0.045, 0.09, 0.17, 0.33, 0.65 };      // five sizes of loop, in seconds
        const int anchors = std::clamp ((int) ((double) num / sr / 0.12), 1, 200);   // a place to look about every 120 ms
        const int jitter = std::max (4, (int) (0.02 * sd));
        for (int ai = 0; ai < anchors; ++ai)
        {
            const int centre = (int) (((double) ai + 0.5) / (double) anchors * (double) m);
            for (int li = 0; li < 5; ++li)
            {
                const double ld = lens[li] * sd;
                const int B = std::clamp ((int) (ld / 3.0), 16, 128);
                float bestScore = 0.f; int bestS = -1, bestL = 0;
                for (int s = centre - jitter; s <= centre + jitter; s += 4)
                {
                    if (s - B < 0) continue;
                    // reference window around the start
                    double e1 = 0.0;
                    for (int k = -B; k < B; ++k) e1 += (double) x[(size_t) (s + k)] * (double) x[(size_t) (s + k)];
                    if (e1 < 1e-9) continue;
                    for (int fk = 0; fk < 10; ++fk)
                    {
                        const int L = (int) std::lround (ld * 0.8 * std::pow (1.25 / 0.8, (double) fk / 9.0));
                        const int e = s + L;
                        if (e + B > m) break;
                        double dot = 0.0, e2 = 0.0;
                        for (int k = -B; k < B; ++k)
                        {
                            const double a = (double) x[(size_t) (s + k)], b = (double) x[(size_t) (e + k)];
                            dot += a * b; e2 += b * b;
                        }
                        if (e2 < 1e-9) continue;
                        const float sc = (float) (dot / std::sqrt (e1 * e2));
                        if (sc > bestScore) { bestScore = sc; bestS = s; bestL = L; }
                    }
                }
                if (bestS < 0 || bestScore < 0.6f) continue;
                const float level = (float) (rmsOf (bestS, bestS + bestL) / peak);
                if (level < 0.08f) continue;                       // not the hiss between two tracks
                FoundLoop f;
                f.start = bestS * d; f.length = bestL * d; f.score = std::min (1.f, bestScore); f.level = std::min (1.f, level);
                // fine search at the full sample rate around the rough answer
                {
                    const int B2 = std::clamp ((int) (ld * d / 3.0), 32, 256);
                    float fb = -1.f; int fs = f.start, fl = f.length;
                    for (int ds = -d; ds <= d; ++ds)
                    {
                        const int s2 = f.start + ds;
                        if (s2 - B2 < 0) continue;
                        double e1 = 0.0;
                        for (int k = -B2; k < B2; ++k) e1 += (double) mono[s2 + k] * (double) mono[s2 + k];
                        if (e1 < 1e-9) continue;
                        for (int dl = -2 * d; dl <= 2 * d; ++dl)
                        {
                            const int e2i = s2 + f.length + dl;
                            if (e2i + B2 > num || f.length + dl < 16) continue;
                            double dot = 0.0, e2 = 0.0;
                            for (int k = -B2; k < B2; ++k) { const double a = (double) mono[s2 + k], b = (double) mono[e2i + k]; dot += a * b; e2 += b * b; }
                            if (e2 < 1e-9) continue;
                            const float sc = (float) (dot / std::sqrt (e1 * e2));
                            if (sc > fb) { fb = sc; fs = s2; fl = f.length + dl; }
                        }
                    }
                    if (fb > 0.f) { f.start = fs; f.length = fl; f.score = std::min (1.f, fb); }
                    if (f.score < 0.6f) continue;
                }
                if (f.start + f.length < num) found.push_back (f);
            }
        }
    }

    // Where a loop sits on the map: x = place in the record (0..1), y = size on a log scale (0 = shortest, 1 = longest).
    static float sizeToY (double lengthSeconds) { return (float) std::clamp (std::log (lengthSeconds / 0.03) / std::log (25.0), 0.0, 1.0); }
    static double yToSeconds (float y) { return 0.03 * std::pow (25.0, (double) std::clamp (y, 0.f, 1.f)); }

    // The loop closest to a point of the map (the record is a circle, so 0 and 1 are neighbours).
    // A seamless loop wins over a rough one at the same distance.
    int nearest (float x01, float y01, double sampleRate) const
    {
        if (found.empty() || n <= 0) return -1;
        int best = -1; float bestCost = 1e9f;
        for (int i = 0; i < (int) found.size(); ++i)
        {
            const auto& f = found[(size_t) i];
            float dx = std::abs ((float) f.start / (float) n - x01);
            dx = std::min (dx, 1.f - dx);
            const float dy = sizeToY ((double) f.length / sampleRate) - y01;
            const float cost = dx * dx * 4.f + dy * dy + 0.05f * (1.f - f.score);
            if (cost < bestCost) { bestCost = cost; best = i; }
        }
        return best;
    }

private:
    std::vector<FoundLoop> found;
    int n = 0;
};

// A record: the sound itself (mono) and what the loop finder found in it.
struct Record
{
    std::vector<float> x;
    double sampleRate = 44100.0;
    Atlas atlas;
    void analyse() { atlas.analyse (x.data(), (int) x.size(), sampleRate); }
};

// The record you get before dropping your own: a worn jazz record, electric piano chords (Dm9, G13, Cmaj9...),
// brushes, and the dust of the groove. Made on the spot, so there is nothing to download.
inline Record makeDefaultRecord (double sr = 44100.0)
{
    Record r;
    r.sampleRate = sr;
    constexpr int nChords = 6;
    const double chordSec = 4.6;
    r.x.assign ((size_t) ((nChords * chordSec + 3.0) * sr), 0.f);
    static const int chords[nChords][5] = {
        { 38, 53, 57, 60, 64 },   // Dm9
        { 43, 53, 59, 62, 64 },   // G13
        { 36, 52, 59, 62, 67 },   // Cmaj9
        { 45, 55, 59, 62, 64 },   // Am11
        { 41, 52, 57, 60, 67 },   // Fmaj9
        { 40, 50, 56, 59, 62 },   // E7#9 (ish)
    };
    gg::Rng rng (0xFACEu);
    const double twoPi = 6.283185307179586;
    for (int c = 0; c < nChords; ++c)
        for (int k = 0; k < 5; ++k)
        {
            const double t0 = (double) c * chordSec + 0.014 * k * (double) rng.uni();
            const double f = 440.0 * std::pow (2.0, (chords[c][k] - 69) / 12.0);
            const double amp = (k == 0 ? 0.5 : 0.26) * (0.8 + 0.4 * (double) rng.uni());
            const double decay = 2.6 - 0.9 * (double) (chords[c][k] - 36) / 36.0;
            const int i0 = (int) (t0 * sr), len = (int) (4.4 * sr);
            for (int i = 0; i < len && i0 + i < (int) r.x.size(); ++i)
            {
                const double t = (double) i / sr;
                const double env = (1.0 - std::exp (-t / 0.005)) * std::exp (-t / decay);
                const double index = 0.25 + 1.3 * std::exp (-t / 0.4);
                const double ph = twoPi * f * t;
                const double trem = 1.0 + 0.1 * std::sin (twoPi * 4.6 * t + k);
                r.x[(size_t) (i0 + i)] += (float) (amp * env * trem * std::sin (ph + index * std::sin (ph)));
            }
        }
    // brushes on every beat and the dust of the groove
    float hp = 0.f, popEnv = 0.f;
    for (int i = 0; i < (int) r.x.size(); ++i)
    {
        const double t = (double) i / sr;
        const double inBeat = std::fmod (t, 0.575);
        const float brush = (float) (0.045 * std::exp (-inBeat / 0.02)) * rng.bi();
        const float w = rng.bi();
        hp += 0.35f * (w - hp);
        const float hiss = 0.004f * (w - hp);
        if (rng.uni() < (float) (5.0 / sr)) popEnv = 0.12f * rng.uni() * rng.uni() * (rng.bi() < 0.f ? -1.f : 1.f);
        r.x[(size_t) i] += brush + hiss + popEnv;
        popEnv *= 0.9f;
    }
    float peak = 1e-6f;
    for (float v : r.x) peak = std::max (peak, std::abs (v));
    for (float& v : r.x) v *= 0.8f / peak;
    return r;
}
} // namespace lp
