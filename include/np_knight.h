#ifndef NP_KNIGHT_H
#define NP_KNIGHT_H

#include "np_types.h"
#include <stddef.h>

/* Knight µV: 4/32767/gain*1e6 / this. CLIP, plot, plates, and ID use it. */
#define NP_KNIGHT_DIV 79.57f

struct np_sample {
    uint8_t seq;
    uint8_t loff_p; /* P contact, bit 0 = ch1 */
    uint8_t loff_n; /* N contact, bit 0 = ch1 */
    uint8_t drops;  /* (seq - expected) & 0xFF; 0 if first or in order */
    int imu;
    float uv[NP_NCHAN];
    float acc[3]; /* m/s² */
    float gyr[3]; /* rad/s */
    float mag[3]; /* µT */
};

struct np_parser {
    enum np_board board;
    int gain[NP_NCHAN];
    unsigned char buf[NP_FRAME_MAX];
    int have;
    int locked;
    int frame_len; /* 21 in EXG mode, 57 in IMU mode. No 22-byte frame. */
    int have_seq;
    uint8_t last_seq;
    uint32_t resyncs;
    uint32_t drops;
    /* AUTO: a finished frame sits in buf until the next byte arrives. */
    int ready;
    int stashed;
    unsigned char stash;
};

/* Zero the parser. IMU uses 57-byte frames, AUTO starts unlocked, any other board uses 21-byte EEG. Each channel gain starts at 12. */
void np_parser_init(struct np_parser *p, enum np_board board);
/* Set one channel, numbered 1..8. A bad channel or a gain other than 1, 2, 3, 4, 6, 8, or 12 is ignored. */
void np_parser_set_gain(struct np_parser *p, int ch, int gain);
/* Copy legal gains into slots 0..7, which are channels 1..8. A null parser or array returns. An illegal entry is left as it was. */
void np_parser_set_gains(struct np_parser *p, const int gain[NP_NCHAN]);
/* Feed one byte. Returns 1 with a sample, 0 while a frame is still open, or -1 on a resync. */
int np_parser_feed(struct np_parser *p, unsigned char b, struct np_sample *out);

/* Write "chon_<ch>_<gain>" and a newline. Returns the snprintf count, which can exceed n. */
int np_fmt_chon(char *s, size_t n, int ch, int gain);
/* Write "choff_<ch>" and a newline. Returns the snprintf count, which can exceed n. */
int np_fmt_choff(char *s, size_t n, int ch);
/* Write "rldadd_<ch>" and a newline. Returns the snprintf count, which can exceed n. */
int np_fmt_rldadd(char *s, size_t n, int ch);
/* Write "rldremove_<ch>" and a newline. Returns the snprintf count, which can exceed n. */
int np_fmt_rldremove(char *s, size_t n, int ch);
/* Send chon and wait 1.25 s. Returns -1 when the write is short. */
int np_cmd_chon(int fd, int ch, int gain);
/* Send choff and wait 1.25 s. Returns -1 when the write is short. */
int np_cmd_choff(int fd, int ch);
/* Send rldadd and wait 1.25 s. Returns -1 when the write is short. */
int np_cmd_rldadd(int fd, int ch);
/* Send rldremove and wait 1.25 s. Returns -1 when the write is short. */
int np_cmd_rldremove(int fd, int ch);
/* Write "exgmode_<mode>" and a newline. mode outside 0..2 is written as 0. Returns the snprintf count, which can exceed n. */
int np_fmt_mode(char *s, size_t n, int mode);
/* Send exgmode and wait 1.25 s. Returns -1 when the write is short. */
int np_cmd_mode(int fd, int mode);

#endif
