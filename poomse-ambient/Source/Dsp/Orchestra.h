#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include "Compose.h"
#include "Samples.h"

// Plays what the composer writes with the four sounds. A held note of the clip starts the piece (its pitch is the key, its length the length of
// the piece); letting go of it ends the piece softly.
namespace amb
{
class Orchestra
{
public:
    static constexpr int maxVoices = 64;

    void prepare (double sampleRate) { sr = sampleRate; for (auto& v : voices) v = Voice{}; held.fill (false); composer.stop(); pendingStop = false; }
    void setBank (int slot, const Bank* b)
    {
        banks[(size_t) slot] = b;
        for (auto& v : voices) if (v.active && kLayerSlot[v.layer] == slot) v = Voice{};
    }
    void killAll() { for (auto& v : voices) v = Voice{}; held.fill (false); composer.stop(); pendingStop = false; }

    void noteOn (int note, uint32_t seed, double beat, const GlobalParams& g)
    {
        const bool firstKey = heldCount() == 0;
        held[(size_t) std::clamp (note, 0, 127)] = true;
        const int tonic = lowestHeld();
        if (firstKey)
        {
            if (composer.running()) composer.setTonic (tonic);                        // a new key right after the old one: the same piece changes key
            else { pieceSeed = seed; composer.start (beat, seed, tonic, g); expectedBeat = beat; }
            pendingStop = false;
        }
        else composer.setTonic (tonic);
    }
    void noteOff (int note, double beat)
    {
        held[(size_t) std::clamp (note, 0, 127)] = false;
        if (heldCount() == 0) { lastOffBeat = beat; pendingStop = true; }
        else composer.setTonic (lowestHeld());
    }
    void allNotesOff() { held.fill (false); pendingStop = true; lastOffBeat = -1e9; }

    // a short example of a sound (the pads of the interface)
    void preview (int slot, int note = 60)
    {
        if (banks[(size_t) slot] == nullptr) return;
        Event e; e.layer = slot == 0 ? Keys : slot == 1 ? Vocals : slot == 2 ? Pad : Field;
        e.note = slot == 3 ? -1 : note; e.velocity = 0.8f; e.beat = 0.0; e.durBeats = 6.f;
        spawn (e, 0, sr * 0.5, true);
    }

    // one block; adds into L/R (dry) and sendL/sendR (what goes to the reverb)
    void process (int n, double beatStart, double bpm, const GlobalParams& g, const LayerParams* layers,
                  float* L, float* R, float* sendL, float* sendR)
    {
        const double spb = sr * 60.0 / std::clamp (bpm, 20.0, 400.0);
        const double beatEnd = beatStart + (double) n / spb;
        // the transport jumped (stopped, started, moved): the piece starts again from here, if a key is still down
        if (composer.running() && std::abs (beatStart - expectedBeat) > 1.0)
        {
            if (heldCount() > 0) composer.start (beatStart, pieceSeed, lowestHeld(), g); else composer.stop();
        }
        expectedBeat = beatEnd;
        if (pendingStop && composer.running() && heldCount() == 0 && (beatEnd - lastOffBeat >= 0.5 || lastOffBeat < -1e8))
        {
            composer.stop();
            releaseAll (2.5f);
            pendingStop = false;
        }
        else if (heldCount() > 0) pendingStop = false;

        Event ev[Composer::maxEvents];
        const int ne = composer.advance (beatStart, beatEnd, g, layers, ev, Composer::maxEvents);
        for (int i = 0; i < ne; ++i)
            spawn (ev[i], (int) std::clamp ((ev[i].beat - beatStart) * spb, 0.0, (double) (n - 1)), spb, false);
        render (n, L, R, sendL, sendR);
    }

    bool pieceRunning() const { return composer.running(); }
    float progress() const { return composer.progress(); }
    float layerActivity (int l) const { return composer.layerActivity (l); }
    int chordRoot() const { return composer.chordRoot(); }
    bool chordMinor() const { return composer.chordMinor(); }
    bool chordDim() const { return composer.chordDim(); }
    int mood() const { return composer.mood(); }
    int activeVoices() const { int c = 0; for (const auto& v : voices) c += v.active; return c; }
    int started() const { return startedCount; }

private:
    struct Voice
    {
        bool active = false, releasing = false;
        int layer = 0, delay = 0, age = 0, attack = 1, gate = 1, release = 1, relAge = 0;
        const Zone* zone = nullptr;
        double pos = 0.0, inc = 1.0;
        float gl = 0.f, gr = 0.f, send = 0.f;
    };

    int heldCount() const { int c = 0; for (bool h : held) c += h; return c; }
    int lowestHeld() const { for (int i = 0; i < 128; ++i) if (held[(size_t) i]) return i; return 48; }
    void releaseAll (float seconds)
    {
        for (auto& v : voices)
            if (v.active && ! v.releasing) { v.releasing = true; v.relAge = 0; v.release = std::max (v.release, (int) (seconds * (float) sr)); }
    }

    void spawn (const Event& e, int offset, double spb, bool preview)
    {
        const int slot = kLayerSlot[e.layer];
        const Bank* bank = banks[(size_t) slot];
        if (bank == nullptr || bank->zones.empty()) return;
        Voice* v = nullptr;
        for (auto& x : voices) if (! x.active) { v = &x; break; }
        if (v == nullptr) { for (auto& x : voices) if (x.releasing) { v = &x; break; } }
        if (v == nullptr) return;
        static const float attackSec[kNumLayers]  = { 0.002f, 0.02f, 2.5f, 0.002f, 4.0f, 0.004f };
        static const float releaseSec[kNumLayers] = { 1.1f, 2.2f, 4.5f, 2.6f, 6.0f, 1.1f };
        static const float gainOf[kNumLayers]     = { 0.9f, 0.75f, 0.5f, 0.55f, 0.7f, 0.9f };
        static const float sendOf[kNumLayers]     = { 0.45f, 0.5f, 0.4f, 0.65f, 0.2f, 0.25f };
        const Zone& z = e.note < 0 || ! bank->pitched ? bank->zones.front() : bank->zoneFor(e.note);
        *v = Voice{};
        v->active = true; v->layer = e.layer; v->zone = &z; v->delay = offset;
        v->attack = std::max (1, (int) (attackSec[e.layer] * (float) sr));
        v->release = std::max (1, (int) (releaseSec[e.layer] * (float) sr));
        v->gate = std::max (v->attack, (int) ((double) e.durBeats * spb));
        const double ratio = (e.note < 0 || ! bank->pitched) ? 1.0 : std::clamp (std::pow (2.0, (double) (e.note - z.root) / 12.0), 0.25, 4.0);
        v->inc = ratio * z.sampleRate / sr;
        v->pos = (e.layer == Field && z.loops()) ? (double) (((uint32_t) (e.beat * 7919.0 + 13.0) * 2654435761u) % (uint32_t) std::max (1, z.length() - 1)) : 0.0;
        const float angle = (std::clamp (e.pan, -1.f, 1.f) + 1.f) * 0.78539816f;
        const float g = e.velocity * gainOf[e.layer];
        v->gl = std::cos (angle) * 1.4142f * g; v->gr = std::sin (angle) * 1.4142f * g;
        v->send = preview ? 0.3f : sendOf[e.layer];
        ++startedCount;
    }

    static void read (const Zone& z, double pos, float& l, float& r)
    {
        const int i1 = (int) pos, n = z.length();
        const float f = (float) (pos - (double) i1);
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
    }

    void render (int n, float* L, float* R, float* sendL, float* sendR)
    {
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const Zone& z = *v.zone;
            for (int i = 0; i < n; ++i)
            {
                if (v.delay > 0) { --v.delay; continue; }
                float env;
                if (! v.releasing && v.age >= v.gate) { v.releasing = true; v.relAge = 0; }
                if (v.releasing)
                {
                    env = std::max (0.f, 1.f - (float) v.relAge / (float) v.release);
                    if (++v.relAge >= v.release) { v = Voice{}; break; }
                    if (v.age < v.attack) env *= (float) v.age / (float) v.attack;
                }
                else env = v.age < v.attack ? (float) v.age / (float) v.attack : 1.f;
                ++v.age;
                if (! z.loops() && v.pos >= (double) (z.length() - 3)) { v = Voice{}; break; }
                float l, r;
                read (z, v.pos, l, r);
                const float ol = l * env * v.gl, orr = r * env * v.gr;
                L[i] += ol; R[i] += orr; sendL[i] += ol * v.send; sendR[i] += orr * v.send;
                v.pos += v.inc;
                if (z.loops() && v.pos >= (double) z.loopEnd) v.pos -= (double) (z.loopEnd - z.loopStart);
            }
        }
    }

    double sr = 48000.0, lastOffBeat = -1e9, expectedBeat = 0.0;
    uint32_t pieceSeed = 1;
    Composer composer;
    std::array<Voice, maxVoices> voices {};
    std::array<bool, 128> held {};
    std::array<const Bank*, 4> banks {};
    bool pendingStop = false;
    int startedCount = 0;
};
} // namespace amb
