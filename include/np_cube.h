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

/* Index x + 8*y + 64*z. A coordinate outside 0..7 returns -1. */
int np_cube_idx(int x, int y, int z);
/* Split an index into x, y, z. An index outside 0..511 is clamped. Any out pointer may be null. */
void np_cube_unidx(int i, int *x, int *y, int *z);
/* 1 when x, y, or z is 0 or 7. */
int np_cube_shell(int x, int y, int z);
/* 1 when that cell is on. A null record or a bad index returns 0. */
int np_cube_get(const struct np_smx *m, int x, int y, int z);
/* Store on as 0 or 1. kind 0 leaves the old kind tag. A null record or a bad index returns. */
void np_cube_set(struct np_smx *m, int x, int y, int z, int on, int kind);
/* Turn off every cell whose kind tag matches. The tags themselves stay. A null record returns. */
void np_cube_clear_kind(struct np_smx *m, int kind);
/* 512 characters, index order, '1' or '0', NUL terminated. cap below 2, or a null pointer, returns 0. */
int np_cube_pack(const struct np_smx *m, char *out, int cap);
/* 64 bytes, bit i in byte i>>3 at bit (i&7). A null pointer returns 0. Success returns 64. */
int np_cube_pack_bin(const struct np_smx *m, uint8_t out[64]);
/* Bit distance of two 64-byte masks. A null array returns 512. */
int np_cube_hamming(const uint8_t a[64], const uint8_t b[64]);

/* 10-10 site → outer-shell cell. x 0=left..7=right, y 0=down..7=up, z 0=back..7=front. */
int np_1010_ijk(int site, int *x, int *y, int *z);
/* Map a cell center into [-1, 1] on each axis. Any out pointer may be null. */
void np_ijk_world(int x, int y, int z, float *wx, float *wy, float *wz);

/* Interior virtual sensors. claim fails on the EEG shell. */
int np_virt_claim(struct np_smx *m, const char *name, int x, int y, int z);
/* Turn the named cell on or off and tag it virtual. An unknown name does nothing. */
void np_virt_write(struct np_smx *m, const char *name, int on);
/* 1 when the named cell is on. An unknown name, or a null record, returns 0. */
int np_virt_read(const struct np_smx *m, const char *name);
/* Slot of an exact name, or -1. A null record or name returns -1. */
int np_virt_find(const struct np_smx *m, const char *name);

/* Built-in IMU → reserved interior cells acc/gyr/mag × XYZ. */
void np_cube_imu(struct np_smx *m, const float acc[3], const float gyr[3], const float mag[3]);

#ifdef __cplusplus
}
#endif

#endif
