#pragma once
#include "Rng.h"
#include "GranularEngine.h"
#include <algorithm>
#include <cmath>
#include <vector>

// Original ambient effects chain: tape -> ping-pong delay -> reverb with shimmer.
namespace gg
{

//==============================================================================
// Tape: wow/flutter/drift (modulated short delay), soft saturation, tone roll-off, hiss.
class Tape
{
public:
    void prepare (double sampleRate, uint32_t seed = 0x7A9Eu)
    {
        sr = sampleRate;
        size = (int) (0.05 * sr) + 8;
        bufL.assign ((size_t) size, 0.f);
        bufR.assign ((size_t) size, 0.f);
        w = 0; wowPh = flutPh = 0.f; drift = 0.f; lpL = lpR = 0.f;
        rng = Rng (seed);
    }

    void process (float& l, float& r, float amount)
    {
        bufL[(size_t) w] = l;
        bufR[(size_t) w] = r;
        if (amount > 1e-4f)
        {
            wowPh += 2.f * kPi * 0.6f / (float) sr;
            flutPh += 2.f * kPi * 6.5f / (float) sr;
            if (wowPh > 2.f * kPi) wowPh -= 2.f * kPi;
            if (flutPh > 2.f * kPi) flutPh -= 2.f * kPi;
            drift += (rng.bi() - drift) * 0.0005f;

            const float modMs = amount * (2.0f * std::sin (wowPh) + 0.4f * std::sin (flutPh) + 1.5f * drift);
            const float d = std::clamp ((4.0f + modMs) * 0.001f * (float) sr, 1.f, (float) size - 3.f);
            float rp = (float) w - d;
            if (rp < 0.f) rp += (float) size;
            const int i0 = std::min ((int) rp, size - 1);
            const float fr = rp - (float) i0;
            const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
            float xl = bufL[(size_t) i0] + fr * (bufL[(size_t) i1] - bufL[(size_t) i0]);
            float xr = bufR[(size_t) i0] + fr * (bufR[(size_t) i1] - bufR[(size_t) i0]);

            const float drive = 1.f + 2.f * amount;
            const float mk = 1.f / std::sqrt (drive);
            xl = std::tanh (xl * drive) * mk + rng.bi() * 0.0008f * amount;
            xr = std::tanh (xr * drive) * mk + rng.bi() * 0.0008f * amount;

            const float fc = 18000.f - 13000.f * amount;
            const float a = std::exp (-2.f * kPi * fc / (float) sr);
            lpL = xl + a * (lpL - xl);
            lpR = xr + a * (lpR - xr);
            l = lpL; r = lpR;
        }
        w = w + 1 >= size ? 0 : w + 1;
    }

private:
    double sr = 48000.0;
    int size = 1, w = 0;
    std::vector<float> bufL, bufR;
    float wowPh = 0.f, flutPh = 0.f, drift = 0.f, lpL = 0.f, lpR = 0.f;
    Rng rng;
};

//==============================================================================
// Stereo ping-pong delay with a darkened feedback path and glided delay time.
class PingPongDelay
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        size = (int) (2.0 * sr) + 4;
        bufL.assign ((size_t) size, 0.f);
        bufR.assign ((size_t) size, 0.f);
        w = 0; cur = (float) (0.38 * sr); lpL = lpR = 0.f;
        k = 1.f - std::exp (-2.f * kPi * 5000.f / (float) sr);
    }

    void process (float& l, float& r, float timeMs, float feedback, float mix)
    {
        const float target = std::clamp (timeMs * 0.001f * (float) sr, 16.f, (float) size - 4.f);
        cur += (target - cur) * 0.0005f;
        float rp = (float) w - cur;
        if (rp < 0.f) rp += (float) size;
        const int i0 = std::min ((int) rp, size - 1);
        const float fr = rp - (float) i0;
        const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
        const float dl = bufL[(size_t) i0] + fr * (bufL[(size_t) i1] - bufL[(size_t) i0]);
        const float dr = bufR[(size_t) i0] + fr * (bufR[(size_t) i1] - bufR[(size_t) i0]);
        lpL += k * (dl - lpL);
        lpR += k * (dr - lpR);

        const float fb = std::clamp (feedback, 0.f, 0.95f);
        bufL[(size_t) w] = (l + r) * 0.5f + lpR * fb; // L feeds R feeds L: ping-pong
        bufR[(size_t) w] = lpL * fb;
        w = w + 1 >= size ? 0 : w + 1;

        l += dl * mix;
        r += dr * mix;
    }

private:
    double sr = 48000.0;
    int size = 1, w = 0;
    std::vector<float> bufL, bufR;
    float cur = 0.f, lpL = 0.f, lpR = 0.f, k = 0.1f;
};

//==============================================================================
// Schroeder-style reverb (parallel damped combs + series allpasses).
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
        setParams (0.7f, 0.4f);
    }

    void setParams (float size, float damp)
    {
        feedback = 0.7f + 0.28f * std::clamp (size, 0.f, 1.f);
        const float d = 0.4f * std::clamp (damp, 0.f, 1.f);
        for (auto& ch : combs) for (auto& c : ch) { c.feedback = feedback; c.damp1 = d; c.damp2 = 1.f - d; }
    }

    float getFeedback() const { return feedback; }

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
    float feedback = 0.8f;
};

//==============================================================================
// Octave-up shifter (two cross-faded delay taps) used inside the reverb loop for shimmer.
class OctaveUp
{
public:
    void prepare (double sampleRate)
    {
        win = std::max (64, (int) (0.04 * sampleRate));
        buf.assign ((size_t) win + 8, 0.f);
        w = 0; phase = 0.f;
    }

    float process (float x)
    {
        const int size = (int) buf.size();
        buf[(size_t) w] = x;
        phase -= 1.f / (float) win; // ratio 2: read head moves twice as fast as write head
        if (phase < 0.f) phase += 1.f;
        float out = 0.f;
        for (int k = 0; k < 2; ++k)
        {
            float ph = phase + 0.5f * (float) k;
            ph -= std::floor (ph);
            float rp = (float) w - ph * (float) win;
            if (rp < 0.f) rp += (float) size;
            const int i0 = std::min ((int) rp, size - 1);
            const float fr = rp - (float) i0;
            const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
            const float s = buf[(size_t) i0] + fr * (buf[(size_t) i1] - buf[(size_t) i0]);
            out += s * (0.5f - 0.5f * std::cos (2.f * kPi * ph));
        }
        w = w + 1 >= size ? 0 : w + 1;
        return out;
    }

private:
    std::vector<float> buf;
    int w = 0, win = 64;
    float phase = 0.f;
};

//==============================================================================
struct AmbientParams
{
    float tape = 0.f;                         // 0..1
    float delayMs = 380.f, delayFeedback = 0.4f, delayMix = 0.f;
    float reverbSize = 0.7f, reverbDamp = 0.4f, reverbMix = 0.2f, shimmer = 0.f;
};

class AmbientFx
{
public:
    void prepare (double sampleRate)
    {
        tape.prepare (sampleRate);
        delay.prepare (sampleRate);
        reverb.prepare (sampleRate);
        shiftL.prepare (sampleRate);
        shiftR.prepare (sampleRate);
        fbL = fbR = 0.f;
    }

    void process (float& l, float& r, const AmbientParams& p)
    {
        tape.process (l, r, p.tape);
        delay.process (l, r, p.delayMs, p.delayFeedback, p.delayMix);

        if ((++counter & 63u) == 0u) reverb.setParams (p.reverbSize, p.reverbDamp);
        float wl, wr;
        reverb.process (l + fbL, r + fbR, wl, wr);
        // shimmer: the reverb output, an octave up, is fed back into the reverb input
        // loop gain is scaled by (1 - reverb feedback): long tails amplify the loop, so it must be backed off
        const float g = std::clamp (p.shimmer, 0.f, 1.f) * 1.0f * (1.f - reverb.getFeedback());
        fbL = std::tanh (shiftL.process (wl) * g);
        fbR = std::tanh (shiftR.process (wr) * g);

        l += wl * p.reverbMix;
        r += wr * p.reverbMix;
    }

private:
    Tape tape;
    PingPongDelay delay;
    Reverb reverb;
    OctaveUp shiftL, shiftR;
    float fbL = 0.f, fbR = 0.f;
    uint32_t counter = 0;
};
} // namespace gg
