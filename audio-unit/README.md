# Glitch Grain — synthé granulaire ambient AU (glitch, stutter, tape, shimmer, notes génératives)

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

**Tape** : wow/flutter/dérive, saturation douce, adoucissement des aigus et léger souffle.

**Delay** : ping-pong stéréo (10–1500 ms), feedback assombri.

**Reverb & Shimmer** : réverbération à peignes/allpass ; *Shimmer* renvoie la queue de réverb une octave plus haut dans la réverb
(le gain de la boucle est automatiquement réduit quand la réverb est longue, pour rester stable).

**Drift** (notes génératives) : à chaque pas (1 mesure … 1/16, selon le tempo), avec une probabilité *Drift*, une marche aléatoire
dans la gamme choisie (majeure, mineure, pentatonique, dorien, lydien, tons entiers ; tonique au choix ; 3 octaves) joue une note
dans le synthé. Fonctionne sans MIDI ; le jeu MIDI se mélange aux notes générées.

> Instrument original. Il est *inspiré par la liste de fonctions* de GrainBrick (TrimUI Brick, code source fermé) mais n'en reprend
> ni code ni nom ; le moteur de notes, les effets et l'interface sont écrits ici de zéro.

Chaîne audio : synthé granulaire → stutter/glitch → tape → delay → reverb/shimmer → niveau + limiteur `tanh`.

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
