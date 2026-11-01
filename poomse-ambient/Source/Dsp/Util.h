#pragma once
#include <cmath>

namespace gg
{
// Output limiter that leaves everything below 0.8 untouched and only rounds off the peaks above it.
inline float softClip (float x)
{
    const float a = std::abs (x);
    if (a <= 0.8f) return x;
    const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
    return x < 0.f ? -y : y;
}
} // namespace gg
