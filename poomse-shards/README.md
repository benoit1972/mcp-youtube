# Poomse Shards

Un instrument Audio Unit de batterie pour le style de **Telefon Tel Aviv** : une boucle qui se défait en jouant, des rampes de répétition à la Stutter Edit (la cadence monte dans la grille, la hauteur monte, ça gonfle ; ou ça s'étire, ralentit et s'éteint), le tout **toujours sur la grille** du morceau.

Glisse une boucle de batterie (ou n'importe quel son) n'importe où sur la fenêtre, ou garde celle fournie : un « poum, chak » avec une charley en l'air. Le transport de Live doit tourner.

## Deux zones
### La lecture (7 pads, touches blanches de C3 à B3)
Quelle version de la boucle joue. Un seul pad à la fois, et il **reste sélectionné** (bouton **Latch** allumé). Latch éteint : le pad ne joue que tant qu'il est tenu, et on peut en empiler plusieurs.

| Pad | Ce que ça fait |
|---|---|
| **Original** | la boucle exactement telle qu'elle est |
| **Break** | la boucle défaite au hasard, mesure après mesure : coups échangés, décalés, à l'envers, fantômes, rolls, un fill en fin de phrase |
| **Ratchet** | les caisses claires et les charleys deviennent des rolls qui accélèrent |
| **Flip** | les temps 2 et 4 sont joués à l'envers |
| **Swap** | grosses caisses et caisses claires échangées |
| **Shove** | la boucle décalée d'une croche |
| **Ghost** | des coups fantômes dans tous les creux |

### Les fills (7 pads, touches blanches de C1 à B1)
Ils prennent la place de la lecture **exactement tant que la touche est enfoncée**. Tu relâches : la lecture sélectionnée est de retour tout de suite. Une seconde de touche = une seconde de fill.

| Pad | Ce que ça fait |
|---|---|
| **Roll up** | la rampe de Stutter Edit : la cadence monte (1/8 → 1/64), la hauteur monte, le niveau gonfle ; tenu, ça reste à pleine vitesse |
| **Roll down** | la rampe qui s'étire : de plus en plus lent, de plus en plus grave, ça s'éteint |
| **Stutter** | le dernier coup, répété |
| **Reverse** | le temps qui vient de passer, à l'envers |
| **Stop** | la boucle ralentit et s'arrête comme une bande |
| **Hats** | une rafale de charleys |
| **Drop** | silence tant que c'est tenu ; relâché : retour avec un coup dur |

Chaque touche noire fait comme la touche blanche juste en dessous. Les pads de la fenêtre se jouent à la souris, comme les touches.

## Les réglages
| | |
|---|---|
| Mutate | la part de la boucle dont le pad de lecture s'empare. Le bas du potard donne de petites retouches discrètes, le haut une tout autre boucle |
| Ramp | l'ampleur des rampes : vitesse atteinte et hauteur parcourue |
| Groove | 1 : les coups gardent leur feeling ; 0 : tout est calé sur la grille |
| Tune | hauteur de tout le break, en demi-tons |
| Start on | un pad de lecture (ou un fill) démarre immédiatement, ou sur le prochain 1/4, 1/8, 1/16. Le relâchement d'un fill, lui, est immédiat |
| Loop length | 1, 2, 4 ou 8 mesures (devinée au chargement) |
| Level | volume |
| **Reset** (deux clics) | **remet tous les réglages comme à l'ouverture du plugin** : Mutate, Ramp, Groove, Tune, Start on, Level, Latch, version, et la longueur de boucle (celle qui correspond au son chargé). Le son chargé reste en place |
| **Reroll** | **une autre version.** Break et les rolls ne sont pas tirés au hasard à chaque lecture : pour un même morceau ils donnent toujours le même résultat (donc ton export est identique à ce que tu entends). Reroll relance les dés. Le numéro de version est affiché sous le bouton |

## Déposer un son
Glisse un fichier **depuis le Finder** n'importe où sur la fenêtre : le nom apparaît dès que tu survoles, puis « LOADING… », puis « LOADED » avec le détail du fichier (durée, canaux, fréquence, bits, nombre de coups trouvés) ou la raison précise de l'échec. Le chargement se fait en arrière-plan et réessaie pendant une dizaine de secondes : un fichier qui arrive encore d'iCloud n'est pas lisible au premier essai. (Les fichiers glissés depuis le navigateur de Live ne sont pas transmis aux fenêtres des plugins : c'est une limite de Live.) Les sons rythmiques sont coupés à chaque attaque ; sans attaques (nappe, voix tenue), le son est coupé en 16 morceaux réguliers.

## Les fichiers AIFF
Le plugin embarque son **propre lecteur AIFF / AIFF-C** pour les fichiers que la bibliothèque audio du système refuse : AIFF 8, 16, 24, 32 bits, AIFF-C en little-endian (« sowt ») ou en flottant (« fl32 », « fl64 »), blocs en plus avant le format, SSND décalé ou tronqué. Si un fichier reste illisible (une compression propriétaire comme « ima4 »), le message affiché dit exactement ce que c'est.

## Tests
`tests/dsp_test.cpp` (sans JUCE) : découpe du break fourni ; Original intacte quels que soient les réglages, au bon tempo et à la bonne hauteur ; Break sur la grille (6 000 événements) ; mêmes dés pour la même mesure ; de Mutate 0,1 (5 % des coups changent) à 1 (96 %) ; rampes qui montent ou descendent ; chaque pad de lecture fait exactement ce qu'il dit (Flip : 40 coups sur 40 à l'envers aux temps 2 et 4 ; Swap : 16 grosses caisses sur 16 deviennent des caisses claires...) ; Latch ; chaque fill dure exactement le temps de la touche, rien après le relâchement, la lecture revient dans le pas qui suit ; Drop silencieux ; transport ; 500 touches aléatoires.
