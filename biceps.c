#include "gescom.h"
#include "creme.h"

#include <readline/history.h>
#include <readline/readline.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HISTORY_FILE ".biceps_history"

/* ─── Historique persistant ──────────────────────────────────────────────── */
static char history_path[512];

static void init_history(void)
{
    const char *home = getenv("HOME");
    if (!home)
        home = ".";
    snprintf(history_path, sizeof(history_path), "%s/%s", home, HISTORY_FILE);
    read_history(history_path);
}

static void save_history(void)
{
    write_history(history_path);
}

/* ─── Gestion de SIGINT (Control-C) : ne pas quitter ─────────────────────── */
static void handler_sigint(int sig)
{
    (void)sig;
    printf("\n");
    rl_on_new_line();
    rl_replace_line("", 0);
    rl_redisplay();
}

/* ─── Construction du prompt dynamique ───────────────────────────────────── */
static char *make_prompt(void)
{
    char        machine[256];
    const char *user = getenv("USER");
    if (!user)
        user = "inconnu";
    if (gethostname(machine, sizeof(machine)) != 0)
        strncpy(machine, "localhost", sizeof(machine));

    char suffix = (getuid() == 0) ? '#' : '$';

    /* Taille : user + '@' + machine + suffix + ' ' + '\0' */
    size_t len  = strlen(user) + strlen(machine) + 4;
    char  *prom = malloc(len);
    if (!prom)
        return NULL;
    snprintf(prom, len, "%s@%s%c ", user, machine, suffix);
    return prom;
}

/* ─── Exécution d'une ligne (peut contenir des ';') ─────────────────────── */
static void execute_line(char *line)
{
    char *ptr = line;
    char *cmd;

    while ((cmd = strsep(&ptr, ";")) != NULL) {
        /* Saute les blancs de début */
        while (*cmd == ' ' || *cmd == '\t')
            cmd++;
        if (*cmd == '\0')
            continue;

        int n = analyseCom(cmd);
        if (n == 0) {
            libereMots();
            continue;
        }

        char **mots = getMots();

#ifdef TRACE
        printf("[TRACE] commande='%s' (%d mots)\n", mots[0], n);
#endif

        if (!execComInt(n, mots))
            execComExt(mots);

        libereMots();
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * main
 * ═══════════════════════════════════════════════════════════════════════════ */
int main(void)
{
    signal(SIGINT, handler_sigint);
    majComInt();
    init_history();

#ifdef TRACE
    listeComInt();
#endif

    while (1) {
        char *prompt = make_prompt();
        char *line   = readline(prompt);
        free(prompt);

        /* EOF (Control-D) → sortie propre */
        if (!line) {
            printf("\nAu revoir !\n");
            if (creme_running())
                creme_stop();
            break;
        }

        if (*line == '\0') {
            free(line);
            continue;
        }

        /* Historique : évite les doublons consécutifs */
        HIST_ENTRY *last = (history_length > 0)
                           ? history_get(history_base + history_length - 1)
                           : NULL;
        if (!last || strcmp(last->line, line) != 0)
            add_history(line);

        execute_line(line);
        free(line);
    }

    save_history();
    return EXIT_SUCCESS;
}
