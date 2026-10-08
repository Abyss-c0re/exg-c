#ifndef NP_LEARN_H
#define NP_LEARN_H

#include "np_app.h"

/* Internal. Owner: src/np_learn.c. Product API stays in np_host.h. */

extern float atom_raw[NP_ATOM_RING][NP_NCHAN * NP_ATOM_WIN];
extern float id_base[NP_NCHAN];
extern int id_base_ok;
/* Copies only letters, digits, '-' and '_' into dst. A null or short dst returns without writing, and a null source clears dst. */
void atom_sanitize(char *dst, int n, const char *src);
/* Path of the .npat file after the letter filter. -1 if the name is empty once filtered. */
int atom_path(char *out, int n, const char *name);
/* Copies the newest k one-second folds, oldest first. k under 1 returns without writing, k is clamped to the folds on hand, bits or rms may be NULL, and each rms second is 8 floats in µV. */
void atom_last(uint64_t *bits, float *rms, int k);
/* Bit agreement of the live folds with the loaded take, 0..1. Sets 0 and returns if either side has no seconds. */
void atom_score(void);
/* Median calm RMS in µV over active channels above 50 µV. 50 µV if there is no calm plate or no such channel. */
float atom_scale(void);
extern uint8_t rec_smx[NPL_SMX_SEC];
extern int rec_smx_n;
/* Fills missing built-in algos up to the default eight and clamps the selection into range. Does not write the ini. */
void alib_seed(void);
/* Algo index for that cube bit. A negative bit inherits the cube algo, then 0 if that is outside the library. Out of range returns the live algo index. */
int made_eff_algo(int cube, int q);
/* Source text for library index i, or the built-in text if that slot is empty. An index outside the library, including -1, returns the compare default. */
const char *alib_src(int i);
/* Last 2 seconds of each active channel, filtered, into the algo bank. A channel with no samples is left empty. */
void fill_algo_bank(struct np_algo_bank *b);
/* Copies bits the algo wrote. If it did not write self but asked for self_bit, that channel is forced on. A null result returns immediately. */
void apply_algo_out(uint8_t bits[NP_NCHAN], const struct np_algo_out *o,
                           int self);
/* Stamps the algo clock from the monotonic clock, in milliseconds. A failed clock read leaves the stamp alone. */
void algo_now(void);
/* Runs the live algo, or each made-cube bit, and returns the eight channel bits in one byte. bits is cleared first. Bit 0 is channel 1. */
uint8_t learn_fold_byte(uint8_t bits[NP_NCHAN]);
/* Last ~0.5 s vs a rolling quiet floor. A shared floor above about 0.8 mV, with NEG RAIL off, turns CAR on and saves. ratio may be NULL. */
int stream_id(float *ratio);
/* Rereads *.npat names into the take list, sorted. A missing directory returns 0. */
int atom_rescan(void);
/* Closeness of the two picked take files. Empty slots or a bad path leave it at 0. A montage mismatch is negative; identical RMS is 1. */
void atom_pair_score(void);

#endif /* NP_LEARN_H */
