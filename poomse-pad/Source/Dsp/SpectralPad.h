#pragma once
#include "Fft.h"
#include "Rng.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Turns any input into an evolving pad: a real-time spectral "smear".
//   input -> STFT (Hann, 75 % overlap) -> magnitudes smoothed over seconds (fast rise, slow fall, or frozen)
//         -> harmonic layers (octave down / body / fifth / octave up) -> tilt + slow moving spectral emphasis
//         -> random phases (decorrelated between L and R by `width`) -> inverse STFT, overlap-add.
// The random phases erase attacks and make the sound continuous; the magnitude smoothing is what keeps the
// harmonic content of the input alive for a long time.
namespace pad
{
constexpr int kDisplayBands = 96;

struct PadParams
{
    float riseSec = 0.8f;     // how fast the pad builds up towards new input
    float timeSec = 4.f;      // how long it takes to fade away when the input is gone (time constant)
    bool hold = false;        // freeze the spectrum
    float sub = 0.f, body = 1.f, fifth = 0.f, octave = 0.f; // layer levels (-12 st, 0, +7 st, +12 st)
    float tone = 0.f;         // -1..1 spectral tilt, +-6 dB/oct around 1 kHz
    float width = 0.7f;       // 0 = mono pad, 1 = fully decorrelated
    float evolve = 0.3f;      // depth of the slowly moving spectral emphasis
    float evolveRate = 0.1f;  // Hz
};

class SpectralPad
{
public:
    void prepare (double sampleRate, uint32_t seed = 0xBADA55u)
    {
        sr = sampleRate;
        N = sampleRate > 52000.0 ? 16384 : 8192;
        H = N / 4;
        bins = N / 2 + 1;
        fft.init (N);
        window.resize ((size_t) N);
        for (int i = 0; i < N; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.f * kPi * (float) i / (float) N);
        for (int c = 0; c < 2; ++c)
        {
            inRing[c].assign ((size_t) N, 0.f);
            outRing[c].assign ((size_t) N, 0.f);
            mag[c].assign ((size_t) bins, 0.f);
            shaped[c].assign ((size_t) bins, 0.f);
        }
        re.assign ((size_t) N, 0.f); im.assign ((size_t) N, 0.f);
        binGain.assign ((size_t) bins, 1.f);
        freqOfBin.resize ((size_t) bins);
        for (int k = 0; k < bins; ++k) freqOfBin[(size_t) k] = (float) ((double) k * sr / (double) N);
        rng = gg::Rng (seed);
        writeIdx = readIdx = hopCounter = 0;
        evolvePhase = 0.f;
        display.fill (0.f);
    }

    // one stereo sample in, one stereo (wet only) sample out
    void process (float inL, float inR, const PadParams& p, float& outL, float& outR)
    {
        inRing[0][(size_t) writeIdx] = inL;
        inRing[1][(size_t) writeIdx] = inR;
        writeIdx = writeIdx + 1 >= N ? 0 : writeIdx + 1;

        outL = outRing[0][(size_t) readIdx]; outRing[0][(size_t) readIdx] = 0.f;
        outR = outRing[1][(size_t) readIdx]; outRing[1][(size_t) readIdx] = 0.f;
        readIdx = readIdx + 1 >= N ? 0 : readIdx + 1;

        if (++hopCounter >= H) { hopCounter = 0; processFrame (p); }
    }

    int frameSize() const { return N; }
    const std::array<float, kDisplayBands>& getDisplay() const { return display; }

private:
    void processFrame (const PadParams& p)
    {
        const float hopSec = (float) H / (float) sr;
        const float aRise = std::exp (-hopSec / std::max (0.01f, p.riseSec));
        const float aFall = std::exp (-hopSec / std::max (0.05f, p.timeSec));

        // ---- analysis + magnitude smoothing (per channel) ----
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < N; ++i)
            {
                const int idx = (writeIdx + i) % N; // oldest sample first
                re[(size_t) i] = inRing[c][(size_t) idx] * window[(size_t) i];
                im[(size_t) i] = 0.f;
            }
            fft.transform (re.data(), im.data(), false);
            auto& m = mag[(size_t) c];
            if (! p.hold)
                for (int k = 0; k < bins; ++k)
                {
                    const float in = std::hypot (re[(size_t) k], im[(size_t) k]);
                    const float a = in > m[(size_t) k] ? aRise : aFall;
                    m[(size_t) k] = a * m[(size_t) k] + (1.f - a) * in;
                }
        }

        // ---- spectral shaping curve: tilt (fixed) and a slow moving emphasis (opposite directions for L and R) ----
        evolvePhase += 2.f * kPi * std::max (0.f, p.evolveRate) * hopSec;
        if (evolvePhase > 2.f * kPi) evolvePhase -= 2.f * kPi;

        // layer table: ratio, level, loudness compensation for the bin stretching
        const float ratios[4] = { 0.5f, 1.f, 1.4983f, 2.f };
        const float levels[4] = { p.sub, p.body, p.fifth, p.octave };

        std::array<float, kDisplayBands> bandAcc {};
        std::array<int, kDisplayBands> bandCount {};

        for (int c = 0; c < 2; ++c)
        {
            auto& m = mag[(size_t) c];
            auto& sh = shaped[(size_t) c];

            // 1) harmonic layers (power sum)
            for (int k = 0; k < bins; ++k)
            {
                float pw = 0.f;
                for (int l = 0; l < 4; ++l)
                {
                    if (levels[l] <= 0.f) continue;
                    const float x = (float) k / ratios[l];
                    const int i0 = (int) x;
                    if (i0 >= bins - 1) continue;
                    const float f = x - (float) i0;
                    float v = (1.f - f) * m[(size_t) i0] + f * m[(size_t) i0 + 1];
                    if (ratios[l] < 0.75f && i0 + 2 < bins)          // octave down: fold two source bins into one
                        v = std::sqrt (0.5f * (v * v + m[(size_t) i0 + 1] * m[(size_t) i0 + 1]));
                    const float g = levels[l] / std::sqrt (ratios[l]); // keep layer loudness comparable
                    pw += g * g * v * v;
                }
                sh[(size_t) k] = std::sqrt (pw);
            }

            // 2) tilt around 1 kHz
            if (std::abs (p.tone) > 0.001f)
                for (int k = 1; k < bins; ++k)
                    sh[(size_t) k] *= std::clamp (std::pow (freqOfBin[(size_t) k] / 1000.f, p.tone), 0.03f, 30.f);

            // 3) moving emphasis, energy-normalised so the loudness does not pump
            if (p.evolve > 0.001f)
            {
                const float ph = evolvePhase + (c == 0 ? 0.f : kPi);
                const float centre = 150.f * std::exp2 (5.5f * (0.5f + 0.5f * std::sin (ph)));
                double e0 = 0.0, e1 = 0.0;
                for (int k = 1; k < bins; ++k)
                {
                    const float o = std::log2 (freqOfBin[(size_t) k] / centre) / 0.7f;
                    const float g = 1.f + p.evolve * 4.f * std::exp (-0.5f * o * o);
                    binGain[(size_t) k] = g;
                    const double s2 = (double) sh[(size_t) k] * sh[(size_t) k];
                    e0 += s2; e1 += s2 * (double) g * g;
                }
                const float norm = e1 > 1e-20 ? (float) std::sqrt (e0 / e1) : 1.f;
                for (int k = 1; k < bins; ++k) sh[(size_t) k] *= binGain[(size_t) k] * norm;
            }

            // display: log-spaced bands, summed over both channels
            for (int k = 1; k < bins; ++k)
            {
                const float f = freqOfBin[(size_t) k];
                if (f < 30.f || f > 16000.f) continue;
                const int b = std::clamp ((int) (std::log2 (f / 30.f) / std::log2 (16000.f / 30.f) * (float) kDisplayBands), 0, kDisplayBands - 1);
                bandAcc[(size_t) b] += sh[(size_t) k] * sh[(size_t) k];
                ++bandCount[(size_t) b];
            }
        }
        for (int b = 0; b < kDisplayBands; ++b)
            display[(size_t) b] = bandCount[(size_t) b] > 0 ? std::sqrt (bandAcc[(size_t) b] / (float) bandCount[(size_t) b]) : 0.f;

        // ---- synthesis: random phases, inverse FFT, window, overlap-add ----
        // calibrated on tonal input (a steady sine comes out at its own level); noisy input ends up ~2 dB hotter
        // because the fast-rise / slow-fall smoothing ratchets the fluctuating magnitudes upwards
        constexpr float makeup = 1.34f;
        const float scale = makeup / (float) N;
        for (int k = 0; k < bins; ++k)
        {
            const float u1 = rng.uni() * 2.f * kPi, u2 = rng.uni() * 2.f * kPi;
            ph2[0] = u1;
            ph2[1] = u1 + p.width * (u2 - u1); // width 0: same phases (mono pad), 1: independent
            for (int c = 0; c < 2; ++c)
            {
                const float s = shaped[(size_t) c][(size_t) k];
                const float a = ph2[c];
                specRe[c][(size_t) k] = (k == 0 || k == bins - 1) ? 0.f : s * std::cos (a);
                specIm[c][(size_t) k] = (k == 0 || k == bins - 1) ? 0.f : s * std::sin (a);
            }
        }
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 0; k < bins; ++k) { re[(size_t) k] = specRe[c][(size_t) k]; im[(size_t) k] = specIm[c][(size_t) k]; }
            for (int k = 1; k < bins - 1; ++k) { re[(size_t) (N - k)] = re[(size_t) k]; im[(size_t) (N - k)] = -im[(size_t) k]; }
            fft.transform (re.data(), im.data(), true);
            for (int i = 0; i < N; ++i)
            {
                const int idx = (readIdx + i) % N;
                outRing[c][(size_t) idx] += re[(size_t) i] * scale * window[(size_t) i];
            }
        }
    }

    double sr = 48000.0;
    int N = 8192, H = 2048, bins = 4097;
    Fft fft;
    std::vector<float> window, re, im, binGain, freqOfBin;
    std::vector<float> inRing[2], outRing[2], mag[2], shaped[2];
    int writeIdx = 0, readIdx = 0, hopCounter = 0;
    float evolvePhase = 0.f;
    gg::Rng rng;
    std::array<float, kDisplayBands> display {};

    // scratch for the random-phase spectrum
    std::vector<float> specRe[2] = { std::vector<float> (16384 / 2 + 1), std::vector<float> (16384 / 2 + 1) };
    std::vector<float> specIm[2] = { std::vector<float> (16384 / 2 + 1), std::vector<float> (16384 / 2 + 1) };
    float ph2[2] = { 0.f, 0.f };
};
} // namespace pad
