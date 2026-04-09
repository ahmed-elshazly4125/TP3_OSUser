#ifndef CREME_H
#define CREME_H

#include <sys/types.h>
#include <pthread.h>

#define LPSEUDO 23

struct elt {
    char nom[LPSEUDO + 1];
    char adip[16];
    struct elt *next;
};

extern struct elt *liste_contacts;
extern pthread_mutex_t mutex_table;

char *addrip(unsigned long A);
int message_beuip_valide(const char *msg, int n);
void construire_message(char *dest, char code, const char *pseudo);
int taille_message_beuip(const char *pseudo);

void ajouteElt(char *pseudo, char *adip);
void supprimeElt(char *adip);
void listeElts(void);

void *serveur_udp(void *p);
int beuip_start(const char *pseudo);
int beuip_stop(void);
int beuip_actif(void);

int commande(char octet1, char *message, char *pseudo);

int mess_liste(void);
int mess_msg(const char *pseudo, const char *texte);
int mess_all(const char *texte);

#endif