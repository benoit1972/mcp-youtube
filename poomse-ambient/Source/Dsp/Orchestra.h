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
// How the sounds are worked: each of the four sounds can be played as a cloud of small grains (so a held note is never the same twice), and a slow
// LFO, synchronised to the tempo, moves the grain size, the grain mix, the brightness and the stereo position of each sound, each one at its own phase.
struct Texture
{
    float grain[4] { 0.f, 0.f, 0.f, 0.f };    // how much of each sound is played as grains (0 = the sound as it is)
    float size = 0.4f;                        // grain length, 30 ms .. 400 ms
    float scatter = 0.3f;                     // how far the grains wander in time and in pitch
    int lfoBars = 4;                          // length of one turn of the LFO, in bars
    float lfoDepth = 0.f;                     // how much the LFO moves things (0 = nothing moves)
};

class Orchestra
{
public:
    static constexpr int maxBlock = 512;
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
    void process (int n, double beatStart, double bpm, const GlobalParams& g, const LayerParams* layers, const Texture& tex,
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

        const double lfoBeats = std::max (1, tex.lfoBars) * (double) std::max (1, g.beatsPerBar);
        const double lfoInc = 1.0 / (lfoBeats * spb);
        for (int off = 0; off < n; off += maxBlock)
        {
            const int c = std::min (maxBlock, n - off);
            for (int k = 0; k < 4; ++k) { std::fill (busL[(size_t) k].begin(), busL[(size_t) k].begin() + c, 0.f); std::fill (busR[(size_t) k].begin(), busR[(size_t) k].begin() + c, 0.f);
                                           std::fill (busSL[(size_t) k].begin(), busSL[(size_t) k].begin() + c, 0.f); std::fill (busSR[(size_t) k].begin(), busSR[(size_t) k].begin() + c, 0.f); }
            // what the LFO does to each sound for this part of the block
            const double phase0 = std::fmod ((beatStart + (double) off / spb) / lfoBeats, 1.0);
            const float depth = std::clamp (tex.lfoDepth, 0.f, 1.f);
            for (int k = 0; k < 4; ++k)
            {
                const double ph = phase0 + 0.25 * k;
                const float u = 0.5f - 0.5f * (float) std::cos (6.2831853 * ph), sv = (float) std::sin (6.2831853 * ph + 0.9);
                slotGrain[(size_t) k] = std::clamp (tex.grain[k] * (1.f + 0.7f * depth * sv), 0.f, 1.f);
                slotSize[(size_t) k] = std::clamp (tex.size * std::exp2 (1.2f * depth * sv), 0.f, 1.f);
                slotCut[(size_t) k] = 16000.f * std::pow (0.1f, depth * u);
                const float th = (0.5f + 0.35f * depth * (float) std::sin (6.2831853 * ph * 0.5 + (double) k)) * 1.5707963f;
                slotGl[(size_t) k] = 1.4142f * std::cos (th); slotGr[(size_t) k] = 1.4142f * std::sin (th);
            }
            render (c, tex);
            for (int k = 0; k < 4; ++k)
            {
                const float lk = 1.f - std::exp (-6.2831853f * std::min (slotCut[(size_t) k], 0.45f * (float) sr) / (float) sr);
                auto& f = fx[(size_t) k];
                for (int i = 0; i < c; ++i)
                {
                    float sig[4] = { busL[(size_t) k][(size_t) i], busR[(size_t) k][(size_t) i], busSL[(size_t) k][(size_t) i], busSR[(size_t) k][(size_t) i] };
                    for (int q = 0; q < 4; ++q) { f.a[q] += lk * (sig[q] - f.a[q]); f.b[q] += lk * (f.a[q] - f.b[q]); sig[q] = f.b[q]; }
                    L[off + i] += sig[0] * slotGl[(size_t) k]; R[off + i] += sig[1] * slotGr[(size_t) k];
                    sendL[off + i] += sig[2] * slotGl[(size_t) k]; sendR[off + i] += sig[3] * slotGr[(size_t) k];
                }
            }
        }
        (void) lfoInc;
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
        struct Grain { bool on = false; double pos = 0.0, inc = 1.0; int age = 0, len = 1; };
        Grain grains[4];
        int nextGrain = 0;
        uint32_t rng = 1;
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
        v->rng = 0x9E3779B1u * (uint32_t) (startedCount + 1) + 17u;
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

    static float rnd (uint32_t& st) { st ^= st << 13; st ^= st >> 17; st ^= st << 5; return (float) (st >> 8) * (2.0f / 16777216.0f) - 1.0f; }       // [-1, 1)

    void render (int n, const Texture& tex)
    {
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const Zone& z = *v.zone;
            const int slot = kLayerSlot[v.layer];
            auto& oL = busL[(size_t) slot]; auto& oR = busR[(size_t) slot]; auto& oSL = busSL[(size_t) slot]; auto& oSR = busSR[(size_t) slot];
            const float mix = slotGrain[(size_t) slot];
            const int glen = std::max (64, (int) ((0.03f + 0.37f * slotSize[(size_t) slot]) * (float) sr));
            const float scatter = std::clamp (tex.scatter, 0.f, 1.f);
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
                if (mix > 0.002f)
                {
                    // a cloud of grains around the place where the note is: each one a little earlier or later and a little out of tune
                    if (--v.nextGrain <= 0)
                    {
                        v.nextGrain = std::max (1, glen / 4);
                        for (auto& gr : v.grains)
                            if (! gr.on)
                            {
                                gr.on = true; gr.age = 0; gr.len = glen;
                                gr.pos = v.pos + (double) rnd (v.rng) * scatter * 0.25 * z.sampleRate;
                                if (gr.pos < 0.0) gr.pos = z.loops() ? gr.pos + (double) (z.loopEnd - z.loopStart) : 0.0;
                                gr.inc = v.inc * std::exp2 ((double) rnd (v.rng) * scatter * 0.35 / 12.0);
                                break;
                            }
                    }
                    float gl = 0.f, grr = 0.f;
                    for (auto& gr : v.grains)
                    {
                        if (! gr.on) continue;
                        float a, b; read (z, gr.pos, a, b);
                        const float w = 0.5f - 0.5f * std::cos (6.2831853f * (float) gr.age / (float) gr.len);
                        gl += a * w; grr += b * w;
                        gr.pos += gr.inc;
                        if (z.loops() && gr.pos >= (double) z.loopEnd) gr.pos -= (double) (z.loopEnd - z.loopStart);
                        if (++gr.age >= gr.len || (! z.loops() && gr.pos >= (double) (z.length() - 3))) gr.on = false;
                    }
                    l = l * (1.f - mix) + 0.5f * gl * mix; r = r * (1.f - mix) + 0.5f * grr * mix;
                }
                const float ol = l * env * v.gl, orr = r * env * v.gr;
                oL[(size_t) i] += ol; oR[(size_t) i] += orr; oSL[(size_t) i] += ol * v.send; oSR[(size_t) i] += orr * v.send;
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
    struct SlotFx { float a[4] { 0.f, 0.f, 0.f, 0.f }, b[4] { 0.f, 0.f, 0.f, 0.f }; };
    std::array<std::array<float, maxBlock>, 4> busL {}, busR {}, busSL {}, busSR {};
    std::array<float, 4> slotGrain {}, slotSize {}, slotCut { 16000.f, 16000.f, 16000.f, 16000.f }, slotGl { 1.f, 1.f, 1.f, 1.f }, slotGr { 1.f, 1.f, 1.f, 1.f };
    std::array<SlotFx, 4> fx {};
};
} // namespace amb
