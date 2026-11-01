#pragma once
#include <cstdint>

namespace gg
{
// Tiny xorshift32 RNG: allocation-free and deterministic, safe on the audio thread.
class Rng
{
public:
    explicit Rng (uint32_t seed = 0x1234567u) : s (seed ? seed : 1u) {}

    uint32_t nextU32() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (nextU32() >> 8) * (1.0f / 16777216.0f); } // [0,1)
    float bi() { return uni() * 2.0f - 1.0f; }                              // [-1,1)

private:
    uint32_t s;
};
} // namespace gg
