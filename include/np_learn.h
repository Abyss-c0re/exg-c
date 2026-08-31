#ifndef NP_LEARN_H
#define NP_LEARN_H

#include "np_types.h"

#define NP_LEARN_MAX 16
#define NP_LEARN_LEN 64
#define NP_LEARN_NAME 24

struct np_tmpl {
    char name[NP_LEARN_NAME];
    uint8_t mask;
    float rms[NP_NCHAN];
    float wave[NP_NCHAN][NP_LEARN_LEN];
};

struct np_learn {
    struct np_tmpl t[NP_LEARN_MAX];
    int n;
    int match;
    int sel;
    int best;
    float score[NP_LEARN_MAX];
};

void np_learn_init(struct np_learn *L);
/* Downsample + unit-energy. src is already filtered/detrended. */
void np_learn_pack(float *dst, const float *src, int n);
float np_learn_rms(const float *w);
int np_learn_add(struct np_learn *L, const char *name, const float wave[NP_NCHAN][NP_LEARN_LEN],
                 const float rms[NP_NCHAN], uint8_t mask);
void np_learn_del(struct np_learn *L, int i);
void np_learn_score(struct np_learn *L, const float wave[NP_NCHAN][NP_LEARN_LEN],
                    const float rms[NP_NCHAN], uint8_t mask);
int np_learn_save(const struct np_learn *L, const char *path);
int np_learn_load(struct np_learn *L, const char *path);

#endif
