#pragma once
#include "Cloud.h"
#include "Fx.h"
#include "Generative.h"
#include "PitchDetect.h"

namespace amb
{
// ---------------------------------------------------------------------------------------------------------------
struct SourceBuffer
{
    std::vector<float> l, r;
    double sampleRate = 48000.0;
    int size() const { return (int) l.size(); }

    // Built-in sounds so every layer plays straight away (until you drop your own samples on the zones).
    static std::shared_ptr<SourceBuffer> makeDefault (int layer, double sr = 48000.0, bool night = true)
    {
        if (! night && layer != Sub) return makeClean (layer, sr);
        auto b = std::make_shared<SourceBuffer>();
        b->sampleRate = sr;
        static const double seconds[kNumLayers] = { 6.0, 5.0, 10.0, 0.7, 12.0, 8.0 };
        const int n = (int) (sr * seconds[layer]);
        b->l.assign ((size_t) n, 0.f); b->r.assign ((size_t) n, 0.f);
        const double twoPi = 2.0 * 3.14159265358979323846;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto& out = ch == 0 ? b->l : b->r;
            gg::Rng rng (1000u + (uint32_t) layer * 7u + (uint32_t) ch);
            const double det = ch == 0 ? 1.0 : 1.0025;     // tiny difference between the channels: a wider image
            double lp = 0.0, lp2 = 0.0, prevNoise = 0.0, rain = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double t = (double) i / sr;
                double v = 0.0;
                switch (layer)
                {
                    case Keys:    // a soft electric piano: a bell-like tine over a mellow body, with a slow tremolo
                    {
                        const double f0 = 261.63 * det, idx = 1.4 * std::exp (-t * 5.0);
                        v = std::sin (twoPi * f0 * t + idx * std::sin (twoPi * 14.0 * f0 * t)) * std::exp (-t * 1.1)
                          + 0.3 * std::sin (twoPi * 2.0 * f0 * t) * std::exp (-t * 2.4);
                        v *= (1.0 + 0.12 * std::sin (twoPi * 4.6 * t)) * std::min (1.0, t / 0.003);
                        break;
                    }
                    case Vocals:  // a ghostly "ooh": low formants, a slow vibrato and some breath
                    {
                        const double f0 = 261.63 * det * (1.0 + 0.004 * std::sin (twoPi * 5.2 * t));
                        for (int k = 1; k <= 24; ++k)
                        {
                            const double fk = f0 * k;
                            const double a = std::exp (-0.5 * std::pow ((fk - 320.0) / 110.0, 2.0)) + 0.5 * std::exp (-0.5 * std::pow ((fk - 850.0) / 160.0, 2.0)) + 0.04 / k;
                            v += a * std::sin (twoPi * fk * t + 0.3 * k);
                        }
                        lp += 0.12 * (rng.bi() - lp); v += 0.07 * lp;
                        v *= std::min (1.0, t / 0.08) * (0.75 + 0.25 * std::sin (twoPi * 0.5 * t));
                        break;
                    }
                    case Pad:     // dark chord-less pad: detuned stacks whose "filter" slowly opens and closes
                    {
                        const double bright = 3.0 + 2.0 * std::sin (twoPi * 0.07 * t + ch);
                        for (double d : { 0.996, 1.0, 1.004 })
                            for (int k = 1; k <= 20; ++k)
                                v += std::sin (twoPi * 261.63 * det * d * k * t) / ((double) k * k * 0.5 + 1.0) * std::exp (-(double) k / bright);
                        break;
                    }
                    case Beat:    // one dusty drum hit: a soft body and a short band of noise, lo-fi
                    {
                        const double noise = rng.bi();
                        const double hp = noise - prevNoise; prevNoise = noise;       // darker noise, no deep rumble
                        lp += 0.5 * (hp - lp);
                        v = 0.8 * std::sin (twoPi * 185.0 * t * (1.0 + 0.6 * std::exp (-t * 30.0))) * std::exp (-t * 22.0)
                          + 0.9 * lp * std::exp (-t * 13.0)
                          + 0.10 * lp * std::exp (-t * 4.0);
                        v *= std::min (1.0, t / 0.0008);
                        break;
                    }
                    case Atmos:   // rain on a window and the odd crackle, slowly breathing
                    {
                        const double noise = rng.bi();
                        rain += 0.6 * ((noise - prevNoise) - rain); prevNoise = noise;  // hissy rain
                        lp += 0.02 * (noise - lp); lp2 += 0.02 * (lp - lp2);             // distant rumble
                        v = 0.55 * rain + 1.4 * lp2;
                        v *= 0.65 + 0.2 * std::sin (twoPi * 0.11 * t + ch) + 0.15 * std::sin (twoPi * 0.37 * t);
                        if (rng.uni() < 0.0009) v += 0.8 * (rng.bi() < 0.f ? -1.0 : 1.0) * rng.uni();   // crackle
                        break;
                    }
                    default:      // sub: a sine with a trace of second harmonic, pushed gently
                    {
                        const double f0 = 65.406 * det;
                        v = std::tanh (1.3 * (std::sin (twoPi * f0 * t) + 0.12 * std::sin (twoPi * 2.0 * f0 * t)));
                        v *= 0.9 + 0.1 * std::sin (twoPi * 0.1 * t + ch);
                        break;
                    }
                }
                out[(size_t) i] = (float) v;
            }
        }
        float peak = 1e-6f;
        for (int i = 0; i < n; ++i) peak = std::max ({ peak, std::abs (b->l[(size_t) i]), std::abs (b->r[(size_t) i]) });
        const float g = (layer == Atmos ? 0.55f : 0.7f) / peak;
        for (int i = 0; i < n; ++i) { b->l[(size_t) i] *= g; b->r[(size_t) i] *= g; }
        return b;
    }

    // Clean ambient sounds (the "Eno" side): a plucked piano, an "ahh" choir, a soft pad, bells and an airy room tone.
    static std::shared_ptr<SourceBuffer> makeClean (int layer, double sr)
    {
        auto b = std::make_shared<SourceBuffer>();
        b->sampleRate = sr;
        static const double seconds[kNumLayers] = { 6.0, 8.0, 10.0, 8.0, 12.0, 8.0 };
        const int n = (int) (sr * seconds[layer]);
        b->l.assign ((size_t) n, 0.f); b->r.assign ((size_t) n, 0.f);
        const double twoPi = 2.0 * 3.14159265358979323846;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& out = ch == 0 ? b->l : b->r;
            gg::Rng rng (2000u + (uint32_t) layer * 7u + (uint32_t) ch);
            const double det = ch == 0 ? 1.0 : 1.0025;
            double lp = 0.0, lp2 = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double t = (double) i / sr;
                double v = 0.0;
                switch (layer)
                {
                    case Keys:     // plucked piano: slightly inharmonic partials that decay
                    {
                        const double f0 = 261.63 * det;
                        for (int k = 1; k <= 10; ++k)
                            v += std::sin (twoPi * f0 * k * std::sqrt (1.0 + 0.0004 * k * k) * t) / std::pow ((double) k, 1.15) * std::exp (-t * (0.7 + 0.45 * k));
                        if (t < 0.006) v += 0.5 * rng.bi() * (1.0 - t / 0.006);
                        v *= std::min (1.0, t / 0.002);
                        break;
                    }
                    case Vocals:   // "ahh": harmonics shaped by two vowel formants, vibrato, breathing
                    {
                        const double f0 = 261.63 * det * (1.0 + 0.004 * std::sin (twoPi * 5.2 * t));
                        for (int k = 1; k <= 24; ++k)
                        {
                            const double fk = f0 * k;
                            const double a = std::exp (-0.5 * std::pow ((fk - 800.0) / 180.0, 2.0)) + 0.6 * std::exp (-0.5 * std::pow ((fk - 1200.0) / 220.0, 2.0)) + 0.05 / k;
                            v += a * std::sin (twoPi * fk * t + 0.3 * k);
                        }
                        v *= 0.6 + 0.4 * std::sin (twoPi * 0.4 * t);
                        lp += 0.15 * (rng.bi() - lp); v += 0.05 * lp;
                        break;
                    }
                    case Pad:      // three detuned stacks of soft harmonics that swell
                    {
                        for (double d : { 0.996, 1.0, 1.004 })
                            for (int k = 1; k <= 20; ++k) v += std::sin (twoPi * 261.63 * det * d * k * t) / std::pow ((double) k, 1.7);
                        v *= 0.7 + 0.3 * std::sin (twoPi * 0.12 * t + ch);
                        break;
                    }
                    case Beat:     // bells: inharmonic partials with long decays
                    {
                        static const double ratio[] = { 1.0, 2.01, 2.76, 4.07, 5.4, 6.9 }, amp[] = { 1.0, 0.6, 0.5, 0.35, 0.25, 0.15 };
                        for (int k = 0; k < 6; ++k) v += amp[k] * std::sin (twoPi * 523.25 * det * ratio[k] * t) * std::exp (-t * (0.45 + 0.35 * k));
                        if (t < 0.004) v += 0.4 * rng.bi() * (1.0 - t / 0.004);
                        break;
                    }
                    default:       // atmos: air / room tone, slowly breathing
                    {
                        lp += 0.08 * (rng.bi() - lp); lp2 += 0.01 * (lp - lp2);
                        v = 2.2 * lp2 + 0.6 * lp;
                        v *= 0.6 + 0.25 * std::sin (twoPi * 0.17 * t + ch) + 0.15 * std::sin (twoPi * 0.41 * t);
                        break;
                    }
                }
                out[(size_t) i] = (float) v;
            }
        }
        float peak = 1e-6f;
        for (int i = 0; i < n; ++i) peak = std::max ({ peak, std::abs (b->l[(size_t) i]), std::abs (b->r[(size_t) i]) });
        const float g = (layer == Atmos ? 0.5f : 0.7f) / peak;
        for (int i = 0; i < n; ++i) { b->l[(size_t) i] *= g; b->r[(size_t) i] *= g; }
        return b;
    }
};

// ---------------------------------------------------------------------------------------------------------------
class Engine
{
public:
    static constexpr int maxVoices = 72;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& v : voices) v = Voice{};
        held.fill (false);
        conductor.stop();
        wasRunning = false;
    }

    // the sources of the six layers; they must stay valid for the whole block
    void setSources (const SourceBuffer* const* s) { for (int l = 0; l < kNumLayers; ++l) sources[(size_t) l] = s[l]; }
    void killLayer (int l) { for (auto& v : voices) if (v.layer == l) v = Voice{}; }

    void noteOn (int note, uint32_t seed, double beat, const GlobalParams& g, const LayerParams* layers)
    {
        const bool firstKey = heldCount() == 0;
        held[(size_t) std::clamp (note, 0, 127)] = true;
        const int tonic = lowestHeld();
        if (firstKey)
        {
            // a key pressed right after the previous one ended is the same piece changing key
            if (conductor.running() || ((beat - lastOffBeat) < 0.5 && fadingPiece))
                conductor.setTonic (tonic, heldClasses());
            else
            {
                conductor.start (beat, seed, tonic, heldClasses(), g, layers);
                pieceSeed = seed;
            }
            fadingPiece = false;
        }
        else
            conductor.setTonic (tonic, heldClasses());
    }

    void noteOff (int note, double beat)
    {
        held[(size_t) std::clamp (note, 0, 127)] = false;
        if (heldCount() == 0) { lastOffBeat = beat; pendingStop = true; stopBeat = beat; }
        else conductor.setTonic (lowestHeld(), heldClasses());
    }

    void allNotesOff() { held.fill (false); pendingStop = true; stopBeat = -1e9; }

    // one block; adds into L/R (dry) and sendL/sendR (what goes to the reverb)
    void process (int n, double beatStart, double bpm, const GlobalParams& g, const LayerParams* layers,
                  float* L, float* R, float* sendL, float* sendR, float* cloudL, float* cloudR)
    {
        const double samplesPerBeat = sr * 60.0 / std::max (20.0, bpm);
        const double beatEnd = beatStart + (double) n / samplesPerBeat;

        if (pendingStop && conductor.running() && stopBeat < beatEnd)
        {
            // key released, but a quick re-press (a new note of the same clip) keeps the piece going
            if (heldCount() == 0)
            {
                // wait half a beat for a possible next note
                if (beatEnd - lastOffBeat >= 0.5 || stopBeat < -1e8)
                {
                    conductor.stop();
                    releaseAll (2.5f);
                    pendingStop = false;
                    fadingPiece = false;
                }
                else fadingPiece = true;
            }
            else pendingStop = false;
        }
        else if (heldCount() > 0) pendingStop = false;

        Event ev[Conductor::maxEvents];
        const int ne = conductor.advance (beatStart, beatEnd, g, layers, ev, Conductor::maxEvents);
        for (int i = 0; i < ne; ++i)
            spawn (ev[i], (int) std::clamp ((ev[i].beat - beatStart) * samplesPerBeat, 0.0, (double) (n - 1)), samplesPerBeat, layers[ev[i].layer], g.night);

        render (n, L, R, sendL, sendR, cloudL, cloudR);
    }

    bool pieceRunning() const { return conductor.running(); }
    float progress() const { return conductor.progress(); }
    float layerActivity (int l) const { return conductor.layerActivity (l); }
    int activeVoices() const { int c = 0; for (const auto& v : voices) c += v.active; return c; }
    int tonic() const { return conductor.tonicNote(); }
    int mood() const { return conductor.mood(); }
    int numScenes() const { return conductor.numScenes(); }
    const Conductor::Scene& scene (int i) const { return conductor.scene (i); }
    double pieceStartBeat() const { return conductor.startBeatOfPiece(); }
    uint32_t seedInUse() const { return pieceSeed; }

private:
    struct Voice
    {
        bool active = false, loop = false;
        int layer = 0, delay = 0, age = 0, attack = 1, gate = 1, release = 1, relAge = 0;
        const SourceBuffer* src = nullptr;
        double pos = 0.0, inc = 1.0;
        float gl = 0.f, gr = 0.f, sendAmount = 0.f, cloudAmount = 0.f;
        bool releasing = false;
    };

    int heldCount() const { int c = 0; for (bool h : held) c += h; return c; }
    int lowestHeld() const { for (int i = 0; i < 128; ++i) if (held[(size_t) i]) return i; return 48; }
    uint32_t heldClasses() const { uint32_t m = 0; for (int i = 0; i < 128; ++i) if (held[(size_t) i]) m |= 1u << (unsigned) (i % 12); return m; }

    void releaseAll (float seconds)
    {
        for (auto& v : voices)
            if (v.active && ! v.releasing) { v.releasing = true; v.relAge = 0; v.release = std::max (v.release, (int) (seconds * (float) sr)); }
    }

    void spawn (const Event& e, int offset, double samplesPerBeat, const LayerParams& lp, bool night)
    {
        const auto* src = sources[(size_t) e.layer];
        if (src == nullptr || src->size() < 8) return;
        Voice* v = nullptr;
        for (auto& x : voices) if (! x.active) { v = &x; break; }
        if (v == nullptr) { for (auto& x : voices) if (x.releasing) { v = &x; break; } }   // steal one that is already fading
        if (v == nullptr) return;

        static const float attackSec[kNumLayers]  = { 0.004f, 0.02f, 4.0f, 0.001f, 4.0f, 0.05f };
        static const float releaseSec[kNumLayers] = { 1.6f, 2.5f, 6.0f, 0.35f, 6.0f, 1.2f };
        static const bool loops[kNumLayers]       = { false, false, true, false, true, true };

        *v = Voice{};
        v->active = true; v->layer = e.layer; v->src = src; v->delay = offset;
        v->loop = loops[e.layer];
        const bool bells = e.layer == Beat && ! night;      // the beat slot is bells in Clean style: a long ring
        v->attack = std::max (1, (int) ((bells ? 0.003f : attackSec[e.layer]) * (float) sr));
        v->release = std::max (1, (int) ((bells ? 3.2f : releaseSec[e.layer]) * (float) sr));
        v->gate = std::max (v->attack, (int) ((double) e.durBeats * samplesPerBeat));
        const double ratio = e.note < 0 ? 1.0 : std::exp2 ((double) (e.note - lp.root) / 12.0);
        v->inc = ratio * src->sampleRate / sr;
        v->pos = e.layer == Atmos ? (double) (((uint32_t) (e.beat * 7919.0) * 2654435761u) % (uint32_t) src->size()) : 0.0;
        const float angle = (std::clamp (e.pan, -1.f, 1.f) + 1.f) * kPi * 0.25f;
        const float g = e.velocity * lp.level;
        v->gl = std::cos (angle) * 1.4142f * g;
        v->gr = std::sin (angle) * 1.4142f * g;
        v->sendAmount = lp.space;
        v->cloudAmount = layerConfig (e.layer, night).cloud;
    }

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

    void render (int n, float* L, float* R, float* sendL, float* sendR, float* cloudL, float* cloudR)
    {
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const auto& src = *v.src;
            const int len = src.size();
            const int fade = std::min (len / 4, (int) (0.03 * src.sampleRate));
            const int loopEnd = len - fade;
            for (int i = 0; i < n; ++i)
            {
                if (v.delay > 0) { --v.delay; continue; }

                // envelope: attack, gate, release
                float env;
                if (! v.releasing && v.age >= v.gate) { v.releasing = true; v.relAge = 0; }
                if (v.releasing)
                {
                    env = 1.f - (float) v.relAge / (float) v.release;
                    if (++v.relAge >= v.release) { v = Voice{}; break; }
                    env = std::max (0.f, env);
                    if (v.age < v.attack) env *= (float) v.age / (float) v.attack;
                }
                else env = v.age < v.attack ? (float) v.age / (float) v.attack : 1.f;
                ++v.age;

                float l, r;
                if (! v.loop && v.pos >= (double) (len - 1)) { v.releasing = true; l = r = 0.f; }
                else if (v.loop && fade > 1 && v.pos >= (double) loopEnd)
                {
                    const float t = (float) ((v.pos - (double) loopEnd) / (double) fade);
                    float al, ar, bl, br;
                    read (src, v.pos, al, ar);
                    read (src, v.pos - (double) loopEnd, bl, br);
                    l = al * (1.f - t) + bl * t; r = ar * (1.f - t) + br * t;
                }
                else read (src, v.pos, l, r);

                const float ol = l * env * v.gl, orr = r * env * v.gr;
                L[i] += ol; R[i] += orr;
                sendL[i] += ol * v.sendAmount; sendR[i] += orr * v.sendAmount;
                cloudL[i] += ol * v.cloudAmount; cloudR[i] += orr * v.cloudAmount;

                v.pos += v.inc;
                if (v.loop) { while (v.pos >= (double) len) v.pos -= (double) loopEnd; }
            }
        }
    }

    double sr = 48000.0;
    Conductor conductor;
    std::array<Voice, maxVoices> voices;
    std::array<bool, 128> held {};
    std::array<const SourceBuffer*, kNumLayers> sources {};
    bool pendingStop = false, fadingPiece = false, wasRunning = false;
    double lastOffBeat = -1e9, stopBeat = 0.0;
    uint32_t pieceSeed = 0;
};
} // namespace amb
