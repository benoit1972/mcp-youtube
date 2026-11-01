#pragma once
#include "Modulators.h"
#include "Rng.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

// Polyphonic one-shot / looping sample player. It only feeds the spectral pad: every key plays the sample,
// transposed (root note = MIDI 60, C3 in Live), and the sum of all voices goes into the pad engine.
namespace pad
{
struct SourceBuffer
{
    std::vector<float> l, r;
    double sampleRate = 48000.0;
    int size() const { return (int) l.size(); }

    // Built-in sources used until a file is dropped on the window.
    //   variant 0: evolving harmonic drone with soft noise ticks   (zone A)
    //   variant 1: hollow, airy tone: odd harmonics + breathy noise (zone B)
    static std::shared_ptr<SourceBuffer> makeDefault (double sr = 48000.0, int variant = 0)
    {
        auto b = std::make_shared<SourceBuffer>();
        b->sampleRate = sr;
        const int n = (int) (sr * 4.0);
        b->l.resize ((size_t) n);
        b->r.resize ((size_t) n);
        gg::Rng rng (42);
        double phL = 0.0, phR = 0.0;
        float breathL = 0.f, breathR = 0.f;
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            // 261.6 Hz = C3 in Live (MIDI 60): the default sample is in tune with the keyboard
            const double f0 = 261.63 * (1.0 + 0.004 * std::sin (2.0 * gg::kPi * 0.25 * t));
            phL += 2.0 * gg::kPi * f0 / sr;
            phR += 2.0 * gg::kPi * f0 * 1.002 / sr;
            float sl = 0.f, sr_ = 0.f;
            if (variant == 0)
            {
                for (int k = 1; k <= 12; ++k)
                {
                    const float amp = (0.5f + 0.5f * (float) std::sin (2.0 * gg::kPi * 0.5 * t + k)) / (float) k;
                    sl += amp * (float) std::sin (k * phL);
                    sr_ += amp * (float) std::sin (k * phR);
                }
                const double tickT = std::fmod (t, 0.5);
                const float tick = (float) std::exp (-tickT * 60.0) * 0.4f;
                b->l[(size_t) i] = sl * 0.3f + tick * rng.bi() * 0.3f;
                b->r[(size_t) i] = sr_ * 0.3f + tick * rng.bi() * 0.3f;
            }
            else
            {
                for (int k = 1; k <= 15; k += 2) // odd harmonics only: hollow, clarinet-like
                {
                    const float amp = (0.6f + 0.4f * (float) std::sin (2.0 * gg::kPi * 0.31 * t + 2.0 * k)) / (float) k;
                    sl += amp * (float) std::sin (k * phL);
                    sr_ += amp * (float) std::sin (k * phR);
                }
                breathL += 0.12f * (rng.bi() - breathL);    // low-passed noise = breath
                breathR += 0.12f * (rng.bi() - breathR);
                b->l[(size_t) i] = sl * 0.3f + breathL * 0.5f;
                b->r[(size_t) i] = sr_ * 0.3f + breathR * 0.5f;
            }
        }
        float peak = 1e-6f;
        for (int i = 0; i < n; ++i)
            peak = std::max ({ peak, std::abs (b->l[(size_t) i]), std::abs (b->r[(size_t) i]) });
        const float g = 0.6f / peak;
        for (int i = 0; i < n; ++i) { b->l[(size_t) i] *= g; b->r[(size_t) i] *= g; }
        return b;
    }
};

class Sampler
{
public:
    static constexpr int numVoices = 8;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        attackInc = 1.f / (0.004f * (float) sr);   // 4 ms: no click, attack stays sharp
        releaseInc = 1.f / (0.06f * (float) sr);   // 60 ms: the pad itself provides the long tail
        for (auto& v : voices) v = Voice{};
        counter = 0;
    }

    // every key plays BOTH samples (A and B) at the same transposition; the morph is done later, on the spectra
    void noteOn (int note, float velocity, const SourceBuffer& a, const SourceBuffer& b)
    {
        Voice* v = nullptr;
        for (auto& x : voices) if (! x.active) { v = &x; break; }
        if (v == nullptr)
        {
            for (auto& x : voices)
                if (v == nullptr || (! x.held && v->held) || (x.held == v->held && x.order < v->order)) v = &x;
        }
        *v = Voice{};
        v->active = true; v->held = true; v->note = note; v->vel = velocity;
        const double ratio = std::exp2 ((double) (note - 60) / 12.0);
        v->inc[0] = ratio * a.sampleRate / sr;
        v->inc[1] = ratio * b.sampleRate / sr;
        v->order = ++counter;
    }

    void noteOff (int note)
    {
        for (auto& v : voices) if (v.active && v.held && v.note == note) v.held = false;
    }

    void allOff() { for (auto& v : voices) v.held = false; }
    void kill() { for (auto& v : voices) v = Voice{}; }
    bool anyActive() const { for (const auto& v : voices) if (v.active) return true; return false; }

    // one stereo sample of each source (sum of all voices)
    void process (const SourceBuffer& a, const SourceBuffer& b, bool loop,
                  float& aL, float& aR, float& bL, float& bR)
    {
        aL = aR = bL = bR = 0.f;
        const SourceBuffer* srcs[2] = { &a, &b };
        float* outs[2][2] = { { &aL, &aR }, { &bL, &bR } };

        for (auto& v : voices)
        {
            if (! v.active) continue;
            if (v.held) v.env = std::min (1.f, v.env + attackInc);
            else { v.env -= releaseInc; if (v.env <= 0.f) { v = Voice{}; continue; } }

            const float g = v.env * v.vel * 0.7f;
            bool finished = true;
            for (int s = 0; s < 2; ++s)
            {
                const auto& src = *srcs[s];
                const int n = src.size();
                if (n < 4) continue;
                const int fade = std::min (n / 4, (int) (0.03 * src.sampleRate)); // loop cross-fade length
                const int loopEnd = n - fade;
                double pos = v.pos[s];
                float l, r;
                if (loop && fade > 1 && pos >= (double) loopEnd)
                {
                    const float t = (float) ((pos - (double) loopEnd) / (double) fade);
                    float al, ar, bl, br;
                    read (src, pos, al, ar);
                    read (src, pos - (double) loopEnd, bl, br);
                    l = al * (1.f - t) + bl * t;
                    r = ar * (1.f - t) + br * t;
                }
                else
                    read (src, pos, l, r);
                *outs[s][0] += l * g; *outs[s][1] += r * g;

                v.pos[s] += v.inc[s];
                if (loop) { while (v.pos[s] >= (double) n) v.pos[s] -= (double) loopEnd; finished = false; }
                else if (v.pos[s] < (double) (n - 1)) finished = false;
            }
            if (! loop && finished) v.held = false; // both one-shots reached their end: let the voice fade out
        }
    }

private:
    struct Voice
    {
        bool active = false, held = false;
        int note = 60;
        double pos[2] = { 0.0, 0.0 }, inc[2] = { 1.0, 1.0 };
        float vel = 1.f, env = 0.f;
        uint64_t order = 0;
    };

    static void read (const SourceBuffer& s, double pos, float& l, float& r)
    {
        const int n = s.size();
        int i0 = (int) pos;
        const float f = (float) (pos - (double) i0);
        i0 = std::clamp (i0, 0, n - 1);
        const int i1 = std::min (i0 + 1, n - 1);
        l = s.l[(size_t) i0] + f * (s.l[(size_t) i1] - s.l[(size_t) i0]);
        r = s.r[(size_t) i0] + f * (s.r[(size_t) i1] - s.r[(size_t) i0]);
    }

    double sr = 48000.0;
    float attackInc = 0.01f, releaseInc = 0.001f;
    uint64_t counter = 0;
    std::array<Voice, numVoices> voices;
};
} // namespace pad
