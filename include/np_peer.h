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

/* Zero both lists. A null pointer does nothing. */
void np_peers_init(struct np_peers *p);
/* Replace both lists from text. A missing file returns 0 and leaves the lists empty. A null p or path returns -1. Blank lines, # lines, and short lines are skipped. */
int np_peers_load(struct np_peers *p, const char *path);
/* Write F lines, then A lines. Returns -1 when p or path is missing or the file will not open. */
int np_peers_save(const struct np_peers *p, const char *path);
/* 1 when grant matches an allow entry. A null list, or an empty grant, returns 0. */
int np_peers_grant_ok(const struct np_peers *p, const char *grant);
/* Update dest and grant when the name exists, otherwise append, and return the slot. Returns -1 for an empty name or a full list. A full list will not update an existing name. */
int np_peers_follow_add(struct np_peers *p, const char *name, const char *dest, const char *grant);
/* Update an entry that already has this grant or this name, otherwise append. Returns -1 for an empty grant or a full list. */
int np_peers_allow_add(struct np_peers *p, const char *name, const char *grant);
/* Drop slot i by swapping it with the last follow. Order is not kept. Out of range does nothing. */
void np_peers_follow_del(struct np_peers *p, int i);
/* Drop slot i by swapping it with the last allow. Order is not kept. Out of range does nothing. */
void np_peers_allow_del(struct np_peers *p, int i);
/* Fill out with a clock-seeded token. The alphabet has no 0, 1, l, or o. n < 9 returns without writing. Length is min(n - 1, NP_PEER_GRANT - 1). */
void np_peers_mkgrant(char *out, int n);

#endif
