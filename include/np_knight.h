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
    int frame_len; /* 21 (NP_DEFAULT), 57 (NP_IMU); 22 is a hunt leftover */
    int have_seq;
    uint8_t last_seq;
    uint32_t resyncs;
    uint32_t drops;
};

void np_parser_init(struct np_parser *p, enum np_board board);
void np_parser_set_gain(struct np_parser *p, int ch, int gain);
void np_parser_set_gains(struct np_parser *p, const int gain[NP_NCHAN]);
int np_parser_feed(struct np_parser *p, unsigned char b, struct np_sample *out);

int np_fmt_chon(char *s, size_t n, int ch, int gain);
int np_fmt_choff(char *s, size_t n, int ch);
int np_fmt_rldadd(char *s, size_t n, int ch);
int np_fmt_rldremove(char *s, size_t n, int ch);
int np_cmd_chon(int fd, int ch, int gain);
int np_cmd_choff(int fd, int ch);
int np_cmd_rldadd(int fd, int ch);
int np_cmd_rldremove(int fd, int ch);

#endif
