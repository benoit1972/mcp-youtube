# Poomse Pad — instrument AU qui transforme n'importe quel sample en pad évolutif

Instrument Audio Unit (`aumu`) basé sur [JUCE](https://juce.com). **Glisse un sample sur la zone A et un autre sur la zone B** (deux zones de dépôt de part et d'autre du spectre ; des sons intégrés servent au départ) (piano, voix, guitare, batterie…, wav / aiff / mp3 / flac / m4a / ogg, 60 s max ; un drone intégré sert au départ), puis **joue au clavier MIDI** (note 60 = C3 = hauteur d'origine ; polyphonie 8 voix) :
le son est étiré en une **nappe continue** qui garde sa couleur harmonique, se transforme lentement et dure bien
après la fin de la note. Le réglage **Morph A-B** (modulable par la matrice : LFO, séquenceur, macro) fait passer le pad de A à B. Deux façons de les combiner, au bouton **Layers** : *éteint* = **Morph** (les spectres sont interpolés : A se transforme en B en passant par un hybride, idéal pour le mouvement) ; *allumé* = **Layers** (A et B sonnent toujours ensemble, Morph ne règle que leur équilibre à puissance constante : un pad plus épais, sans creux au milieu). Le bouton **Loop** (en haut à droite de la zone du spectre) fait boucler le sample tant que la touche est tenue ; sans Loop, il joue une fois puis le pad continue de se déployer. Quand tu relâches la touche, l'échantillon s'éteint en 60 ms et le pad retombe selon *Time*. *Mix* mélange le sample d'origine (dry) et le pad. Indépendant de Poomse Glitch (dossier `audio-unit/`), mêmes fonctions de modulation, autre couleur.

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

Menu **presets** (en haut, avec flèches < >) : 13 presets d'usine, de « Warm Pad » à « Pulsing Choir », certains avec LFO et séquenceur déjà câblés. Chacun part des réglages neutres, puis règle le pad ; tous (sauf *Init*) donnent aussi 6 macros utiles : Macro 1 espace, 2 brillance, 3 mouvement, 4 plus long et plus doux, 5 profondeur, 6 largeur.

Boutons du haut : **Hold** (fige le spectre : le pad dure indéfiniment), **Reset** (deux clics : tout à la valeur par défaut, matrice vidée),
**Random** (agit sur la page affichée, Maj = Pad + Tone ; sur Mod/Matrix il donne aux 8 macros des cibles et des valeurs aléatoires ;
*Mix*, *Level* et *Hold* ne sont jamais touchés).

Dans Ableton Live : *Configure* dans le plugin, bouge *Macro 1…8* et mappe-les sur les potards d'un Rack.

## Compilation (macOS + Xcode + CMake)

```sh
cd poomse-pad
cmake -B build -G Xcode
cmake --build build --config Release
auval -v aumu Pad1 Benp
```

Le dépôt contient un workflow GitHub (`.github/workflows/build-pad.yml`) qui compile le plugin sur Mac et publie le zip dans `poomse-pad/release/`.

## Tests DSP (sans JUCE)

```sh
g++ -std=c++17 -O2 -fsanitize=address,undefined tests/dsp_test.cpp -o dsp_test && ./dsp_test
```
