# Poomse Ambient — musique générative qui ne se répète jamais

Instrument Audio Unit (`aumu`) basé sur [JUCE](https://juce.com). **Tu poses une seule note longue dans un clip** (par exemple un do sur
120 mesures) : sa hauteur est la tonalité et sa durée est la longueur du morceau. Le plugin compose le reste : notes, silences,
variations, moments légers et profonds, surprises. Rien ne se répète.

## Comment il compose (à la Eno)

Le principe d'Eno (*Music for Airports*) : **chaque boucle joue toujours la même note**, répétée à sa propre cadence. Il n'y a que quelques
notes en tout, toutes consonantes entre elles ; l'imprévisible vient uniquement du **décalage** entre boucles de longueurs différentes,
jamais de notes tirées au hasard à chaque fois.

- Le plugin réduit la gamme choisie à **cinq notes qui ne frottent jamais** (pas de demi-ton entre deux d'entre elles) : c'est le « jeu de notes » du morceau.
- Chaque couche est faite de **voix** : une voix = une de ces cinq notes, toujours la même, avec sa propre durée de boucle
  (7,3 / 9,1 / 11,7 / 14,3 / 17,9 / 23,3 / 24,7 / 31,9 temps…, jamais multiples les uns des autres). Elles se déphasent sans jamais retomber au même endroit.
- L'**harmonie** ne bouge que rarement et doucement : tout le jeu de notes glisse d'une quarte ou d'une quinte (quatre notes sur cinq restent communes), puis revient.
- Le morceau traverse des **scènes** : *légère* (peu de notes, de l'air), *profonde* (nappe, basse, voix plus graves), *calme* (une pause : pluie et queues de réverb),
  sur un **arc** (il monte, culmine, redescend). Les **surprises** sont rares et restent dans le jeu de notes : phrase de voix fantôme qui descend, chute de la basse,
  petit roulement, note aiguë isolée.
- Mesuré dans les tests : dans les 8 gammes et les 2 styles, **aucune paire de notes qui sonnent ensemble ne forme un demi-ton ou une septième majeure**.

## Les six couches (un sample chacune : glisse-le sur la zone)

| Couche | Rôle |
|---|---|
| Keys | notes de clavier éparses |
| Vocals | voix fantômes hachées et transposées |
| Pad | nappes sombres (accords) |
| Beat | un coup de batterie joué en rythme cassé et rare (Night) ; des cloches éparses (Clean) |
| Atmos | pluie, ville, craquements : presque continu |
| Sub | notes graves lentes sur la tonique / l'accord |

Des sons intégrés jouent dès l'ouverture. Pour chaque couche : *Level*, *Amount* (à quelle fréquence elle joue), *Space* (envoi réverb).
La hauteur d'un sample déposé est **détectée automatiquement** ; boutons +/- pour la corriger.

## Les réglages

- **Space** (beaucoup de silence), **Depth** (moments profonds plutôt que légers), **Surprise**, **Journey** (l'arc : 0 = constant),
  **Length** (mesures), **Scale** (pentatonique, mineure, dorien, lydien…), **Seed** (0 = un morceau neuf à chaque lecture ; un numéro = toujours
  le même ; *Keep seed* garde la version que tu entends).
- **Night / Clean** : *Night* = grain poussiéreux (son étouffé *Dark*, craquement de vinyle *Crackle*, ondulation de bande *Tape*, beat fantôme).
  *Clean* = ambient limpide : pas de poussière, la couche Beat devient des cloches, les sons intégrés redeviennent piano / chœur / nappe / cloches.
- **Reverb**, **Echo**, **Cloud** (un nuage granulaire nourri de la musique elle-même : une sorte de halo qui prolonge les notes) et **Level**.
- Menu de **presets** (8), **Reset** (deux clics), **Random** (la page affichée, Maj = tout).

## Compilation

```sh
cd poomse-ambient && cmake -B build -G Xcode && cmake --build build --config Release && auval -v aumu Amb1 Benp
```

Le workflow `.github/workflows/build-ambient.yml` compile sur Mac et publie le zip dans `poomse-ambient/release/`.
Tests du moteur (sans JUCE) : `g++ -std=c++17 -O2 tests/dsp_test.cpp -o dsp_test && ./dsp_test`.
