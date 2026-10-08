#ifndef NP_FW_H
#define NP_FW_H

#include "np_version.h"

/* One hex: <config>/exg-c/firmware/knight.hex
 * Mode 0/1/2 is the EEPROM byte, not a different file. */
int np_fw_count(void);
/* The hex name. mode is ignored; every slot is knight.hex. */
const char *np_fw_file(int mode);
/* Write the mode name and its blurb into out. A null out or n < 1 returns. A mode outside 0..2 stores an empty string. */
void np_fw_label(int mode, char *out, int n);
/* Write the short name: "125 + IMU", "250 EEG", or "500 EEG". A null out or n < 1 returns. A mode outside 0..2 stores an empty string. */
void np_fw_short(int mode, char *out, int n);
/* Read <root>/exg-c/firmware/knight.hex and decode it into dst. cap must be at least NP_STK_APP_MAX. A bad slot, a missing folder, or a file under 11 or over 200000 bytes returns -1. */
int np_fw_load(int mode, const char *root, unsigned char *dst, int cap, int *out_len,
               char *err, int err_n);

#endif
