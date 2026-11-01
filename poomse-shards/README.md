# Poomse Shards

Un instrument Audio Unit de batterie pour le style de **Telefon Tel Aviv** : une boucle qui se défait en jouant, des rafales à la Stutter Edit, des coups retournés, plies, décalés, des fantômes, des fills, le tout **toujours sur la grille** du morceau.

Ce n'est pas un beat repeat. **Une touche joue la boucle**, et la boucle est réécrite à chaque mesure : plus ou moins détruite, jamais déplacée hors du rythme.

## Le principe
Tu glisses une boucle de batterie (ou tu gardes celle fournie : un « poum, chak » avec une charley en l'air). Le plugin la découpe à chaque attaque et la joue calée sur le tempo de Live. **Maintiens une touche** : la boucle joue. Chaque touche est une personnalité différente, et **on peut en tenir plusieurs à la fois** (plusieurs versions de la boucle les unes sur les autres).

Les changements sont **écrits, pas tirés au hasard** : la même mesure d'un même morceau se défait toujours de la même façon, et chaque mesure est différente de la précédente. « New edits » change de graine.

## Les touches
**C3 à B3 : 12 personnalités** (pads du milieu) : Loop (quasi fidèle), Flip (à l'envers), Ratchet (roulements), Swap (les coups changent de place), Hollow (trous et rentrées), Bend (hauteurs, tape stop), Shove (décalages), Ghost (coups fantômes), Mirror (le temps à rebours), Storm (tout à la fois), Stretch (coups lents, étirés), Drift (calme au début de la mesure, sauvage à la fin).
La vélocité accentue ou adoucit l'effet.

**C1 à B1 : 12 fills** (pads du haut). Un fill prend la place de la boucle pendant un ou deux temps, puis la rend : Roll, Big roll (roulement qui accélère et enfle), Roll down (qui ralentit), Hats (rafale de charleys), Triplets (triolets de grosse caisse), Rewind (le dernier temps à l'envers), Stop (tape stop), Drop (silence puis retour en force), Stutter (le dernier coup répété), Double (la boucle doublée en décalé), Cascade (un coup qui dégringole), Scatter (éclats pris partout).

## Les réglages
**Ce qui est abîmé** : Mutate (jusqu'où ; le bas du potard donne de toutes petites retouches, le haut une autre boucle), Shuffle (coups échangés, décalés, miroir), Reverse (coups à l'envers), Ramp (rampes de répétition à la Stutter Edit : positif, la cadence monte dans la grille et enfle ; négatif, elle descend et s'éteint), Pitch (sauts de hauteur, glissandos, tape stop), Ghosts (coups fantômes dans les creux), Holes (trous, puis rentrée plus forte).
**La boucle** : Groove (1 : les coups gardent leur feeling ; 0 : tout est calé sur la grille), Gate (longueur des répétitions), Tune, Start on (les notes démarrent à la prochaine : tout de suite, 1/4, 1/8, 1/16), Loop length (1, 2, 4 ou 8 mesures ; deviné au chargement), Level.

## Tests
`tests/dsp_test.cpp` (sans JUCE) : le découpeur retrouve les 17 coups du break fourni et les classe (grosse caisse < caisse claire < charley) ; avec Mutate à 0 la boucle sort telle qu'elle est (même à un autre tempo, avec la même hauteur) ; de Mutate 0,1 (1 % des coups changent) à Storm au maximum (76 %) ; 10 000 événements testés, tous sur la grille (1/64 ou triolets de 1/48) ; mêmes dés pour la même mesure, graine différente = autre version ; rampes qui accélèrent / ralentissent avec swell / fade ; douze fills qui restent dans leur fenêtre, prennent la place de la boucle puis la rendent ; Drop parfaitement silencieux ; plusieurs touches ; arrêt de la lecture ; 400 gestes aléatoires sans problème.
