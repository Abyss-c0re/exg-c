#ifndef NP_RING_H
#define NP_RING_H

#include "np_types.h"
#include <pthread.h>

struct np_sample;

struct np_ring {
    pthread_mutex_t mu;
    float ch[NP_NCHAN][NP_RING];
    float acc[3][NP_RING];
    uint32_t wr;
    uint64_t total;
    uint32_t good;
    uint32_t bad;
    uint8_t loff_p;
    uint8_t loff_n;
};

void np_ring_init(struct np_ring *r);
void np_ring_push(struct np_ring *r, const struct np_sample *s);
uint32_t np_ring_copy(struct np_ring *r, int ch, float *dst, uint32_t n);
void np_ring_stats(struct np_ring *r, uint64_t *total, uint32_t *good, uint32_t *bad);
void np_ring_loff(struct np_ring *r, uint8_t *p, uint8_t *n);

#endif
