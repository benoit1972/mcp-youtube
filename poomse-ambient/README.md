# Poomse Ambient

Instrument AU. Tu tiens **une seule note** dans un clip : sa hauteur est la tonalité, sa durée la longueur du morceau. Le plugin compose le reste.

## Comment il compose

- une **progression de 4 accords** (2 mesures chacun) dans le mode choisi (majeur, mineur, dorien, mixolydien) ; elle change toutes les deux fois ;
- un **pad** qui joue la fondamentale de chaque accord, une **basse** qui la suit, un **violon** qui tient une note de l'accord et bouge par petits pas ;
- un **motif de piano** de 4 à 6 notes qui revient à chaque accord (il est écrit *dans* l'accord, donc il suit l'harmonie) et change un peu à chaque tour ;
  ses **cloches** en sont l'écho, 3 temps plus tard : les deux se décalent contre la mesure ;
- des **scènes** (légère, profonde, calme), un arc sur toute la durée, un **lieu** (forêt) en dessous ;
- tout est sur la grille de croches ; chaque note est choisie parmi celles qui ne frottent jamais avec l'accord (testé : aucun demi-ton entre deux notes qui sonnent ensemble).

La graine écrit le morceau : *Reroll* en choisit un autre et le garde ; *New each time* en fait un nouveau à chaque lecture.

## Les quatre sons

Piano (court : mélodie, cloches, basse), Violin (long), Pad (accord tenu), Forest (un lieu). Fabriqués dans le code, rien n'est enregistré.
Dépose ton propre sample sur un pad pour le remplacer (clic droit : l'original). **Amount** règle la quantité de chaque son dans le morceau.
