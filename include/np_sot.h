#ifndef NP_SOT_H
#define NP_SOT_H

#include <stdint.h>

/*
 * Networkless lattice SoT. Same cells.bin the Cube app already reads:
 *   first byte n=8, then n³ digits 0–9.
 * OUT sensor name lights a named cell. EEG host writes this when connected.
 */

#define NP_SOT_N 8
#define NP_SOT_CELLS 512
#define NP_SOT_NAMES 16
#define NP_SOT_NAME 16

const char *np_sot_dir(void);
const char *np_sot_path(void);
void np_sot_set(const char *name, int on);
void np_sot_clear(void);
/* Overlay named OUTs onto 512 digits (0/1 from cube bits). */
void np_sot_apply(uint8_t cells[NP_SOT_CELLS]);
int np_sot_write(const uint8_t cells[NP_SOT_CELLS]);
int np_sot_write_bits01(const char *bits512);
int np_sot_write_cpu(void);

#endif
