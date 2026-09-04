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
/* Per-channel scale from leftover baseline (CALM / id_base). Not 50 µV. */
uint64_t np_atom_pack_rel(const float *planar, int n_ch, int n_samp, int stride,
                          const float base_uv[8]);
/* One EXG1 sample vs baseline. n=1 is honest but thin; prefer a short window. */
uint64_t np_atom_from_uv8(const float uv[8], const float base_uv[8]);
/* 64 signal bits on 8 cube faces (ch × feature). Interior stays 0. Not 512 independent bits. */
void np_atom_faces8(uint64_t atom, uint8_t cube[512]);

int np_atom_popcount(uint64_t a);
int np_atom_hamming(uint64_t a, uint64_t b);
/* 1 - Hamming/64. Two zeros are 1 — caller must require n ≥ 1. */
float np_atom_unity(uint64_t a, uint64_t b);

/* Newest-aligned mean unity. 0 if either side is empty. */
float np_atom_ring_unity(const uint64_t *live, int nlive, const uint64_t *ref, int nref);

/* Binary chain: NPAT + ver + n_ch + win + count + u64le atoms. */
void np_atom_rms8(const float *planar, int n_ch, int n_samp, int stride, float rms[8]);
/* Newest-aligned mean cosine of 8-ch RMS vectors. Scale-blind — do not use for ID. */
float np_atom_rms_cos(const float *live, int nlive, const float *ref, int nref);
/* Newest-aligned closeness on log RMS. 1 = same loudness+shape, 2× all-ch ≈ 0.5. */
float np_atom_rms_close(const float *live, int nlive, const float *ref, int nref);
/* Last live second vs mean RMS of the take. Dilutes a gesture with rest padding. */
float np_atom_rms_close_to_mean(const float *live, int nlive, const float *ref, int nref);
/* Distinctive seconds vs baseline (rest/CALM). Rest-like take → full mean.
 * Action → mean of seconds farther than 0.85 from baseline. 0 if none. */
int np_atom_rms_pattern(const float *ref, int nref, const float *base, int nbase, float out[8]);
/* Last live second vs that pattern. No live accumulation. */
float np_atom_rms_close_to_pattern(const float *live, int nlive, const float *ref, int nref,
                                   const float *base, int nbase);
/* Two NPAT files: log-RMS if both v2. 0 if either is empty or v1. */
float np_atom_file_close(const char *pa, const char *pb);

/* v2: NPAT + bits + 8×f32 RMS per second. v1 load still works (rms left 0). */
int np_atom_save(const char *path, const uint64_t *a, int n, int win);
int np_atom_save2(const char *path, const uint64_t *a, const float *rms, int n, int win);
int np_atom_load(const char *path, uint64_t *a, int cap, int *win);
int np_atom_load2(const char *path, uint64_t *a, float *rms, int cap, int *win, int *have_rms);

/* Raw planar (ch-major) so a filter change can recook plates and takes. */
int np_raw_save(const char *path, const float *planar, int n_ch, int n_samp, float sps);
int np_raw_load(const char *path, float *planar, int cap, int *n_ch, int *n_samp, float *sps);

#endif
