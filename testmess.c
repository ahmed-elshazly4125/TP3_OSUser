#include <stdio.h>
#include <string.h>
#include "creme.h"

int main(int N, char *P[])
{
    if (N < 2) {
        fprintf(stderr, "Utilisation : %s liste | %s msg pseudo message | %s all message\n",
                P[0], P[0], P[0]);
        return 1;
    }

    if (strcmp(P[1], "liste") == 0) {
        if (N != 2) {
            fprintf(stderr, "Utilisation : %s liste\n", P[0]);
            return 1;
        }

        if (mess_liste() == -1) {
            fprintf(stderr, "Echec de la commande liste.\n");
            return 2;
        }

        printf("Commande liste envoyee.\n");
        return 0;
    }

    if (strcmp(P[1], "msg") == 0) {
        if (N != 4) {
            fprintf(stderr, "Utilisation : %s msg pseudo message\n", P[0]);
            return 1;
        }

        if (mess_msg(P[2], P[3]) == -1) {
            fprintf(stderr, "Echec de l'envoi du message prive.\n");
            return 3;
        }

        printf("Commande msg envoyee.\n");
        return 0;
    }

    if (strcmp(P[1], "all") == 0) {
        if (N != 3) {
            fprintf(stderr, "Utilisation : %s all message\n", P[0]);
            return 1;
        }

        if (mess_all(P[2]) == -1) {
            fprintf(stderr, "Echec de l'envoi du message collectif.\n");
            return 4;
        }

        printf("Commande all envoyee.\n");
        return 0;
    }

    fprintf(stderr, "Commande inconnue : %s\n", P[1]);
    return 1;
}