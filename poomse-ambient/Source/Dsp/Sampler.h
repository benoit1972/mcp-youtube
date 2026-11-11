#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include "Samples.h"

// Plays a Bank with the keys. A key plays the note (and, if asked, a chord on it); the sound of a place is let in by the key instead.
// Loops: a held key says its note again and again, softly, at a period of its own (5, 7, 9, 11... beats), so that several keys
// drift against each other like the tape loops of Music for Airports.
namespace amb
{
inline constexpr int kChords = 6;
inline constexpr const char* kChordNames[kChords] = { "Off", "Octave", "Fifth", "Major", "Minor", "Sus" };
inline constexpr int kChordNotes[kChords][3] = { { 0, -1, -1 }, { 0, 12, -1 }, { 0, 7, 12 }, { 0, 4, 7 }, { 0, 3, 7 }, { 0, 5, 7 } };
inline constexpr double kLoopBeats[12] = { 5, 7, 9, 11, 13, 6, 10, 14, 15, 8, 12, 17 };      // by pitch class: never the same period twice

struct SamplerParams
{
    float tone = 0.85f;      // 0 dark .. 1 open
    float bloom = 0.f;       // how slowly a note swells in (0 .. 3 s)
    float tail = 0.5f;       // how long it takes to fade out after the key is let go (0.08 .. 6 s)
    float loops = 0.f;       // how loud the repeats of held keys are
    int chord = 0;
    bool latch = false;
};

class Sampler
{
public:
    static constexpr int maxVoices = 32, maxHeld = 12, displayVoices = 8;

    void prepare (double sampleRate) { sr = sampleRate; killAll(); lpL[0] = lpL[1] = lpR[0] = lpR[1] = 0.f; }
    void setBank (const Bank* b) { bank = b; for (auto& v : voices) if (v.on) v.on = false; }

    void noteOn (int note, float vel, const SamplerParams& p, double beat)
    {
        if (bank == nullptr) return;
        for (auto& h : held)
            if (h.on && h.note == note)
            {
                if (p.latch && h.latched) { release (h); return; }          // pressed again: let go
                release (h);
                break;
            }
        Held* slot = nullptr;
        for (auto& h : held) if (! h.on) { slot = &h; break; }
        if (slot == nullptr) { long oldest = 1L << 60; for (auto& h : held) if (h.order < oldest) { oldest = h.order; slot = &h; } release (*slot); }
        *slot = Held{}; slot->on = true; slot->note = note; slot->vel = vel; slot->order = ++order; slot->startBeat = beat; slot->latched = p.latch;
        slot->repeats = 0;
        start (note, vel, p, false, 0.0);
    }
    void noteOff (int note)
    {
        for (auto& h : held) if (h.on && h.note == note && ! h.latched) release (h);
    }
    void allNotesOff() { for (auto& h : held) if (h.on) release (h); }
    void killAll() { for (auto& v : voices) v = Voice{}; for (auto& h : held) h = Held{}; order = 0; }

    void process (int num, const SamplerParams& p, double beat0, double bpm, float* L, float* R)
    {
        const double spb = sr * 60.0 / std::clamp (bpm, 20.0, 400.0);
        const double tailSec = 0.08 * std::pow (75.0, (double) std::clamp (p.tail, 0.f, 1.f));
        relK = 1.0 - std::exp (-1.0 / (tailSec * sr));
        bloomSec = (double) std::clamp (p.bloom, 0.f, 1.f) * (double) std::clamp (p.bloom, 0.f, 1.f) * 3.0;
        const float cut = 250.f * std::pow (72.f, std::clamp (p.tone, 0.f, 1.f));
        const float k = 1.f - std::exp (-6.2831853f * std::min (cut, 0.45f * (float) sr) / (float) sr);
        for (int i = 0; i < num; ++i)
        {
            // the repeats of the held keys
            if (p.loops > 0.001f && bank != nullptr && bank->pitched)
                for (auto& h : held)
                {
                    if (! h.on) continue;
                    const double period = kLoopBeats[((h.note % 12) + 12) % 12];
                    const long n = (long) std::floor ((beat0 + (double) i / spb - h.startBeat) / period);
                    if (n > h.repeats) { h.repeats = n; start (h.note, h.vel * p.loops * 0.8f, p, true, period * spb * 0.85); }
                }
            float l = 0.f, r = 0.f;
            for (auto& v : voices)
            {
                if (! v.on) continue;
                if (v.echoLeft > 0.0 && (v.echoLeft -= 1.0) <= 0.0) v.released = true;
                if (v.released) { v.env -= v.env * (float) relK; if (v.env < 1e-4f) { v = Voice{}; continue; } }
                else if (v.env < 1.f) v.env = std::min (1.f, v.env + v.attackInc);
                const Zone& z = *v.zone;
                float sl, sr_;
                if (! read (z, v.pos, sl, sr_)) { v = Voice{}; continue; }
                const float g = v.env * v.gain;
                l += sl * g * v.gl; r += sr_ * g * v.gr;
                v.pos += v.inc;
                if (z.loops()) { if (v.pos >= (double) z.loopEnd) v.pos -= (double) (z.loopEnd - z.loopStart); }
                else if (v.pos >= (double) (z.length() - 3)) { v.released = true; }
            }
            lpL[0] += k * (l - lpL[0]); lpL[1] += k * (lpL[0] - lpL[1]);
            lpR[0] += k * (r - lpR[0]); lpR[1] += k * (lpR[0] - lpR[1]);
            L[i] = std::tanh (lpL[1]); R[i] = std::tanh (lpR[1]);
        }
        // what the interface shows: where the first voices are in their sound
        int d = 0, n = 0;
        for (const auto& v : voices)
        {
            if (! v.on) continue;
            ++n;
            if (d < displayVoices) dispPos[(size_t) d++].store ((float) (v.pos / std::max (1, v.zone->length())));
        }
        for (; d < displayVoices; ++d) dispPos[(size_t) d].store (-1.f);
        dispCount.store (n);
    }

    int activeVoices() const { int n = 0; for (const auto& v : voices) if (v.on) ++n; return n; }
    int heldKeys() const { int n = 0; for (const auto& h : held) if (h.on) ++n; return n; }
    int started() const { return startedCount.load(); }
    float voicePosition (int i) const { return dispPos[(size_t) i].load(); }

private:
    struct Voice
    {
        bool on = false, released = false;
        const Zone* zone = nullptr;
        double pos = 0.0, inc = 1.0, echoLeft = 0.0;
        float env = 0.f, attackInc = 1.f, gain = 0.f, gl = 1.f, gr = 1.f;
        int note = 0; long order = 0;
    };
    struct Held { bool on = false, latched = false; int note = 0; float vel = 0.f; long order = 0, repeats = 0; double startBeat = 0.0; };

    void release (Held& h)
    {
        h.on = false;
        for (auto& v : voices) if (v.on && v.note == h.note && v.echoLeft <= 0.0) v.released = true;
        for (auto& v : voices) if (v.on && v.note == h.note && v.echoLeft > 0.0) v.released = true;
    }

    void start (int note, float vel, const SamplerParams& p, bool echo, double echoLife)
    {
        bloomSec = (double) p.bloom * (double) p.bloom * 3.0;
        const int ci = std::clamp (p.chord, 0, kChords - 1);
        int count = 0; for (int k = 0; k < 3; ++k) if (kChordNotes[ci][k] >= 0) ++count;
        const bool field = ! bank->pitched;
        for (int k = 0; k < 3; ++k)
        {
            const int iv = kChordNotes[ci][k]; if (iv < 0) continue;
            if (field && k > 0) break;
            Voice* slot = nullptr;
            for (auto& v : voices) if (! v.on) { slot = &v; break; }
            if (slot == nullptr) { long oldest = 1L << 60; for (auto& v : voices) if (v.order < oldest) { oldest = v.order; slot = &v; } }
            const int n = note + iv;
            const Zone& z = bank->zoneFor (field ? 60 : n);
            *slot = Voice{};
            slot->on = true; slot->zone = &z; slot->note = note; slot->order = ++order;
            const double ratio = field ? 1.0 : std::clamp (std::pow (2.0, (double) (n - z.root) / 12.0), 0.25, 4.0);
            slot->inc = ratio * z.sampleRate / sr;
            slot->pos = field ? (double) (z.loopStart + (int) ((uint32_t) (note * 2654435761u) % (uint32_t) std::max (1, z.length() - 2))) : 0.0;
            const double att = std::max (field ? 0.9 : 0.002, bloomSec);
            slot->attackInc = (float) (1.0 / (att * sr));
            slot->gain = vel * (0.55f / std::sqrt ((float) count)) * (field ? 0.9f : 1.f) * (echo ? 0.9f : 1.f);
            const float pan = field ? 0.5f : std::clamp (0.5f + (float) (n - 60) * 0.006f + (echo ? 0.15f * ((order & 1) ? 1.f : -1.f) : 0.f), 0.15f, 0.85f);
            slot->gl = 1.4142f * std::cos (pan * 1.5707963f); slot->gr = 1.4142f * std::sin (pan * 1.5707963f);
            if (echo) slot->echoLeft = echoLife;
            if (! bloomOn()) slot->env = z.loops() ? 0.f : 0.f;
            startedCount.fetch_add (1);
        }
    }
    bool bloomOn() const { return true; }

    static bool read (const Zone& z, double pos, float& l, float& r)
    {
        int i1 = (int) pos; const float f = (float) (pos - (double) i1);
        const int n = z.length();
        if (i1 >= n) return false;
        auto at = [&] (int ch, int idx) -> float
        {
            if (idx < 0) return 0.f;
            if (z.loops()) { if (idx >= z.loopEnd) idx -= (z.loopEnd - z.loopStart); }
            else if (idx >= n) return 0.f;
            return z.x[ch][(size_t) idx];
        };
        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            const float y0 = at (c, i1 - 1), y1 = at (c, i1), y2 = at (c, i1 + 1), y3 = at (c, i1 + 2);
            const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            out[c] = ((c3 * f + c2) * f + c1) * f + y1;
        }
        l = out[0]; r = out[1];
        return true;
    }

    double sr = 48000.0, relK = 0.001, bloomSec = 0.0;
    const Bank* bank = nullptr;
    long order = 0;
    float lpL[2] = { 0.f, 0.f }, lpR[2] = { 0.f, 0.f };
    std::array<Voice, maxVoices> voices {};
    std::array<Held, maxHeld> held {};
    std::array<std::atomic<float>, displayVoices> dispPos {};
    std::atomic<int> dispCount { 0 }, startedCount { 0 };
};
} // namespace amb
