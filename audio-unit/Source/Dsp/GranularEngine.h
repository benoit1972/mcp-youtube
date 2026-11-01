#pragma once
#include "Rng.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace gg
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kSqrt2 = 1.41421356237f;

//==============================================================================
// Audio material the grains are cut from (stereo, immutable once published).
struct SourceBuffer
{
    std::vector<float> l, r;
    double sampleRate = 48000.0;
    int size() const { return (int) l.size(); }

    // Built-in evolving harmonic drone + noise ticks, used until a file is loaded.
    static std::shared_ptr<SourceBuffer> makeDefault (double sr = 48000.0)
    {
        auto b = std::make_shared<SourceBuffer>();
        b->sampleRate = sr;
        const int n = (int) (sr * 4.0);
        b->l.resize ((size_t) n);
        b->r.resize ((size_t) n);
        Rng rng (42);
        double phL = 0.0, phR = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            const double f0 = 110.0 * (1.0 + 0.04 * std::sin (2.0 * kPi * 0.25 * t));
            phL += 2.0 * kPi * f0 / sr;
            phR += 2.0 * kPi * f0 * 1.003 / sr;
            float sl = 0.f, sr_ = 0.f;
            for (int k = 1; k <= 16; ++k)
            {
                const float amp = (0.5f + 0.5f * (float) std::sin (2.0 * kPi * 0.5 * t + k)) / (float) k;
                sl += amp * (float) std::sin (k * phL);
                sr_ += amp * (float) std::sin (k * phR);
            }
            // a short decaying noise burst every half second
            const double tickT = std::fmod (t, 0.5);
            const float tick = (float) std::exp (-tickT * 60.0) * 0.6f;
            b->l[(size_t) i] = sl * 0.3f + tick * rng.bi() * 0.5f;
            b->r[(size_t) i] = sr_ * 0.3f + tick * rng.bi() * 0.5f;
        }
        float peak = 1e-6f;
        for (int i = 0; i < n; ++i)
            peak = std::max ({ peak, std::abs (b->l[(size_t) i]), std::abs (b->r[(size_t) i]) });
        const float g = 0.6f / peak;
        for (int i = 0; i < n; ++i) { b->l[(size_t) i] *= g; b->r[(size_t) i] *= g; }
        return b;
    }
};

// Output limiter that leaves everything below 0.8 untouched (so a sound is heard as it is)
// and only rounds off the peaks above it, never exceeding 1.
inline float softClip (float x)
{
    const float a = std::abs (x);
    if (a <= 0.8f) return x;
    const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
    return x < 0.f ? -y : y;
}

//==============================================================================
struct GrainParams
{
    // Defaults are neutral: a note plays the loaded sound as it is (position 0, playhead scanning at 1x,
    // no spray / pitch spray / reverse, centred, Hann windows with ~4x overlap).
    float position = 0.f;    // 0..1 where the note starts in the source
    float scan = 1.f;        // playhead speed: 1 = natural, 0 = frozen at position, negative = backwards
    float spray = 0.f;       // 0..1 random position offset (fraction of source)
    float sizeMs = 120.f;    // grain length
    float density = 30.f;    // grains per second per voice
    float pitch = 0.f;       // semitones
    float pitchSpray = 0.f;  // +/- semitones, random per grain
    float reverse = 0.f;     // 0..1 probability a grain plays backwards
    float panSpread = 0.f;   // 0..1
    float shape = 0.f;       // 0 = hann, 1 = near-rectangular window
    float attack = 0.01f, decay = 0.3f, sustain = 1.f, release = 0.4f; // seconds / level
};

//==============================================================================
class Envelope
{
public:
    void setSampleRate (double s) { sr = (float) s; }
    void setParams (float a, float d, float s, float r)
    {
        attackInc = 1.0f / std::max (0.001f, a) / sr;
        decayCoef = std::exp (-4.6f / (std::max (0.005f, d) * sr));
        sustain = std::clamp (s, 0.f, 1.f);
        releaseCoef = std::exp (-6.9f / (std::max (0.005f, r) * sr));
    }
    void noteOn() { stage = Stage::Attack; }
    void noteOff() { if (stage != Stage::Idle) stage = Stage::Release; }
    void kill() { stage = Stage::Idle; level = 0.f; }
    bool isActive() const { return stage != Stage::Idle; }
    bool isReleasing() const { return stage == Stage::Release; }

    float next()
    {
        switch (stage)
        {
            case Stage::Idle: return 0.f;
            case Stage::Attack:
                level += attackInc;
                if (level >= 1.f) { level = 1.f; stage = Stage::Decay; }
                break;
            case Stage::Decay:
                level = sustain + (level - sustain) * decayCoef;
                if (std::abs (level - sustain) < 1e-4f)
                {
                    level = sustain;
                    stage = sustain < 1e-4f ? Stage::Idle : Stage::Sustain;
                }
                break;
            case Stage::Sustain: level = sustain; break;
            case Stage::Release:
                level *= releaseCoef;
                if (level < 1e-4f) { level = 0.f; stage = Stage::Idle; }
                break;
        }
        return level;
    }

private:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };
    Stage stage = Stage::Idle;
    float sr = 48000.f, level = 0.f, attackInc = 0.01f, decayCoef = 0.999f, sustain = 0.8f, releaseCoef = 0.999f;
};

//==============================================================================
struct Grain
{
    bool active = false;
    double pos = 0.0, inc = 1.0;
    int age = 0, len = 1;
    float gl = 0.f, gr = 0.f, fade = 0.5f;

    inline void tick (const SourceBuffer& s, float& outL, float& outR)
    {
        const int n = s.size();
        const float t = ((float) age + 0.5f) / (float) len;
        float w = 1.f;
        if (t < fade)             w = 0.5f * (1.f - std::cos (kPi * t / fade));
        else if (t > 1.f - fade)  w = 0.5f * (1.f - std::cos (kPi * (1.f - t) / fade));

        const int i0 = std::clamp ((int) pos, 0, n - 1);
        const float fr = (float) (pos - (double) i0);
        const int i1 = i0 + 1 >= n ? 0 : i0 + 1;
        const size_t a = (size_t) i0, b = (size_t) i1;
        outL += (s.l[a] + fr * (s.l[b] - s.l[a])) * w * gl;
        outR += (s.r[a] + fr * (s.r[b] - s.r[a])) * w * gr;

        pos += inc;
        while (pos >= (double) n) pos -= (double) n;
        while (pos < 0.0) pos += (double) n;
        if (++age >= len) active = false;
    }
};

class Voice
{
public:
    static constexpr int maxGrains = 24;

    void prepare (double sampleRate, uint32_t seed)
    {
        sr = (float) sampleRate;
        rng = Rng (seed);
        env.setSampleRate (sampleRate);
        kill();
    }

    void noteOn (int n, float vel, uint64_t order)
    {
        note = n; velocity = vel; startOrder = order; held = true;
        countdown = 0.f;
        scanFrac = 0.0;
        env.noteOn();
    }
    void noteOff() { held = false; env.noteOff(); }
    void kill() { env.kill(); held = false; for (auto& g : grains) g.active = false; }

    bool isActive() const { return env.isActive(); }
    bool isHeld() const { return held; }
    int getNote() const { return note; }
    uint64_t getStartOrder() const { return startOrder; }

    void render (float* L, float* R, int numSamples, const SourceBuffer& src, const GrainParams& p)
    {
        if (! env.isActive() || src.size() < 2) return;
        env.setParams (p.attack, p.decay, p.sustain, p.release);
        const float interval = sr / std::max (1.f, p.density);
        // spacing is almost regular unless Spray asks for a looser cloud (regular overlap = transparent playback)
        const float jitter = 0.02f + 0.23f * std::min (1.f, p.spray * 4.f);
        // playhead advance per output sample, as a fraction of the source (natural speed at scan = 1)
        const double scanInc = (double) p.scan * src.sampleRate / ((double) sr * (double) src.size());
        const float overlap = std::max (1.f, p.density * p.sizeMs * 0.001f);
        const float norm = velocity / std::sqrt (overlap);

        for (int i = 0; i < numSamples; ++i)
        {
            if (countdown <= 0.f)
            {
                spawn (src, p, norm);
                countdown += interval * (1.f - jitter + 2.f * jitter * rng.uni());
            }
            countdown -= 1.f;
            scanFrac += scanInc;
            if (scanFrac >= 1.0 || scanFrac < 0.0) scanFrac -= std::floor (scanFrac);

            float l = 0.f, r = 0.f;
            for (auto& g : grains)
                if (g.active) g.tick (src, l, r);

            const float e = env.next();
            L[i] += l * e;
            R[i] += r * e;
            if (! env.isActive()) { kill(); break; }
        }
    }

private:
    void spawn (const SourceBuffer& src, const GrainParams& p, float norm)
    {
        Grain* g = nullptr;
        for (auto& x : grains) if (! x.active) { g = &x; break; }
        if (g == nullptr) return;

        const int n = src.size();
        const float lenSamples = p.sizeMs * 0.001f * sr;
        g->len = std::max (16, (int) (lenSamples * (0.9f + 0.2f * rng.uni())));
        g->age = 0;
        g->fade = 0.5f - 0.45f * std::clamp (p.shape, 0.f, 1.f);

        double start = (double) p.position + scanFrac + (double) p.spray * rng.bi();
        start -= std::floor (start);

        const double semis = (double) (note - 60) + p.pitch + p.pitchSpray * rng.bi();
        const double rate = std::exp2 (semis / 12.0) * src.sampleRate / (double) sr;

        double pos = start * (double) n;
        const bool rev = rng.uni() < p.reverse;
        if (rev) pos += rate * (double) g->len; // play the same region backwards
        pos = std::fmod (pos, (double) n);
        if (pos < 0.0) pos += (double) n;
        g->pos = pos;
        g->inc = rev ? -rate : rate;

        const float pan = p.panSpread * rng.bi();
        const float angle = (pan + 1.f) * kPi * 0.25f;
        g->gl = std::cos (angle) * kSqrt2 * norm;
        g->gr = std::sin (angle) * kSqrt2 * norm;
        g->active = true;
    }

    std::array<Grain, maxGrains> grains;
    Envelope env;
    Rng rng;
    float sr = 48000.f, countdown = 0.f, velocity = 1.f;
    double scanFrac = 0.0;
    int note = 60;
    bool held = false;
    uint64_t startOrder = 0;
};

//==============================================================================
class GranularEngine
{
public:
    static constexpr int numVoices = 8;

    void prepare (double sampleRate)
    {
        for (int i = 0; i < numVoices; ++i)
            voices[(size_t) i].prepare (sampleRate, 0x9E3779B9u * (uint32_t) (i + 1));
        counter = 0;
    }

    void noteOn (int note, float velocity)
    {
        Voice* v = nullptr;
        for (auto& x : voices) if (! x.isActive()) { v = &x; break; }
        if (v == nullptr) // steal: prefer a releasing/unheld voice, otherwise the oldest
        {
            for (auto& x : voices)
            {
                const bool better = v == nullptr
                    || (! x.isHeld() && v->isHeld())
                    || (x.isHeld() == v->isHeld() && x.getStartOrder() < v->getStartOrder());
                if (better) v = &x;
            }
        }
        v->kill();
        v->noteOn (note, velocity, ++counter);
    }

    void noteOff (int note)
    {
        for (auto& v : voices)
            if (v.isActive() && v.isHeld() && v.getNote() == note) v.noteOff();
    }

    void allNotesOff() { for (auto& v : voices) v.kill(); }

    // true while at least one key is physically held (voices in release do not count)
    bool anyHeld() const
    {
        for (const auto& v : voices) if (v.isActive() && v.isHeld()) return true;
        return false;
    }

    // Adds into L/R (caller clears).
    void render (float* L, float* R, int numSamples, const SourceBuffer& src, const GrainParams& p)
    {
        for (auto& v : voices) v.render (L, R, numSamples, src, p);
    }

private:
    std::array<Voice, numVoices> voices;
    uint64_t counter = 0;
};
} // namespace gg
