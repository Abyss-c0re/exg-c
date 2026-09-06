#ifndef NP_ALGO_H
#define NP_ALGO_H

/*
 * Resource-friendly 0/1 folds (Algocube / FOLDBITS style).
 * One pass over a short window. No heap, no FFT.
 * Custom is a CubalC-shaped if/else over last/mean/rms.
 */

#define NP_ALGO_DETECT 0 /* 1 only if EXG vs worn CALM is SIGNAL */
#define NP_ALGO_SIGN 1   /* last sample > 0 */
#define NP_ALGO_MEAN 2   /* |last| above mean |x| */
#define NP_ALGO_ENERGY 3 /* rms above mean |x| */
#define NP_ALGO_DELTA 4  /* step above mean |dx| */
#define NP_ALGO_FOLD 5   /* majority of samples > 0 */
#define NP_ALGO_PROTON 6 /* +energy > half total */
#define NP_ALGO_CUSTOM 7 /* per-bit if/else; ch1..ch8 are last µV */
#define NP_ALGO_N 8
#define NP_ALGO_SRC 512
#define NP_ALGO_SRC_DEFAULT "if ch < 100 then 1\nelse 0\n"

/* Last / mean|x| / rms of each jack. self is 0..7 for bare ch/last/mean/rms. */
struct np_algo_bank {
    float last[8];
    float mean[8];
    float rms[8];
    float nn[8];
    int self;
};

const char *np_algo_name(int id);
/* One line: what makes this algo emit 1. */
const char *np_algo_rule(int id);
/* 1 or 0. detect_bit is used only for NP_ALGO_DETECT. */
int np_algo_bit(int id, const float *x, int n, int detect_bit);
/* Compile CubalC-shaped custom source. 0 ok, -1 and err set on fail. */
int np_algo_compile(const char *src, char *err, int errn);
/* Eval custom source against window x[0..n). ch = last sample. */
int np_algo_custom(const char *src, const float *x, int n);
void np_algo_bank_clear(struct np_algo_bank *b);
void np_algo_bank_set(struct np_algo_bank *b, int ch, const float *x, int n);
int np_algo_custom_bank(const char *src, const struct np_algo_bank *b);

#endif
