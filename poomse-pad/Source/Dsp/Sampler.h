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

    // Built-in evolving harmonic drone + noise ticks, used until a file is dropped on the window.
    static std::shared_ptr<SourceBuffer> makeDefault (double sr = 48000.0)
    {
        auto b = std::make_shared<SourceBuffer>();
        b->sampleRate = sr;
        const int n = (int) (sr * 4.0);
        b->l.resize ((size_t) n);
        b->r.resize ((size_t) n);
        gg::Rng rng (42);
        double phL = 0.0, phR = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            // 261.6 Hz = C3 in Live (MIDI 60): the default sample is in tune with the keyboard
            const double f0 = 261.63 * (1.0 + 0.004 * std::sin (2.0 * gg::kPi * 0.25 * t));
            phL += 2.0 * gg::kPi * f0 / sr;
            phR += 2.0 * gg::kPi * f0 * 1.002 / sr;
            float sl = 0.f, sr_ = 0.f;
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

    void noteOn (int note, float velocity, const SourceBuffer& src)
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
        v->inc = std::exp2 ((double) (note - 60) / 12.0) * src.sampleRate / sr;
        v->order = ++counter;
    }

    void noteOff (int note)
    {
        for (auto& v : voices) if (v.active && v.held && v.note == note) v.held = false;
    }

    void allOff() { for (auto& v : voices) v.held = false; }
    void kill() { for (auto& v : voices) v = Voice{}; }
    bool anyActive() const { for (const auto& v : voices) if (v.active) return true; return false; }

    // one stereo sample (the sum of all voices)
    void process (const SourceBuffer& src, bool loop, float& outL, float& outR)
    {
        outL = outR = 0.f;
        const int n = src.size();
        if (n < 4) return;
        const int fade = std::min (n / 4, (int) (0.03 * src.sampleRate)); // loop cross-fade length
        const int loopEnd = n - fade;

        for (auto& v : voices)
        {
            if (! v.active) continue;
            if (v.held) v.env = std::min (1.f, v.env + attackInc);
            else { v.env -= releaseInc; if (v.env <= 0.f) { v = Voice{}; continue; } }

            double pos = v.pos;
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

            const float g = v.env * v.vel * 0.7f;
            outL += l * g; outR += r * g;

            v.pos += v.inc;
            if (loop) { while (v.pos >= (double) n) v.pos -= (double) loopEnd; }
            else if (v.pos >= (double) (n - 1)) { v.held = false; } // one-shot: let it fade out
        }
    }

private:
    struct Voice
    {
        bool active = false, held = false;
        int note = 60;
        double pos = 0.0, inc = 1.0;
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
