#ifndef NP_FW_H
#define NP_FW_H

#include "np_version.h"

/* One hex: <config>/exg-c/firmware/knight.hex
 * Mode 0/1/2 is the EEPROM byte, not a different file. */
int np_fw_count(void);
const char *np_fw_file(int mode);
void np_fw_label(int mode, char *out, int n);
void np_fw_short(int mode, char *out, int n);
int np_fw_load(int mode, const char *root, unsigned char *dst, int cap, int *out_len,
               char *err, int err_n);

#endif
