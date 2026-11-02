# Poomse Shards

Un instrument Audio Unit de batterie pour le style de **Telefon Tel Aviv** : une boucle qui se défait en jouant, des rampes de répétition à la Stutter Edit (la cadence monte dans la grille, la hauteur monte, ça gonfle ; ou ça s'étire, ralentit et s'éteint), le tout **toujours sur la grille** du morceau.

## Comment s'en servir
Tu glisses une boucle de batterie sur la fenêtre (ou « Load »), ou tu gardes celle fournie : un « poum, chak » avec une charley en l'air. Le transport de Live doit tourner.

**Tiens un C pour toute la durée du clip : la boucle joue.** Pose ensuite les autres touches par-dessus : elles prennent le dessus **tant qu'elles sont enfoncées**, puis la boucle revient, pile sur la grille.

| Touche | Nom | Ce que ça fait |
|---|---|---|
| C | **Loop** | la boucle exactement telle qu'elle est |
| D | **Break** | la boucle défaite, mesure après mesure : coups échangés, décalés, à l'envers, fantômes, rolls, un fill en fin de phrase. **Mutate** dit jusqu'où |
| E | **Roll up** | la rampe de Stutter Edit : la cadence monte (1/8 → 1/64), la hauteur monte, le niveau gonfle. Tenu : reste à pleine vitesse |
| F | **Roll down** | la rampe qui s'étire : de plus en plus lent, de plus en plus grave, ça s'éteint |
| G | **Reverse** | le temps qui vient de passer, à l'envers, encore et encore |
| A | **Stop** | la boucle ralentit et s'arrête comme une bande. Tenu : reste arrêtée |
| B | **Drop** | silence tant que la touche est tenue ; quand tu la lâches, la boucle revient avec un coup dur |

Chaque touche noire fait comme la touche blanche juste en dessous, à toutes les octaves. On peut tenir plusieurs touches de boucle en même temps (Loop + Break = deux boucles superposées).

## Les réglages
| | |
|---|---|
| Mutate | jusqu'où Break défait la boucle. Le bas du potard donne de petites retouches discrètes, le haut une tout autre boucle |
| Ramp | l'ampleur des rampes : vitesse atteinte et hauteur parcourue |
| Groove | 1 : les coups gardent leur feeling ; 0 : tout est calé sur la grille |
| Tune | hauteur de tout le break, en demi-tons |
| Start on | une touche démarre immédiatement, ou sur le prochain 1/4, 1/8, 1/16 |
| Loop length | 1, 2, 4 ou 8 mesures (devinée au chargement) |
| Level | volume |
| **Reroll** | **une autre version.** Break et les rolls ne sont pas tirés au hasard à chaque lecture : pour un même morceau ils donnent toujours le même résultat (donc ton export est identique à ce que tu entends). Reroll relance les dés : le même réglage, un autre mélange. Le numéro de version est affiché sous le bouton |

## N'importe quel son
Glisse un fichier audio n'importe où sur la fenêtre, ou utilise « Load » : une boucle de batterie, mais aussi un piano, une voix, un field recording. Le son est coupé à chaque attaque ; s'il n'y en a pas, il est coupé en 16 morceaux réguliers.

## Tests
`tests/dsp_test.cpp` (sans JUCE) : découpe des coups du break fourni ; Loop intacte quels que soient les réglages, au bon tempo et à la bonne hauteur ; Break sur la grille (6 000 événements) ; mêmes dés pour la même mesure ; de Mutate 0,1 (5 % des coups changent) à 1 (96 %) ; rampes qui montent (cadence, hauteur, niveau) ou descendent ; chaque geste prend la place de la boucle, reste sur la grille, dure tant que la touche est tenue puis rend la boucle ; Drop silencieux ; plusieurs touches ; arrêt du transport ; 500 touches aléatoires.
