/*****
* clibeuip.c
* Client local BEUIP
*****/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#define PORT 9998
#define LBUF 512

void construire_message_liste(char *dest)
{
    dest[0] = '3';
    strcpy(dest + 1, "BEUIP");
    dest[6] = '\0';
}

int taille_message_liste(void)
{
    return 7;
}

void construire_message_prive(char *dest, const char *pseudo, const char *texte)
{
    dest[0] = '4';
    strcpy(dest + 1, "BEUIP");
    strcpy(dest + 6, pseudo);
    strcpy(dest + 6 + strlen(pseudo) + 1, texte);
}

int taille_message_prive(const char *pseudo, const char *texte)
{
    return 6 + strlen(pseudo) + 1 + strlen(texte) + 1;
}

void construire_message_all(char *dest, const char *texte)
{
    dest[0] = '5';
    strcpy(dest + 1, "BEUIP");
    strcpy(dest + 6, texte);
}

int taille_message_all(const char *texte)
{
    return 6 + strlen(texte) + 1;
}

int main(int N, char *P[])
{
    int sid;
    struct sockaddr_in Sock;
    char message[LBUF + 1];
    int taille;

    if (N < 2) {
        fprintf(stderr, "Utilisation : %s liste | %s msg pseudo message | %s all message\n",
                P[0], P[0], P[0]);
        return 1;
    }

    if ((sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("socket");
        return 2;
    }

    bzero(&Sock, sizeof(Sock));
    Sock.sin_family = AF_INET;
    Sock.sin_port = htons(PORT);
    Sock.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (strcmp(P[1], "liste") == 0) {
        if (N != 2) {
            fprintf(stderr, "Utilisation : %s liste\n", P[0]);
            return 1;
        }

        construire_message_liste(message);
        taille = taille_message_liste();

        if (sendto(sid, message, taille, 0,
                   (struct sockaddr *)&Sock, sizeof(Sock)) == -1) {
            perror("sendto");
            return 3;
        }

        printf("Commande liste envoyee au serveur.\n");
        return 0;
    }

    if (strcmp(P[1], "msg") == 0) {
        if (N != 4) {
            fprintf(stderr, "Utilisation : %s msg pseudo message\n", P[0]);
            return 1;
        }

        construire_message_prive(message, P[2], P[3]);
        taille = taille_message_prive(P[2], P[3]);

        if (sendto(sid, message, taille, 0,
                   (struct sockaddr *)&Sock, sizeof(Sock)) == -1) {
            perror("sendto");
            return 4;
        }

        printf("Commande message prive envoyee au serveur.\n");
        return 0;
    }

    if (strcmp(P[1], "all") == 0) {
        if (N != 3) {
            fprintf(stderr, "Utilisation : %s all message\n", P[0]);
            return 1;
        }

        construire_message_all(message, P[2]);
        taille = taille_message_all(P[2]);

        if (sendto(sid, message, taille, 0,
                   (struct sockaddr *)&Sock, sizeof(Sock)) == -1) {
            perror("sendto");
            return 5;
        }

        printf("Commande message collectif envoyee au serveur.\n");
        return 0;
    }

    fprintf(stderr, "Commande inconnue : %s\n", P[1]);
    return 1;
}