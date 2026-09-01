#ifndef NP_ALGO_H
#define NP_ALGO_H

/*
 * Resource-friendly 0/1 folds (Algocube / FOLDBITS style).
 * One pass over a short window. No heap, no FFT.
 */

#define NP_ALGO_DETECT 0 /* 1 only if leftover vs worn CALM is SIGNAL */
#define NP_ALGO_SIGN 1   /* last sample > 0 */
#define NP_ALGO_MEAN 2   /* |last| above mean |x| */
#define NP_ALGO_ENERGY 3 /* rms above mean |x| */
#define NP_ALGO_DELTA 4  /* step above mean |dx| */
#define NP_ALGO_FOLD 5   /* majority of samples > 0 */
#define NP_ALGO_PROTON 6 /* +energy > half total */
#define NP_ALGO_N 7

const char *np_algo_name(int id);
/* 1 or 0. detect_bit is used only for NP_ALGO_DETECT. */
int np_algo_bit(int id, const float *x, int n, int detect_bit);

#endif
