#include "np_ring.h"
#include "np_knight.h"

#include <string.h>

void np_ring_init(struct np_ring *r)
{
    memset(r, 0, sizeof(*r));
    pthread_mutex_init(&r->mu, NULL);
}

void np_ring_push(struct np_ring *r, const struct np_sample *s)
{
    uint32_t i, w;
    pthread_mutex_lock(&r->mu);
    w = r->wr % NP_RING;
    for (i = 0; i < NP_NCHAN; i++) {
        r->ch[i][w] = s->uv[i];
    }
    r->acc[0][w] = s->acc[0];
    r->acc[1][w] = s->acc[1];
    r->acc[2][w] = s->acc[2];
    r->loff_p = s->loff_p;
    r->loff_n = s->loff_n;
    r->wr++;
    r->total++;
    r->good++;
    pthread_mutex_unlock(&r->mu);
}

uint32_t np_ring_copy(struct np_ring *r, int ch, float *dst, uint32_t n)
{
    uint32_t have, i, start;
    if (ch < 0 || ch >= NP_NCHAN) {
        return 0;
    }
    pthread_mutex_lock(&r->mu);
    have = r->wr < NP_RING ? r->wr : NP_RING;
    if (n > have) {
        n = have;
    }
    start = (r->wr + NP_RING - n) % NP_RING;
    for (i = 0; i < n; i++) {
        dst[i] = r->ch[ch][(start + i) % NP_RING];
    }
    pthread_mutex_unlock(&r->mu);
    return n;
}

void np_ring_stats(struct np_ring *r, uint64_t *total, uint32_t *good, uint32_t *bad)
{
    pthread_mutex_lock(&r->mu);
    if (total) {
        *total = r->total;
    }
    if (good) {
        *good = r->good;
    }
    if (bad) {
        *bad = r->bad;
    }
    pthread_mutex_unlock(&r->mu);
}

void np_ring_loff(struct np_ring *r, uint8_t *p, uint8_t *n)
{
    pthread_mutex_lock(&r->mu);
    if (p) {
        *p = r->loff_p;
    }
    if (n) {
        *n = r->loff_n;
    }
    pthread_mutex_unlock(&r->mu);
}
