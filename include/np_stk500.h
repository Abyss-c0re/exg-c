#ifndef NP_STK500_H
#define NP_STK500_H

/* Optiboot on the Knight Nano speaks STK500v1 at 115200.
 * The application image must stay below the 512-byte bootloader. */
#define NP_STK_PAGE 128
#define NP_STK_APP_MAX (32768 - 512)

struct np_stk_io {
    void *ctx;
    /* Return n on success, or -1. */
    int (*write)(void *ctx, const unsigned char *buf, int n);
    /* Read up to n bytes, waiting at most timeout_ms. 0 on timeout, -1 on error. */
    int (*read)(void *ctx, unsigned char *buf, int n, int timeout_ms);
    void (*pulse_dtr)(void *ctx);
    /* Optional progress line. NULL is fine. */
    void (*note)(void *ctx, const char *line);
};

/* Decode Intel hex into dst[0..*out_len), gaps filled with 0xFF.
 * Rejects records that would land in the bootloader. */
int np_ihex_decode(const char *text, unsigned char *dst, int cap, int *out_len,
                   char *err, int err_n);

/* Program image[0..len) at flash address 0. Pads each page with 0xFF.
 * Pulses DTR, checks the ATmega328P signature, writes, then reads back. */
int np_stk_program(const struct np_stk_io *io, const unsigned char *image, int len,
                   char *err, int err_n);

/* image may be NULL when len < 1. eeprom_byte < 0 skips EEPROM.
 * 0..2 is written at EEPROM address 0 before leaving the bootloader. */
int np_stk_program_ex(const struct np_stk_io *io, const unsigned char *image, int len,
                      int eeprom_byte, char *err, int err_n);

#endif
