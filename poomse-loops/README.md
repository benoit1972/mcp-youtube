# Poomse Loops

Un instrument Audio Unit construit autour d'une seule idée : **le point de bouclage est le son** (à la manière de Jan Jelinek).

Tu glisses un vieux disque (jazz, soul, n'importe quoi) sur la carte. Le plugin y **cherche lui-même les endroits où le son peut se refermer sur lui-même sans clic** : il en trouve des centaines. Chaque touche que tu joues prend la boucle la plus proche du réticule et la fait tourner, à la hauteur de la touche.

Rien n'est généré, rien n'est ajouté : pas de réverb, pas de bruit, pas de filtre. Le son, c'est le disque.

## La carte
- chaque point est une boucle trouvée ; **horizontal = l'endroit dans le disque**, **vertical = la taille de la boucle** (30 ms à 750 ms) ;
- le réticule orange (Position, Size) se déplace en cliquant / glissant sur la carte ; les touches tenues allument leur boucle ;
- en bas, le disque lui-même ; glisse un fichier audio n'importe où sur la zone pour le remplacer (sinon : un disque de jazz usé, fabriqué sur place).

## Les réglages
| | |
|---|---|
| Seam | 0 : on entend le point de bouclage (une pulsation) ; 1 : tout est lissé (un drone) |
| Moire | chaque note joue 3 copies de sa boucle dont la longueur diffère d'un cheveu (jusqu'à 5 %) : elles se décalent lentement, et ce battement est le rythme |
| Scan | le réticule voyage dans le disque pendant que la touche est tenue (négatif : à l'envers) |
| Pulse | accent au début de chaque tour de boucle |
| Tail | durée du relâchement (0,05 à 6 s) |
| Level | volume |
| Keys = pitch / places | **pitch** : la touche règle la hauteur ; **places** : chaque touche est un endroit du disque (jouer les accords du disque au clavier) |

Position et Size sont des paramètres automatisables : dessiner leur courbe dans Live, c'est composer en se promenant dans le disque.

## Tests
`tests/dsp_test.cpp` (sans JUCE) : boucles trouvées en nombre et de toutes tailles, aucune dans le silence ni dans du bruit blanc, sur un sinus chaque boucle contient un nombre entier de périodes, octave exacte, aucun clic au bouclage, relâchement, moiré, pulse, mode places, scan.
