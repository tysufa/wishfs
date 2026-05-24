# Projet PNL — Image and Kernel Build

Ce dépôt contient des outils et un arbre Linux préconfiguré pour assembler :
- un noyau Linux local (dans `linux-6.5.7`),
- un module/utilitaire `ouichefs` (sous `ouichefs/`),
- des images disque de test (ex. `test.img`, `myHome.img`, `pnl-tp-2021.img`).

Objectif
- Fournir un environnement reproductible pour construire un noyau et générer des images de fichiers.

Prérequis
- GNU make, gcc et outils de compilation pour le noyau.
- `dd`, `xz`, `cp`, `mkfs.ext4`, `tune2fs`.
- Utilitaire `ouichefs` (le binaire de création d'image est produit par `make` dans `ouichefs/mkfs`).

Principaux targets (dans la racine)
- `make` — construit `kernel`, `ouichefs` et les images (`test.img`, `myHome.img`, `pnl-tp-2021.img`) selon les règles du `Makefile`.
- `make kernel` — lance la compilation du noyau en utilisant l'arbre `linux-6.5.7`.
- `make ouichefs` — construit la partie `ouichefs` (dépend du noyau si nécessaire).
- `make clean` — nettoie la compilation noyau et supprime les images temporaires.

Variables configurables
- `nproc` (valeur par défaut : résultat de `nproc`) — nombre de jobs parallèles pour `make -j`.
- `kdir` (par défaut `./linux-6.5.7`) — chemin vers l'arbre source du noyau.
- `ouichefsdir` — chemin vers le sous-projet `ouichefs`.
- `test_disk`, `home_disk`, `boot_disk` — noms des images générées.

Exemples d'utilisation

Construire tout (par défaut) :

```sh
make
```

Construire uniquement le noyau avec 8 jobs :

```sh
make kernel nproc=8
```

Générer uniquement l'image de test (utilise le binaire `ouichefs/mkfs/mkfs.ouichefs`) :

```sh
make test.img
```

Nettoyer la construction du noyau :

```sh
make clean
```

Structure importante
- [linux-6.5.7](linux-6.5.7) : arbre source du noyau fourni (voir COPYING et README du noyau).
- [ouichefs](ouichefs) : code utilisateur pour le système de fichiers et outils de création d'image.
- `Makefile` : orchestration principale pour construire noyau, outils et images.

Contribuer
- Ouvrez une issue pour proposer des améliorations ou signaler des problèmes.
- Privilégiez des commits petits et ciblés ; documentez toute nouvelle variable du `Makefile`.

Licence
- Voir le fichier de licence du noyau `linux-6.5.7/COPYING` pour les composants du noyau.
- Les composants utilisateurs (ex. `ouichefs`) contiennent leurs propres mentions de licence dans leurs répertoires.

Contact
- Auteur / Mainteneur : voir le dépôt ou contactez l'équipe PNL.
