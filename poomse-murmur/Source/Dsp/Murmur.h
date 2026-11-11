#pragma once
#include <array>
#include <atomic>
#include <memory>
#include "Flock.h"

// Plays the flock with one sample. A held note of the clip starts the flight (its pitch is the key, its length the length of the piece);
// letting go of it makes the flock fade out.
namespace mur
{
class Murmur
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate; killAll();
        for (auto& a : shownPitch) a.store (-1.f);
    }
    void setZone (const Zone* z) { zone = z; for (auto& v : voices) v = Voice{}; }
    void killAll() { for (auto& v : voices) v = Voice{}; held.fill (false); flock.stop(); master = 0.f; target = 0.f; pending = false; }

    void noteOn (int note, uint32_t seed, double beat, const FlockParams& p)
    {
        const bool first = heldCount() == 0;
        held[(size_t) std::clamp (note, 0, 127)] = true;
        if (first)
        {
            if (! flock.isRunning()) { pieceSeed = seed; flock.start (beat, seed, lowestHeld()); expectedBeat = beat; }
            else flock.setTonic (lowestHeld());
            target = 1.f; pending = false;
        }
        else flock.setTonic (lowestHeld());
        (void) p;
    }
    void noteOff (int note, double beat)
    {
        held[(size_t) std::clamp (note, 0, 127)] = false;
        if (heldCount() == 0) { target = 0.f; pending = true; offBeat = beat; }
        else flock.setTonic (lowestHeld());
    }
    void allNotesOff() { held.fill (false); target = 0.f; pending = true; offBeat = -1e9; }

    // adds into L/R (dry) and sendL/sendR (to the reverb)
    void process (int n, double beatStart, double bpm, const FlockParams& p, float* L, float* R, float* sendL, float* sendR)
    {
        if (zone == nullptr || zone->length() < 16) return;
        const double spb = sr * 60.0 / std::clamp (bpm, 20.0, 400.0);
        const double beatEnd = beatStart + (double) n / spb;
        if (flock.isRunning() && std::abs (beatStart - expectedBeat) > 1.0)           // the transport jumped
        {
            if (heldCount() > 0) flock.start (beatStart, pieceSeed, lowestHeld()); else flock.stop();
        }
        expectedBeat = beatEnd;
        if (pending && master < 0.001f && target == 0.f) { flock.stop(); pending = false; for (auto& v : voices) v = Voice{}; }
        if (! flock.isRunning()) { for (auto& a : shownPitch) a.store (-1.f); return; }

        FlockParams q = p;
        const double rel = (double) zone->sampleRate / sr;
        const float masterK = target > master ? 1.f / (float) (2.0 * sr) : 1.f / (float) (5.0 * sr);
        const int step = 32;
        for (int off = 0; off < n; off += step)
        {
            const int c = std::min (step, n - off);
            const double beat = beatStart + (double) off / spb;
            flock.advance (beat, q);
            float mArr[32];
            for (int i = 0; i < c; ++i) { master += std::clamp (target - master, -masterK, masterK); mArr[i] = master; }
            const float nAlive = (float) std::max (1, flock.aliveCount());
            const float norm = 0.8f / std::sqrt (std::max (1.f, nAlive * 0.7f));
            for (int vi = 0; vi < kMaxVoices; ++vi)
            {
                auto& v = voices[(size_t) vi];
                const auto vw = flock.view (vi, beat, q);
                shownPitch[(size_t) vi].store (vw.alive ? vw.pitch : -1.f); shownLevel[(size_t) vi].store (vw.level);
                if (! vw.alive && v.gain < 1e-5f) { v.active = false; continue; }
                if (! v.active) { v.active = true; v.pos = (double) zone->loopStart + (double) ((mixHash (pieceSeed, (uint32_t) vi, 91u) >> 3) % (uint32_t) std::max (1, zone->loopEnd - zone->loopStart)); v.gain = 0.f; }
                const double ratio = std::clamp (std::pow (2.0, (double) (vw.pitch - (float) zone->root) / 12.0), 0.25, 4.0);
                const double inc = ratio * rel * (vw.reverse ? -1.0 : 1.0);
                const float tg = vw.alive ? vw.level * norm : 0.f;
                const float g0 = v.gain, g1 = tg, th = (0.5f + 0.5f * std::clamp (vw.pan, -1.f, 1.f)) * 1.5707963f;
                const float gl = 1.4142f * std::cos (th), gr = 1.4142f * std::sin (th);
                for (int i = 0; i < c; ++i)
                {
                    const float g = g0 + (g1 - g0) * (float) (i + 1) / (float) c;
                    float l, r; read (*zone, v.pos, l, r);
                    const float m = mArr[i];
                    const float ol = l * g * m * gl, orr = r * g * m * gr;
                    L[off + i] += ol; R[off + i] += orr; sendL[off + i] += ol * 0.5f; sendR[off + i] += orr * 0.5f;
                    v.pos += inc;
                    const double len = (double) (zone->loopEnd - zone->loopStart);
                    if (v.pos >= (double) zone->loopEnd) v.pos -= len; else if (v.pos < (double) zone->loopStart) v.pos += len;
                }
                v.gain = g1;
            }
        }
    }

    bool pieceRunning() const { return flock.isRunning(); }
    float voicePitch (int v) const { return shownPitch[(size_t) v].load(); }
    float voiceLevel (int v) const { return shownLevel[(size_t) v].load(); }
    float progress (double beat, const FlockParams& p) const { return flock.isRunning() ? flock.progress (beat, p) : -1.f; }
    float wind() const { return flock.wind(); }

private:
    struct Voice { bool active = false; double pos = 0.0; float gain = 0.f; };
    int heldCount() const { int c = 0; for (bool h : held) c += h; return c; }
    int lowestHeld() const { for (int i = 0; i < 128; ++i) if (held[(size_t) i]) return i; return 48; }

    static void read (const Zone& z, double pos, float& l, float& r)
    {
        const int i1 = (int) std::floor (pos), n = z.length();
        const float f = (float) (pos - (double) i1);
        const int len = z.loopEnd - z.loopStart;
        auto at = [&] (int ch, int idx) -> float
        {
            while (idx >= z.loopEnd) idx -= len;
            while (idx < z.loopStart && idx >= 0 && false) idx += len;
            if (idx < 0 || idx >= n) return 0.f;
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
    }

    double sr = 48000.0, expectedBeat = 0.0, offBeat = 0.0;
    const Zone* zone = nullptr;
    Flock flock;
    std::array<Voice, kMaxVoices> voices {};
    std::array<bool, 128> held {};
    std::array<std::atomic<float>, kMaxVoices> shownPitch {}, shownLevel {};
    float master = 0.f, target = 0.f;
    bool pending = false;
    uint32_t pieceSeed = 1;
};
} // namespace mur
