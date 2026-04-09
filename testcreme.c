#include <stdio.h>
#include <string.h>
#include "creme.h"

#define LBUF 512

int main(void)
{
    char ligne[LBUF + 1];
    char *cmd;
    char *arg1;
    char *arg2;

    printf("Mini-biceps TP3\n");

    for (;;) {
        printf("tp3> ");
        fflush(stdout);

        if (fgets(ligne, sizeof(ligne), stdin) == NULL)
            break;

        ligne[strcspn(ligne, "\n")] = '\0';

        cmd = strtok(ligne, " ");
        if (cmd == NULL)
            continue;

        if (strcmp(cmd, "start") == 0) {
            arg1 = strtok(NULL, " ");
            if (arg1 == NULL) {
                printf("Usage: start pseudo\n");
                continue;
            }

            if (beuip_start(arg1) == -1) {
                printf("Impossible de lancer les serveurs.\n");
            } else {
                printf("Serveurs UDP/TCP demarres.\n");
            }
            continue;
        }

        if (strcmp(cmd, "stop") == 0) {
            if (beuip_stop() == -1) {
                printf("Impossible d'arreter les serveurs.\n");
            } else {
                printf("Serveurs UDP/TCP arretes.\n");
            }
            continue;
        }

        if (strcmp(cmd, "liste") == 0) {
            mess_liste();
            continue;
        }

        if (strcmp(cmd, "msg") == 0) {
            arg1 = strtok(NULL, " ");
            arg2 = strtok(NULL, "");
            if (arg1 == NULL || arg2 == NULL) {
                printf("Usage: msg pseudo message\n");
                continue;
            }
            mess_msg(arg1, arg2);
            continue;
        }

        if (strcmp(cmd, "all") == 0) {
            arg1 = strtok(NULL, "");
            if (arg1 == NULL) {
                printf("Usage: all message\n");
                continue;
            }
            mess_all(arg1);
            continue;
        }

        if (strcmp(cmd, "ls") == 0) {
            arg1 = strtok(NULL, " ");
            if (arg1 == NULL) {
                printf("Usage: ls pseudo\n");
                continue;
            }
            demandeListe(arg1);
            continue;
        }

        if (strcmp(cmd, "actif") == 0) {
            printf("Serveur actif = %d\n", beuip_actif());
            continue;
        }

        if (strcmp(cmd, "quit") == 0) {
            if (beuip_actif())
                beuip_stop();
            break;
        }

        printf("Commande inconnue.\n");
    }

    return 0;
}