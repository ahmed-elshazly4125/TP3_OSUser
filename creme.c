#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "creme.h"

#define PORT 9998
#define LBUF 512

participant_t table[MAX_USERS];
int nb_participants = 0;
pthread_mutex_t mutex_table = PTHREAD_MUTEX_INITIALIZER;

static pthread_t g_th_udp;
static int g_udp_actif = 0;
static int g_udp_sid = -1;
static char g_udp_pseudo[256];

char *addrip(unsigned long A)
{
    static char b[16];
    sprintf(b, "%u.%u.%u.%u",
            (unsigned int)((A >> 24) & 0xFF),
            (unsigned int)((A >> 16) & 0xFF),
            (unsigned int)((A >> 8) & 0xFF),
            (unsigned int)(A & 0xFF));
    return b;
}

int message_beuip_valide(const char *msg, int n)
{
    if (n < 6)
        return 0;

    if (strncmp(msg + 1, "BEUIP", 5) != 0)
        return 0;

    return 1;
}

int deja_present(unsigned long ip, const char *pseudo)
{
    int i;

    for (i = 0; i < nb_participants; i++) {
        if (table[i].ip == ip && strcmp(table[i].pseudo, pseudo) == 0)
            return 1;
    }

    return 0;
}

void ajouter_participant(unsigned long ip, const char *pseudo)
{
    if (deja_present(ip, pseudo))
        return;

    if (nb_participants >= MAX_USERS) {
        fprintf(stderr, "Table pleine, impossible d'ajouter %s (%s)\n",
                pseudo, addrip(ip));
        return;
    }

    table[nb_participants].ip = ip;
    strncpy(table[nb_participants].pseudo, pseudo,
            sizeof(table[nb_participants].pseudo) - 1);
    table[nb_participants].pseudo[sizeof(table[nb_participants].pseudo) - 1] = '\0';
    nb_participants++;
}

void supprimer_participant(unsigned long ip, const char *pseudo)
{
    int i;

    for (i = 0; i < nb_participants; i++) {
        if (table[i].ip == ip && strcmp(table[i].pseudo, pseudo) == 0) {
            int j;
            for (j = i; j < nb_participants - 1; j++) {
                table[j] = table[j + 1];
            }
            nb_participants--;
            return;
        }
    }
}

void afficher_table(void)
{
    int i;

    printf("---- Table des participants ----\n");
    for (i = 0; i < nb_participants; i++) {
        printf("%2d : %s - %s\n",
               i + 1,
               addrip(table[i].ip),
               table[i].pseudo);
    }
    printf("-------------------------------\n");
}

void construire_message(char *dest, char code, const char *pseudo)
{
    dest[0] = code;
    strcpy(dest + 1, "BEUIP");
    strcpy(dest + 6, pseudo);
}

int taille_message_beuip(const char *pseudo)
{
    return 6 + strlen(pseudo) + 1;
}

int chercher_ip_par_pseudo(const char *pseudo, unsigned long *ip)
{
    int i;

    for (i = 0; i < nb_participants; i++) {
        if (strcmp(table[i].pseudo, pseudo) == 0) {
            *ip = table[i].ip;
            return 1;
        }
    }

    return 0;
}

char *chercher_pseudo_par_ip(unsigned long ip)
{
    int i;

    for (i = 0; i < nb_participants; i++) {
        if (table[i].ip == ip)
            return table[i].pseudo;
    }

    return NULL;
}

void *serveur_udp(void *p)
{
    char *pseudo = (char *)p;
    int sid, n, i;
    int opt = 1;
    char buf[LBUF + 1];
    char message[LBUF + 1];
    char reponse[LBUF + 1];
    char message9[LBUF + 1];
    char *pseudo_recu;
    char *pseudo_dest;
    char *texte;
    char *pseudo_source;
    unsigned long ip_dest;
    unsigned long ip_source;
    struct sockaddr_in SockConf;
    struct sockaddr_in Sock;
    struct sockaddr_in Broadcast;
    struct sockaddr_in Dest;
    socklen_t ls;

    if ((sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("socket");
        g_udp_actif = 0;
        return NULL;
    }

    g_udp_sid = sid;

    if (setsockopt(sid, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) == -1) {
        perror("setsockopt SO_BROADCAST");
        close(sid);
        g_udp_actif = 0;
        return NULL;
    }

    memset(&SockConf, 0, sizeof(SockConf));
    SockConf.sin_family = AF_INET;
    SockConf.sin_port = htons(PORT);
    SockConf.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sid, (struct sockaddr *)&SockConf, sizeof(SockConf)) == -1) {
        perror("bind");
        close(sid);
        g_udp_actif = 0;
        return NULL;
    }

    memset(&Broadcast, 0, sizeof(Broadcast));
    Broadcast.sin_family = AF_INET;
    Broadcast.sin_port = htons(PORT);
    Broadcast.sin_addr.s_addr = inet_addr("192.168.88.255");

    printf("thread UDP lance sur le port %d avec le pseudo %s\n", PORT, pseudo);

    pthread_mutex_lock(&mutex_table);
    ajouter_participant(0x7F000001, pseudo);
    pthread_mutex_unlock(&mutex_table);

    construire_message(message, '1', pseudo);

    if (sendto(sid, message, taille_message_beuip(pseudo), 0,
               (struct sockaddr *)&Broadcast, sizeof(Broadcast)) == -1) {
        perror("sendto broadcast");
    } else {
        printf("Broadcast d'identification envoye.\n");
    }

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
            printf("Message ignore : entete invalide\n");
            continue;
        }

        if (buf[0] == '0') {
            pseudo_recu = buf + 6;

            pthread_mutex_lock(&mutex_table);
            printf("%s (%s) quitte le reseau.\n",
                   pseudo_recu, addrip(ip_source));
            supprimer_participant(ip_source, pseudo_recu);
            afficher_table();
            pthread_mutex_unlock(&mutex_table);
            continue;
        }

        if (buf[0] == '3') {
            if (ip_source != 0x7F000001) {
                printf("Commande refusee : l'expediteur n'est pas 127.0.0.1\n");
                continue;
            }

            pthread_mutex_lock(&mutex_table);
            printf("Commande liste recue depuis 127.0.0.1\n");
            afficher_table();
            pthread_mutex_unlock(&mutex_table);
            continue;
        }

        if (buf[0] == '4') {
            if (ip_source != 0x7F000001) {
                printf("Commande refusee : l'expediteur n'est pas 127.0.0.1\n");
                continue;
            }

            pseudo_dest = buf + 6;
            texte = pseudo_dest + strlen(pseudo_dest) + 1;

            pthread_mutex_lock(&mutex_table);
            if (!chercher_ip_par_pseudo(pseudo_dest, &ip_dest)) {
                pthread_mutex_unlock(&mutex_table);
                printf("Pseudo inconnu : %s\n", pseudo_dest);
                continue;
            }
            pthread_mutex_unlock(&mutex_table);

            memset(&Dest, 0, sizeof(Dest));
            Dest.sin_family = AF_INET;
            Dest.sin_port = htons(PORT);
            Dest.sin_addr.s_addr = htonl(ip_dest);

            construire_message(message9, '9', texte);

            if (sendto(sid, message9, taille_message_beuip(texte), 0,
                       (struct sockaddr *)&Dest, sizeof(Dest)) == -1) {
                perror("sendto message prive");
            } else {
                printf("Message prive envoye a %s (%s)\n",
                       pseudo_dest, addrip(ip_dest));
            }

            continue;
        }

        if (buf[0] == '5') {
            if (ip_source != 0x7F000001) {
                printf("Commande refusee : l'expediteur n'est pas 127.0.0.1\n");
                continue;
            }

            texte = buf + 6;
            construire_message(message9, '9', texte);

            pthread_mutex_lock(&mutex_table);
            for (i = 0; i < nb_participants; i++) {
                if (table[i].ip == 0x7F000001)
                    continue;

                memset(&Dest, 0, sizeof(Dest));
                Dest.sin_family = AF_INET;
                Dest.sin_port = htons(PORT);
                Dest.sin_addr.s_addr = htonl(table[i].ip);

                if (sendto(sid, message9, taille_message_beuip(texte), 0,
                           (struct sockaddr *)&Dest, sizeof(Dest)) == -1) {
                    perror("sendto message collectif");
                } else {
                    printf("Message collectif envoye a %s (%s)\n",
                           table[i].pseudo, addrip(table[i].ip));
                }
            }
            pthread_mutex_unlock(&mutex_table);

            continue;
        }

        if (buf[0] == '9') {
            texte = buf + 6;

            pthread_mutex_lock(&mutex_table);
            pseudo_source = chercher_pseudo_par_ip(ip_source);
            if (pseudo_source == NULL) {
                printf("Message recu d'une IP inconnue (%s) : %s\n",
                       addrip(ip_source), texte);
            } else {
                printf("Message de %s : %s\n", pseudo_source, texte);
            }
            pthread_mutex_unlock(&mutex_table);

            continue;
        }

        pseudo_recu = buf + 6;

        pthread_mutex_lock(&mutex_table);
        printf("Message recu de %s : code=%c pseudo=%s\n",
               addrip(ip_source), buf[0], pseudo_recu);

        ajouter_participant(ip_source, pseudo_recu);
        afficher_table();
        pthread_mutex_unlock(&mutex_table);

        if (buf[0] == '1') {
            construire_message(reponse, '2', pseudo);

            if (sendto(sid, reponse, taille_message_beuip(pseudo), 0,
                       (struct sockaddr *)&Sock, ls) == -1) {
                perror("sendto AR");
            } else {
                printf("AR envoye a %s\n", addrip(ip_source));
            }
        }
    }

    return NULL;
}

int beuip_start(const char *pseudo)
{
    if (g_udp_actif)
        return -1;

    strncpy(g_udp_pseudo, pseudo, sizeof(g_udp_pseudo) - 1);
    g_udp_pseudo[sizeof(g_udp_pseudo) - 1] = '\0';

    if (pthread_create(&g_th_udp, NULL, serveur_udp, g_udp_pseudo) != 0) {
        perror("pthread_create");
        return -1;
    }

    g_udp_actif = 1;
    return 0;
}

int beuip_actif(void)
{
    return g_udp_actif;
}

int mess_liste(void)
{
    int sid;
    struct sockaddr_in Sock;
    char message[LBUF + 1];

    if ((sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("socket");
        return -1;
    }

    memset(&Sock, 0, sizeof(Sock));
    Sock.sin_family = AF_INET;
    Sock.sin_port = htons(PORT);
    Sock.sin_addr.s_addr = inet_addr("127.0.0.1");

    message[0] = '3';
    strcpy(message + 1, "BEUIP");
    message[6] = '\0';

    if (sendto(sid, message, 7, 0,
               (struct sockaddr *)&Sock, sizeof(Sock)) == -1) {
        perror("sendto");
        close(sid);
        return -1;
    }

    close(sid);
    return 0;
}

int mess_msg(const char *pseudo, const char *texte)
{
    int sid;
    struct sockaddr_in Sock;
    char message[LBUF + 1];
    int taille;

    if ((sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("socket");
        return -1;
    }

    memset(&Sock, 0, sizeof(Sock));
    Sock.sin_family = AF_INET;
    Sock.sin_port = htons(PORT);
    Sock.sin_addr.s_addr = inet_addr("127.0.0.1");

    message[0] = '4';
    strcpy(message + 1, "BEUIP");
    strcpy(message + 6, pseudo);
    strcpy(message + 6 + strlen(pseudo) + 1, texte);

    taille = 6 + strlen(pseudo) + 1 + strlen(texte) + 1;

    if (sendto(sid, message, taille, 0,
               (struct sockaddr *)&Sock, sizeof(Sock)) == -1) {
        perror("sendto");
        close(sid);
        return -1;
    }

    close(sid);
    return 0;
}

int mess_all(const char *texte)
{
    int sid;
    struct sockaddr_in Sock;
    char message[LBUF + 1];
    int taille;

    if ((sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("socket");
        return -1;
    }

    memset(&Sock, 0, sizeof(Sock));
    Sock.sin_family = AF_INET;
    Sock.sin_port = htons(PORT);
    Sock.sin_addr.s_addr = inet_addr("127.0.0.1");

    message[0] = '5';
    strcpy(message + 1, "BEUIP");
    strcpy(message + 6, texte);

    taille = 6 + strlen(texte) + 1;

    if (sendto(sid, message, taille, 0,
               (struct sockaddr *)&Sock, sizeof(Sock)) == -1) {
        perror("sendto");
        close(sid);
        return -1;
    }

    close(sid);
    return 0;
}