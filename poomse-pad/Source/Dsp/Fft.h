#pragma once
#include <cmath>
#include <vector>

namespace pad
{
constexpr float kPi = 3.14159265358979323846f;

// In-place radix-2 complex FFT (float). Forward uses e^{-j...}; the inverse is NOT scaled (divide by N yourself).
class Fft
{
public:
    void init (int n)
    {
        N = n;
        cosT.resize ((size_t) n / 2);
        sinT.resize ((size_t) n / 2);
        rev.resize ((size_t) n);
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
        for (int i = 0; i < n / 2; ++i)
        {
            cosT[(size_t) i] = (float) std::cos (2.0 * 3.14159265358979323846 * i / n);
            sinT[(size_t) i] = (float) std::sin (2.0 * 3.14159265358979323846 * i / n);
        }
    }

    int size() const { return N; }

    void transform (float* re, float* im, bool inverse) const
    {
        for (int i = 0; i < N; ++i)
        {
            const int j = rev[(size_t) i];
            if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }
        const float sgn = inverse ? 1.f : -1.f;
        for (int len = 2; len <= N; len <<= 1)
        {
            const int half = len / 2, step = N / len;
            for (int i = 0; i < N; i += len)
                for (int j = 0; j < half; ++j)
                {
                    const float wr = cosT[(size_t) (j * step)], wi = sgn * sinT[(size_t) (j * step)];
                    const int a = i + j, b = a + half;
                    const float tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                }
        }
    }

private:
    int N = 0;
    std::vector<float> cosT, sinT;
    std::vector<int> rev;
};
} // namespace pad
