#ifndef NP_CUBE_H
#define NP_CUBE_H

/*
 * BrainCube plugin API — one 8×8×8 cube (512 bits).
 *
 * Outside faces follow the headset (Fp1 on the front-left, Cz on top).
 * Inside cells are virtual: IMU by default, or your own name.
 * Bits only. Do not write personal data.
 *
 *   #include "np_cube.h"
 *   np_virt_claim(m, "emg", 3, 4, 5);  // interior only
 *   np_virt_write(m, "emg", 1);
 */

#include "np_smx.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NP_CELL_EMPTY 0
#define NP_CELL_EEG 1
#define NP_CELL_IMU 2
#define NP_CELL_VIRT 3

int np_cube_idx(int x, int y, int z);
void np_cube_unidx(int i, int *x, int *y, int *z);
int np_cube_shell(int x, int y, int z);
int np_cube_get(const struct np_smx *m, int x, int y, int z);
void np_cube_set(struct np_smx *m, int x, int y, int z, int on, int kind);
void np_cube_clear_kind(struct np_smx *m, int kind);
int np_cube_pack(const struct np_smx *m, char *out, int cap);
int np_cube_pack_bin(const struct np_smx *m, uint8_t out[64]);
int np_cube_hamming(const uint8_t a[64], const uint8_t b[64]);

/* 10-10 site → outer-shell cell. x 0=left..7=right, y 0=down..7=up, z 0=back..7=front. */
int np_1010_ijk(int site, int *x, int *y, int *z);
void np_ijk_world(int x, int y, int z, float *wx, float *wy, float *wz);

/* Interior virtual sensors. claim fails on the EEG shell. */
int np_virt_claim(struct np_smx *m, const char *name, int x, int y, int z);
void np_virt_write(struct np_smx *m, const char *name, int on);
int np_virt_read(const struct np_smx *m, const char *name);
int np_virt_find(const struct np_smx *m, const char *name);

/* Built-in IMU → reserved interior cells acc/gyr/mag × XYZ. */
void np_cube_imu(struct np_smx *m, const float acc[3], const float gyr[3], const float mag[3]);

#ifdef __cplusplus
}
#endif

#endif
