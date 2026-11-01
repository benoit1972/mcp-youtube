#pragma once
#include "Rng.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// 8 macros x 3 slots. Each slot = (destination parameter, bipolar amount).
// The modulated value of a destination is   base + sum(macro * amount)   in normalised (0..1) units.
namespace gg
{
constexpr int kNumMacros = 8;
constexpr int kSlotsPerMacro = 3;

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

// 1-based destination index of a parameter id (0 = none / not modulatable)
inline int destIndexOf (const char* id)
{
    for (int i = 0; i < kNumDests; ++i)
        if (std::strcmp (kModDests[i].id, id) == 0) return i + 1;
    return 0;
}

struct Slot
{
    int dest = 0;        // 0 = none, k = kModDests[k - 1]
    float amount = 0.f;  // -1 .. +1
};

// offsets[d] = total normalised offset to add to destination d
inline void accumulate (const float* macros, const Slot (*slots)[kSlotsPerMacro], float* offsets)
{
    for (int d = 0; d < kNumDests; ++d) offsets[d] = 0.f;
    for (int m = 0; m < kNumMacros; ++m)
        for (int s = 0; s < kSlotsPerMacro; ++s)
        {
            const auto& sl = slots[m][s];
            if (sl.dest >= 1 && sl.dest <= kNumDests && macros[m] != 0.f && sl.amount != 0.f)
                offsets[sl.dest - 1] += macros[m] * sl.amount;
        }
}

// One musical theme per macro. Amounts are signed fractions of each parameter's full range.
struct Theme { const char* ids[kSlotsPerMacro]; float amounts[kSlotsPerMacro]; };
inline constexpr Theme kThemes[kNumMacros] = {
    { { "position", "spray", "shape" },        { 0.50f, 0.40f, 0.30f } },   // texture
    { { "density", "size", "reverse" },        { 0.40f, -0.25f, 0.30f } },  // cloud
    { { "pitch", "pitchSpray", "pan" },        { 0.10f, 0.40f, 0.30f } },   // pitch
    { { "reverbMix", "reverbSize", "shimmer" }, { 0.50f, 0.30f, 0.40f } },  // space
    { { "delayMix", "delayFb", "delayTime" },  { 0.50f, 0.30f, 0.20f } },   // echo
    { { "tape", "glitch", "stutter" },         { 0.50f, 0.30f, 0.30f } },   // grit
    { { "attack", "release", "sustain" },      { 0.30f, 0.40f, -0.20f } },  // envelope
    { { "drift", "reverbDamp", "decay" },      { 0.40f, 0.30f, 0.30f } },   // motion
};

inline Slot defaultSlot (int macro, int slot)
{
    return { destIndexOf (kThemes[macro].ids[slot]), kThemes[macro].amounts[slot] };
}

// Musical automatic assignment: every macro gets a distinct theme (shuffled), amounts are
// moderate and mostly positive, pitch moves gently, and Level is never assigned (no volume surprises).
inline void randomAssign (Rng& rng, Slot (*slots)[kSlotsPerMacro])
{
    int order[kNumMacros];
    for (int i = 0; i < kNumMacros; ++i) order[i] = i;
    for (int i = kNumMacros - 1; i > 0; --i)
        std::swap (order[i], order[(int) (rng.uni() * (float) (i + 1)) % (i + 1)]);

    for (int m = 0; m < kNumMacros; ++m)
        for (int s = 0; s < kSlotsPerMacro; ++s)
        {
            const auto& th = kThemes[order[m]];
            Slot sl;
            sl.dest = destIndexOf (th.ids[s]);
            float mag = 0.2f + 0.5f * rng.uni();                       // 20..70 %
            if (std::strcmp (th.ids[s], "pitch") == 0) mag *= 0.3f;    // +-48 st range: keep it gentle
            if (std::strcmp (th.ids[s], "delayTime") == 0) mag *= 0.5f;
            sl.amount = (rng.uni() < 0.75f ? 1.f : -1.f) * mag;
            slots[m][s] = sl;
        }
}
} // namespace gg
