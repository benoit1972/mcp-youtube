# Poomse Pad — effet AU qui transforme n'importe quel son en pad évolutif

Effet Audio Unit (`aufx`) basé sur [JUCE](https://juce.com). Tu l'insères sur une piste (piano, voix, guitare, sample de batterie…) :
le son qui entre est étiré en une **nappe continue** qui garde sa couleur harmonique, se transforme lentement et dure bien
après la fin de la note. Indépendant de Poomse Glitch (dossier `audio-unit/`), mêmes fonctions de modulation, autre couleur.

## Comment ça marche

Étirement spectral en temps réel : le son est analysé par FFT (fenêtres de 170 ms, recouvrement 75 %), les amplitudes de chaque
fréquence sont lissées sur plusieurs secondes (montée rapide, retombée lente), puis le son est resynthétisé avec des **phases
aléatoires** : les attaques disparaissent, la matière devient continue, et l'harmonie de l'original reste.

## Interface (4 pages)

- **Pad** (accueil) : spectre en direct du pad, *Time* (combien de temps le son reste après son extinction, 0,1 à 60 s),
  *Rise* (vitesse de montée vers un nouveau son), *Evolve* / *Evolve rate* (un renforcement spectral qui se déplace lentement,
  en sens inverse à gauche et à droite), *Mix* (dry/wet), et les **couches** : *Sub* (-12 demi-tons), *Body*, *Fifth* (+7), *Octave* (+12).
- **Tone** : *Tone* (inclinaison spectrale ±6 dB/oct), *Width* (0 = pad mono, 1 = stéréo décorrélée), réverb (*Size*, *Mix*), *Level*.
- **Mod** : 8 **macros**, 2 **LFO** (sinus, triangle, dent de scie, carré, aléatoire ; libres ou synchronisés), un **séquenceur euclidien** (motif visible).
- **Matrix** : grille paramètre × source (M1-M8, LFO1, LFO2, SEQ). Glisse une case vers le haut/bas pour régler la quantité (Maj = précis),
  double-clic = effacer, molette = défiler. Turquoise = positif, ambre = négatif. La matrice est **vide** au départ.

Boutons du haut : **Hold** (fige le spectre : le pad dure indéfiniment), **Reset** (deux clics : tout à la valeur par défaut, matrice vidée),
**Random** (agit sur la page affichée, Maj = Pad + Tone ; sur Mod/Matrix il donne aux 8 macros des cibles et des valeurs aléatoires ;
*Mix*, *Level* et *Hold* ne sont jamais touchés).

Dans Ableton Live : *Configure* dans le plugin, bouge *Macro 1…8* et mappe-les sur les potards d'un Rack.

## Compilation (macOS + Xcode + CMake)

```sh
cd poomse-pad
cmake -B build -G Xcode
cmake --build build --config Release
auval -v aufx Pad1 Benp
```

Le dépôt contient un workflow GitHub (`.github/workflows/build-pad.yml`) qui compile le plugin sur Mac et publie le zip dans `poomse-pad/release/`.

## Tests DSP (sans JUCE)

```sh
g++ -std=c++17 -O2 -fsanitize=address,undefined tests/dsp_test.cpp -o dsp_test && ./dsp_test
```
