# biceps — Bel Interpréteur de Commandes des Élèves de Polytech Sorbonne

NOM: CHIKHI, NAIMI

PRENOM: Madjid, Nassim

## Structure du projet

```
.
├── biceps.c      # Programme principal : boucle interactive (readline, historique, SIGINT)
├── gescom.c/.h   # Librairie de gestion des commandes (analyse, internes, externes)
├── creme.c/.h    # Librairie réseau : protocole BEUIP, liste chaînée des contacts
├── Makefile
├── .gitignore
└── README.md
```

## Compilation

```bash
make              # produit le binaire ./biceps
make memory-leak  # produit ./biceps-memory-leaks (compilé avec -g -O0 pour valgrind)
make trace        # compile avec -DTRACE pour les messages de debug
make clean        # supprime tous les binaires et .o
```

## Utilisation

```
./biceps
```

### Commandes internes

| Commande | Description |
|---|---|
| `exit` | Quitte biceps (arrête aussi le serveur BEUIP si actif) |
| `cd [répertoire]` | Change le répertoire courant |
| `pwd` | Affiche le répertoire courant |
| `vers` | Affiche la version de biceps |
| `beuip start <pseudo>` | Démarre le serveur UDP BEUIP avec le pseudo donné |
| `beuip stop` | Arrête le serveur UDP BEUIP (envoie un message de déconnexion) |
| `beuip list` | Affiche la liste des utilisateurs connectés sur le réseau |
| `beuip message <pseudo> <message>` | Envoie un message privé à un utilisateur |
| `beuip message all <message>` | Envoie un message à tous les utilisateurs connectés |

### Commandes séquentielles

Il est possible d'enchaîner plusieurs commandes sur la même ligne avec `;` :

```
ls -l ; pwd ; vers
```

### Sortie du programme

- **Control-D** (EOF) : quitte proprement biceps
- **Control-C** : n'interrompt pas biceps, réaffiche le prompt

## Protocole BEUIP

Le protocole BEUIP (BEUI over IP) utilise des datagrammes UDP sur le port **9998**.

Format d'un message : `[code(1 octet)][BEUIP(5 octets)][payload]`

| Code | Signification |
|---|---|
| `'0'` | Déconnexion (broadcast) |
| `'1'` | Annonce de présence (broadcast) |
| `'2'` | Accusé de réception |
| `'9'` | Message privé entrant |

L'adresse broadcast du réseau TPOSUSER (`192.168.88.255`) est définie dans un unique `#define BEUIP_BROADCAST` dans `creme.h`.

## Architecture multi-thread (TP3)

Le serveur UDP tourne dans un thread dédié (`serveur_udp`) lancé par `beuip start`. Il partage avec le thread principal (interpréteur de commandes) la liste chaînée des contacts, protégée par un mutex POSIX.

La liste est triée par ordre alphabétique des pseudos et supporte l'ajout, la suppression et la recherche en O(n).

## Vérification des fuites mémoire

```bash
make memory-leak
valgrind --leak-check=full --track-origins=yes --errors-for-leak-kinds=all \
         --error-exitcode=1 ./biceps-memory-leaks
```

## Auteurs

Groupe SERIANE — Polytech Sorbonne, 2026
