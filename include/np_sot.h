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

/* Cached directory for cells.bin. CUBEBRAIN_VIZ_CELLS wins as the parent of that path. Else NP_SOT_DIR, CUBEBRAIN_VIZ_DIR, CUBE_SOT_DIR, or CUBALC_SOT_DIR, then the platform default. */
const char *np_sot_dir(void);
/* Path of cells.bin under that directory. Cached after the first call. */
const char *np_sot_path(void);
/* Remember a name as on or off. Match is case-insensitive. An empty name returns. A new name past 16 entries is dropped. The stored name is cut to 15 characters. */
void np_sot_set(const char *name, int on);
/* Drop every named overlay. The file on disk is left as it was. */
void np_sot_clear(void);
/* Overlay named OUTs onto 512 digits (0/1 from cube bits). */
void np_sot_apply(uint8_t cells[NP_SOT_CELLS]);
/* Write one byte 8, then 512 cell bytes, through a .tmp rename. Also rewrites nodes.tsv. Returns -1 when the temp file cannot be written or renamed. A nodes.tsv failure still returns 0. */
int np_sot_write(const uint8_t cells[NP_SOT_CELLS]);
/* '1' becomes digit 1 and any other character becomes 0. A short string leaves the tail at 0. Named cells are then painted 5 or 0 on top. */
int np_sot_write_bits01(const char *bits512);
/* On Linux, cells 0..7 are loadavg steps of 0.25 (digit 5) and cells 8..15 are busy-percent steps of 12 (digit 4). The name cpu is on when busy percent is over 8. */
int np_sot_write_cpu(void);

#endif
