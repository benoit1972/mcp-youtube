#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

// Poomse Warp: a phase-shaping synth. One sine, whose phase is bent (Warp) and twisted by a second sine (Twist): from a round tone to a
// metallic one with a single gesture. Pluck makes the bend and the filter snap shut over the first moments of every note.
namespace wp
{
inline constexpr float kRatios[7] = { 0.5f, 1.f, 2.f, 3.f, 4.f, 5.f, 7.f };
inline constexpr const char* kRatioNames[7] = { "0.5", "1", "2", "3", "4", "5", "7" };

struct Params
{
    float warp = 0.35f, twist = 0.f; int ratio = 1;
    float pluck = 0.4f, cutoff = 0.8f, reso = 0.15f, detune = 0.2f;
    float attack = 0.05f, decay = 0.45f, sustain = 0.6f, release = 0.35f;
};

// one cycle of the oscillator: phase in [0,1)
inline float shape (float phase, float modPhase, float warp, float twist)
{
    const float d = 0.5f - 0.47f * std::clamp (warp, 0.f, 1.f);                       // where the bend sits
    const float q = phase < d ? phase / (2.f * d) : 0.5f + (phase - d) / (2.f * (1.f - d));
    return std::sin (6.2831853f * q + 4.f * twist * std::sin (6.2831853f * modPhase));
}

class Engine
{
public:
    static constexpr int maxVoices = 8;

    void prepare (double sampleRate) { sr = sampleRate; killAll(); }
    void killAll() { for (auto& v : voices) v = Voice{}; order = 0; }

    void noteOn (int note, float vel)
    {
        Voice* slot = nullptr;
        for (auto& v : voices) if (v.on && v.note == note) { slot = &v; break; }
        if (slot == nullptr) for (auto& v : voices) if (! v.on) { slot = &v; break; }
        if (slot == nullptr) { long oldest = 1L << 60; for (auto& v : voices) if (v.order < oldest) { oldest = v.order; slot = &v; } }
        const auto keepPhase = slot->on;
        *slot = Voice{};
        slot->on = true; slot->gate = true; slot->note = note; slot->vel = std::clamp (vel, 0.05f, 1.f); slot->order = ++order;
        slot->freq = 440.0 * std::exp2 ((note - 69) / 12.0);
        (void) keepPhase;
    }
    void noteOff (int note) { for (auto& v : voices) if (v.on && v.gate && v.note == note) { v.gate = false; v.stage = 3; } }
    void allNotesOff() { for (auto& v : voices) if (v.on) { v.gate = false; v.stage = 3; } }
    void pitchBend (float semitones) { bend = semitones; }
    void modWheel (float v) { wheel = std::clamp (v, 0.f, 1.f); }
    int activeVoices() const { int n = 0; for (const auto& v : voices) if (v.on) ++n; return n; }

    void process (int num, const Params& p, float* L, float* R)
    {
        const float ratio = kRatios[std::clamp (p.ratio, 0, 6)];
        const double aS = 0.001 * std::pow (2000.0, (double) p.attack);
        const double dS = 0.05 * std::pow (100.0, (double) p.decay);
        const double rS = 0.02 * std::pow (200.0, (double) p.release);
        const float aInc = (float) (1.0 / (aS * sr)), dK = (float) (1.0 - std::exp (-1.0 / (dS * sr))), rK = (float) (1.0 - std::exp (-1.0 / (rS * sr)));
        const float wK = (float) std::exp (-1.0 / (std::max (0.08, dS * 0.6) * sr));
        const float baseCut = 40.f * std::pow (450.f, std::clamp (p.cutoff, 0.f, 1.f));
        const float k = 2.f - 1.9f * std::clamp (p.reso, 0.f, 1.f);
        const double bendMul = std::exp2 ((double) bend / 12.0), detCents = 25.0 * (double) p.detune;
        for (int i = 0; i < num; ++i)
        {
            float l = 0.f, r = 0.f;
            for (auto& v : voices)
            {
                if (! v.on) continue;
                switch (v.stage)
                {
                    case 0: v.env += aInc; if (v.env >= 1.f) { v.env = 1.f; v.stage = 1; } break;
                    case 1: v.env += (p.sustain - v.env) * dK; break;
                    default: v.env -= v.env * rK; if (v.env < 1e-4f) { v = Voice{}; continue; } break;
                }
                v.wenv *= wK;
                const float warp = std::clamp (p.warp + p.pluck * 0.6f * v.wenv + wheel * 0.4f, 0.f, 1.f);
                const float tw = std::clamp (p.twist, 0.f, 1.f);
                const double f = v.freq * bendMul;
                const double fl = f * std::exp2 (-detCents / 1200.0), fr = f * std::exp2 (detCents / 1200.0);
                const float cutoffHz = std::min (baseCut * std::exp2 (p.pluck * v.wenv * 3.f + (float) (v.note - 60) / 12.f * 0.5f), 0.45f * (float) sr);
                const float g = std::tan (3.14159265f * cutoffHz / (float) sr), a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
                const float amp = v.env * std::pow (v.vel, 0.7f) * 0.3f;
                for (int c = 0; c < 2; ++c)
                {
                    const double fc = c == 0 ? fl : fr;
                    double& ph = v.ph[c]; double& pm = v.pm[c];
                    const float x = shape ((float) ph, (float) pm, warp, tw);
                    ph += fc / sr; if (ph >= 1.0) ph -= 1.0;
                    pm += fc * (double) ratio / sr; pm -= std::floor (pm);
                    const float v3 = x - v.ic2[c], v1 = a1 * v.ic1[c] + a2 * v3, v2 = v.ic2[c] + a2 * v.ic1[c] + a3 * v3;
                    v.ic1[c] = 2.f * v1 - v.ic1[c]; v.ic2[c] = 2.f * v2 - v.ic2[c];
                    (c == 0 ? l : r) += v2 * amp;
                }
            }
            L[i] = std::tanh (l); R[i] = std::tanh (r);
        }
    }

private:
    struct Voice
    {
        bool on = false, gate = false; int note = 0, stage = 0; float vel = 0.f, env = 0.f, wenv = 1.f; double freq = 440.0; long order = 0;
        double ph[2] = { 0.0, 0.25 }, pm[2] = { 0.0, 0.0 }; float ic1[2] = { 0.f, 0.f }, ic2[2] = { 0.f, 0.f };
    };
    double sr = 48000.0; long order = 0; float bend = 0.f, wheel = 0.f;
    std::array<Voice, maxVoices> voices {};
};
} // namespace wp
