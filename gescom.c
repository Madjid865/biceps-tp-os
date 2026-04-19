#include "gescom.h"
#include "creme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* ══════════════════════════════════════════════════════════════════════════
 * Analyse de la commande
 * ══════════════════════════════════════════════════════════════════════════ */

static char **Mots  = NULL;
static int    NMots = 0;

char **getMots(void)  { return Mots;  }
int    getNMots(void) { return NMots; }

void libereMots(void)
{
    if (Mots) {
        for (int i = 0; i < NMots; i++)
            free(Mots[i]);
        free(Mots);
        Mots  = NULL;
        NMots = 0;
    }
}

int analyseCom(char *b)
{
    libereMots();

    if (!b || *b == '\0')
        return 0;

    char *copie = strdup(b);
    if (!copie) {
        perror("strdup analyseCom");
        return 0;
    }

    /* 1er passage : comptage des mots */
    char *tmp = strdup(copie);
    if (!tmp) {
        perror("strdup analyseCom count");
        free(copie);
        return 0;
    }
    int   count = 0;
    char *ptr   = tmp;
    char *tok;
    while ((tok = strsep(&ptr, " \t\n")) != NULL)
        if (*tok != '\0')
            count++;
    free(tmp);

    if (count == 0) {
        free(copie);
        return 0;
    }

    /* count+1 slots : tableau null-terminé pour execvp */
    Mots = malloc((size_t)(count + 1) * sizeof(char *));
    if (!Mots) {
        perror("malloc analyseCom");
        free(copie);
        return 0;
    }

    /* 2e passage : remplissage */
    ptr   = copie;
    NMots = 0;
    while ((tok = strsep(&ptr, " \t\n")) != NULL) {
        if (*tok == '\0')
            continue;
        Mots[NMots] = strdup(tok);
        if (!Mots[NMots]) {
            perror("strdup analyseCom fill");
            break;
        }
        NMots++;
    }
    Mots[NMots] = NULL; /* sentinelle pour execvp */

    free(copie);
    return NMots;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Table des commandes internes
 * ══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    const char *nom;
    cmd_fn      fn;
} CmdInterne;

static CmdInterne tabCom[NBMAXC];
static int        nbCom = 0;

void ajouteCom(const char *nom, cmd_fn fn)
{
    if (nbCom >= NBMAXC) {
        fprintf(stderr,
                "ERREUR : table des commandes internes pleine (max %d)\n",
                NBMAXC);
        exit(EXIT_FAILURE);
    }
    tabCom[nbCom].nom = nom;
    tabCom[nbCom].fn  = fn;
    nbCom++;
}

void listeComInt(void)
{
    printf("Commandes internes (%d) :\n", nbCom);
    for (int i = 0; i < nbCom; i++)
        printf("  %s\n", tabCom[i].nom);
}

int execComInt(int n, char **mots)
{
    if (n == 0 || !mots || !mots[0])
        return 0;
    for (int i = 0; i < nbCom; i++) {
        if (strcmp(tabCom[i].nom, mots[0]) == 0) {
            tabCom[i].fn(n, mots);
            return 1;
        }
    }
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Commandes externes
 * ══════════════════════════════════════════════════════════════════════════ */

int execComExt(char **mots)
{
    if (!mots || !mots[0])
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        execvp(mots[0], mots);
        perror(mots[0]);
        exit(EXIT_FAILURE);
    }

    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Fonctions des commandes internes
 * ══════════════════════════════════════════════════════════════════════════ */

static int cmd_exit(int n, char **p)
{
    (void)n; (void)p;
    printf("Au revoir !\n");
    if (creme_running())
        creme_stop();
    exit(EXIT_SUCCESS);
    return 0;
}

static int cmd_cd(int n, char **p)
{
    const char *dir = (n >= 2) ? p[1] : getenv("HOME");
    if (!dir)
        dir = "/";
    if (chdir(dir) != 0)
        perror("cd");
    return 0;
}

static int cmd_pwd(int n, char **p)
{
    (void)n; (void)p;
    char buf[4096];
    if (getcwd(buf, sizeof(buf)))
        printf("%s\n", buf);
    else
        perror("pwd");
    return 0;
}

static int cmd_vers(int n, char **p)
{
    (void)n; (void)p;
    printf("biceps version 3.0 — Bel Interpréteur de Commandes"
           " des Elèves de Polytech Sorbonne\n");
    return 0;
}

/* ─── Aide beuip ─────────────────────────────────────────────────────────── */
static void beuip_usage(void)
{
    printf("Usage:\n"
           "  beuip start <pseudo>           démarre le serveur BEUIP\n"
           "  beuip stop                     arrête le serveur BEUIP\n"
           "  beuip list                     liste les utilisateurs\n"
           "  beuip message <pseudo> <msg>   envoie un message privé\n"
           "  beuip message all <msg>        envoie un message à tous\n");
}

/* ─── Reconstitue un message à partir de tokens p[start..n-1] ───────────── */
static void joinTokens(char *out, size_t outsz, char **p, int start, int n)
{
    out[0] = '\0';
    for (int i = start; i < n; i++) {
        if (i > start)
            strncat(out, " ", outsz - strlen(out) - 1);
        strncat(out, p[i], outsz - strlen(out) - 1);
    }
}

/* ─── beuip ─────────────────────────────────────────────────────────────── */
static int cmd_beuip(int n, char **p)
{
    if (n < 2) {
        beuip_usage();
        return 1;
    }

    if (strcmp(p[1], "start") == 0) {
        if (n < 3) {
            printf("Usage: beuip start <pseudo>\n");
            return 1;
        }
        creme_start(p[2]);
        return 0;
    }

    if (strcmp(p[1], "stop") == 0) {
        creme_stop();
        return 0;
    }

    if (strcmp(p[1], "list") == 0) {
        listeElts();
        return 0;
    }

    if (strcmp(p[1], "message") == 0) {
        if (n < 4) {
            printf("Usage: beuip message <pseudo|all> <message>\n");
            return 1;
        }
        char msg[512];
        joinTokens(msg, sizeof(msg), p, 3, n);

        if (strcmp(p[2], "all") == 0)
            beuipSendAll(msg, creme_pseudo());
        else
            beuipSendMsg(p[2], msg);
        return 0;
    }

    printf("Sous-commande beuip inconnue : %s\n", p[1]);
    beuip_usage();
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Enregistrement de toutes les commandes internes
 * ══════════════════════════════════════════════════════════════════════════ */

void majComInt(void)
{
    ajouteCom("exit",  cmd_exit);
    ajouteCom("cd",    cmd_cd);
    ajouteCom("pwd",   cmd_pwd);
    ajouteCom("vers",  cmd_vers);
    ajouteCom("beuip", cmd_beuip);
}
