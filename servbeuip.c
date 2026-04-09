/*****
* servbeuip.c
* Serveur BEUIP
*****/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "creme.h"

#define PORT 9998
#define LBUF 512

#ifdef TRACE
#define TPRINTF(...) printf(__VA_ARGS__)
#else
#define TPRINTF(...)
#endif

char buf[LBUF + 1];
struct sockaddr_in SockConf;

int g_sid = -1;
char g_pseudo[256];
struct sockaddr_in g_broadcast;

void envoyer_quit_et_sortir(int sig)
{
    char message0[LBUF + 1];

    (void)sig;

    if (g_sid != -1) {
        construire_message(message0, '0', g_pseudo);

        sendto(g_sid, message0, taille_message_beuip(g_pseudo), 0,
               (struct sockaddr *)&g_broadcast, sizeof(g_broadcast));

        TPRINTF("\nMessage de deconnexion envoye.\n");
        close(g_sid);
    }

    exit(0);
}

int main(int N, char *P[])
{
    int sid, n, i;
    int opt = 1;
    char message[LBUF + 1];
    char reponse[LBUF + 1];
    char message9[LBUF + 1];
    char *pseudo_recu;
    char *pseudo_dest;
    char *texte;
    char *pseudo_source;
    unsigned long ip_dest;
    unsigned long ip_source;
    struct sockaddr_in Sock;
    struct sockaddr_in Broadcast;
    struct sockaddr_in Dest;
    socklen_t ls;

    if (N != 2) {
        fprintf(stderr, "Utilisation : %s pseudo\n", P[0]);
        return 1;
    }

    strncpy(g_pseudo, P[1], sizeof(g_pseudo) - 1);
    g_pseudo[sizeof(g_pseudo) - 1] = '\0';

    if ((sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("socket");
        return 2;
    }

    g_sid = sid;

    if (setsockopt(sid, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) == -1) {
        perror("setsockopt SO_BROADCAST");
        return 3;
    }

    bzero(&SockConf, sizeof(SockConf));
    SockConf.sin_family = AF_INET;
    SockConf.sin_port = htons(PORT);
    SockConf.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sid, (struct sockaddr *)&SockConf, sizeof(SockConf)) == -1) {
        perror("bind");
        return 4;
    }

    bzero(&Broadcast, sizeof(Broadcast));
    Broadcast.sin_family = AF_INET;
    Broadcast.sin_port = htons(PORT);
    Broadcast.sin_addr.s_addr = inet_addr("192.168.88.255");

    g_broadcast = Broadcast;

    signal(SIGINT, envoyer_quit_et_sortir);

    TPRINTF("servbeuip lance sur le port %d avec le pseudo %s\n", PORT, P[1]);
    ajouter_participant(0x7F000001, P[1]);

    construire_message(message, '1', P[1]);

    if (sendto(sid, message, taille_message_beuip(P[1]), 0,
               (struct sockaddr *)&Broadcast, sizeof(Broadcast)) == -1) {
        perror("sendto broadcast");
        return 5;
    }

    TPRINTF("Broadcast d'identification envoye.\n");

    for (;;) {
        ls = sizeof(Sock);

        n = recvfrom(sid, buf, LBUF, 0, (struct sockaddr *)&Sock, &ls);
        if (n == -1) {
            perror("recvfrom");
            continue;
        }

        buf[n] = '\0';
        ip_source = ntohl(Sock.sin_addr.s_addr);

        if (!message_beuip_valide(buf, n)) {
            TPRINTF("Message ignore : entete invalide\n");
            continue;
        }

        if (buf[0] == '0') {
            pseudo_recu = buf + 6;
            printf("%s (%s) quitte le reseau.\n",
                   pseudo_recu, addrip(ip_source));
            supprimer_participant(ip_source, pseudo_recu);
            afficher_table();
            continue;
        }

        if (buf[0] == '3') {
            if (ip_source != 0x7F000001) {
                TPRINTF("Commande refusee : l'expediteur n'est pas 127.0.0.1\n");
                continue;
            }

            TPRINTF("Commande liste recue depuis 127.0.0.1\n");
            afficher_table();
            continue;
        }

        if (buf[0] == '4') {
            if (ip_source != 0x7F000001) {
                TPRINTF("Commande refusee : l'expediteur n'est pas 127.0.0.1\n");
                continue;
            }

            pseudo_dest = buf + 6;
            texte = pseudo_dest + strlen(pseudo_dest) + 1;

            if (!chercher_ip_par_pseudo(pseudo_dest, &ip_dest)) {
                TPRINTF("Pseudo inconnu : %s\n", pseudo_dest);
                continue;
            }

            bzero(&Dest, sizeof(Dest));
            Dest.sin_family = AF_INET;
            Dest.sin_port = htons(PORT);
            Dest.sin_addr.s_addr = htonl(ip_dest);

            construire_message(message9, '9', texte);

            if (sendto(sid, message9, taille_message_beuip(texte), 0,
                       (struct sockaddr *)&Dest, sizeof(Dest)) == -1) {
                perror("sendto message prive");
            } else {
                TPRINTF("Message prive envoye a %s (%s)\n",
                        pseudo_dest, addrip(ip_dest));
            }

            continue;
        }

        if (buf[0] == '5') {
            if (ip_source != 0x7F000001) {
                TPRINTF("Commande refusee : l'expediteur n'est pas 127.0.0.1\n");
                continue;
            }

            texte = buf + 6;
            construire_message(message9, '9', texte);

            for (i = 0; i < nb_participants; i++) {
                if (table[i].ip == 0x7F000001)
                    continue;

                bzero(&Dest, sizeof(Dest));
                Dest.sin_family = AF_INET;
                Dest.sin_port = htons(PORT);
                Dest.sin_addr.s_addr = htonl(table[i].ip);

                if (sendto(sid, message9, taille_message_beuip(texte), 0,
                           (struct sockaddr *)&Dest, sizeof(Dest)) == -1) {
                    perror("sendto message collectif");
                } else {
                    TPRINTF("Message collectif envoye a %s (%s)\n",
                            table[i].pseudo, addrip(table[i].ip));
                }
            }

            continue;
        }

        if (buf[0] == '9') {
            texte = buf + 6;
            pseudo_source = chercher_pseudo_par_ip(ip_source);

            if (pseudo_source == NULL) {
                printf("Message recu d'une IP inconnue (%s) : %s\n",
                       addrip(ip_source), texte);
            } else {
                printf("Message de %s : %s\n", pseudo_source, texte);
            }

            continue;
        }

        pseudo_recu = buf + 6;

        TPRINTF("Message recu de %s : code=%c pseudo=%s\n",
                addrip(ip_source), buf[0], pseudo_recu);

        ajouter_participant(ip_source, pseudo_recu);
        afficher_table();

        if (buf[0] == '1') {
            construire_message(reponse, '2', P[1]);

            if (sendto(sid, reponse, taille_message_beuip(P[1]), 0,
                       (struct sockaddr *)&Sock, ls) == -1) {
                perror("sendto AR");
            } else {
                TPRINTF("AR envoye a %s\n", addrip(ip_source));
            }
        }
    }

    return 0;
}