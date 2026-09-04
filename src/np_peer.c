#define _GNU_SOURCE
#include "np_peer.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void np_peers_init(struct np_peers *p)
{
    if (p) {
        memset(p, 0, sizeof(*p));
    }
}

static void clip(char *dst, int n, const char *s)
{
    if (!dst || n < 2) {
        return;
    }
    snprintf(dst, (size_t)n, "%s", s ? s : "");
}

int np_peers_load(struct np_peers *p, const char *path)
{
    FILE *f;
    char line[160];
    if (!p) {
        return -1;
    }
    np_peers_init(p);
    if (!path || !path[0]) {
        return -1;
    }
    f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    while (fgets(line, sizeof(line), f)) {
        char kind, name[24], a[64], b[32];
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }
        name[0] = a[0] = b[0] = 0;
        if (sscanf(line, "%c %23s %63s %31s", &kind, name, a, b) < 3) {
            continue;
        }
        if (kind == 'F' && p->nfollow < NP_PEER_MAX) {
            clip(p->follow[p->nfollow].name, NP_PEER_NAME, name);
            clip(p->follow[p->nfollow].dest, NP_PEER_DEST, a);
            clip(p->follow[p->nfollow].grant, NP_PEER_GRANT, b);
            p->nfollow++;
        } else if (kind == 'A' && p->nallow < NP_PEER_MAX) {
            clip(p->allow[p->nallow].name, NP_PEER_NAME, name);
            clip(p->allow[p->nallow].grant, NP_PEER_GRANT, a);
            p->nallow++;
        }
    }
    fclose(f);
    return 0;
}

int np_peers_save(const struct np_peers *p, const char *path)
{
    FILE *f;
    int i;
    if (!p || !path || !path[0]) {
        return -1;
    }
    f = fopen(path, "w");
    if (!f) {
        return -1;
    }
    for (i = 0; i < p->nfollow; i++) {
        fprintf(f, "F %s %s %s\n", p->follow[i].name, p->follow[i].dest, p->follow[i].grant);
    }
    for (i = 0; i < p->nallow; i++) {
        fprintf(f, "A %s %s\n", p->allow[i].name, p->allow[i].grant);
    }
    fclose(f);
    return 0;
}

int np_peers_grant_ok(const struct np_peers *p, const char *grant)
{
    int i;
    if (!p || !grant || !grant[0]) {
        return 0;
    }
    for (i = 0; i < p->nallow; i++) {
        if (strcmp(p->allow[i].grant, grant) == 0) {
            return 1;
        }
    }
    return 0;
}

int np_peers_follow_add(struct np_peers *p, const char *name, const char *dest, const char *grant)
{
    int i;
    if (!p || !name || !name[0] || p->nfollow >= NP_PEER_MAX) {
        return -1;
    }
    for (i = 0; i < p->nfollow; i++) {
        if (strcmp(p->follow[i].name, name) == 0) {
            clip(p->follow[i].dest, NP_PEER_DEST, dest);
            clip(p->follow[i].grant, NP_PEER_GRANT, grant);
            return i;
        }
    }
    clip(p->follow[p->nfollow].name, NP_PEER_NAME, name);
    clip(p->follow[p->nfollow].dest, NP_PEER_DEST, dest);
    clip(p->follow[p->nfollow].grant, NP_PEER_GRANT, grant);
    p->nfollow++;
    return p->nfollow - 1;
}

int np_peers_allow_add(struct np_peers *p, const char *name, const char *grant)
{
    int i;
    if (!p || !grant || !grant[0] || p->nallow >= NP_PEER_MAX) {
        return -1;
    }
    for (i = 0; i < p->nallow; i++) {
        if (strcmp(p->allow[i].grant, grant) == 0 ||
            (name && name[0] && strcmp(p->allow[i].name, name) == 0)) {
            clip(p->allow[i].name, NP_PEER_NAME, name);
            clip(p->allow[i].grant, NP_PEER_GRANT, grant);
            return i;
        }
    }
    clip(p->allow[p->nallow].name, NP_PEER_NAME, name);
    clip(p->allow[p->nallow].grant, NP_PEER_GRANT, grant);
    p->nallow++;
    return p->nallow - 1;
}

void np_peers_follow_del(struct np_peers *p, int i)
{
    if (!p || i < 0 || i >= p->nfollow) {
        return;
    }
    if (i < p->nfollow - 1) {
        p->follow[i] = p->follow[p->nfollow - 1];
    }
    memset(&p->follow[p->nfollow - 1], 0, sizeof(p->follow[0]));
    p->nfollow--;
}

void np_peers_allow_del(struct np_peers *p, int i)
{
    if (!p || i < 0 || i >= p->nallow) {
        return;
    }
    if (i < p->nallow - 1) {
        p->allow[i] = p->allow[p->nallow - 1];
    }
    memset(&p->allow[p->nallow - 1], 0, sizeof(p->allow[0]));
    p->nallow--;
}

void np_peers_mkgrant(char *out, int n)
{
    struct timespec ts;
    unsigned x;
    int i;
    if (!out || n < 9) {
        return;
    }
    clock_gettime(CLOCK_REALTIME, &ts);
    x = (unsigned)ts.tv_nsec ^ (unsigned)ts.tv_sec * 2654435761u;
    for (i = 0; i < n - 1 && i < NP_PEER_GRANT - 1; i++) {
        x = x * 1664525u + 1013904223u;
        out[i] = "abcdefghijkmnpqrstuvwxyz23456789"[x % 32];
    }
    out[i] = 0;
}
