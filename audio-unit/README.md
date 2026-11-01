# Poomse Glitch — synthé granulaire ambient AU (glitch, stutter, tape, shimmer, notes génératives)

Instrument Audio Unit (`aumu`) basé sur [JUCE](https://juce.com). Indépendant du serveur MCP YouTube du reste du dépôt.

## Interface

Cinq pages (onglets **Grain / Play / FX / Mod / Matrix**) pour garder l'écran d'accueil léger. Tout démarre « à zéro » : effets, stutter, glitch, Drift et modulations de LFO/séquenceur sont désactivés, on sculpte d'abord le son (Grain, Play) puis on ajoute les effets :
- **Grain** : forme d'onde cliquable (position + zone de spray) et les réglages du nuage de grains. Glisse un fichier audio dessus pour le charger.
- **Play** : enveloppe ADSR et moteur de notes génératif *Drift*.
- **FX** : stutter/glitch, tape/delay, reverb/shimmer.
- **Mod** : les sources de modulation. 8 **macros** (les 8 potards à mapper dans Live), 2 **LFO** (sinus, triangle, dent de scie, carré, aléatoire ; libres en Hz ou synchronisés au tempo) et un **séquenceur euclidien** (1-16 pas, nombre de coups, rotation, vitesse, déclin, hasard) dont le motif s'affiche.
- **Matrix** : la matrice de modulation. Lignes = tous les paramètres modulables, colonnes = les 11 sources (M1-M8, LFO1, LFO2, SEQ). Glisse une case vers le haut/bas pour régler la quantité (de -100 % à +100 % ; Maj = précis), double-clic = effacer, molette = défiler. Orange = positif, bleu = négatif. N'importe quelle source peut piloter n'importe quel paramètre, et plusieurs sources peuvent s'additionner sur un même paramètre.

Dans Ableton Live : place le plugin dans un Instrument Rack, clique sur *Configure*, bouge *Macro 1…8* et mappe-les sur les potards du Rack. Les macros ont des affectations par défaut (texture, nuage, hauteur, espace, écho, grain, enveloppe, mouvement) que tu peux changer dans la Matrix. Les modulations sont calculées une fois par bloc audio.

Bouton **Reset** (en haut à droite, à gauche de Random) : remet **tous** les réglages à leur valeur neutre (effets, stutter, glitch, Drift, LFO, séquenceur et macros à zéro, matrice aux affectations par défaut des macros) pour repartir d'un son vierge. Il demande un second clic dans les 3 secondes (« Sure? »). L'échantillon chargé est conservé.

Bouton **Random** : agit sur la **page affichée** : Grain, Play ou FX = ces réglages ; Mod ou Matrix = donne aux 8 macros **des cibles aléatoires** dans la matrice (2 à 4 paramètres chacune) **et des valeurs aléatoires** (les colonnes LFO et séquenceur sont conservées) ; **Maj + clic** = Grain + Play + FX. Les tirages restent dans des plages musicales ; *Level* (volume) et *Drift* (qui ne doit pas se mettre à jouer tout seul) ne sont jamais touchés.
**Glisser-déposer** : dépose un fichier audio (wav, aiff, mp3, flac, m4a, ogg) depuis le Finder sur la zone de la forme d'onde pour le charger. Double-clic sur un potard : valeur par défaut.

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
dans le synthé. L'option de gamme **From MIDI** prend pour gamme les notes (classes de hauteur) que tu as jouées sur le clavier
MIDI pendant les 20 dernières secondes ; tant que rien n'a été joué, Drift utilise la pentatonique. Fonctionne sans MIDI ; le jeu MIDI se mélange aux notes générées.

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
