#include <stdio.h>
#include <string.h>
#include "creme.h"

int main(int N, char *P[])
{
    if (N != 3 || strcmp(P[1], "start") != 0) {
        fprintf(stderr, "Utilisation : %s start pseudo\n", P[0]);
        return 1;
    }

    if (beuip_start(P[2]) == -1) {
        fprintf(stderr, "Echec du lancement du thread UDP.\n");
        return 2;
    }

    printf("Thread UDP lance. Appuie sur Entree pour terminer le programme.\n");
    getchar();

    return 0;
}