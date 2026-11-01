#pragma once
#include "Rng.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// "Drift": original generative note engine. A tempo-clocked random walk over a musical
// scale that plays slowly evolving notes into the granular synth.
namespace gg
{
struct NoteEvent { int offset = 0; int note = 60; float velocity = 0.f; bool on = true; };

struct GenParams
{
    float probability = 0.f; // 0 = off; chance that a step plays a note
    int rateDiv = 2;         // 0:1 bar 1:1/2 2:1/4 3:1/8 4:1/16
    int scale = 2;           // 0..5 see scaleIntervals(); 6 = "From MIDI" (use scaleMask)
    unsigned scaleMask = 0;  // bit n set = pitch class n (0=C) is allowed; used when scale == 6
    int root = 0;            // 0..11 (C..B)
    float length = 1.5f;     // note length in steps
    double bpm = 120.0;
};

inline double genStepBeats (int idx)
{
    static const double t[] = { 4.0, 2.0, 1.0, 0.5, 0.25 };
    return t[std::clamp (idx, 0, 4)];
}

inline int scaleIntervals (int scale, const int*& iv)
{
    static const int major[]  = { 0, 2, 4, 5, 7, 9, 11 };
    static const int minor[]  = { 0, 2, 3, 5, 7, 8, 10 };
    static const int pent[]   = { 0, 2, 4, 7, 9 };
    static const int dorian[] = { 0, 2, 3, 5, 7, 9, 10 };
    static const int lydian[] = { 0, 2, 4, 6, 7, 9, 11 };
    static const int whole[]  = { 0, 2, 4, 6, 8, 10 };
    switch (std::clamp (scale, 0, 5))
    {
        case 0: iv = major;  return 7;
        case 1: iv = minor;  return 7;
        case 2: iv = pent;   return 5;
        case 3: iv = dorian; return 7;
        case 4: iv = lydian; return 7;
        default: iv = whole; return 6;
    }
}

class NoteGen
{
public:
    static constexpr int maxPending = 16;

    void prepare (double sampleRate, uint32_t seed = 0xD21F7u)
    {
        sr = sampleRate;
        rng = Rng (seed);
        untilStep = 0.0;
        walk = 0;
        for (auto& p : pending) p.left = -1.0;
    }

    // Writes up to maxOut events (offsets within [0,numSamples)); returns the count.
    int generate (int numSamples, const GenParams& p, NoteEvent* out, int maxOut)
    {
        int count = 0;
        auto push = [&] (int off, int note, float vel, bool on)
        {
            if (count < maxOut) out[count++] = { std::clamp (off, 0, std::max (0, numSamples - 1)), note, vel, on };
        };

        // pending note-offs
        for (auto& pn : pending)
        {
            if (pn.left < 0.0) continue;
            if (pn.left < (double) numSamples) { push ((int) pn.left, pn.note, 0.f, false); pn.left = -1.0; }
            else pn.left -= (double) numSamples;
        }

        const double stepLen = std::max (64.0, 60.0 / std::max (20.0, p.bpm) * sr * genStepBeats (p.rateDiv));
        const int* iv = nullptr;
        int midiPcs[12];
        int nDeg = 0;
        int root = p.root;
        if (p.scale == 6 && p.scaleMask != 0)
        {
            // "From MIDI": the scale is whatever pitch classes were recently played; notes are absolute
            for (int pc = 0; pc < 12; ++pc)
                if (p.scaleMask & (1u << pc)) midiPcs[nDeg++] = pc;
            iv = midiPcs;
            root = 0;
        }
        else
            nDeg = scaleIntervals (p.scale == 6 ? 2 : p.scale, iv); // nothing played yet: pentatonic fallback
        const int span = nDeg * 3; // three octaves

        while (untilStep < (double) numSamples)
        {
            if (p.probability > 0.f && rng.uni() < p.probability)
            {
                // random walk: mostly stepwise, sometimes a leap
                const float r = rng.uni();
                const int step = r < 0.35f ? 1 : r < 0.7f ? -1 : r < 0.82f ? 2 : r < 0.94f ? -2 : (int) (rng.bi() * 4.f);
                walk = std::clamp (walk + step, 0, span - 1);
                if (walk == 0 || walk == span - 1) walk = std::clamp (walk + (walk == 0 ? 2 : -2), 0, span - 1);
                const int note = 48 + root + (walk / nDeg) * 12 + iv[walk % nDeg];
                const float vel = 0.4f + 0.5f * rng.uni();
                const int off = (int) untilStep;
                push (off, note, vel, true);

                const double lenSamples = stepLen * std::clamp (p.length, 0.1f, 8.f);
                const double endAt = untilStep + lenSamples;
                // keep note-offs for later blocks in the pending list
                if (endAt < (double) numSamples) push ((int) endAt, note, 0.f, false);
                else
                {
                    Pending* slot = nullptr;
                    for (auto& pn : pending) if (pn.left < 0.0) { slot = &pn; break; }
                    if (slot == nullptr) // table full: release the oldest note now rather than leave it stuck
                    {
                        slot = &pending[0];
                        push (off, slot->note, 0.f, false);
                    }
                    *slot = { note, endAt - (double) numSamples };
                }
            }
            untilStep += stepLen;
        }
        untilStep -= (double) numSamples;
        return count;
    }

private:
    struct Pending { int note = 0; double left = -1.0; };
    double sr = 48000.0, untilStep = 0.0;
    int walk = 0;
    Rng rng;
    std::array<Pending, maxPending> pending;
};
} // namespace gg
