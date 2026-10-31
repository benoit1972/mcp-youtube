#pragma once
#include "Rng.h"
#include "GranularEngine.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace gg
{
struct FxParams
{
    float stutterAmount = 0.f; // 0..1 probability that a beat gets a stutter
    int stutterDiv = 2;        // loop length: see divisionBeats()
    float stutterPitch = 0.f;  // semitones, loop playback speed
    float glitchAmount = 0.f;  // 0..1 probability that a glitch slot fires
    int glitchDiv = 2;         // glitch slot length: see divisionBeats()
    double bpm = 120.0;
};

// 0:1/4 1:1/8 2:1/16 3:1/32 4:1/8T 5:1/16T 6:1/32T, expressed in beats (quarter notes)
inline double divisionBeats (int idx)
{
    static const double t[] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0 };
    return t[std::clamp (idx, 0, 6)];
}

// Stutter: every beat, with probability `stutterAmount`, the last loop-length
//          of audio is captured and repeated until the next beat.
// Glitch:  every glitch slot, with probability `glitchAmount`, one of
//          dropout / bit+rate crush / reverse / micro-loop is applied.
// Both are clocked from the host tempo (free-running, not phase-locked to the song position).
class GlitchFx
{
public:
    void prepare (double sampleRate, uint32_t seed = 0xC0FFEEu)
    {
        sr = sampleRate;
        ringSize = (int) (8.0 * sr);
        ringL.assign ((size_t) ringSize, 0.f);
        ringR.assign ((size_t) ringSize, 0.f);
        freezeL.assign ((size_t) ringSize, 0.f);
        freezeR.assign ((size_t) ringSize, 0.f);
        rng = Rng (seed);
        smooth = 1.f - std::exp (-1.f / (0.002f * (float) sr));
        writeIdx = 0; beatCounter = glitchCounter = 0.0;
        stutterOn = glitchOn = false; stutterMix = glitchMix = 0.f;
        freezeLen = 0; readPos = 0.0; glitchType = 0;
    }

    void process (float& l, float& r, const FxParams& p)
    {
        const size_t w = (size_t) writeIdx;
        ringL[w] = l;
        ringR[w] = r;

        const double beatLen = std::max (64.0, 60.0 / std::max (20.0, p.bpm) * sr);

        // ---- stutter ----
        beatCounter += 1.0;
        if (beatCounter >= beatLen)
        {
            beatCounter -= beatLen;
            if (rng.uni() < p.stutterAmount)
            {
                const int loopLen = std::clamp ((int) (beatLen * divisionBeats (p.stutterDiv)), 32, ringSize / 2);
                const int start = wrap (writeIdx - loopLen + 1);
                for (int i = 0; i < loopLen; ++i)
                {
                    const size_t src = (size_t) wrap (start + i);
                    freezeL[(size_t) i] = ringL[src];
                    freezeR[(size_t) i] = ringR[src];
                }
                freezeLen = loopLen;
                readPos = 0.0;
                stutterOn = true;
            }
            else
                stutterOn = false;
        }
        stutterMix += ((stutterOn ? 1.f : 0.f) - stutterMix) * smooth;

        float ol = l, orr = r;
        if (stutterMix > 0.0005f && freezeLen > 1)
        {
            const int i0 = std::min ((int) readPos, freezeLen - 1);
            const float fr = (float) (readPos - (double) i0);
            const int i1 = i0 + 1 >= freezeLen ? 0 : i0 + 1;
            const float edge = std::clamp (std::min ((float) readPos, (float) freezeLen - (float) readPos) / 32.f, 0.f, 1.f);
            const float sl = (freezeL[(size_t) i0] + fr * (freezeL[(size_t) i1] - freezeL[(size_t) i0])) * edge;
            const float sr_ = (freezeR[(size_t) i0] + fr * (freezeR[(size_t) i1] - freezeR[(size_t) i0])) * edge;
            readPos += std::exp2 ((double) p.stutterPitch / 12.0);
            while (readPos >= (double) freezeLen) readPos -= (double) freezeLen;
            ol = l * (1.f - stutterMix) + sl * stutterMix;
            orr = r * (1.f - stutterMix) + sr_ * stutterMix;
        }

        // ---- glitch ----
        glitchCounter += 1.0;
        const double glitchLen = std::clamp (beatLen * divisionBeats (p.glitchDiv), 32.0, (double) (ringSize / 2 - 1));
        if (glitchCounter >= glitchLen)
        {
            glitchCounter -= glitchLen;
            startGlitch (p);
        }
        glitchMix += ((glitchOn ? 1.f : 0.f) - glitchMix) * smooth;

        float outL = ol, outR = orr;
        if (glitchMix > 0.0005f)
        {
            float wl = ol, wr = orr;
            switch (glitchType)
            {
                case 0: wl = wr = 0.f; break; // dropout
                case 1:                       // bit + rate crush
                    if (holdCounter <= 0) { heldL = ol; heldR = orr; holdCounter = holdFactor; }
                    --holdCounter;
                    wl = quantise (heldL); wr = quantise (heldR);
                    break;
                case 2:                       // reverse of what just played
                {
                    const size_t idx = (size_t) wrap (revBase - revK);
                    if (revK < ringSize / 2) ++revK;
                    wl = ringL[idx]; wr = ringR[idx];
                    break;
                }
                default:                      // micro-loop (buzz)
                {
                    const size_t idx = (size_t) wrap (microStart + microPos);
                    microPos = microPos + 1 >= microLen ? 0 : microPos + 1;
                    wl = ringL[idx]; wr = ringR[idx];
                    break;
                }
            }
            outL = ol * (1.f - glitchMix) + wl * glitchMix;
            outR = orr * (1.f - glitchMix) + wr * glitchMix;
        }

        l = outL; r = outR;
        writeIdx = writeIdx + 1 >= ringSize ? 0 : writeIdx + 1;
    }

private:
    int wrap (int i) const { i %= ringSize; return i < 0 ? i + ringSize : i; }

    float quantise (float v) const
    {
        const float levels = (float) (1 << (crushBits - 1));
        return std::round (v * levels) / levels;
    }

    void startGlitch (const FxParams& p)
    {
        glitchOn = rng.uni() < p.glitchAmount;
        if (! glitchOn) return;
        glitchType = (int) (rng.nextU32() % 4u);
        switch (glitchType)
        {
            case 1: crushBits = 3 + (int) (rng.uni() * 4.f); holdFactor = 4 + (int) (rng.uni() * 12.f); holdCounter = 0; break;
            case 2: revBase = writeIdx; revK = 0; break;
            case 3:
                microLen = std::max (16, (int) (sr * (0.008 + 0.03 * (double) rng.uni())));
                microStart = wrap (writeIdx - microLen + 1);
                microPos = 0;
                break;
            default: break;
        }
    }

    double sr = 48000.0;
    int ringSize = 1, writeIdx = 0;
    std::vector<float> ringL, ringR, freezeL, freezeR;
    Rng rng;
    float smooth = 0.01f;

    double beatCounter = 0.0, glitchCounter = 0.0, readPos = 0.0;
    int freezeLen = 0;
    bool stutterOn = false, glitchOn = false;
    float stutterMix = 0.f, glitchMix = 0.f;

    int glitchType = 0;
    int crushBits = 4, holdFactor = 8, holdCounter = 0;
    float heldL = 0.f, heldR = 0.f;
    int revBase = 0, revK = 0;
    int microStart = 0, microLen = 256, microPos = 0;
};
} // namespace gg
