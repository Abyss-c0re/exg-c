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
/* Per-channel scale from baseline (CALM / id_base). Not 50 µV. */
uint64_t np_atom_pack_rel(const float *planar, int n_ch, int n_samp, int stride,
                          const float base_uv[8]);
/* One EXG1 sample vs baseline. n=1 is honest but thin; prefer a short window. */
uint64_t np_atom_from_uv8(const float uv[8], const float base_uv[8]);
/* Pack layout: 8 feature bits × 8 ch. Not the crimson 8³ picture. */
void np_atom_faces8(uint64_t atom, uint8_t cube[64]);

/* Count the bits that are set. */
int np_atom_popcount(uint64_t a);
/* Count the bits that differ. */
int np_atom_hamming(uint64_t a, uint64_t b);
/* 1 - Hamming/64. Two zeros are 1 — caller must require n ≥ 1. */
float np_atom_unity(uint64_t a, uint64_t b);

/* Newest-aligned mean unity. 0 if either side is empty. */
float np_atom_ring_unity(const uint64_t *live, int nlive, const uint64_t *ref, int nref);

/* RMS of each planar channel, up to 8. A short window stores zeros. */
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

/* v2: NPAT + bits + 8×f32 RMS per second. v1 load still works (rms left 0).
 * n_ch byte bit 7 is the montage: 0 referential, 1 NEG RAIL pair. Old files read as 0. */
int np_atom_save(const char *path, const uint64_t *a, int n, int win);
/* Write NPAT version 2 when rms is non-null, otherwise version 1. The montage pair bit stays clear. */
int np_atom_save2(const char *path, const uint64_t *a, const float *rms, int n, int win);
/* Write a 12-byte NPAT header, then one atom per second and eight floats when rms is non-null. The channel byte is 8, with bit 7 set when pair is non-zero. win below 1 becomes 125. */
int np_atom_save_m(const char *path, const uint64_t *a, const float *rms, int n, int win, int pair);
/* 0 referential or legacy, 1 NEG RAIL pair, -1 unreadable. */
int np_atom_montage(const char *path);
/* Load atoms only. Version-2 RMS floats are read and discarded. */
int np_atom_load(const char *path, uint64_t *a, int cap, int *win);
/* Load at most cap atoms. A longer file is clipped and still returns the count stored. *have_rms is 1 for version 2 even when rms is null. */
int np_atom_load2(const char *path, uint64_t *a, float *rms, int cap, int *win, int *have_rms);

/* Raw planar (ch-major) so a filter change can recook plates and takes. */
int np_raw_save(const char *path, const float *planar, int n_ch, int n_samp, float sps);
/* Read NPRW and return how many floats were stored, the lesser of the file and cap. n_samp stays the file's full count. sps is copied as raw float bits. A short read returns -1. */
int np_raw_load(const char *path, float *planar, int cap, int *n_ch, int *n_samp, float *sps);

#endif
