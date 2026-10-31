# Glitch Grain — synthé granulaire AU avec glitch & stutter

Instrument Audio Unit (`aumu`) basé sur [JUCE](https://juce.com). Indépendant du serveur MCP YouTube du reste du dépôt.

## Fonctionnement

**Moteur granulaire** (8 voix, 24 grains par voix, MIDI) : les grains sont découpés dans un échantillon
(drone intégré par défaut, ou n'importe quel fichier audio via *Load sample…*, 60 s max). Do central (note 60) = hauteur d'origine.

| Paramètre | Rôle |
|---|---|
| Position / Spray | point de lecture dans l'échantillon / dispersion aléatoire |
| Grain Size / Density | durée du grain (5–500 ms) / grains par seconde |
| Window | forme de la fenêtre (Hann → quasi rectangulaire) |
| Pitch / Pitch Spray | transposition / dispersion de hauteur par grain |
| Reverse | probabilité qu'un grain soit lu à l'envers |
| Stereo | dispersion du panoramique par grain |
| ADSR, Level | enveloppe par voix, volume de sortie (limiteur `tanh`) |

**Stutter** : à chaque temps, avec une probabilité *Stutter*, les dernières *Stutter Rate* (1/4 … 1/32T) de son sont
capturées et bouclées jusqu'au temps suivant. *Stutter Pitch* change la vitesse de la boucle.

**Glitch** : à chaque tranche *Glitch Rate*, avec une probabilité *Glitch*, un effet est tiré au hasard :
coupure (dropout), bit/rate crusher, lecture inversée de ce qui vient d'être joué, micro-boucle (buzz).

Les horloges suivent le tempo de l'hôte (120 BPM par défaut) mais ne sont **pas** calées sur la position de lecture.

## Compilation (macOS + Xcode + CMake)

```sh
cd audio-unit
cmake -B build -G Xcode
cmake --build build --config Release
auval -v aumu Ggr1 Benp
```

Le plugin est copié dans `~/Library/Audio/Plug-Ins/Components/` ; rescanner les plugins dans Logic / GarageBand.
Personnaliser `PLUGIN_MANUFACTURER_CODE` / `PLUGIN_CODE` dans `CMakeLists.txt` si le plugin est distribué.

Sous Linux/Windows, seule la cible *Standalone* est construite (l'AU n'existe que sur macOS).

## Tests DSP (sans JUCE)

```sh
g++ -std=c++17 -O1 -fsanitize=address,undefined tests/dsp_test.cpp -o dsp_test && ./dsp_test
```
