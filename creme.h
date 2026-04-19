#ifndef CREME_H
#define CREME_H

#include <netinet/in.h>

/* ─── Protocole BEUIP ─────────────────────────────────────────────────────── */
#define BEUIP_PORT       9998
#define BEUIP_MAGIC      "BEUIP"
#define BEUIP_MAGIC_LEN  5
#define BEUIP_BROADCAST  "192.168.88.255"

/* Codes des messages (format ASCII) */
#define CODE_QUIT    '0'   /* déconnexion broadcast  */
#define CODE_HELLO   '1'   /* annonce de présence    */
#define CODE_ACK     '2'   /* accusé de réception    */
#define CODE_MSG_TO  '9'   /* message privé entrant  */

/* Taille maximale d'un pseudo */
#define LPSEUDO 23

/* ─── Liste chaînée des contacts ─────────────────────────────────────────── */
struct elt {
    char        nom[LPSEUDO + 1];
    char        adip[16];
    struct elt *next;
};

/* ─── API publique ────────────────────────────────────────────────────────── */

/* Gestion de la liste */
void        ajouteElt(const char *pseudo, const char *adip);
void        supprimeElt(const char *adip);
void        listeElts(void);
const char *ipDePseudo(const char *pseudo);

/* Thread serveur UDP */
void *serveur_udp(void *pseudo);

/* Envoi de messages */
void beuipSendHello(int sock, const char *pseudo);
void beuipSendQuit(int sock, const char *pseudo);
void beuipSendMsg(const char *pseudo, const char *message);
void beuipSendAll(const char *message, const char *pseudo_local);

/* Contrôle du serveur */
void        creme_start(const char *pseudo);
void        creme_stop(void);
int         creme_running(void);
const char *creme_pseudo(void);

#endif /* CREME_H */
