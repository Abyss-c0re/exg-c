#ifndef NP_PEER_H
#define NP_PEER_H

#define NP_PEER_MAX 16
#define NP_PEER_NAME 24
#define NP_PEER_DEST 64
#define NP_PEER_GRANT 32

struct np_follow {
    char name[NP_PEER_NAME];
    char dest[NP_PEER_DEST];
    char grant[NP_PEER_GRANT];
};

struct np_allow {
    char name[NP_PEER_NAME];
    char grant[NP_PEER_GRANT];
};

struct np_peers {
    struct np_follow follow[NP_PEER_MAX];
    int nfollow;
    struct np_allow allow[NP_PEER_MAX];
    int nallow;
};

void np_peers_init(struct np_peers *p);
int np_peers_load(struct np_peers *p, const char *path);
int np_peers_save(const struct np_peers *p, const char *path);
int np_peers_grant_ok(const struct np_peers *p, const char *grant);
int np_peers_follow_add(struct np_peers *p, const char *name, const char *dest, const char *grant);
int np_peers_allow_add(struct np_peers *p, const char *name, const char *grant);
void np_peers_follow_del(struct np_peers *p, int i);
void np_peers_allow_del(struct np_peers *p, int i);
void np_peers_mkgrant(char *out, int n);

#endif
