#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "Sounds.h"

// The flock. A handful of voices of the same instrument, each one gliding from note to note of the scale. No voice is told what to play:
// each time one of them leaves a note it looks at where the others are and chooses its next note by itself. It is drawn to the notes that
// sit well with the others (thirds, fifths, sixths), pushed away from the ones that rub (semitones, tritones), held near its own register,
// and a slow wind moves the whole flock up and down the scale. The harmony is not written anywhere: it comes out of the flock.
// Everything happens on the half-beat grid and nothing is random at run time: the seed writes the flight.
namespace mur
{
constexpr int kMaxVoices = 8;

inline uint32_t mixHash (uint32_t a, uint32_t b, uint32_t c, uint32_t d = 0)
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu ^ (c + 0x165667B1u) * 0xC2B2AE35u ^ (d + 0x27D4EB2Fu) * 0x9E3779B1u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
inline float unitOf (uint32_t h) { return (float) (h >> 8) * (1.0f / 16777216.0f); }
inline float smooth (float x) { x = std::clamp (x, 0.f, 1.f); return x * x * (3.f - 2.f * x); }
inline float arcShape (float p)
{
    p = std::clamp (p, 0.f, 1.f);
    return p < 0.5f ? 0.2f + 0.8f * smooth (p / 0.5f) : 1.f - 0.8f * smooth ((p - 0.5f) / 0.5f);
}

struct FlockParams
{
    int scale = 1;           // index into kScales
    int key = -1;            // pitch class of the key (0 = C), -1 = from the held note
    int voices = 5;          // how many voices at the most
    float wild = 0.4f;       // 0: calm, close notes, consonant. 1: bold leaps, slow slides, reversed voices
    float speed = 0.5f;      // how long a voice stays on a note
    float range = 0.5f;      // how far the flock spreads
    float journey = 0.6f;    // 0: all voices from the start; 1: it grows from one voice, peaks, and thins out
    int pieceBars = 96;
    int beatsPerBar = 4;
    int rootNote = 60;       // the pitch of the sample
};

class Flock
{
public:
    static constexpr double lowest = 14.0, highest = 19.0;      // the window of the flock, around the root of the sample: -14 .. +19 semitones

    struct VoiceView { bool alive; float pitch, level, pan; bool reverse; };

    void start (double originBeat, uint32_t seed_, int tonicNote)
    {
        origin = std::floor (originBeat * 2.0 + 0.5) / 2.0; seed = seed_ * 2654435761u + 977u; tonic = tonicNote; running = true;
        nextTick = origin; nextWind = origin + 16.0; windSemis = 0.f; windCount = 0; tickCount = 0;
        for (auto& v : voices) v = Voice{};
    }
    void stop() { running = false; }
    bool isRunning() const { return running; }
    void setTonic (int tonicNote) { tonic = tonicNote; }
    double originBeat() const { return origin; }
    float progress (double beat, const FlockParams& p) const { return (float) ((beat - origin) / std::max (1.0, (double) p.pieceBars * p.beatsPerBar)); }

    // the world up to `beat`: everything that was to be decided before it, in order of time
    void advance (double beat, const FlockParams& p)
    {
        if (! running) return;
        for (int guard = 0; guard < 4000; ++guard)
        {
            double tNext = nextTick; int who = -1;
            for (int v = 0; v < kMaxVoices; ++v) if (voices[(size_t) v].alive && ! voices[(size_t) v].dying && voices[(size_t) v].nextLeg < tNext - 1e-9) { tNext = voices[(size_t) v].nextLeg; who = v; }
            if (tNext > beat) break;
            if (who < 0) { onTick (tNext, p); nextTick += 1.0; }
            else leg (who, tNext, p);
        }
    }

    VoiceView view (int v, double beat, const FlockParams& p) const
    {
        const Voice& s = voices[(size_t) v];
        VoiceView o { false, 60.f, 0.f, 0.f, s.reverse };
        if (! running && ! s.alive) return o;
        if (! s.alive) return o;
        o.pitch = pitchNow (v, beat);
        const float fadeIn = smooth ((float) ((beat - s.born) / 6.0));
        const float fadeOut = s.dying ? 1.f - smooth ((float) ((beat - s.died) / 8.0)) : 1.f;
        const float breath = 0.78f + 0.22f * (float) std::cos (6.2831853 * (beat - origin) / std::max (1.0, s.legLen) + s.phase);
        o.level = (0.85f - 0.04f * (float) v) * fadeIn * fadeOut * breath;
        o.pan = 0.75f * (float) std::sin (6.2831853 * (beat - origin) / (40.0 + 11.0 * v) + s.phase * 2.0);
        o.alive = o.level > 1e-4f;
        (void) p;
        return o;
    }
    float pitchNow (int v, double beat) const
    {
        const Voice& s = voices[(size_t) v];
        const float g = s.glideLen > 0.0 ? smooth ((float) ((beat - s.legStart) / s.glideLen)) : 1.f;
        return s.from + (s.to - s.from) * g;
    }
    float target (int v) const { return voices[(size_t) v].to; }
    bool alive (int v) const { return voices[(size_t) v].alive && ! voices[(size_t) v].dying; }
    int aliveCount() const { int n = 0; for (int v = 0; v < kMaxVoices; ++v) n += alive (v); return n; }
    float wind() const { return windSemis; }
    double legLength (int v) const { return voices[(size_t) v].legLen; }

private:
    struct Voice
    {
        bool alive = false, dying = false, reverse = false;
        double born = 0.0, died = 0.0, legStart = 0.0, legLen = 8.0, glideLen = 2.0, nextLeg = 0.0;
        float from = 60.f, to = 60.f, phase = 0.f;
        int legCount = 0;
    };

    int tonicPc(const FlockParams& p) const { return p.key >= 0 ? p.key : ((tonic % 12) + 12) % 12; }
    bool inScale (int note, const FlockParams& p) const
    {
        const auto& sd = kScales[std::clamp (p.scale, 0, kNumScales - 1)];
        const int rel = (((note - tonicPc (p)) % 12) + 12) % 12;
        for (int i = 0; i < sd.n; ++i) if (sd.iv[i] == rel) return true;
        return false;
    }
    float homeOf (int v) const { static const float h[kMaxVoices] = { -10.f, -3.f, 0.f, 4.f, 7.f, 10.f, -6.f, 13.f }; return h[v]; }
    double baseLeg (int v) const { static const double b[kMaxVoices] = { 8, 12, 6, 16, 10, 14, 20, 24 }; return b[v]; }

    static float consonance (int diff)         // the interval between two voices, in semitones
    {
        static const float c[12] = { 0.9f, -6.f, -0.6f, 1.0f, 1.2f, 0.8f, -4.f, 1.6f, 0.9f, 1.1f, -0.6f, -6.f };
        if (diff == 0) return -2.0f;
        return c[diff % 12];
    }

    void onTick (double t, const FlockParams& p)
    {
        ++tickCount;
        // the wind: now and then the whole flock is pushed up or down the scale
        if (t >= nextWind)
        {
            static const float steps[8] = { -7.f, -5.f, -3.f, 0.f, 2.f, 3.f, 5.f, 7.f };
            const uint32_t h = mixHash (seed, (uint32_t) windCount++, 11u);
            float w = steps[h % 8u];
            if (unitOf (h >> 5) > 0.35f + 0.5f * p.wild) w *= 0.5f;                         // a calm flock takes smaller steps
            windSemis = w;
            nextWind = t + 16.0 + 16.0 * (double) unitOf (mixHash (seed, (uint32_t) windCount, 12u));
        }
        // births and deaths: the number of voices follows the arc of the piece
        const float arc = 1.f + p.journey * (arcShape (progress (t, p)) - 1.f);
        const int want = std::clamp ((int) std::lround ((float) std::clamp (p.voices, 1, kMaxVoices) * arc), 1, std::clamp (p.voices, 1, kMaxVoices));
        for (int v = 0; v < kMaxVoices; ++v)
        {
            auto& s = voices[(size_t) v];
            if (v < want && ! (s.alive && ! s.dying)) birth (v, t, p);
            else if (v >= want && s.alive && ! s.dying) { s.dying = true; s.died = t; }
            else if (s.alive && s.dying && t > s.died + 8.0) { s = Voice{}; }
        }
    }

    void birth (int v, double t, const FlockParams& p)
    {
        auto& s = voices[(size_t) v];
        const int prev = s.legCount;
        s = Voice{}; s.alive = true; s.born = t; s.legCount = prev + 1;
        s.phase = 6.2831853f * unitOf (mixHash (seed, (uint32_t) v, 21u, (uint32_t) s.legCount));
        s.reverse = unitOf (mixHash (seed, (uint32_t) v, 22u, (uint32_t) s.legCount)) < 0.4f * p.wild * p.wild + (v == 0 ? 0.f : 0.05f * p.wild);
        s.from = s.to = 60.f;
        s.legStart = t; s.glideLen = 0.0;
        choose (v, t, p, true);
        scheduleLeg (v, t, p);
    }

    void scheduleLeg (int v, double t, const FlockParams& p)
    {
        auto& s = voices[(size_t) v];
        const double sf = std::pow (2.0, ((double) p.speed - 0.5) * 2.0);                       // 0.5 .. 2
        double len = baseLeg (v) / sf;
        len = std::max (2.0, std::floor (len * 2.0 + 0.5) / 2.0);
        s.legLen = len; s.nextLeg = t + len;
    }

    void leg (int v, double t, const FlockParams& p)
    {
        auto& s = voices[(size_t) v];
        s.from = pitchNow (v, t);                          // from where it really is, even if the last slide is not over
        s.legStart = t;
        ++s.legCount;
        choose (v, t, p, false);
        scheduleLeg (v, t, p);
        // how long the slide takes: a short one, or (for a bold flock) a long slow one; a big leap always takes its time
        const double slow = unitOf (mixHash (seed, (uint32_t) v, 31u, (uint32_t) s.legCount)) < 0.5f * p.wild ? 0.75 : 0.3;
        const double dist = std::abs ((double) (s.to - s.from));
        s.glideLen = dist < 0.01 ? 0.0 : std::clamp (std::max (s.legLen * slow, dist * 0.3), 1.0, 16.0);
    }

    // the choice of the next note
    void choose (int v, double t, const FlockParams& p, bool fresh)
    {
        auto& s = voices[(size_t) v];
        const float mid = (float) p.rootNote + 2.5f;
        const float exc = 3.f + 21.f * p.range;
        const float pref = std::clamp (mid + windSemis * (0.4f + 0.6f * p.range) + homeOf (v) * (0.35f + 0.65f * p.range), (float) p.rootNote - (float) lowest, (float) p.rootNote + (float) highest);
        const float cur = fresh ? pref : s.from;
        int cand[48]; float score[48]; int n = 0;
        for (int note = p.rootNote - (int) lowest; note <= p.rootNote + (int) highest && n < 48; ++note)
        {
            if (! inScale (note, p)) continue;
            float sc = -2.0f * std::pow ((note - pref) / exc, 2.f);
            sc -= (0.14f * (1.f - p.wild) - 0.05f * p.wild) * std::abs (note - cur);
            if (! fresh && note == (int) std::lround (s.from)) sc += 0.35f * (1.f - p.wild);                    // staying put is allowed
            for (int j = 0; j < kMaxVoices; ++j)
            {
                if (j == v || ! alive (j)) continue;
                const int d = std::abs (note - (int) std::lround (voices[(size_t) j].to));
                float c = consonance (d % 12 == 0 && d > 0 ? 0 : d);
                if (d % 12 == 0 && d > 0) c = 0.9f;
                const int dc = d % 12;
                if (dc == 1 || dc == 11 || dc == 6) c -= 100.f;                                                  // never a semitone or a tritone with a voice that sings
                sc += 0.6f * c;
            }
            cand[n] = note; score[n] = sc; ++n;
        }
        if (n == 0) { s.to = (float) p.rootNote; return; }
        const float temp = 0.3f + 1.0f * p.wild;
        float best = -1e9f; for (int i = 0; i < n; ++i) best = std::max (best, score[i]);
        float total = 0.f; float w[48];
        for (int i = 0; i < n; ++i) { w[i] = std::exp ((score[i] - best) / temp); total += w[i]; }
        float r = unitOf (mixHash (seed, (uint32_t) v, 41u, (uint32_t) s.legCount)) * total; int pick = n - 1;
        for (int i = 0; i < n; ++i) { r -= w[i]; if (r <= 0.f) { pick = i; break; } }
        s.to = (float) cand[pick];
        if (fresh) { s.from = s.to; }
        (void) t;
    }

    bool running = false;
    double origin = 0.0, nextTick = 0.0, nextWind = 0.0;
    uint32_t seed = 1;
    int tonic = 60, windCount = 0, tickCount = 0;
    float windSemis = 0.f;
    std::array<Voice, kMaxVoices> voices {};
};
} // namespace mur
