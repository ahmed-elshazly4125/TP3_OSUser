#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <net/if.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "creme.h"

#define PORT 9998
#define LBUF 512

struct elt *liste_contacts = NULL;
pthread_mutex_t mutex_table = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t mutex_aff = PTHREAD_MUTEX_INITIALIZER;

static pthread_t g_th_udp;
static pthread_t g_th_tcp;

static int g_udp_actif = 0;
static int g_tcp_actif = 0;

static int g_udp_sid = -1;
static int g_tcp_sid = -1;

static volatile int g_stop_udp = 0;
static volatile int g_stop_tcp = 0;

static char g_udp_pseudo[LPSEUDO + 1];
static char g_reppub[256] = "reppub";

static void reaffiche_prompt(void)
{
    pthread_mutex_lock(&mutex_aff);
    printf("tp3> ");
    fflush(stdout);
    pthread_mutex_unlock(&mutex_aff);
}

static void affiche_async(const char *fmt, ...)
{
    va_list ap;

    pthread_mutex_lock(&mutex_aff);
    printf("\n");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
    pthread_mutex_unlock(&mutex_aff);
}

#ifdef TRACE
#define TRACEF(...)                  \
    do {                            \
        affiche_async(__VA_ARGS__); \
        reaffiche_prompt();         \
    } while (0)
#else
#define TRACEF(...)
#endif

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

static int assure_reppub(void)
{
    struct stat st;

    if (stat(g_reppub, &st) == -1) {
        if (mkdir(g_reppub, 0775) == -1) {
            perror("mkdir reppub");
            return -1;
        }
        return 0;
    }

    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "%s existe mais n'est pas un repertoire\n", g_reppub);
        return -1;
    }

    return 0;
}

static void videListe(void)
{
    struct elt *cour;
    struct elt *suiv;

    pthread_mutex_lock(&mutex_table);
    cour = liste_contacts;
    while (cour != NULL) {
        suiv = cour->next;
        free(cour);
        cour = suiv;
    }
    liste_contacts = NULL;
    pthread_mutex_unlock(&mutex_table);
}

void ajouteElt(char *pseudo, char *adip)
{
    struct elt *nv;
    struct elt **pp;
    struct elt *cour;

    if (pseudo == NULL || adip == NULL)
        return;

    pthread_mutex_lock(&mutex_table);

    cour = liste_contacts;
    while (cour != NULL) {
        if (strcmp(cour->adip, adip) == 0) {
            strncpy(cour->nom, pseudo, LPSEUDO);
            cour->nom[LPSEUDO] = '\0';
            pthread_mutex_unlock(&mutex_table);
            return;
        }
        cour = cour->next;
    }

    nv = malloc(sizeof(struct elt));
    if (nv == NULL) {
        perror("malloc");
        pthread_mutex_unlock(&mutex_table);
        return;
    }

    strncpy(nv->nom, pseudo, LPSEUDO);
    nv->nom[LPSEUDO] = '\0';
    strncpy(nv->adip, adip, 15);
    nv->adip[15] = '\0';
    nv->next = NULL;

    pp = &liste_contacts;
    while (*pp != NULL) {
        int c = strcmp((*pp)->nom, nv->nom);
        if (c > 0)
            break;
        if (c == 0 && strcmp((*pp)->adip, nv->adip) > 0)
            break;
        pp = &((*pp)->next);
    }

    nv->next = *pp;
    *pp = nv;

    pthread_mutex_unlock(&mutex_table);
}

void supprimeElt(char *adip)
{
    struct elt **pp;
    struct elt *tmp;

    if (adip == NULL)
        return;

    pthread_mutex_lock(&mutex_table);

    pp = &liste_contacts;
    while (*pp != NULL) {
        if (strcmp((*pp)->adip, adip) == 0) {
            tmp = *pp;
            *pp = (*pp)->next;
            free(tmp);
            pthread_mutex_unlock(&mutex_table);
            return;
        }
        pp = &((*pp)->next);
    }

    pthread_mutex_unlock(&mutex_table);
}

void listeElts(void)
{
    struct elt *cour;

    pthread_mutex_lock(&mutex_table);
    pthread_mutex_lock(&mutex_aff);

    printf("---- Liste des contacts ----\n");
    cour = liste_contacts;
    while (cour != NULL) {
        printf("%s - %s\n", cour->nom, cour->adip);
        cour = cour->next;
    }
    printf("----------------------------\n");
    fflush(stdout);

    pthread_mutex_unlock(&mutex_aff);
    pthread_mutex_unlock(&mutex_table);
}

static struct elt *chercheEltParNom_locked(const char *pseudo)
{
    struct elt *cour = liste_contacts;

    while (cour != NULL) {
        if (strcmp(cour->nom, pseudo) == 0)
            return cour;
        cour = cour->next;
    }

    return NULL;
}

static struct elt *chercheEltParIp_locked(const char *adip)
{
    struct elt *cour = liste_contacts;

    while (cour != NULL) {
        if (strcmp(cour->adip, adip) == 0)
            return cour;
        cour = cour->next;
    }

    return NULL;
}

static int ip_est_locale(const char *adip)
{
    struct ifaddrs *ifaddr;
    struct ifaddrs *ifa;
    char host[NI_MAXHOST];

    if (strcmp(adip, "127.0.0.1") == 0)
        return 1;

    if (getifaddrs(&ifaddr) == -1)
        return 0;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;

        if (getnameinfo(ifa->ifa_addr,
                        sizeof(struct sockaddr_in),
                        host, sizeof(host),
                        NULL, 0,
                        NI_NUMERICHOST) != 0) {
            continue;
        }

        if (strcmp(host, adip) == 0) {
            freeifaddrs(ifaddr);
            return 1;
        }
    }

    freeifaddrs(ifaddr);
    return 0;
}

static void envoie_broadcasts_identification(int sid, const char *pseudo)
{
    struct ifaddrs *ifaddr;
    struct ifaddrs *ifa;
    char host[NI_MAXHOST];
    char message[LBUF + 1];
    struct sockaddr_in Dest;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return;
    }

    construire_message(message, '1', pseudo);

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_broadaddr == NULL)
            continue;

        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;

        if (!(ifa->ifa_flags & IFF_UP))
            continue;

        if (!(ifa->ifa_flags & IFF_BROADCAST))
            continue;

        if (ifa->ifa_flags & IFF_LOOPBACK)
            continue;

        if (getnameinfo(ifa->ifa_broadaddr,
                        sizeof(struct sockaddr_in),
                        host, sizeof(host),
                        NULL, 0,
                        NI_NUMERICHOST) != 0) {
            continue;
        }

        if (strcmp(host, "127.0.0.1") == 0)
            continue;

        memset(&Dest, 0, sizeof(Dest));
        Dest.sin_family = AF_INET;
        Dest.sin_port = htons(PORT);
        Dest.sin_addr.s_addr = inet_addr(host);

        if (sendto(sid, message, taille_message_beuip(pseudo), 0,
                   (struct sockaddr *)&Dest, sizeof(Dest)) == -1) {
            perror("sendto broadcast");
        } else {
            TRACEF("Broadcast d'identification envoye sur %s -> %s\n",
                   ifa->ifa_name, host);
        }
    }

    freeifaddrs(ifaddr);
}

void *serveur_udp(void *p)
{
    char *pseudo = (char *)p;
    int sid, n;
    int opt = 1;
    struct timeval tv;
    char buf[LBUF + 1];
    char reponse[LBUF + 1];
    char *pseudo_recu;
    char *texte;
    char iptxt[16];
    struct elt *elt_trouve;
    unsigned long ip_source;
    struct sockaddr_in SockConf;
    struct sockaddr_in Sock;
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
        g_udp_sid = -1;
        g_udp_actif = 0;
        return NULL;
    }

    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (setsockopt(sid, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
        perror("setsockopt SO_RCVTIMEO");
    }

    memset(&SockConf, 0, sizeof(SockConf));
    SockConf.sin_family = AF_INET;
    SockConf.sin_port = htons(PORT);
    SockConf.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sid, (struct sockaddr *)&SockConf, sizeof(SockConf)) == -1) {
        perror("bind");
        close(sid);
        g_udp_sid = -1;
        g_udp_actif = 0;
        return NULL;
    }

    TRACEF("thread UDP lance sur le port %d avec le pseudo %s\n", PORT, pseudo);

    ajouteElt(pseudo, "127.0.0.1");
    envoie_broadcasts_identification(sid, pseudo);

    while (!g_stop_udp) {
        ls = sizeof(Sock);

        n = recvfrom(sid, buf, LBUF, 0, (struct sockaddr *)&Sock, &ls);
        if (n == -1) {
            if (g_stop_udp)
                break;
            continue;
        }

        buf[n] = '\0';
        ip_source = ntohl(Sock.sin_addr.s_addr);
        strncpy(iptxt, addrip(ip_source), sizeof(iptxt) - 1);
        iptxt[sizeof(iptxt) - 1] = '\0';

        if (!message_beuip_valide(buf, n)) {
            affiche_async("Message ignore : entete invalide\n");
            reaffiche_prompt();
            continue;
        }

        if (buf[0] != '0' && buf[0] != '1' && buf[0] != '2' && buf[0] != '9') {
            affiche_async("Tentative de piratage ou code non autorise : %c depuis %s\n",
                          buf[0], iptxt);
            reaffiche_prompt();
            continue;
        }

        if (buf[0] == '0') {
            pseudo_recu = buf + 6;
            affiche_async("%s (%s) quitte le reseau.\n", pseudo_recu, iptxt);
            supprimeElt(iptxt);
            listeElts();
            reaffiche_prompt();
            continue;
        }

        if (buf[0] == '9') {
            texte = buf + 6;

            pthread_mutex_lock(&mutex_table);
            elt_trouve = chercheEltParIp_locked(iptxt);
            if (elt_trouve == NULL) {
                pthread_mutex_unlock(&mutex_table);
                affiche_async("Message recu d'une IP inconnue (%s) : %s\n",
                              iptxt, texte);
            } else {
                char nomtmp[LPSEUDO + 1];
                strncpy(nomtmp, elt_trouve->nom, LPSEUDO);
                nomtmp[LPSEUDO] = '\0';
                pthread_mutex_unlock(&mutex_table);
                affiche_async("Message de %s : %s\n", nomtmp, texte);
            }
            reaffiche_prompt();
            continue;
        }

        pseudo_recu = buf + 6;
        TRACEF("Message recu de %s : code=%c pseudo=%s\n",
               iptxt, buf[0], pseudo_recu);

        ajouteElt(pseudo_recu, iptxt);
        listeElts();
        reaffiche_prompt();

        if (buf[0] == '1') {
            construire_message(reponse, '2', pseudo);

            if (sendto(sid, reponse, taille_message_beuip(pseudo), 0,
                       (struct sockaddr *)&Sock, ls) == -1) {
                perror("sendto AR");
            } else {
                TRACEF("AR envoye a %s\n", iptxt);
            }
        }
    }

    if (sid != -1)
        close(sid);

    g_udp_sid = -1;
    g_udp_actif = 0;
    g_stop_udp = 0;

    TRACEF("thread UDP arrete.\n");
    return NULL;
}

void envoiContenu(int fd)
{
    char code;
    char nomfic[256];
    char path[512];
    ssize_t nr;
    int i;
    pid_t pid;
    int status;

    if (read(fd, &code, 1) <= 0) {
        close(fd);
        return;
    }

    if (code == 'L') {
        pid = fork();
        if (pid < 0) {
            perror("fork");
            close(fd);
            return;
        }

        if (pid == 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
            execlp("ls", "ls", "-l", g_reppub, (char *)NULL);
            perror("execlp ls");
            _exit(1);
        }

        waitpid(pid, &status, 0);
        close(fd);
        return;
    }

    if (code == 'F') {
        i = 0;
        while (i < (int)sizeof(nomfic) - 1) {
            nr = read(fd, &nomfic[i], 1);
            if (nr <= 0)
                break;
            if (nomfic[i] == '\n')
                break;
            i++;
        }
        nomfic[i] = '\0';

        if (i == 0 || strchr(nomfic, '/') != NULL || strstr(nomfic, "..") != NULL) {
            write(fd, "ERR nom de fichier invalide\n", 28);
            close(fd);
            return;
        }

        snprintf(path, sizeof(path), "%s/%s", g_reppub, nomfic);

        if (access(path, R_OK) != 0) {
            write(fd, "ERR fichier introuvable\n", 24);
            close(fd);
            return;
        }

        if (write(fd, "OK\n", 3) != 3) {
            close(fd);
            return;
        }

        pid = fork();
        if (pid < 0) {
            perror("fork");
            close(fd);
            return;
        }

        if (pid == 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
            execlp("cat", "cat", path, (char *)NULL);
            perror("execlp cat");
            _exit(1);
        }

        waitpid(pid, &status, 0);
        close(fd);
        return;
    }

    close(fd);
}

void *serveur_tcp(void *rep)
{
    char *repertoire = (char *)rep;
    int sid, fd, opt = 1, rc;
    fd_set rset;
    struct timeval tv;
    struct sockaddr_in SockConf;
    struct sockaddr_in Cli;
    socklen_t lcli;
    char ipcli[INET_ADDRSTRLEN];

    if ((sid = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket TCP");
        g_tcp_actif = 0;
        return NULL;
    }

    g_tcp_sid = sid;

    if (setsockopt(sid, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt SO_REUSEADDR");
    }

    memset(&SockConf, 0, sizeof(SockConf));
    SockConf.sin_family = AF_INET;
    SockConf.sin_port = htons(PORT);
    SockConf.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sid, (struct sockaddr *)&SockConf, sizeof(SockConf)) == -1) {
        perror("bind TCP");
        close(sid);
        g_tcp_sid = -1;
        g_tcp_actif = 0;
        return NULL;
    }

    if (listen(sid, 5) == -1) {
        perror("listen");
        close(sid);
        g_tcp_sid = -1;
        g_tcp_actif = 0;
        return NULL;
    }

    TRACEF("thread TCP lance sur le port %d avec le repertoire %s\n",
           PORT, repertoire);

    while (!g_stop_tcp) {
        FD_ZERO(&rset);
        FD_SET(sid, &rset);

        tv.tv_sec = 1;
        tv.tv_usec = 0;

        rc = select(sid + 1, &rset, NULL, NULL, &tv);
        if (rc < 0) {
            if (g_stop_tcp)
                break;
            perror("select");
            continue;
        }

        if (rc == 0)
            continue;

        if (FD_ISSET(sid, &rset)) {
            lcli = sizeof(Cli);
            fd = accept(sid, (struct sockaddr *)&Cli, &lcli);
            if (fd < 0) {
                if (g_stop_tcp)
                    break;
                perror("accept");
                continue;
            }

            if (inet_ntop(AF_INET, &Cli.sin_addr, ipcli, sizeof(ipcli)) == NULL) {
                strcpy(ipcli, "inconnue");
            }

            TRACEF("Connexion TCP acceptee depuis %s\n", ipcli);
            envoiContenu(fd);
        }
    }

    if (sid != -1)
        close(sid);

    g_tcp_sid = -1;
    g_tcp_actif = 0;
    g_stop_tcp = 0;

    TRACEF("thread TCP arrete.\n");
    return NULL;
}

int demandeListe(char *pseudo)
{
    int sid, n;
    char ipdest[16];
    char code = 'L';
    char buf[LBUF];
    struct sockaddr_in Dest;
    struct elt *cour;

    if (pseudo == NULL)
        return -1;

    pthread_mutex_lock(&mutex_table);
    cour = chercheEltParNom_locked(pseudo);
    if (cour == NULL) {
        pthread_mutex_unlock(&mutex_table);
        printf("Pseudo inconnu : %s\n", pseudo);
        return -1;
    }

    strncpy(ipdest, cour->adip, sizeof(ipdest) - 1);
    ipdest[sizeof(ipdest) - 1] = '\0';
    pthread_mutex_unlock(&mutex_table);

    if ((sid = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket TCP client");
        return -1;
    }

    memset(&Dest, 0, sizeof(Dest));
    Dest.sin_family = AF_INET;
    Dest.sin_port = htons(PORT);
    Dest.sin_addr.s_addr = inet_addr(ipdest);

    if (connect(sid, (struct sockaddr *)&Dest, sizeof(Dest)) == -1) {
        perror("connect");
        close(sid);
        return -1;
    }

    if (write(sid, &code, 1) != 1) {
        perror("write");
        close(sid);
        return -1;
    }

    while ((n = read(sid, buf, sizeof(buf))) > 0) {
        write(STDOUT_FILENO, buf, n);
    }

    close(sid);
    return 0;
}

int demandeFichier(char *pseudo, char *nomfic)
{
    int sid, fd, n, i;
    char ipdest[16];
    char req[512];
    char ligne[256];
    char c;
    char buf[LBUF];
    char path[512];
    struct sockaddr_in Dest;
    struct elt *cour;

    if (pseudo == NULL || nomfic == NULL)
        return -1;

    if (strchr(nomfic, '/') != NULL || strstr(nomfic, "..") != NULL) {
        printf("Nom de fichier invalide.\n");
        return -1;
    }

    if (assure_reppub() == -1)
        return -1;

    snprintf(path, sizeof(path), "%s/%s", g_reppub, nomfic);

    if (access(path, F_OK) == 0) {
        printf("Fichier local deja present : %s\n", path);
        return -1;
    }

    pthread_mutex_lock(&mutex_table);
    cour = chercheEltParNom_locked(pseudo);
    if (cour == NULL) {
        pthread_mutex_unlock(&mutex_table);
        printf("Pseudo inconnu : %s\n", pseudo);
        return -1;
    }

    strncpy(ipdest, cour->adip, sizeof(ipdest) - 1);
    ipdest[sizeof(ipdest) - 1] = '\0';
    pthread_mutex_unlock(&mutex_table);

    if ((sid = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket TCP client");
        return -1;
    }

    memset(&Dest, 0, sizeof(Dest));
    Dest.sin_family = AF_INET;
    Dest.sin_port = htons(PORT);
    Dest.sin_addr.s_addr = inet_addr(ipdest);

    if (connect(sid, (struct sockaddr *)&Dest, sizeof(Dest)) == -1) {
        perror("connect");
        close(sid);
        return -1;
    }

    snprintf(req, sizeof(req), "F%s\n", nomfic);

    if (write(sid, req, strlen(req)) != (ssize_t)strlen(req)) {
        perror("write");
        close(sid);
        return -1;
    }

    i = 0;
    while (i < (int)sizeof(ligne) - 1) {
        n = read(sid, &c, 1);
        if (n <= 0)
            break;
        if (c == '\n')
            break;
        ligne[i++] = c;
    }
    ligne[i] = '\0';

    if (strcmp(ligne, "OK") != 0) {
        if (ligne[0] == '\0')
            printf("Erreur distante ou connexion fermee.\n");
        else
            printf("%s\n", ligne);
        close(sid);
        return -1;
    }

    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0664);
    if (fd == -1) {
        perror("open local");
        close(sid);
        return -1;
    }

    while ((n = read(sid, buf, sizeof(buf))) > 0) {
        if (write(fd, buf, n) != n) {
            perror("write local");
            close(fd);
            close(sid);
            unlink(path);
            return -1;
        }
    }

    close(fd);
    close(sid);

    printf("Fichier recu dans %s\n", path);
    return 0;
}

int beuip_start(const char *pseudo)
{
    if (g_udp_actif || g_tcp_actif)
        return -1;

    if (assure_reppub() == -1)
        return -1;

    videListe();

    strncpy(g_udp_pseudo, pseudo, LPSEUDO);
    g_udp_pseudo[LPSEUDO] = '\0';

    g_stop_udp = 0;
    g_stop_tcp = 0;

    if (pthread_create(&g_th_udp, NULL, serveur_udp, g_udp_pseudo) != 0) {
        perror("pthread_create UDP");
        return -1;
    }
    g_udp_actif = 1;

    if (pthread_create(&g_th_tcp, NULL, serveur_tcp, g_reppub) != 0) {
        perror("pthread_create TCP");
        g_stop_udp = 1;
        pthread_join(g_th_udp, NULL);
        g_udp_actif = 0;
        return -1;
    }
    g_tcp_actif = 1;

    return 0;
}

int beuip_stop(void)
{
    char message0[LBUF + 1];
    struct sockaddr_in Dest;
    struct elt *cour;

    if (!g_udp_actif && !g_tcp_actif)
        return -1;

    if (g_udp_actif) {
        construire_message(message0, '0', g_udp_pseudo);

        pthread_mutex_lock(&mutex_table);
        cour = liste_contacts;
        while (cour != NULL) {
            if (!ip_est_locale(cour->adip)) {
                memset(&Dest, 0, sizeof(Dest));
                Dest.sin_family = AF_INET;
                Dest.sin_port = htons(PORT);
                Dest.sin_addr.s_addr = inet_addr(cour->adip);

                if (sendto(g_udp_sid, message0, taille_message_beuip(g_udp_pseudo), 0,
                           (struct sockaddr *)&Dest, sizeof(Dest)) == -1) {
                    perror("sendto stop");
                }
            }
            cour = cour->next;
        }
        pthread_mutex_unlock(&mutex_table);
    }

    g_stop_udp = 1;
    g_stop_tcp = 1;

    if (g_udp_actif)
        pthread_join(g_th_udp, NULL);

    if (g_tcp_actif)
        pthread_join(g_th_tcp, NULL);

    videListe();

    return 0;
}

int beuip_actif(void)
{
    return (g_udp_actif || g_tcp_actif);
}

int commande(char octet1, char *message, char *pseudo)
{
    char message9[LBUF + 1];
    struct sockaddr_in Dest;
    struct elt *cour;
    char ipdest[16];

    if (!g_udp_actif) {
        printf("Serveur UDP inactif.\n");
        return -1;
    }

    if (octet1 == '3') {
        listeElts();
        return 0;
    }

    if (octet1 == '4') {
        if (pseudo == NULL || message == NULL)
            return -1;

        pthread_mutex_lock(&mutex_table);
        cour = chercheEltParNom_locked(pseudo);
        if (cour == NULL) {
            pthread_mutex_unlock(&mutex_table);
            printf("Pseudo inconnu : %s\n", pseudo);
            return -1;
        }

        strncpy(ipdest, cour->adip, sizeof(ipdest) - 1);
        ipdest[sizeof(ipdest) - 1] = '\0';
        pthread_mutex_unlock(&mutex_table);

        memset(&Dest, 0, sizeof(Dest));
        Dest.sin_family = AF_INET;
        Dest.sin_port = htons(PORT);
        Dest.sin_addr.s_addr = inet_addr(ipdest);

        construire_message(message9, '9', message);

        if (sendto(g_udp_sid, message9, taille_message_beuip(message), 0,
                   (struct sockaddr *)&Dest, sizeof(Dest)) == -1) {
            perror("sendto message prive");
            return -1;
        }

        printf("Message prive envoye a %s (%s)\n", pseudo, ipdest);
        return 0;
    }

    if (octet1 == '5') {
        if (message == NULL)
            return -1;

        construire_message(message9, '9', message);

        pthread_mutex_lock(&mutex_table);
        cour = liste_contacts;
        while (cour != NULL) {
            if (!ip_est_locale(cour->adip)) {
                memset(&Dest, 0, sizeof(Dest));
                Dest.sin_family = AF_INET;
                Dest.sin_port = htons(PORT);
                Dest.sin_addr.s_addr = inet_addr(cour->adip);

                if (sendto(g_udp_sid, message9, taille_message_beuip(message), 0,
                           (struct sockaddr *)&Dest, sizeof(Dest)) == -1) {
                    perror("sendto message collectif");
                } else {
                    printf("Message collectif envoye a %s (%s)\n",
                           cour->nom, cour->adip);
                }
            }
            cour = cour->next;
        }
        pthread_mutex_unlock(&mutex_table);

        return 0;
    }

    printf("Commande interne inconnue : %c\n", octet1);
    return -1;
}

int mess_liste(void)
{
    return commande('3', NULL, NULL);
}

int mess_msg(const char *pseudo, const char *texte)
{
    return commande('4', (char *)texte, (char *)pseudo);
}

int mess_all(const char *texte)
{
    return commande('5', (char *)texte, NULL);
}