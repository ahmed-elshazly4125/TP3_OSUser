#ifndef CREME_H
#define CREME_H

#include <sys/types.h>
#include <pthread.h>

#define MAX_USERS 255

typedef struct {
    unsigned long ip;
    char pseudo[256];
} participant_t;

extern participant_t table[MAX_USERS];
extern int nb_participants;
extern pthread_mutex_t mutex_table;

char *addrip(unsigned long A);
int message_beuip_valide(const char *msg, int n);
int deja_present(unsigned long ip, const char *pseudo);
void ajouter_participant(unsigned long ip, const char *pseudo);
void supprimer_participant(unsigned long ip, const char *pseudo);
void afficher_table(void);
void construire_message(char *dest, char code, const char *pseudo);
int taille_message_beuip(const char *pseudo);
int chercher_ip_par_pseudo(const char *pseudo, unsigned long *ip);
char *chercher_pseudo_par_ip(unsigned long ip);

void *serveur_udp(void *p);
int beuip_start(const char *pseudo);
int beuip_actif(void);

int mess_liste(void);
int mess_msg(const char *pseudo, const char *texte);
int mess_all(const char *texte);

#endif