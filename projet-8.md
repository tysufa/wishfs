Projet PNL du groupe 8
======================

# Architecture de la solution
[comment]: <> (1 ou 2 paragraphes qui expliquent vite fait vos choix d’implémentation et de design.)

# Travail réalisé

## 1.2 Reimplementation of the read and the write functions
Testé et fonctionnel, suppression des autres moyens d'iterragir avec le fs.

## 1.3 Extent-based index block (1.4 read ; 1.5 write)
Testé et fonctionnel, rajoute bien les blocs à l'extent précédente s'ils se suivent.

## 1.6 Contiguous block allocator
Testé et fonctionnel, renvoie dès qu'il trouve une suite de blocs de taille suffisante.
Amélioré plus tard pour :
 - sélectionner le plus grand bloc de taille inférieur à la taille demandée
 - sélectionner les blocs qui suivent le dernier bloc alloué.

## 1.7 Write-time block reservation
Testé et fonctionnel.
Algo:
    Si pas de réservation:
    on essaye d'allouer l'estimation de blocs nécessaires + la taille d'allocation (on fait au plus grand pour réduire la fragmentation)
Les blocs pré alloués sont réservés et libérés quand il faut (unmount, unlink, etc...).

## 1.7.4 Garbage collector
Testé et fonctionnel. libère la place quand il en manque. S'arrète dès qu'il a libéré suffisament de place consécutivement.

## 1.8 Sysfs statistics
- Testé et fonctionnel
- Pas complètement fonctionnel (bug, etc..), dans ce cas, précisez les problèmes
- Non traité

## 1.9 Sparse files and holes (read)
Testé et fonctionnel, lis bien des 0 alors qu'aucun bloc physique n'a été alloué.

## 1.9 Sparse files and holes (write in hole)
Testé et fonctionnel
utilise la logique de bloc reservation pendant le remplissage des holes.

## 1.10 Bonus: File defragmentation
- Testé et fonctionnel
- Pas complètement fonctionnel (bug, etc..), dans ce cas, précisez les problèmes
- Non traité

## 1.11 MEGA Bonus: advanced block allocator
- Testé et fonctionnel
- Pas complètement fonctionnel (bug, etc..), dans ce cas, précisez les problèmes
- Non traité

# Remarques
[comment]: <> (Vous pouvez rajouter ici tout ce qui vous paraît pertinent et qui n’apparaît pas dans les autres sections.)
