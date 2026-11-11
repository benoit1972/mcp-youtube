#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// The composer. A held note of the clip is the key; its length is the length of the piece. From it the plugin writes a piece that is
// meant to be listened to like music, not like a texture:
//   - a chord progression (four chords, two bars each, in the mode you chose), which changes every two cycles;
//   - a pad that plays the root of each chord, a bass that follows it, a violin that holds a note of the chord and moves by small steps;
//   - a piano motif of four to six notes that comes back with every chord (it is written in the chord, so it follows the harmony) and
//     changes a little each time round; its echo is rung by bells three beats later, so the two drift against the bar like two tape loops;
//   - scenes (light, deep, still) and one long arc over the whole piece; a place (field) underneath all of it.
// Everything sits on the eighth-note grid. Every note is chosen among the notes that do not rub against the chord (no semitone with its root,
// third or fifth), so what sounds together is always consonant. Nothing is random at run time: the seed writes the piece.
namespace amb
{
constexpr int kNumLayers = 6;
enum LayerId { Keys = 0, Vocals, Pad, Bells, Field, Bass };
inline constexpr const char* kLayerNames[kNumLayers] = { "Keys", "Violin", "Pad", "Bells", "Field", "Bass" };
inline constexpr int kLayerSlot[kNumLayers] = { 0, 1, 2, 0, 3, 0 };           // the sound (piano, violin, pad, forest) that plays each layer

inline uint32_t mixHash (uint32_t a, uint32_t b, uint32_t c, uint32_t d = 0)
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu ^ (c + 0x165667B1u) * 0xC2B2AE35u ^ (d + 0x27D4EB2Fu) * 0x9E3779B1u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
inline float unitOf (uint32_t h) { return (float) (h >> 8) * (1.0f / 16777216.0f); }

struct Event
{
    int layer = 0;
    int note = 60;          // MIDI note, -1 for an unpitched event (the field)
    float velocity = 0.6f;
    float pan = 0.f;        // -1..1
    double beat = 0.0;
    float durBeats = 4.f;
};

struct GlobalParams
{
    int scale = 1;             // index into kScales
    int key = -1;              // pitch class of the key (0 = C), or -1 = the pitch of the held note
    float journey = 0.7f;      // 0 = stationary, 1 = full arc (builds up, peaks, fades)
    float silence = 0.5f;      // 0 = busy, 1 = lots of silence
    float depth = 0.5f;        // 0 = mostly light moments, 1 = mostly deep moments
    float surprise = 0.3f;     // chance and size of the unexpected
    int pieceBars = 120;
    int beatsPerBar = 4;
};
struct LayerParams { float amount = 0.7f; };      // how much of this layer there is (0 = it does not play)

// The scales of Ableton Live (same names). The plugin cannot read the scale that is set in the clip, so choose the same one here.
struct ScaleDef { const char* name; int n; int iv[8]; };
inline constexpr ScaleDef kScales[] = {
    { "Major", 7, { 0, 2, 4, 5, 7, 9, 11, 0 } },          { "Minor", 7, { 0, 2, 3, 5, 7, 8, 10, 0 } },
    { "Dorian", 7, { 0, 2, 3, 5, 7, 9, 10, 0 } },         { "Mixolydian", 7, { 0, 2, 4, 5, 7, 9, 10, 0 } },
    { "Lydian", 7, { 0, 2, 4, 6, 7, 9, 11, 0 } },         { "Phrygian", 7, { 0, 1, 3, 5, 7, 8, 10, 0 } },
    { "Locrian", 7, { 0, 1, 3, 5, 6, 8, 10, 0 } },        { "Whole Tone", 6, { 0, 2, 4, 6, 8, 10, 0, 0 } },
    { "Half-whole Dim.", 8, { 0, 1, 3, 4, 6, 7, 9, 10 } }, { "Whole-half Dim.", 8, { 0, 2, 3, 5, 6, 8, 9, 11 } },
    { "Minor Blues", 6, { 0, 3, 5, 6, 7, 10, 0, 0 } },    { "Minor Pentatonic", 5, { 0, 3, 5, 7, 10, 0, 0, 0 } },
    { "Major Pentatonic", 5, { 0, 2, 4, 7, 9, 0, 0, 0 } }, { "Harmonic Minor", 7, { 0, 2, 3, 5, 7, 8, 11, 0 } },
    { "Harmonic Major", 7, { 0, 2, 4, 5, 7, 8, 11, 0 } }, { "Dorian #4", 7, { 0, 2, 3, 6, 7, 9, 10, 0 } },
    { "Phrygian Dominant", 7, { 0, 1, 4, 5, 7, 8, 10, 0 } }, { "Melodic Minor", 7, { 0, 2, 3, 5, 7, 9, 11, 0 } },
    { "Lydian Augmented", 7, { 0, 2, 4, 6, 8, 9, 11, 0 } }, { "Lydian Dominant", 7, { 0, 2, 4, 6, 7, 9, 10, 0 } },
    { "Super Locrian", 7, { 0, 1, 3, 4, 6, 8, 10, 0 } },  { "8-Tone Spanish", 8, { 0, 1, 3, 4, 5, 6, 8, 10 } },
    { "Bhairav", 7, { 0, 1, 4, 5, 7, 8, 11, 0 } },        { "Hungarian Minor", 7, { 0, 2, 3, 6, 7, 8, 11, 0 } },
    { "Hirajoshi", 5, { 0, 2, 3, 7, 8, 0, 0, 0 } },       { "In-Sen", 5, { 0, 1, 5, 7, 10, 0, 0, 0 } },
    { "Iwato", 5, { 0, 1, 5, 6, 10, 0, 0, 0 } },          { "Kumoi", 5, { 0, 2, 3, 7, 9, 0, 0, 0 } },
    { "Pelog Selisir", 5, { 0, 1, 3, 7, 8, 0, 0, 0 } },   { "Pelog Tembung", 5, { 0, 1, 5, 7, 8, 0, 0, 0 } },
};
inline constexpr int kNumScales = (int) (sizeof (kScales) / sizeof (kScales[0]));
inline constexpr const char* kKeyNames[13] = { "From the note", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

enum Mood { Light = 0, Deep = 1, Still = 2 };
inline constexpr float kMoodGain[3][kNumLayers] = {      // by layer: keys, violin, pad, bells, field, bass
    { 1.20f, 0.55f, 0.85f, 1.00f, 1.00f, 0.45f },       // light: the motif and the bells
    { 0.70f, 1.10f, 1.15f, 0.40f, 1.00f, 1.20f },       // deep: violin, bass and pad
    { 0.20f, 0.12f, 0.90f, 0.10f, 1.00f, 0.25f },       // still: a pause, the pad and the place
};

inline float smooth (float x) { x = std::clamp (x, 0.f, 1.f); return x * x * (3.f - 2.f * x); }
inline float arcShape (float p)
{
    p = std::clamp (p, 0.f, 1.f);
    return p < 0.5f ? 0.2f + 0.8f * smooth (p / 0.5f) : 1.f - 0.75f * smooth ((p - 0.5f) / 0.5f);
}
inline float arcDensity (float p, float amount) { return 1.f + std::clamp (amount, 0.f, 1.f) * (arcShape (p) - 1.f); }
inline float layerEntry (int layer, float p, float amount)      // when each layer is allowed to be present (progress range), soft edges
{
    static const float enter[kNumLayers] = { 0.03f, 0.15f, 0.00f, 0.25f, 0.00f, 0.10f };
    static const float exit_[kNumLayers] = { 0.97f, 0.90f, 1.00f, 0.88f, 1.00f, 0.95f };
    const float e = smooth ((p - enter[layer]) / 0.08f) * (1.f - smooth ((p - exit_[layer] + 0.1f) / 0.1f));
    return 1.f + std::clamp (amount, 0.f, 1.f) * (std::clamp (e, 0.f, 1.f) - 1.f);
}

struct ChordInfo
{
    int degree = 0, rootPc = 0, thirdPc = 4, fifthPc = 7;      // pitch classes above the tonic
    bool minor = false, dim = false;
    uint16_t allowed = 0;                                       // pitch classes (above the tonic) that do not rub against the triad
    bool padOk = true;                                          // the open fifth of the pad fits this chord (else the pad stays silent for it)
};

class Composer
{
public:
    static constexpr int maxEvents = 64;
    static constexpr double step = 0.5;                         // an eighth note, in beats
    struct Scene { double startBeat = 0.0; int mood = Light; };

    bool running() const { return active; }
    float progress() const { return active ? lastProgress : -1.f; }
    int tonicNote() const { return tEff; }
    int mood() const { return curMood; }
    int chordRoot() const { return (tEff + curChord.rootPc) % 12; }
    bool chordMinor() const { return curChord.minor; }
    bool chordDim() const { return curChord.dim; }
    double startBeatOfPiece() const { return origin; }
    float layerActivity (int l) const { return activity[(size_t) l]; }
    int numScenes() const { return (int) moods.size(); }

    void start (double startBeat, uint32_t seed_, int tonicNote, const GlobalParams&)
    {
        active = true; origin = std::floor (startBeat / step + 0.5) * step;
        seed = seed_ * 2654435761u + 12345u;
        tonic = tonicNote; nextStep = 0; pauseUntil = -1e9; violinNote = -1; nextField = origin; lastProgress = 0.f; curMood = Light;
        moods.clear(); activity.fill (0.f);
    }
    void stop() { active = false; }
    void setTonic (int tonicNote) { tonic = tonicNote; }

    // Is the chord on this degree one the pad can sit under? Root, third, fifth and the open fifth of the pad (root + 7) never a semitone apart.
    static bool chordUsable (const ScaleDef& sd, int d, int* pcs = nullptr, bool withPad = true)
    {
        auto pc = [&] (int k) { return (sd.iv[k % sd.n] + 12 * (k / sd.n)) % 12; };
        const int r = pc (d), t = pc (d + 2), f = pc (d + 4), o = withPad ? (r + 7) % 12 : r, set[4] = { r, t, f, o };
        for (int a = 0; a < 4; ++a) for (int b = a + 1; b < 4; ++b) { const int diff = std::abs (set[a] - set[b]) % 12; if (diff == 1 || diff == 11) return false; }
        if (pcs) { pcs[0] = r; pcs[1] = t; pcs[2] = f; }
        return true;
    }

    // the chord of chord number c (0 = the first). The progression has four chords and is rewritten every eight chords, from the scale.
    ChordInfo chordInfo (long c, const GlobalParams& g) const
    {
        const ScaleDef& sd = kScales[std::clamp (g.scale, 0, kNumScales - 1)];
        auto pc = [&] (int k) { return (sd.iv[k % sd.n] + 12 * (k / sd.n)) % 12; };
        int usable[8]; int nu = 0;
        for (int d = 0; d < sd.n; ++d) if (chordUsable (sd, d)) usable[nu++] = d;
        if (nu < 3) { nu = 0; for (int d = 0; d < sd.n; ++d) if (chordUsable (sd, d, nullptr, false)) usable[nu++] = d; }      // too few chords fit the pad: more chords, and the pad rests on the others
        int deg = 0;
        if (nu > 0)
        {
            const uint32_t grp = (uint32_t) (c / 8);
            int prev = -1;
            deg = usable[0];
            for (int k = 0; k <= (int) (((c % 4) + 4) % 4); ++k)
            {
                if (k == 0) { deg = (nu > 1 && grp > 0 && unitOf (mixHash (seed, grp, 71u)) < 0.35f) ? usable[(int) (mixHash (seed, grp, 72u) % (uint32_t) nu)] : usable[0]; prev = deg; continue; }
                float w[8]; float total = 0.f;
                for (int u = 0; u < nu; ++u)
                {
                    const int m = (pc (usable[u]) - pc (prev) + 12) % 12;
                    float x = 0.f;
                    if (usable[u] != prev && m != 1 && m != 11)
                        x = (m == 5 || m == 7) ? 3.f : (m == 3 || m == 4 || m == 8 || m == 9) ? 2.f : m == 2 || m == 10 ? 1.f : 0.2f;
                    if (k == 3 && usable[u] == usable[0]) x *= 0.5f;           // the last chord leads back rather than arrives
                    w[u] = x; total += x;
                }
                if (total <= 0.f) { deg = prev; continue; }
                float r = unitOf (mixHash (seed, grp, 73u, (uint32_t) k)) * total; int pick = nu - 1;
                for (int u = 0; u < nu; ++u) { r -= w[u]; if (r <= 0.f && w[u] > 0.f) { pick = u; break; } }
                deg = usable[pick]; prev = deg;
            }
        }
        ChordInfo ci; ci.degree = deg; ci.rootPc = pc (deg); ci.thirdPc = pc (deg + 2); ci.fifthPc = pc (deg + 4);
        const int third = (ci.thirdPc - ci.rootPc + 12) % 12, fifth = (ci.fifthPc - ci.rootPc + 12) % 12;
        ci.minor = third == 3; ci.dim = third != 3 && third != 4; (void) fifth;                 // dim: here, a chord that is neither major nor minor (sus, open...)
        ci.padOk = chordUsable (sd, deg);
        const int avoid[4] = { ci.rootPc, ci.thirdPc, ci.fifthPc, ci.padOk ? (ci.rootPc + 7) % 12 : ci.rootPc };
        for (int i = 0; i < sd.n; ++i)
        {
            bool ok = true;
            for (int ct : avoid) { const int d = std::abs (sd.iv[i] - ct) % 12; if (d == 1 || d == 11) ok = false; }
            if (ok) ci.allowed |= (uint16_t) (1u << sd.iv[i]);
        }
        auto isChordTone = [&] (int pcx) { return pcx == ci.rootPc || pcx == ci.thirdPc || pcx == ci.fifthPc; };
        for (int pcx = 0; pcx < 12; ++pcx)
        {
            const int up = (pcx + 1) % 12;
            if ((ci.allowed & (1u << pcx)) && (ci.allowed & (1u << up)))
            {
                if (isChordTone (up) && ! isChordTone (pcx)) ci.allowed &= (uint16_t) ~(1u << pcx);
                else ci.allowed &= (uint16_t) ~(1u << up);
            }
        }
        for (int ct : { ci.rootPc, ci.thirdPc, ci.fifthPc }) ci.allowed |= (uint16_t) (1u << ct);        // a chord tone is always allowed
        return ci;
    }

    int advance (double b0, double b1, const GlobalParams& g, const LayerParams* layers, Event* out, int maxOut)
    {
        if (! active) return 0;
        tEff = g.key < 0 ? tonic : tonic - (((tonic - g.key) % 12) + 12) % 12;
        int count = 0;
        for (auto& a : activity) a *= 0.9995f;
        const long s0 = std::max (nextStep, (long) std::ceil ((b0 - origin) / step - 1e-9)), s1 = (long) std::ceil ((b1 - origin) / step - 1e-9);
        for (long s = s0; s < s1 && count < maxOut - 12; ++s) onStep (s, g, layers, out, count, maxOut);
        nextStep = std::max (nextStep, s1);
        return count;
    }

    // ----- the structure, as pure functions of the time since the start -----
    double chordBeats (const GlobalParams& g) const { return 2.0 * g.beatsPerBar; }
    double sceneBeats (const GlobalParams& g) const { return 8.0 * g.beatsPerBar; }
    float progressAt (double rel, const GlobalParams& g) const { return (float) (rel / std::max (1.0, (double) g.pieceBars * g.beatsPerBar)); }

    int moodAtScene (long idx, float p, const GlobalParams& g)
    {
        if (p > 0.94f) return Still;
        if (idx <= 0) return Light;
        while ((long) moods.size() <= idx)
        {
            const long i = (long) moods.size();
            if (i == 0) { moods.push_back (Light); continue; }
            const int prev = moods[(size_t) (i - 1)];
            float w[3] = { 0.7f - 0.55f * g.depth, 0.12f + 0.75f * g.depth, 0.10f + 0.28f * g.silence };
            w[prev] *= 0.35f;
            float r = unitOf (mixHash (seed, (uint32_t) i, 31u)) * (w[0] + w[1] + w[2]); int m = Light;
            for (int k = 0; k < 3; ++k) { r -= w[k]; if (r <= 0.f) { m = k; break; } }
            moods.push_back (m);
        }
        return moods[(size_t) idx];
    }

    float layerProb (int layer, double rel, const GlobalParams& g, const LayerParams& lp, float base)
    {
        const float p = progressAt (rel, g);
        const int mood = moodAtScene ((long) std::floor (rel / sceneBeats (g)), p, g);
        const float silence = 1.15f - 0.9f * std::clamp (g.silence, 0.f, 1.f);
        const float v = base * lp.amount * silence * arcDensity (p, g.journey) * layerEntry (layer, p, g.journey) * kMoodGain[mood][layer];
        return std::clamp (v, 0.f, 1.f);
    }

private:
    struct Motif { int n = 0; int step_[6] {}; int idx[6] {}; float vel[6] {}; };

    Motif motifOf (long c) const
    {
        const uint32_t grp = (uint32_t) (c / 8);
        Motif m; m.n = 4 + (int) (mixHash (seed, grp, 41u) % 3u);
        static const int pool[11] = { 0, 2, 3, 4, 6, 7, 8, 10, 11, 12, 14 };
        int chosen[11]; int pn = 0; bool used[11] = {};
        if (unitOf (mixHash (seed, grp, 42u)) < 0.7f) { chosen[pn++] = 0; used[0] = true; }
        for (uint32_t k = 0; pn < m.n && k < 60; ++k)
        {
            const int j = (int) (mixHash (seed, grp, 43u, k) % 11u);
            if (! used[j]) { used[j] = true; chosen[pn++] = j; }
        }
        std::sort (chosen, chosen + pn, [&] (int a, int b) { return pool[a] < pool[b]; });
        int idx = 2 + (int) (mixHash (seed, grp, 44u) % 4u);
        for (int k = 0; k < pn; ++k)
        {
            m.step_[k] = pool[chosen[k]];
            const int d = (int) (mixHash (seed, grp, 45u, (uint32_t) k) % 5u);          // mostly steps, now and then a leap
            idx = std::clamp (idx + (d == 0 ? -2 : d == 1 ? -1 : d == 2 ? 1 : d == 3 ? 2 : 0), 0, 9);
            m.idx[k] = idx;
            m.vel[k] = k == 0 ? 0.78f : 0.42f + 0.3f * unitOf (mixHash (seed, grp, 46u, (uint32_t) k));
        }
        m.n = pn;
        return m;
    }

    // the notes of a register that sit in the allowed pitch classes
    int tonesOf (const ChordInfo& ci, int lo, int hi, int* out, int maxN, int mask = -1) const
    {
        const unsigned m = mask < 0 ? (unsigned) ci.allowed : (unsigned) mask;
        int n = 0;
        for (int note = lo; note <= hi && n < maxN; ++note) if (m & (1u << (((note - tEff) % 12 + 12) % 12))) out[n++] = note;
        return n;
    }
    // the notes of the last two beats of a chord lead into the next one: only the notes that fit both
    int bridgeMask (const ChordInfo& ci, long c, const GlobalParams& g, int sc) const
    {
        if (sc < 10 && sc != 0) return ci.allowed;
        const int both = ci.allowed & chordInfo (c + 1, g).allowed;
        int bits = 0; for (int i = 0; i < 12; ++i) bits += (both >> i) & 1;
        return bits >= 2 ? both : ci.allowed;
    }
    int rootNear (int pc, int lowest) const { int n = lowest + (((tEff + pc) % 12 - lowest % 12) + 12) % 12; return n; }

    bool keysPlays (long c, int k, double rel, const GlobalParams& g, const LayerParams* lp, float baseProb)
    {
        const float prob = layerProb (Keys, rel, g, lp[Keys], baseProb);
        return unitOf (mixHash (seed, (uint32_t) c, (uint32_t) k, 11u)) < prob;
    }

    void push (Event* out, int& count, int maxOut, const Event& e) { if (count < maxOut) out[count++] = e; }

    void onStep (long s, const GlobalParams& g, const LayerParams* lp, Event* out, int& count, int maxOut)
    {
        const double t = origin + (double) s * step, rel = (double) s * step, cb = chordBeats (g);
        const long c = (long) std::floor (rel / cb);
        const int sc = (int) std::lround ((rel - (double) c * cb) / step);                       // step inside the chord (0..15)
        const ChordInfo ci = chordInfo (c, g);
        const float p = progressAt (rel, g);
        lastProgress = std::clamp (p, 0.f, 1.f);
        const long sceneIdx = (long) std::floor (rel / sceneBeats (g));
        curMood = moodAtScene (sceneIdx, p, g);
        curChord = ci;
        auto mk = [&] (int layer, int note, float vel, double beat, float dur, float pan)
        {
            Event e; e.layer = layer; e.note = note; e.velocity = std::clamp (vel, 0.05f, 1.f); e.beat = beat; e.durBeats = dur; e.pan = pan;
            push (out, count, maxOut, e); activity[(size_t) layer] = 1.f;
        };
        const bool paused = t < pauseUntil;
        const float gainArc = 0.55f + 0.45f * arcDensity (p, g.journey);

        // the start of a scene: now and then something unexpected (never in the first scene, never at the end)
        if (sc == 0 && std::fmod (rel, sceneBeats (g)) < 1e-6 && sceneIdx > 0 && p < 0.9f
            && unitOf (mixHash (seed, (uint32_t) sceneIdx, 51u)) < 0.8f * g.surprise)
            gesture (t, c, ci, g, lp, out, count, maxOut);

        if (sc == 0)                                                      // ---- the chord changes ----
        {
            // pad: the root of the chord, held for the whole chord
            if (ci.padOk && unitOf (mixHash (seed, (uint32_t) c, 21u)) < layerProb (Pad, rel, g, lp[Pad], 0.95f))
                mk (Pad, rootNear (ci.rootPc, 48), (0.5f + 0.3f * unitOf (mixHash (seed, (uint32_t) c, 22u))) * gainArc, t, (float) cb, 0.f);
            // bass: the root, two octaves under the motif
            if (unitOf (mixHash (seed, (uint32_t) c, 23u)) < layerProb (Bass, rel, g, lp[Bass], 0.85f))
                mk (Bass, rootNear (ci.rootPc, 36), (0.6f + 0.25f * unitOf (mixHash (seed, (uint32_t) c, 24u))) * gainArc, t, 5.f, 0.f);
            // violin: a note of the chord, near the one before it, held for the chord
            if (! paused && unitOf (mixHash (seed, (uint32_t) c, 25u)) < layerProb (Vocals, rel, g, lp[Vocals], 0.8f))
            {
                int tones[24]; const int n = tonesOf (ci, 57, 79, tones, 24, chordInfo (c + 1, g).allowed & ci.allowed);
                int best = -1, bd = 1 << 20;
                const int want = violinNote < 0 ? tEff + 12 + 12 : violinNote;
                for (int i = 0; i < n; ++i)
                {
                    const int pcx = ((tones[i] - tEff) % 12 + 12) % 12;
                    if (pcx != ci.rootPc && pcx != ci.thirdPc && pcx != ci.fifthPc) continue;       // a chord tone
                    if (violinNote >= 0 && std::abs (tones[i] - violinNote) == 1) continue;
                    const int d = std::abs (tones[i] - want) * 4 + (int) (mixHash (seed, (uint32_t) c, 26u, (uint32_t) i) % 3u);
                    if (d < bd) { bd = d; best = tones[i]; }
                }
                if (best > 0) { violinNote = best; mk (Vocals, best, (0.45f + 0.3f * unitOf (mixHash (seed, (uint32_t) c, 27u))) * gainArc, t, (float) cb - 0.5f, 0.3f * (unitOf (mixHash (seed, (uint32_t) c, 28u)) - 0.5f)); }
            }
            // the place underneath, restarted now and then so it never ends
            if (t >= nextField)
            {
                if (unitOf (mixHash (seed, (uint32_t) c, 29u)) < std::clamp (lp[Field].amount * 1.5f, 0.f, 1.f))
                    mk (Field, -1, 0.85f, t, 64.f, 0.f);
                nextField = t + 48.0;
            }
        }
        // the bass answers on the third beat of the chord's second bar, with the fifth, now and then
        if (sc == 8 && unitOf (mixHash (seed, (uint32_t) c, 61u)) < 0.35f * layerProb (Bass, rel, g, lp[Bass], 1.f))
            mk (Bass, rootNear (ci.fifthPc, 36), 0.55f * gainArc, t, 3.f, 0.f);

        if (paused) return;

        // ---- the motif (keys): written in the chord, so it follows the harmony ----
        {
            const Motif m = motifOf (c);
            int tones[24]; const int n = tonesOf (ci, 60, 86, tones, 24, bridgeMask (ci, c, g, sc));
            const float keyDur = (float) std::clamp (cb - (double) sc * step - 1.0, 0.75, 6.0);
            for (int k = 0; k < m.n && n > 0; ++k)
            {
                if (m.step_[k] != sc) continue;
                const float base = k < 3 ? 0.92f : 0.62f;
                if (! keysPlays (c, k, rel, g, lp, base)) continue;
                int idx = m.idx[k];
                if (unitOf (mixHash (seed, (uint32_t) c, (uint32_t) k, 12u)) < 0.12f + 0.5f * g.surprise) idx += (mixHash (seed, (uint32_t) c, (uint32_t) k, 13u) & 1u) ? 1 : -1;   // it changes a little each time
                idx = std::clamp (idx, 0, n - 1);
                mk (Keys, tones[idx], m.vel[k] * gainArc, t, keyDur, 0.45f * (unitOf (mixHash (seed, (uint32_t) c, (uint32_t) k, 14u)) - 0.5f) * 2.f);
            }
        }
        // ---- the bells: the same motif, an octave and a half higher, three beats later ----
        {
            const double ts = rel - 3.0;
            if (ts >= 0.0)
            {
                const long c2 = (long) std::floor (ts / cb);
                const int sc2 = (int) std::lround ((ts - (double) c2 * cb) / step);
                const Motif m = motifOf (c2);
                for (int k = 0; k < m.n; ++k)
                {
                    if (m.step_[k] != sc2) continue;
                    if (! keysPlays (c2, k, ts, g, lp, k < 3 ? 0.92f : 0.62f)) continue;                 // only what the keys really played
                    if (unitOf (mixHash (seed, (uint32_t) c2, (uint32_t) k, 15u)) >= layerProb (Bells, rel, g, lp[Bells], 0.9f)) continue;
                    int tones[24]; const int n = tonesOf (ci, 72, 96, tones, 24, bridgeMask (ci, c, g, sc));
                    if (n <= 0) continue;
                    const float bellDur = (float) std::clamp (cb - (double) sc * step - 1.0, 0.75, 4.0);
                    mk (Bells, tones[std::clamp (m.idx[k], 0, n - 1)], 0.4f * m.vel[k] * gainArc, t, bellDur, 0.7f * (unitOf (mixHash (seed, (uint32_t) c2, (uint32_t) k, 16u)) - 0.5f) * 2.f);
                }
            }
        }
    }

    // the unexpected: always made of notes that do not rub against the chord
    void gesture (double t, long c, const ChordInfo& ci, const GlobalParams& g, const LayerParams* lp, Event* out, int& count, int maxOut)
    {
        const float r = unitOf (mixHash (seed, (uint32_t) c, 52u));
        auto mk = [&] (int layer, int note, float vel, double beat, float dur)
        {
            Event e; e.layer = layer; e.note = note; e.velocity = vel; e.beat = beat; e.durBeats = (layer == Bass || layer == Pad) ? (float) std::clamp ((double) dur, 0.5, std::max (0.5, t + chordBeats (g) - beat)) : (float) std::clamp ((double) dur, 0.5, std::max (0.5, t + chordBeats (g) - 1.0 - beat)); e.pan = 0.25f * (unitOf (mixHash (seed, (uint32_t) beat * 7u, 53u)) - 0.5f);
            push (out, count, maxOut, e); activity[(size_t) layer] = 1.f;
        };
        int tones[24];
        if (r < 0.35f && lp[Vocals].amount > 0.f)            // a falling phrase of the violin
        {
            const int n = tonesOf (ci, 57, 84, tones, 24);
            int i = std::max (0, n - 3);
            for (int k = 0; k < 4 && n > 0; ++k) { mk (Vocals, tones[std::clamp (i, 0, n - 1)], 0.6f - 0.08f * (float) k, t + 1.0 + 1.5 * k, 2.2f); i -= 1 + (int) (mixHash (seed, (uint32_t) c, 54u, (uint32_t) k) % 2u); }
        }
        else if (r < 0.60f && lp[Bass].amount > 0.f && lp[Pad].amount > 0.f)                                   // the floor drops
        {
            mk (Bass, rootNear (ci.rootPc, 24), 0.85f, t + 0.5, 12.f);
            if (ci.padOk) mk (Pad, rootNear (ci.rootPc, 48), 0.7f, t + 0.5, 16.f);
            pauseUntil = t + (double) g.beatsPerBar;          // and one bar of silence above it
        }
        else if (r < 0.85f && lp[Bells].amount > 0.f)         // a cascade of bells
        {
            const int n = tonesOf (ci, 72, 96, tones, 24);
            int i = n - 1;
            for (int k = 0; k < 5 && n > 0; ++k) { mk (Bells, tones[std::clamp (i, 0, n - 1)], 0.5f - 0.07f * (float) k, t + 1.0 + 1.5 * k, 4.f); i -= 1 + (int) (mixHash (seed, (uint32_t) c, 55u, (uint32_t) k) % 2u); }
        }
        else if (lp[Keys].amount > 0.f)                        // one high, soft note
        {
            const int n = tonesOf (ci, 84, 96, tones, 24);
            if (n > 0) mk (Keys, tones[mixHash (seed, (uint32_t) c, 56u) % (uint32_t) n], 0.35f, t + 1.0, 6.f);
        }
    }

    bool active = false;
    double origin = 0.0, pauseUntil = -1e9, nextField = 0.0;
    uint32_t seed = 1;
    long nextStep = 0;
    int tonic = 48, tEff = 48, curMood = Light, violinNote = -1;
    float lastProgress = 0.f;
    ChordInfo curChord;
    std::vector<int> moods;
    std::array<float, kNumLayers> activity {};
};
} // namespace amb
