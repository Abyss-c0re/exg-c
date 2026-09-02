#ifndef NP_ATOM_H
#define NP_ATOM_H

/*
 * CubalC-compatible EEG atom. One window → 64 bits (8 feature bits × 8 ch).
 * Same layout as cubalc_eeg_pack_matrix (n_ch ≤ 8). Not a waveform.
 */

#include <stdint.h>

#define NP_ATOM_BITS 64
#define NP_ATOM_WIN 125
#define NP_ATOM_RING 256
#define NP_ATOM_NAME 24
#define NP_ATOM_SCALE 50.f

/* planar[ch * stride + sample]. n_ch ≤ 8. */
uint64_t np_atom_pack(const float *planar, int n_ch, int n_samp, int stride, float scale_uv);

int np_atom_popcount(uint64_t a);
int np_atom_hamming(uint64_t a, uint64_t b);
/* 1 - Hamming/64. Two zeros are 1 — caller must require n ≥ 1. */
float np_atom_unity(uint64_t a, uint64_t b);

/* Newest-aligned mean unity. 0 if either side is empty. */
float np_atom_ring_unity(const uint64_t *live, int nlive, const uint64_t *ref, int nref);

/* Binary chain: NPAT + ver + n_ch + win + count + u64le atoms. */
void np_atom_rms8(const float *planar, int n_ch, int n_samp, int stride, float rms[8]);
/* Newest-aligned mean cosine of 8-ch RMS vectors. 0 if empty. */
float np_atom_rms_cos(const float *live, int nlive, const float *ref, int nref);

/* v2: NPAT + bits + 8×f32 RMS per second. v1 load still works (rms left 0). */
int np_atom_save(const char *path, const uint64_t *a, int n, int win);
int np_atom_save2(const char *path, const uint64_t *a, const float *rms, int n, int win);
int np_atom_load(const char *path, uint64_t *a, int cap, int *win);
int np_atom_load2(const char *path, uint64_t *a, float *rms, int cap, int *win, int *have_rms);

#endif
