#ifndef NP_KNIGHT_H
#define NP_KNIGHT_H

#include "np_types.h"

struct np_sample {
    uint8_t seq;
    uint8_t loff_p;
    uint8_t loff_n;
    int imu;
    float uv[NP_NCHAN];
    float acc[3];
    float gyr[3];
    float mag[3];
};

struct np_parser {
    enum np_board board;
    int gain[NP_NCHAN];
    unsigned char buf[NP_FRAME_MAX];
    int have;
    int locked;
    int frame_len; /* 21, 22 or 57 once seen */
    uint32_t resyncs;
};

void np_parser_init(struct np_parser *p, enum np_board board);
void np_parser_set_gain(struct np_parser *p, int ch, int gain);
void np_parser_set_gains(struct np_parser *p, const int gain[NP_NCHAN]);
int np_parser_feed(struct np_parser *p, unsigned char b, struct np_sample *out);

int np_cmd_chon(int fd, int ch, int gain);
int np_cmd_choff(int fd, int ch);
int np_cmd_rldadd(int fd, int ch);
int np_cmd_rldremove(int fd, int ch);

#endif
