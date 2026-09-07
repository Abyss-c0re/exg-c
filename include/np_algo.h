#ifndef NP_ALGO_H
#define NP_ALGO_H

#include <stdint.h>

/*
 * Resource-friendly 0/1 folds (Algocube / FOLDBITS style).
 * One pass over a short window. No heap, no FFT.
 * Custom is the fold CubalC dialect (IF/FOR-seconds/UNTIL-latch/LET).
 * Not official CubalC FOR i=TO (count) and not cubalc_eeg feature-pack.
 */

#define NP_ALGO_DETECT 0 /* 1 only if EXG vs worn CALM is SIGNAL */
#define NP_ALGO_SIGN 1   /* last sample > 0 */
#define NP_ALGO_MEAN 2   /* |last| above mean |x| */
#define NP_ALGO_ENERGY 3 /* rms above mean |x| */
#define NP_ALGO_DELTA 4  /* step above mean |dx| */
#define NP_ALGO_FOLD 5   /* majority of samples > 0 */
#define NP_ALGO_PROTON 6 /* +energy > half total */
#define NP_ALGO_COMPARE 7 /* if ch1 < ch5 */
#define NP_ALGO_CUSTOM 7  /* old name */
#define NP_ALGO_N 8
#define NP_ALGO_SRC 512
#define NP_ALGO_SRC_DEFAULT "if ch2 < ch5 then ch3 ON\nelse ch3 OFF\n"
#define NP_ALIB_N 16
#define NP_ALIB_NAME 16
#define NP_ALIB_DEF 8

/* Per-jack stats. self is 0..7 for bare ch/last/mean/rms/prev/… */
struct np_algo_bank {
    float last[8];
    float mean[8];
    float rms[8];
    float nn[8];
    float prev[8];
    float dxmean[8];
    float above[8];
    float pos[8];
    float signal[8];
    int self;
};

const char *np_algo_name(int id);
const char *np_algo_rule(int id);
const char *np_algo_def_src(int id);
/* 1 or 0. detect_bit is used only for NP_ALGO_DETECT. */
int np_algo_bit(int id, const float *x, int n, int detect_bit);
int np_algo_compile(const char *src, char *err, int errn);
int np_algo_custom(const char *src, const float *x, int n);
void np_algo_bank_clear(struct np_algo_bank *b);
void np_algo_bank_set(struct np_algo_bank *b, int ch, const float *x, int n);
void np_algo_bank_set_ex(struct np_algo_bank *b, int ch, const float *x, int n,
                         int signal);
int np_algo_custom_bank(const char *src, const struct np_algo_bank *b);
/* Per-channel ON/OFF. wrote[c]=1 if the program set ch(c+1). */
struct np_algo_out {
    uint8_t bit[8];
    uint8_t wrote[8];
    int self_bit;
};
int np_algo_custom_out(const char *src, const struct np_algo_bank *b,
                       struct np_algo_out *o);
/* Monotonic ms for FOR <seconds> holds. 0 = no time. */
void np_algo_set_now(uint64_t now_ms);

#endif
