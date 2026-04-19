#ifndef GESCOM_H
#define GESCOM_H

/* Nombre maximum de commandes internes */
#define NBMAXC 20

/* Type d'une fonction de commande interne */
typedef int (*cmd_fn)(int, char **);

/* ─── Analyse de la commande ─────────────────────────────────────────────── */
int    analyseCom(char *b);
char **getMots(void);
int    getNMots(void);
void   libereMots(void);

/* ─── Commandes internes ─────────────────────────────────────────────────── */
void ajouteCom(const char *nom, cmd_fn fn);
void majComInt(void);
void listeComInt(void);
int  execComInt(int n, char **mots);

/* ─── Commandes externes ─────────────────────────────────────────────────── */
int execComExt(char **mots);

#endif /* GESCOM_H */
