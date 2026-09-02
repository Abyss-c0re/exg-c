#ifndef NPLEARN_H
#define NPLEARN_H

/*
 * nplearn — small EXG template matcher.
 *
 * No heap, no FILE, no threads. Compile nplearn.c + nplearn_filt.c
 * for a microcontroller. Optional nplearn_posix.c adds path save/load.
 *
 * Typical MCU loop:
 *   npl_prep(wave[ch], &rms[ch], adc_uv, n, sps, 50.f);
 *   npl_score(&L, wave, rms, mask);
 * Persist with npl_export / npl_import into flash.
 */

#include <stdint.h>

#define NPL_NCHAN 8
#define NPL_LEN 64
#define NPL_MAX 16
#define NPL_NAME 24
#define NPL_HP_HZ 2.f
#define NPL_LP_HZ 40.f
#define NPL_SMX_SEC 8

struct npl_sample {
    char name[NPL_NAME];
    uint8_t mask;
    float rms[NPL_NCHAN];
    float wave[NPL_NCHAN][NPL_LEN];
    uint8_t cube[64]; /* packed 8^3 State Matrix */
    uint8_t have_cube;
    uint8_t smx[NPL_SMX_SEC]; /* 8 channel bits per second */
    uint8_t smx_n;
    uint8_t fold; /* last-second 8-ch fold */
};

struct npl {
    struct npl_sample s[NPL_MAX];
    int n;
    int match;
    int sel;
    int best;
    float score[NPL_MAX];      /* wave+RMS cosine; best is fail-closed */
    float score_cube[NPL_MAX]; /* Jaccard — not ID, do not print as % */
};

void npl_init(struct npl *L);

/* HP 2 Hz + optional notch (off if notch_hz<=1) + LP 40 Hz + detrend. */
void npl_filter(float *x, int n, float sps, float notch_hz);

/* Filter, drop filter warmup, pack to NPL_LEN (unit energy), write RMS.
 * Returns 0 if n is usable. Never rejects on amplitude — rail is valid input. */
int npl_prep(float wave[NPL_LEN], float *rms, const float *src, int n, float sps,
             float notch_hz);

void npl_pack(float *dst, const float *src, int n);

int npl_add(struct npl *L, const char *name, const float wave[NPL_NCHAN][NPL_LEN],
            const float rms[NPL_NCHAN], uint8_t mask);
void npl_set_cube(struct npl *L, int i, const uint8_t cube[64]);
void npl_set_smx(struct npl *L, int i, const uint8_t *rows, int n, uint8_t fold);
void npl_del(struct npl *L, int i);
void npl_score(struct npl *L, const float wave[NPL_NCHAN][NPL_LEN],
               const float rms[NPL_NCHAN], uint8_t mask);
/* Jaccard on occupied cells. Empty vs empty is 0 (fail closed). */
float npl_cube_jaccard(const uint8_t a[64], const uint8_t b[64]);
void npl_score_cube(struct npl *L, const uint8_t cube[64]);
void npl_score_smx(struct npl *L, const uint8_t *rows, int n);

/* Blob for flash / EEPROM. npl_bound() is the max byte size. */
int npl_bound(void);
int npl_export(const struct npl *L, void *buf, int cap);
int npl_import(struct npl *L, const void *buf, int n);

#ifdef NPL_POSIX
int npl_save(const struct npl *L, const char *path);
int npl_load(struct npl *L, const char *path);
#endif

#endif
