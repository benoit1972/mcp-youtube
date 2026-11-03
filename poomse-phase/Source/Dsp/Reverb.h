#pragma once
#include <cmath>
#include <algorithm>
#include <vector>

namespace ph
{
// Schroeder-style reverb (parallel damped combs + series allpasses), stereo.
class Reverb
{
public:
    void prepare (double sampleRate)
    {
        static const int combT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int apT[4] = { 556, 441, 341, 225 };
        const double sc = sampleRate / 44100.0;
        for (int c = 0; c < 2; ++c)
        {
            const int spread = c * 23;
            for (int i = 0; i < 8; ++i) combs[c][i].init ((int) ((combT[i] + spread) * sc));
            for (int i = 0; i < 4; ++i) aps[c][i].init ((int) ((apT[i] + spread) * sc));
        }
        setSize (0.7f);
    }

    void setSize (float size)
    {
        const float fb = 0.7f + 0.26f * std::clamp (size, 0.f, 1.f); // up to 0.96: long, pad-friendly tails
        for (auto& ch : combs) for (auto& c : ch) { c.feedback = fb; c.damp1 = 0.25f; c.damp2 = 0.75f; }
    }

    void process (float inL, float inR, float& outL, float& outR)
    {
        const float in = (inL + inR) * 0.015f;
        float o[2];
        for (int c = 0; c < 2; ++c)
        {
            float s = 0.f;
            for (auto& cb : combs[c]) s += cb.process (in);
            for (auto& ap : aps[c]) s = ap.process (s);
            o[c] = s * 3.f;
        }
        outL = o[0]; outR = o[1];
    }

private:
    struct Comb
    {
        std::vector<float> buf; int idx = 0; float store = 0.f, feedback = 0.8f, damp1 = 0.2f, damp2 = 0.8f;
        void init (int n) { buf.assign ((size_t) std::max (1, n), 0.f); idx = 0; store = 0.f; }
        float process (float in)
        {
            const float out = buf[(size_t) idx];
            store = out * damp2 + store * damp1;
            buf[(size_t) idx] = in + store * feedback;
            if (++idx >= (int) buf.size()) idx = 0;
            return out;
        }
    };
    struct Allpass
    {
        std::vector<float> buf; int idx = 0;
        void init (int n) { buf.assign ((size_t) std::max (1, n), 0.f); idx = 0; }
        float process (float in)
        {
            const float bo = buf[(size_t) idx];
            const float out = bo - in;
            buf[(size_t) idx] = in + bo * 0.5f;
            if (++idx >= (int) buf.size()) idx = 0;
            return out;
        }
    };
    Comb combs[2][8];
    Allpass aps[2][4];
};
} // namespace ph
