#include "np_knight.h"
#include "np_serial.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int i16be(const unsigned char *b)
{
    int v = (b[0] << 8) | b[1];
    if (v & 0x8000) {
        v |= ~0xFFFF;
    }
    return v;
}

static float scale_uv(int raw, int gain)
{
    if (gain < 1) {
        gain = 12;
    }
    return (4.0f / 32767.0f / (float)gain) * 1000000.0f * (float)raw / 79.57f;
}

static float f32le(const unsigned char *b)
{
    union {
        unsigned char c[4];
        float f;
    } u;
    u.c[0] = b[0];
    u.c[1] = b[1];
    u.c[2] = b[2];
    u.c[3] = b[3];
    return u.f;
}

void np_parser_init(struct np_parser *p, enum np_board board)
{
    int i;
    memset(p, 0, sizeof(*p));
    p->board = board;
    p->frame_len = board == NP_BOARD_KNIGHT_IMU ? NP_FRAME_IMU : NP_FRAME_EEG;
    for (i = 0; i < NP_NCHAN; i++) {
        p->gain[i] = 12;
    }
}

void np_parser_set_gain(struct np_parser *p, int ch, int gain)
{
    if (ch >= 1 && ch <= NP_NCHAN && gain >= 1) {
        p->gain[ch - 1] = gain;
    }
}

void np_parser_set_gains(struct np_parser *p, const int gain[NP_NCHAN])
{
    int i;
    if (!p || !gain) {
        return;
    }
    for (i = 0; i < NP_NCHAN; i++) {
        if (gain[i] >= 1) {
            p->gain[i] = gain[i];
        }
    }
}

static int decode_frame(struct np_parser *p, int n, struct np_sample *out)
{
    int i;
    if (n < 21 || p->buf[0] != NP_START || p->buf[n - 1] != NP_END) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->seq = p->buf[1];
    /* Firmware README: 8 × int16 EEG, big-endian, ch 1→8. */
    for (i = 0; i < NP_NCHAN; i++) {
        out->uv[i] = scale_uv(i16be(p->buf + 2 + 2 * i), p->gain[i]);
    }
    out->loff_p = p->buf[18];
    out->loff_n = p->buf[19];
    if (n >= NP_FRAME_IMU) {
        out->imu = 1;
        for (i = 0; i < 3; i++) {
            out->acc[i] = f32le(p->buf + 20 + 4 * i);
            out->gyr[i] = f32le(p->buf + 32 + 4 * i);
            out->mag[i] = f32le(p->buf + 44 + 4 * i);
        }
    }
    return 1;
}

int np_parser_feed(struct np_parser *p, unsigned char b, struct np_sample *out)
{
    const int cand_imu[] = {57, 22, 21};
    const int cand_eeg[] = {22, 21, 57};
    const int *cand = p->board == NP_BOARD_KNIGHT_IMU ? cand_imu : cand_eeg;
    int i;

    if (p->have == 0) {
        if (b != NP_START) {
            return 0;
        }
        p->buf[p->have++] = b;
        return 0;
    }

    if (p->have < NP_FRAME_MAX) {
        p->buf[p->have++] = b;
    } else {
        p->have = 0;
        p->resyncs++;
        if (b == NP_START) {
            p->buf[p->have++] = b;
        }
        return -1;
    }

    /* Locked length: only accept exact end. */
    if (p->locked && p->frame_len > 0) {
        if (p->have < p->frame_len) {
            return 0;
        }
        if (p->buf[p->frame_len - 1] == NP_END && decode_frame(p, p->frame_len, out)) {
            p->have = 0;
            return 1;
        }
        p->have = 0;
        p->locked = 0;
        p->resyncs++;
        if (b == NP_START) {
            p->buf[p->have++] = b;
        }
        return -1;
    }

    /* Hunt: C0 at a known frame end. */
    for (i = 0; i < 3; i++) {
        int n = cand[i];
        if (p->have == n && p->buf[n - 1] == NP_END && decode_frame(p, n, out)) {
            p->frame_len = n;
            p->locked = 1;
            p->have = 0;
            return 1;
        }
    }
    if (p->have >= NP_FRAME_MAX) {
        p->have = 0;
        p->resyncs++;
        return -1;
    }
    return 0;
}

/* Firmware Stream::readString() returns after 1 s of silence, then
 * startsWith("chon_"). A second token inside that window is glued on
 * and dropped. 1.1 s is enough; 1.6 s was padding. Trailing newline
 * is ignored by readString and lets a readStringUntil('\n') build
 * return immediately. */
#define NP_CMD_GAP_US 1250000

static int send_cmd(int fd, const char *s)
{
    int n = (int)strlen(s);
    if (np_serial_write(fd, s, n) != n) {
        return -1;
    }
    usleep(NP_CMD_GAP_US);
    return 0;
}

int np_cmd_chon(int fd, int ch, int gain)
{
    char s[32];
    snprintf(s, sizeof(s), "chon_%d_%d\n", ch, gain);
    return send_cmd(fd, s);
}

int np_cmd_choff(int fd, int ch)
{
    char s[32];
    snprintf(s, sizeof(s), "choff_%d\n", ch);
    return send_cmd(fd, s);
}

int np_cmd_rldadd(int fd, int ch)
{
    char s[32];
    snprintf(s, sizeof(s), "rldadd_%d\n", ch);
    return send_cmd(fd, s);
}

int np_cmd_rldremove(int fd, int ch)
{
    char s[32];
    snprintf(s, sizeof(s), "rldremove_%d\n", ch);
    return send_cmd(fd, s);
}
