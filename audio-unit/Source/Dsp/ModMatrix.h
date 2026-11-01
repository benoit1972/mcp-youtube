#pragma once
#include "Rng.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// Modulation matrix: 11 sources (8 macros, 2 LFOs, 1 euclidean sequencer) x every modulatable parameter.
// A cell is a bipolar amount (-1..+1): the modulated value of a destination is
//     base + sum over sources( source value * amount )      in normalised (0..1) parameter units.
namespace gg
{
constexpr int kNumMacros = 8;
constexpr int kSrcLfo1 = 8, kSrcLfo2 = 9, kSrcSeq = 10;
constexpr int kNumSources = 11;

struct ModDest { const char* id; const char* label; };

inline constexpr ModDest kModDests[] = {
    { "position", "Position" },     { "spray", "Spray" },           { "size", "Grain size" },
    { "density", "Density" },       { "shape", "Window" },          { "pitch", "Pitch" },
    { "pitchSpray", "Pitch spray" }, { "reverse", "Reverse" },      { "pan", "Stereo" },
    { "attack", "Attack" },         { "decay", "Decay" },           { "sustain", "Sustain" },
    { "release", "Release" },       { "drift", "Drift" },           { "driftLength", "Drift length" },
    { "stutter", "Stutter" },       { "stutterPitch", "Stutter pitch" }, { "glitch", "Glitch" },
    { "tape", "Tape" },             { "delayTime", "Delay time" },  { "delayFb", "Delay feedback" },
    { "delayMix", "Delay mix" },    { "reverbSize", "Reverb size" }, { "reverbDamp", "Reverb damping" },
    { "reverbMix", "Reverb mix" },  { "shimmer", "Shimmer" },       { "level", "Level" },
};
constexpr int kNumDests = (int) (sizeof (kModDests) / sizeof (kModDests[0]));

// 1-based destination index of a parameter id (0 = not modulatable)
inline int destIndexOf (const char* id)
{
    for (int i = 0; i < kNumDests; ++i)
        if (std::strcmp (kModDests[i].id, id) == 0) return i + 1;
    return 0;
}

struct Matrix
{
    float amt[kNumSources][kNumDests] = {};
};

// offsets[d] = total normalised offset added to destination d
inline void accumulate (const float* srcValues, const Matrix& m, float* offsets)
{
    for (int d = 0; d < kNumDests; ++d)
    {
        float sum = 0.f;
        for (int s = 0; s < kNumSources; ++s) sum += srcValues[s] * m.amt[s][d];
        offsets[d] = sum;
    }
}

// Default macro assignments: one musical theme per macro.
struct Theme { const char* ids[3]; float amounts[3]; };
inline constexpr Theme kThemes[kNumMacros] = {
    { { "position", "spray", "shape" },         { 0.50f, 0.40f, 0.30f } },   // texture
    { { "density", "size", "reverse" },         { 0.40f, -0.25f, 0.30f } },  // cloud
    { { "pitch", "pitchSpray", "pan" },         { 0.10f, 0.40f, 0.30f } },   // pitch
    { { "reverbMix", "reverbSize", "shimmer" }, { 0.50f, 0.30f, 0.40f } },   // space
    { { "delayMix", "delayFb", "delayTime" },   { 0.50f, 0.30f, 0.20f } },   // echo
    { { "tape", "glitch", "stutter" },          { 0.50f, 0.30f, 0.30f } },   // grit
    { { "attack", "release", "sustain" },       { 0.30f, 0.40f, -0.20f } },  // envelope
    { { "drift", "reverbDamp", "decay" },       { 0.40f, 0.30f, 0.30f } },   // motion
};

inline Matrix defaultMatrix()
{
    Matrix m;
    for (int s = 0; s < kNumMacros; ++s)
        for (int k = 0; k < 3; ++k)
            m.amt[s][destIndexOf (kThemes[s].ids[k]) - 1] = kThemes[s].amounts[k];
    return m;
}

// Random assignment of the 8 macro columns: each macro gets 2-4 distinct, randomly chosen parameters
// with moderate, mostly positive amounts. Pitch moves gently, and Level is never assigned (no volume surprises).
// LFO / sequencer columns are left untouched.
inline void randomAssignMacros (Rng& rng, Matrix& m)
{
    const int levelIdx = destIndexOf ("level") - 1, pitchIdx = destIndexOf ("pitch") - 1,
              delayTimeIdx = destIndexOf ("delayTime") - 1;
    for (int s = 0; s < kNumMacros; ++s)
    {
        for (int d = 0; d < kNumDests; ++d) m.amt[s][d] = 0.f;
        const int count = 2 + (int) (rng.uni() * 3.f) % 3; // 2..4
        int placed = 0, guard = 0;
        while (placed < count && guard++ < 200)
        {
            const int d = (int) (rng.uni() * (float) kNumDests) % kNumDests;
            if (d == levelIdx || m.amt[s][d] != 0.f) continue;
            float mag = 0.2f + 0.5f * rng.uni();                 // 20..70 %
            if (d == pitchIdx) mag *= 0.3f;                       // +-48 st range: keep it gentle
            if (d == delayTimeIdx) mag *= 0.5f;
            m.amt[s][d] = (rng.uni() < 0.75f ? 1.f : -1.f) * std::max (0.05f, mag);
            ++placed;
        }
    }
}
} // namespace gg
