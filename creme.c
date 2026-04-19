#include "creme.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* ─── Liste chaînée des contacts ─────────────────────────────────────────── */
static struct elt      *liste       = NULL;
static pthread_mutex_t  liste_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ─── État du serveur UDP ─────────────────────────────────────────────────── */
static pthread_t udp_thread;
static int       udp_running              = 0;
static int       udp_sock                 = -1;
static char      mon_pseudo[LPSEUDO + 1]  = "";

/* ══════════════════════════════════════════════════════════════════════════
 * Gestion de la liste chaînée (triée alphabétiquement sur le pseudo)
 * ══════════════════════════════════════════════════════════════════════════ */

void ajouteElt(const char *pseudo, const char *adip)
{
    if (!pseudo || pseudo[0] == '\0')
        return;

    pthread_mutex_lock(&liste_mutex);

    /* Mise à jour si l'adresse IP est déjà connue */
    struct elt *cur = liste;
    while (cur) {
        if (strcmp(cur->adip, adip) == 0) {
            strncpy(cur->nom, pseudo, LPSEUDO);
            cur->nom[LPSEUDO] = '\0';
            pthread_mutex_unlock(&liste_mutex);
            return;
        }
        cur = cur->next;
    }

    struct elt *nouv = malloc(sizeof(struct elt));
    if (!nouv) {
        pthread_mutex_unlock(&liste_mutex);
        perror("malloc ajouteElt");
        return;
    }
    strncpy(nouv->nom,  pseudo, LPSEUDO);
    nouv->nom[LPSEUDO] = '\0';
    strncpy(nouv->adip, adip, 15);
    nouv->adip[15] = '\0';
    nouv->next = NULL;

    /* Insertion triée par pseudo (ordre alphabétique croissant) */
    if (!liste || strcmp(pseudo, liste->nom) < 0) {
        nouv->next = liste;
        liste      = nouv;
    } else {
        struct elt *p = liste;
        while (p->next && strcmp(pseudo, p->next->nom) >= 0)
            p = p->next;
        nouv->next = p->next;
        p->next    = nouv;
    }

    pthread_mutex_unlock(&liste_mutex);
}

void supprimeElt(const char *adip)
{
    pthread_mutex_lock(&liste_mutex);

    struct elt **pp = &liste;
    while (*pp) {
        if (strcmp((*pp)->adip, adip) == 0) {
            struct elt *tmp = *pp;
            *pp = tmp->next;
            free(tmp);
            break;
        }
        pp = &(*pp)->next;
    }

    pthread_mutex_unlock(&liste_mutex);
}

void listeElts(void)
{
    pthread_mutex_lock(&liste_mutex);

    struct elt *cur = liste;
    if (!cur)
        printf("(aucun utilisateur connecté)\n");
    while (cur) {
        printf("%s : %s\n", cur->adip, cur->nom);
        cur = cur->next;
    }

    pthread_mutex_unlock(&liste_mutex);
}

const char *ipDePseudo(const char *pseudo)
{
    pthread_mutex_lock(&liste_mutex);

    struct elt *cur = liste;
    while (cur) {
        if (strcmp(cur->nom, pseudo) == 0) {
            pthread_mutex_unlock(&liste_mutex);
            return cur->adip;
        }
        cur = cur->next;
    }

    pthread_mutex_unlock(&liste_mutex);
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Construction / vérification des messages BEUIP
 *
 * Format sur le réseau :
 *   octet 0         : code ASCII ('0'..'9')
 *   octets 1-5      : "BEUIP"
 *   octets 6..n-1   : payload (non null-terminé dans le datagramme)
 * ══════════════════════════════════════════════════════════════════════════ */

/* Construit le message dans buf, retourne la taille à envoyer. */
static int buildMsg(char *buf, size_t bufsz, char code, const char *payload)
{
    if (bufsz < 7)
        return -1;

    buf[0] = code;
    memcpy(buf + 1, BEUIP_MAGIC, BEUIP_MAGIC_LEN);

    if (payload && payload[0] != '\0') {
        size_t plen = strlen(payload);
        if (plen > bufsz - 6)
            plen = bufsz - 6;
        memcpy(buf + 6, payload, plen);
        return (int)(6 + plen);
    }
    return 6;
}

/* Vérifie l'en-tête d'un message reçu.
 * Le code doit être un chiffre ASCII et les octets 1-5 doivent valoir "BEUIP". */
static int checkHeader(const char *buf, int n)
{
    if (n < 6)
        return 0;
    if (buf[0] < '0' || buf[0] > '9')
        return 0;
    return memcmp(buf + 1, BEUIP_MAGIC, BEUIP_MAGIC_LEN) == 0;
}

/* Vérifie qu'un pseudo est valide (non-vide, longueur ≤ LPSEUDO,
 * sans '=', '.', '/' qui trahissent une locale ou un chemin). */
static int pseudoValide(const char *s)
{
    if (!s || s[0] == '\0')
        return 0;
    size_t len = 0;
    for (; s[len]; len++) {
        char c = s[len];
        if (c == '=' || c == '.' || c == '/')
            return 0;
    }
    return len <= LPSEUDO;
}

/* ─── Envoi UDP générique ─────────────────────────────────────────────────── */
static void udpSendTo(int sock, const char *buf, int len,
                      const char *ip, int port)
{
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = htons((uint16_t)port);
    inet_aton(ip, &dst.sin_addr);
    sendto(sock, buf, (size_t)len, 0,
           (struct sockaddr *)&dst, sizeof(dst));
}

/* ─── Broadcast sur toutes les interfaces actives (hors loopback) ─────────── */
static void broadcastMsg(int sock, const char *buf, int len)
{
    struct ifaddrs *ifas, *ifa;
    if (getifaddrs(&ifas) == -1) {
        udpSendTo(sock, buf, len, BEUIP_BROADCAST, BEUIP_PORT);
        return;
    }

    int sent = 0;
    for (ifa = ifas; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;
        if (!ifa->ifa_broadaddr)
            continue;

        char bcast[NI_MAXHOST];
        if (getnameinfo(ifa->ifa_broadaddr, sizeof(struct sockaddr_in),
                        bcast, sizeof(bcast), NULL, 0, NI_NUMERICHOST) != 0)
            continue;

        if (strcmp(bcast, "127.0.0.1") == 0 || strcmp(bcast, "0.0.0.0") == 0)
            continue;

#ifdef TRACE
        printf("[TRACE] broadcast -> %s\n", bcast);
#endif
        udpSendTo(sock, buf, len, bcast, BEUIP_PORT);
        sent = 1;
    }
    freeifaddrs(ifas);

    if (!sent)
        udpSendTo(sock, buf, len, BEUIP_BROADCAST, BEUIP_PORT);
}

/* ══════════════════════════════════════════════════════════════════════════
 * API d'envoi publique
 * ══════════════════════════════════════════════════════════════════════════ */

void beuipSendHello(int sock, const char *pseudo)
{
    char buf[256];
    int  len = buildMsg(buf, sizeof(buf), CODE_HELLO, pseudo);
    if (len > 0)
        broadcastMsg(sock, buf, len);
}

void beuipSendQuit(int sock, const char *pseudo)
{
    char buf[256];
    int  len = buildMsg(buf, sizeof(buf), CODE_QUIT, pseudo);
    if (len <= 0)
        return;

    /* Unicast à tous les contacts connus */
    pthread_mutex_lock(&liste_mutex);
    struct elt *cur = liste;
    while (cur) {
        udpSendTo(sock, buf, len, cur->adip, BEUIP_PORT);
        cur = cur->next;
    }
    pthread_mutex_unlock(&liste_mutex);

    /* Broadcast pour les contacts non encore listés */
    broadcastMsg(sock, buf, len);
}

void beuipSendMsg(const char *pseudo, const char *message)
{
    const char *ip = ipDePseudo(pseudo);
    if (!ip) {
        printf("Utilisateur '%s' introuvable.\n", pseudo);
        return;
    }

    /* Copie avant de libérer le mutex implicitement via ipDePseudo */
    char ip_copy[16];
    strncpy(ip_copy, ip, 15);
    ip_copy[15] = '\0';

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket beuipSendMsg");
        return;
    }

    char buf[512];
    int  len = buildMsg(buf, sizeof(buf), CODE_MSG_TO, message);
    if (len > 0)
        udpSendTo(sock, buf, len, ip_copy, BEUIP_PORT);

    close(sock);
}

void beuipSendAll(const char *message, const char *pseudo_local)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket beuipSendAll");
        return;
    }

    char buf[512];
    int  len = buildMsg(buf, sizeof(buf), CODE_MSG_TO, message);
    if (len <= 0) {
        close(sock);
        return;
    }

    pthread_mutex_lock(&liste_mutex);
    struct elt *cur = liste;
    while (cur) {
        if (!pseudo_local || strcmp(cur->nom, pseudo_local) != 0)
            udpSendTo(sock, buf, len, cur->adip, BEUIP_PORT);
        cur = cur->next;
    }
    pthread_mutex_unlock(&liste_mutex);

    close(sock);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Thread serveur UDP
 * ══════════════════════════════════════════════════════════════════════════ */

void *serveur_udp(void *arg)
{
    char *pseudo = (char *)arg;
    char  buf[513]; /* +1 pour null-terminator de sécurité */

    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        perror("socket serveur_udp");
        udp_running = 0;
        return NULL;
    }

    int opt = 1;
    if (setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0)
        perror("setsockopt SO_BROADCAST");
    if (setsockopt(udp_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        perror("setsockopt SO_REUSEADDR");

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family      = AF_INET;
    srv.sin_port        = htons(BEUIP_PORT);
    srv.sin_addr.s_addr = INADDR_ANY;

    if (bind(udp_sock, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        perror("bind serveur_udp");
        close(udp_sock);
        udp_sock    = -1;
        udp_running = 0;
        return NULL;
    }

    /* Annonce de présence */
    beuipSendHello(udp_sock, pseudo);

    /* ── Boucle de réception ── */
    while (udp_running) {
        struct sockaddr_in src;
        socklen_t          srclen = sizeof(src);

        int n = (int)recvfrom(udp_sock, buf, sizeof(buf) - 1, 0,
                              (struct sockaddr *)&src, &srclen);
        if (n <= 0)
            break;

        /* Null-termine pour manipuler le payload comme une chaîne */
        buf[n] = '\0';

        if (!checkHeader(buf, n)) {
#ifdef TRACE
            printf("[TRACE] paquet non-BEUIP ignoré (n=%d)\n", n);
#endif
            continue;
        }

        char  code      = buf[0];
        char *payload   = buf + 6;   /* null-terminé grâce à buf[n]='\0' */
        char  sender_ip[16];
        strncpy(sender_ip, inet_ntoa(src.sin_addr), 15);
        sender_ip[15] = '\0';

#ifdef TRACE
        printf("[TRACE] code='%c' de %s payload='%s'\n",
               code, sender_ip, payload);
#endif

        switch (code) {
        case CODE_HELLO:
            if (!pseudoValide(payload)) {
#ifdef TRACE
                printf("[TRACE] pseudo invalide '%s' ignoré\n", payload);
#endif
                break;
            }
            ajouteElt(payload, sender_ip);
            {
                char ack[256];
                int  alen = buildMsg(ack, sizeof(ack), CODE_ACK, pseudo);
                if (alen > 0)
                    udpSendTo(udp_sock, ack, alen, sender_ip, BEUIP_PORT);
            }
            break;

        case CODE_ACK:
            if (!pseudoValide(payload)) {
#ifdef TRACE
                printf("[TRACE] ACK: pseudo invalide '%s' ignoré\n", payload);
#endif
                break;
            }
            ajouteElt(payload, sender_ip);
            break;

        case CODE_QUIT:
            supprimeElt(sender_ip);
            break;

        case CODE_MSG_TO:
            {
                /* Résout l'IP de l'expéditeur en pseudo */
                pthread_mutex_lock(&liste_mutex);
                char nom_src[LPSEUDO + 1];
                strncpy(nom_src, sender_ip, sizeof(nom_src) - 1);
                nom_src[sizeof(nom_src) - 1] = '\0';
                struct elt *cur = liste;
                while (cur) {
                    if (strcmp(cur->adip, sender_ip) == 0) {
                        strncpy(nom_src, cur->nom, LPSEUDO);
                        nom_src[LPSEUDO] = '\0';
                        break;
                    }
                    cur = cur->next;
                }
                pthread_mutex_unlock(&liste_mutex);

                printf("\n[Message de %s] %s\n", nom_src, payload);
                fflush(stdout);
            }
            break;

        default:
#ifdef TRACE
            printf("[TRACE] code inconnu '%c' de %s — ignoré\n",
                   code, sender_ip);
#endif
            break;
        }
    }

    close(udp_sock);
    udp_sock = -1;
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Contrôle du serveur
 * ══════════════════════════════════════════════════════════════════════════ */

void creme_start(const char *pseudo)
{
    if (udp_running) {
        printf("Le serveur BEUIP est déjà démarré.\n");
        return;
    }

    strncpy(mon_pseudo, pseudo, LPSEUDO);
    mon_pseudo[LPSEUDO] = '\0';

    udp_running = 1;
    if (pthread_create(&udp_thread, NULL, serveur_udp, mon_pseudo) != 0) {
        perror("pthread_create serveur_udp");
        udp_running  = 0;
        mon_pseudo[0] = '\0';
    }
}

void creme_stop(void)
{
    if (!udp_running) {
        printf("Le serveur BEUIP n'est pas démarré.\n");
        return;
    }

    udp_running = 0;

    if (udp_sock >= 0) {
        beuipSendQuit(udp_sock, mon_pseudo);
        shutdown(udp_sock, SHUT_RDWR);
    }

    pthread_join(udp_thread, NULL);

    /* Libère la liste chaînée */
    pthread_mutex_lock(&liste_mutex);
    struct elt *cur = liste;
    while (cur) {
        struct elt *tmp = cur->next;
        free(cur);
        cur = tmp;
    }
    liste = NULL;
    pthread_mutex_unlock(&liste_mutex);

    mon_pseudo[0] = '\0';
    printf("Serveur BEUIP arrêté.\n");
}

int creme_running(void)
{
    return udp_running;
}

const char *creme_pseudo(void)
{
    return mon_pseudo;
}
