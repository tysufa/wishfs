Projet PNL du groupe 8
======================

# Architecture de la solution
[comment]: <> (1 ou 2 paragraphes qui expliquent vite fait vos choix d’implémentation et de design.)

# Travail réalisé

## 1.2 Reimplementation of the read and the write functions
Testé et fonctionnel, suppression des autres moyens d'iterragir avec le fs.
Algo pour read:
    - On ouvre le block d'index de notre inode
    - On boucle jusqu'à avoir tout lu (count == 0), on être revenu plus tôt à cause d'une erreur
        - à chaque tour de boucle on calcul le numéro du bloc à lire et l'offset en fonction de ppos
        - On sb_bread le bloc actuel, puis on copie dans le buffer utilisateur les datas du bloc en prenant en compte l'offset
        - On libère notre bloc, puis on update ppos, count et le nombre d'octets lus
    - On libère notre bloc d'index

Algo pour write:
    - On ouvre le block d'index de notre inode
    - On boucle jusqu'à avoir tout lu (count == 0), on être revenu plus tôt à cause d'une erreur
        - à chaque tour de boucle on calcul le numéro du bloc à lire et l'offset en fonction de ppos
        - On sb_bread le bloc actuel, puis on copie dans le buffer utilisateur les datas du bloc en prenant en compte l'offset
        - On libère notre bloc, puis on update ppos, count et le nombre d'octets lus
    - On libère notre bloc d'index


## 1.3 Extent-based index block (1.4 read ; 1.5 write)
Testé et fonctionnel, rajoute bien les blocs à l'extent précédente s'ils se suivent.

## 1.6 Contiguous block allocator
Testé et fonctionnel, renvoie dès qu'il trouve une suite de blocs de taille suffisante.
Algo :
    - On lock la bitmap avec un mutex ajouté dans ouichefs_sb_info
    - On parcourt la bfree_bitmap jusqu'à avoir autant de blocs libre que demandé on d'atteindre la fin des blocks libre
        - quand on trouve un bloc libre pour la première fois, on note son numéro, puis on continu jusqu'à arriver à requested où ne plus être contigu
    - Une fois une plage contigu obtenu, on clear tous les bits qui sont maintenant occupés
    - On unlock, on update nr_free_blocks, etc

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

Pour pouvoir récupérer le super block dans nos fonctions show, on à créé une structure custom qui contient le super block et struct kobj_attribute, on passe kobj_attribute à notre fonction sysfs de façon classique, puis on utilise container of pour récupérer le super block
Nous avons intégré la liste des custom struct (donc qui contient les attributs de chaque fonction sysfs) dans ouichefs_sb_info en tant que tableau statique, pour pouvoir y accéder depuis la partition actuelle
De même on à ajouté un kobj à notre ouichefs_sb_info pour séparer nos fonctions de nos partitions

Les sysfs les plus complexes sont ceux qui doivent traiter les extents : 
- On doit flush toutes nos inodes dirty en mémoire avant de commencer à itérer
- Ensuite on parcours toutes les inodes du disque depuis l'inode store
- On vérifie si on est sur un fichier ou un dossier selon le cas à traiter, puis on récupère l'index de notre inode
- On parcours ensuite tous les extents du fichier à partir de cet index
- Enfin on peut voir le contenu de nos extents individuel pour pouvoir les compter, faire un average, etc.

Nous avons rajouté un kobject dans le sysfs pour pouvoir afficher le nombre de blocs total pour simplifier les tests de sanités depuis l'espace user
On récupère le super block pour sysfs a partir du dentry dans ouichefs_mount

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
Non traité

## 1.11 MEGA Bonus: advanced block allocator
Non traité

# Remarques
[comment]: <> (Vous pouvez rajouter ici tout ce qui vous paraît pertinent et qui n’apparaît pas dans les autres sections.)
Les tests ont été réalisés de plusieurs manières :
- test en ligne de commande au fil du développement,
- tests dans des scipts C dédiés (ex: ioctl),
- suite de tests C dans test_monothread.c (la majorité des tests s'y trouvent),
- tests avec concurrence dans test_multithread.c (fonctionnalité 1.7).
Les tests ont été réalisés indépendamment du développement (par des personnes différentes).
Le code de test étant récurrent, il a été factorisé par des fonctions et macros.
