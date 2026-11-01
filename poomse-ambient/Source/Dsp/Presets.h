#pragma once
#include <vector>

// Factory presets. Each one starts from the plugin's defaults and sets the listed parameters (real values).
namespace amb
{
struct PresetParam { const char* id; float value; };
struct Preset { const char* name; std::vector<PresetParam> params; };

inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list = []
    {
        std::vector<Preset> v;
        v.push_back ({ "Init", {} });

        // ---- Night: dusty, dark, a ghost beat ----
        v.push_back ({ "Night Bus",
            { { "scale", 3 }, { "space", 0.55f }, { "depth", 0.55f }, { "surprise", 0.25f }, { "journey", 0.7f },
              { "dark", 0.55f }, { "crackle", 0.35f }, { "tape", 0.35f }, { "reverb", 0.55f }, { "echo", 0.2f }, { "cloud", 0.35f },
              { "keysAmount", 0.8f }, { "vocalsAmount", 0.65f }, { "padAmount", 0.6f }, { "beatAmount", 0.45f }, { "atmosAmount", 0.9f }, { "subAmount", 0.7f } } });
        v.push_back ({ "Rain on Glass",
            { { "scale", 2 }, { "space", 0.7f }, { "depth", 0.35f }, { "surprise", 0.15f }, { "dark", 0.5f }, { "crackle", 0.4f }, { "tape", 0.3f },
              { "reverb", 0.65f }, { "cloud", 0.4f }, { "keysAmount", 0.6f }, { "vocalsAmount", 0.35f }, { "padAmount", 0.5f }, { "beatAmount", 0.f },
              { "atmosAmount", 1.f }, { "atmosLevel", 0.8f }, { "subAmount", 0.5f } } });
        v.push_back ({ "Ghost Shuffle",
            { { "scale", 4 }, { "space", 0.45f }, { "depth", 0.7f }, { "surprise", 0.45f }, { "dark", 0.45f }, { "crackle", 0.3f }, { "tape", 0.4f },
              { "cloud", 0.45f }, { "beatAmount", 0.85f }, { "vocalsAmount", 0.8f }, { "keysAmount", 0.6f }, { "padAmount", 0.5f } } });
        v.push_back ({ "Dark Rooms",
            { { "scale", 1 }, { "space", 0.6f }, { "depth", 0.9f }, { "surprise", 0.3f }, { "dark", 0.7f }, { "crackle", 0.3f }, { "tape", 0.35f },
              { "reverb", 0.7f }, { "padAmount", 0.8f }, { "subAmount", 0.9f }, { "vocalsAmount", 0.55f }, { "beatAmount", 0.3f }, { "keysAmount", 0.5f } } });
        v.push_back ({ "Slow Light",
            { { "scale", 5 }, { "space", 0.7f }, { "depth", 0.1f }, { "surprise", 0.2f }, { "journey", 0.9f }, { "dark", 0.25f }, { "crackle", 0.15f },
              { "tape", 0.2f }, { "reverb", 0.6f }, { "keysAmount", 1.f }, { "vocalsAmount", 0.5f }, { "padAmount", 0.3f }, { "beatAmount", 0.2f } } });

        // ---- Clean: the same construction, without the dust; the beat slot is sparse bells ----
        v.push_back ({ "Airport (Clean)",
            { { "night", 0 }, { "scale", 2 }, { "space", 0.65f }, { "depth", 0.4f }, { "surprise", 0.25f }, { "journey", 0.8f }, { "reverb", 0.6f },
              { "echo", 0.15f }, { "cloud", 0.3f }, { "keysAmount", 0.9f }, { "vocalsAmount", 0.6f }, { "padAmount", 0.6f }, { "beatAmount", 0.5f },
              { "atmosAmount", 0.5f }, { "subAmount", 0.6f } } });
        v.push_back ({ "Bells in Fog (Clean)",
            { { "night", 0 }, { "scale", 5 }, { "space", 0.7f }, { "depth", 0.3f }, { "surprise", 0.35f }, { "reverb", 0.7f }, { "cloud", 0.5f },
              { "beatAmount", 0.9f }, { "keysAmount", 0.5f }, { "padAmount", 0.5f }, { "atmosAmount", 0.4f } } });
        v.push_back ({ "Long Drone (Clean)",
            { { "night", 0 }, { "scale", 3 }, { "space", 0.6f }, { "depth", 0.8f }, { "journey", 1.f }, { "bars", 240 }, { "reverb", 0.75f },
              { "padAmount", 1.f }, { "subAmount", 1.f }, { "vocalsAmount", 0.6f }, { "keysAmount", 0.3f }, { "beatAmount", 0.15f } } });
        return v;
    }();
    return list;
}
} // namespace amb
