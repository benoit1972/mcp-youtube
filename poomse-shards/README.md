# Poomse Shards

Un instrument Audio Unit de batterie, pensé pour le style de **Telefon Tel Aviv** : des beats cassés, des caisses claires qui se brisent en rafales précises, de la mélancolie dans une mécanique impeccable.

Tu glisses un break de batterie (ou tu utilises celui fourni, fabriqué sur place). Le plugin **le découpe lui-même à chaque attaque** et décrit chaque morceau (fort ou doux, sombre ou brillant). Les touches jouent ces morceaux ; et un coup peut **se briser** en rafale, calée sur le tempo de Live.

## La carte
- chaque point est un coup du break ; **horizontal = l'endroit dans le break**, **vertical = la brillance** (les grosses caisses en bas, les charleys en haut) ;
- une touche joue le coup situé à sa hauteur : **les touches graves jouent les sons sombres, les aiguës les sons brillants** (de C1 en bas à C5 en haut) ;
- la ligne bleue (Place) choisit d'où dans le break viennent les coups ; clic / glisser sur la carte pour la déplacer ;
- chaque coup joué allume son point (les éclats d'une rafale aussi) ;
- glisse un fichier audio n'importe où sur la zone pour remplacer le break.

## Les réglages
| | |
|---|---|
| Place | où dans le break sont pris les coups (donc quelle grosse caisse, quelle caisse claire) |
| Shatter | à quelle fréquence un coup se brise, et en combien d'éclats (2 à 8). Les grosses caisses restent entières plus souvent que les caisses claires et les charleys |
| Grid | la vitesse de la rafale, calée sur le tempo : 1/8, 1/12, 1/16, 1/24, 1/32, 1/48, 1/64 |
| Ramp | positif : la rafale accélère et enfle ; négatif : elle ralentit et s'éteint |
| Scatter | 0 : le même éclat répété ; 1 : des éclats pris tout autour, et certains à l'envers |
| Fall | la rafale descend (ou monte) d'une octave sur sa durée |
| Snap | la longueur des coups, de 30 ms à 750 ms |
| Tune | hauteur de tout le break, en demi-tons |
| Level | volume |
| New edits | une autre graine : la même mesure se brise d'une autre façon |

Les cassures sont **écrites, pas tirées au hasard** : la même note au même endroit de la mesure se brise toujours pareil. Un pattern MIDI de deux mesures bouclé reste donc un vrai pattern (et « New edits » en propose une autre version).

## Tests
`tests/dsp_test.cpp` (sans JUCE) : le découpeur retrouve les coups du break fourni et ne crée rien de faux, classe grosses caisses < caisses claires < charleys, les touches graves jouent des sons sombres, les rafales tombent exactement sur la grille du tempo, accélèrent et ralentissent, tombent d'une octave, la même note se brise toujours pareil, une touche rejouée coupe la rafale précédente.
