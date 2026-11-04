#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

// Poomse Spiral: a tempo-synced stereo delay whose echoes change pitch every time round (each side has its own pitch), so the repeats
// climb or fall in a spiral. Hold stops the input and keeps the echoes turning forever.
namespace sp
{
inline constexpr int kTimes = 6;
inline constexpr double kTimeQ[kTimes] = { 0.25, 0.5, 0.75, 1.0, 2.0, 4.0 };       // in quarter notes
inline constexpr const char* kTimeNames[kTimes] = { "1/16", "1/8", "1/8.", "1/4", "1/2", "1 bar" };

struct Params
{
    int time = 2;
    float pitchL = 0.f, pitchR = 0.f;    // semitones, applied at every pass
    float feedback = 0.5f;
    float tone = 0.7f;                   // 0 dark .. 1 open
    float mix = 0.5f;
    bool hold = false;
};

class Engine
{
public:
    static constexpr int bins = 360;
    void prepare (double sampleRate)
    {
        sr = sampleRate; size = (int) (9.0 * sr);
        for (auto& b : buf) b.assign ((size_t) size, 0.f);
        clear();
        win = (int) (0.06 * sr);
        for (auto& c : ch) c.o2 = 0.5f * (float) win;
    }
    void clear()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.f);
        w = 0; for (auto& c : ch) { c = Chan{}; c.o2 = 0.5f * (float) win; } curT = -1.0;
        for (auto& p : peaks) p.store (0.f);
    }

    void process (int num, const Params& p, double bpm, const float* inL, const float* inR, float* outL, float* outR)
    {
        const double targetT = std::clamp (sr * 60.0 / std::clamp (bpm, 20.0, 400.0) * kTimeQ[std::clamp (p.time, 0, kTimes - 1)], (double) win * 2.0, (double) size - win - 16.0);
        if (curT < 0.0) curT = targetT;
        const float mix = std::clamp (p.mix, 0.f, 1.f), wet = std::sin (mix * 1.5707963f), dry = std::cos (mix * 1.5707963f);
        const float fb = p.hold ? 1.f : std::clamp (p.feedback, 0.f, 1.f) * 0.95f;
        const float cut = 400.f * std::pow (40.f, std::clamp (p.tone, 0.f, 1.f));
        const float lpK = 1.f - std::exp (-6.2831853f * cut / (float) sr);
        const float ratio[2] = { std::exp2 (std::clamp (p.pitchL, -24.f, 24.f) / 12.f), std::exp2 (std::clamp (p.pitchR, -24.f, 24.f) / 12.f) };
        const float in[2] = { 0.f, 0.f }; (void) in;
        for (int i = 0; i < num; ++i)
        {
            curT += (targetT - curT) * 0.00015;                                  // a change of time glides, like tape
            float y[2];
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[(size_t) c];
                // the two read heads of the pitch shifter, half a window apart, crossfaded
                const float rate = 1.f - ratio[c];
                s.o1 += rate; s.o2 += rate;
                const float W = (float) win;
                auto wrap = [&] (float& o) { while (o >= W) o -= W; while (o < 0.f) o += W; };
                wrap (s.o1); wrap (s.o2);
                const float g1 = std::sin (3.14159265f * s.o1 / W), g2 = std::sin (3.14159265f * s.o2 / W);
                const float base = (float) curT - 0.5f * W;
                const float v = tap (c, base + s.o1) * g1 * g1 + tap (c, base + s.o2) * g2 * g2;
                s.lp += lpK * (v - s.lp);
                y[c] = std::tanh (s.lp);
            }
            const float inSig[2] = { inL[i], inR[i] };
            for (int c = 0; c < 2; ++c) buf[(size_t) c][(size_t) w] = (p.hold ? 0.f : inSig[c]) + fb * y[c];
            const int bin = (int) ((long) w * bins / size);
            if (bin != lastBin) { peaks[(size_t) bin].store (0.f); lastBin = bin; }
            peaks[(size_t) bin].store (std::max (peaks[(size_t) bin].load(), std::abs (y[0]) + std::abs (y[1])));
            if (++w >= size) w = 0;
            outL[i] = inL[i] * dry + y[0] * wet;
            outR[i] = inR[i] * dry + y[1] * wet;
        }
        writeFrac.store ((float) w / (float) size);
    }
    float peak (int i) const { return peaks[(size_t) i].load(); }
    float writePosition() const { return writeFrac.load(); }

private:
    float tap (int c, float delay) const
    {
        float rp = (float) w - delay; while (rp < 0.f) rp += (float) size;
        const int i0 = std::min ((int) rp, size - 1), i1 = i0 + 1 >= size ? 0 : i0 + 1;
        const float fr = rp - (float) i0;
        const auto& b = buf[(size_t) c];
        return b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * fr;
    }
    struct Chan { float o1 = 0.f, o2 = 0.f, lp = 0.f; };
    double sr = 48000.0, curT = -1.0;
    int size = 1, w = 0, win = 2880, lastBin = -1;
    std::array<std::vector<float>, 2> buf;
    std::array<Chan, 2> ch {};
    std::array<std::atomic<float>, bins> peaks {};
    std::atomic<float> writeFrac { 0.f };
};
} // namespace sp
