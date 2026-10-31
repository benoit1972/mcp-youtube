# Simple Gain — plugin Audio Unit (AU)

Effet AU minimal (gain -60…+12 dB + inversion de phase) basé sur [JUCE](https://juce.com).
Indépendant du serveur MCP YouTube du reste du dépôt.

## Compilation (macOS + Xcode + CMake requis)

```sh
cd audio-unit
cmake -B build -G Xcode
cmake --build build --config Release
```

Le plugin est copié dans `~/Library/Audio/Plug-Ins/Components/`. Validation :

```sh
auval -v aufx Sgn1 Benp
```

Puis rescanner les plugins dans Logic Pro / GarageBand.

Codes à personnaliser dans `CMakeLists.txt` : `PLUGIN_MANUFACTURER_CODE` (1 majuscule + 3 lettres), `PLUGIN_CODE`.
