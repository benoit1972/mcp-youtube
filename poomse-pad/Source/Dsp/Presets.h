#pragma once
#include "ModMatrix.h"
#include <vector>

// Factory presets. A preset starts from the neutral defaults (and an empty matrix), then sets the listed
// parameters (real values: seconds, Hz, ... exactly as shown in the plugin) and matrix cells.
// Every preset except "Init" also gets the same 6 useful macros, so Macro 1-6 do something musical straight away.
namespace pad
{
struct PresetParam { const char* id; float value; };
struct PresetMod { int src; const char* dest; float amount; };
struct Preset
{
    const char* name;
    std::vector<PresetParam> params;
    std::vector<PresetMod> mods;
};

inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list = []
    {
        constexpr int LFO1 = gg::kSrcLfo1, LFO2 = gg::kSrcLfo2, SEQ = gg::kSrcSeq;
        std::vector<Preset> v;
        v.push_back ({ "Init", {}, {} });

        v.push_back ({ "Warm Pad",
            { { "time", 6.f }, { "rise", 1.2f }, { "evolve", 0.35f }, { "rate", 0.08f }, { "sub", 0.35f }, { "body", 1.f },
              { "octave", 0.15f }, { "tone", -0.25f }, { "width", 0.8f }, { "revSize", 0.7f }, { "revMix", 0.35f } }, {} });
        v.push_back ({ "Glass Shimmer",
            { { "time", 8.f }, { "rise", 0.6f }, { "evolve", 0.45f }, { "rate", 0.12f }, { "body", 0.8f }, { "fifth", 0.25f },
              { "octave", 0.7f }, { "tone", 0.35f }, { "width", 1.f }, { "revSize", 0.85f }, { "revMix", 0.45f } }, {} });
        v.push_back ({ "Deep Drone",
            { { "time", 30.f }, { "rise", 3.f }, { "evolve", 0.5f }, { "rate", 0.03f }, { "sub", 0.9f }, { "body", 1.f },
              { "tone", -0.5f }, { "width", 0.6f }, { "revSize", 0.9f }, { "revMix", 0.4f } }, {} });
        v.push_back ({ "Slow Bloom",
            { { "time", 12.f }, { "rise", 4.f }, { "evolve", 0.6f }, { "rate", 0.05f }, { "body", 1.f }, { "fifth", 0.4f },
              { "octave", 0.3f }, { "width", 0.9f }, { "revSize", 0.8f }, { "revMix", 0.4f } }, {} });
        v.push_back ({ "Open Fifths",
            { { "time", 5.f }, { "rise", 1.5f }, { "evolve", 0.3f }, { "rate", 0.1f }, { "sub", 0.2f }, { "body", 1.f },
              { "fifth", 0.6f }, { "octave", 0.4f }, { "tone", 0.1f }, { "width", 0.9f }, { "revSize", 0.7f }, { "revMix", 0.35f } }, {} });
        v.push_back ({ "Dark Cathedral",
            { { "time", 14.f }, { "rise", 2.f }, { "evolve", 0.4f }, { "rate", 0.05f }, { "sub", 0.6f }, { "body", 0.8f },
              { "tone", -0.7f }, { "width", 1.f }, { "revSize", 1.f }, { "revMix", 0.6f } }, {} });
        v.push_back ({ "Halo (blend with dry)",
            { { "mix", 0.4f }, { "time", 3.f }, { "rise", 0.5f }, { "evolve", 0.25f }, { "body", 1.f }, { "octave", 0.4f },
              { "width", 0.8f }, { "revSize", 0.6f }, { "revMix", 0.25f } }, {} });
        v.push_back ({ "Wide Wash",
            { { "time", 10.f }, { "rise", 2.f }, { "evolve", 0.7f }, { "rate", 0.2f }, { "sub", 0.2f }, { "body", 1.f },
              { "octave", 0.2f }, { "width", 1.f }, { "revSize", 0.85f }, { "revMix", 0.5f } }, {} });

        v.push_back ({ "Breathing (LFO)",
            { { "time", 8.f }, { "rise", 1.f }, { "evolve", 0.3f }, { "body", 1.f }, { "octave", 0.3f }, { "width", 0.8f },
              { "revSize", 0.8f }, { "revMix", 0.35f }, { "lfo1Rate", 0.12f }, { "lfo1Sync", 0.f }, { "lfo1Shape", 0.f } },
            { { LFO1, "tone", 0.45f }, { LFO1, "revMix", 0.3f }, { LFO1, "width", -0.3f } } });
        v.push_back ({ "Drifting Light (2 LFOs)",
            { { "time", 10.f }, { "rise", 1.5f }, { "body", 1.f }, { "fifth", 0.3f }, { "octave", 0.4f }, { "width", 0.9f },
              { "revSize", 0.8f }, { "revMix", 0.4f }, { "lfo1Rate", 0.07f }, { "lfo1Shape", 0.f }, { "lfo2Rate", 0.11f }, { "lfo2Shape", 1.f } },
            { { LFO1, "tone", 0.5f }, { LFO1, "evolve", 0.4f }, { LFO2, "octave", 0.35f }, { LFO2, "width", 0.3f } } });
        v.push_back ({ "Rhythmic Swell (Sequencer)",
            { { "time", 4.f }, { "rise", 0.4f }, { "body", 1.f }, { "fifth", 0.2f }, { "octave", 0.2f }, { "width", 0.8f },
              { "revSize", 0.75f }, { "revMix", 0.35f }, { "seqSteps", 8.f }, { "seqHits", 3.f }, { "seqRot", 0.f },
              { "seqRate", 1.f }, { "seqDecay", 0.8f }, { "seqRandom", 0.3f } },
            { { SEQ, "octave", 0.6f }, { SEQ, "fifth", 0.4f }, { SEQ, "revMix", 0.25f } } });
        v.push_back ({ "Pulsing Choir (LFO + Sequencer)",
            { { "time", 6.f }, { "rise", 0.8f }, { "sub", 0.2f }, { "body", 1.f }, { "fifth", 0.5f }, { "octave", 0.5f },
              { "width", 1.f }, { "revSize", 0.8f }, { "revMix", 0.4f }, { "seqSteps", 16.f }, { "seqHits", 5.f }, { "seqRate", 2.f },
              { "seqDecay", 0.5f }, { "lfo1Rate", 0.2f }, { "lfo1Shape", 1.f } },
            { { SEQ, "tone", 0.3f }, { LFO1, "evolve", 0.5f } } });

        // the same 6 macros on every preset but Init: space, brightness, motion, length, depth, width
        for (size_t i = 1; i < v.size(); ++i)
        {
            auto& m = v[i].mods;
            m.push_back ({ 0, "revMix", 0.5f });  m.push_back ({ 0, "revSize", 0.3f });   // Macro 1: space
            m.push_back ({ 1, "tone", 0.5f });    m.push_back ({ 1, "octave", 0.4f });    // Macro 2: brightness
            m.push_back ({ 2, "evolve", 0.5f });  m.push_back ({ 2, "rate", 0.3f });      // Macro 3: motion
            m.push_back ({ 3, "time", 0.3f });    m.push_back ({ 3, "rise", -0.2f });     // Macro 4: longer & smoother
            m.push_back ({ 4, "sub", 0.5f });     m.push_back ({ 4, "body", -0.3f });     // Macro 5: depth
            m.push_back ({ 5, "width", 0.5f });                                            // Macro 6: width
        }
        return v;
    }();
    return list;
}
} // namespace pad
