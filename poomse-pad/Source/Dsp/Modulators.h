#pragma once
#include "Rng.h"
#include <algorithm>
#include <atomic>
#include <cmath>

// Two modulation sources: a tempo-syncable LFO and a euclidean step sequencer.
// Both advance once per audio block and return a control value for that block.
namespace gg
{
constexpr float kPi = 3.14159265358979323846f;

// 0:1/4 1:1/8 2:1/16 3:1/32 4:1/8T 5:1/16T 6:1/32T, in beats (quarter notes)
inline double divisionBeats (int idx)
{
    static const double t[] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0 };
    return t[std::clamp (idx, 0, 6)];
}

// LFO sync choices: 0 = free (Hz), then 1 bar, 1/2, 1/4, 1/8, 1/16 notes
inline double lfoPeriodBeats (int sync)
{
    static const double t[] = { 0.0, 4.0, 2.0, 1.0, 0.5, 0.25 };
    return t[std::clamp (sync, 0, 5)];
}

class Lfo
{
public:
    // shapes: 0 sine, 1 triangle, 2 saw, 3 square, 4 sample & hold
    void prepare (double sampleRate, uint32_t seed)
    {
        sr = sampleRate; phase = 0.0; held = 0.f; rng = Rng (seed);
    }

    // advance by n samples; returns the value at the end of the block, in -1..1
    float advance (int n, float rateHz, int sync, double bpm, int shape)
    {
        double hz = (double) rateHz;
        if (sync > 0) hz = std::max (20.0, bpm) / 60.0 / lfoPeriodBeats (sync);
        phase += hz / sr * (double) n;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            held = rng.bi();
        }
        return value (phase, shape, held);
    }

    static float value (double ph, int shape, float heldValue)
    {
        switch (std::clamp (shape, 0, 4))
        {
            case 0: return (float) std::sin (2.0 * kPi * ph);
            case 1: return (float) (1.0 - 4.0 * std::abs (ph - 0.5));
            case 2: return (float) (2.0 * ph - 1.0);
            case 3: return ph < 0.5 ? 1.f : -1.f;
            default: return heldValue;
        }
    }

private:
    double sr = 48000.0, phase = 0.0;
    float held = 0.f;
    Rng rng;
};

// Euclidean rhythm: `hits` evenly distributed over `steps` (rotated). Every hit fires an envelope
// (jumps to 1, or to a random level when `randomAmt` > 0) that decays over `decaySec`. Output 0..1.
inline bool euclidHit (int step, int steps, int hits, int rotation)
{
    if (steps < 1 || hits <= 0) return false;
    if (hits >= steps) return true;
    const int j = ((step + rotation) % steps + steps) % steps;
    return (j * hits) % steps < hits;
}

struct SeqParams
{
    int steps = 8, hits = 3, rotation = 0;
    int rateDiv = 2;          // index into divisionBeats() (1/4 ... 1/32T)
    float decaySec = 0.25f;
    float randomAmt = 0.f;    // 0..1: how much each hit's level is randomly reduced
    double bpm = 120.0;
};

class EuclidSeq
{
public:
    void prepare (double sampleRate, uint32_t seed)
    {
        sr = sampleRate; rng = Rng (seed); untilStep = 0.0; stepIndex = 0; level = 0.f; current.store (0);
    }

    float advance (int n, const SeqParams& p)
    {
        const int steps = std::clamp (p.steps, 1, 16);
        const double stepLen = std::max (64.0, 60.0 / std::max (20.0, p.bpm) * sr * divisionBeats (p.rateDiv));
        const float perSample = std::exp (-1.f / (std::max (0.005f, p.decaySec) * (float) sr));
        int last = 0;
        while (untilStep < (double) n)
        {
            const int at = (int) untilStep;
            level *= std::pow (perSample, (float) (at - last));
            last = at;
            const int idx = stepIndex % steps;
            current.store (idx);
            if (euclidHit (idx, steps, std::clamp (p.hits, 0, steps), p.rotation))
                level = 1.f - std::clamp (p.randomAmt, 0.f, 1.f) * rng.uni();
            ++stepIndex;
            untilStep += stepLen;
        }
        untilStep -= (double) n;
        level *= std::pow (perSample, (float) (n - last));
        return level;
    }

    int currentStep() const { return current.load(); }

private:
    double sr = 48000.0, untilStep = 0.0;
    int stepIndex = 0;
    float level = 0.f;
    std::atomic<int> current { 0 };
    Rng rng;
};
} // namespace gg
