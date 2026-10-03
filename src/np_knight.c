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
    return (4.0f / 32767.0f / (float)gain) * 1000000.0f / NP_KNIGHT_DIV
        * (float)raw;
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
    if (ch >= 1 && ch <= NP_NCHAN && np_gain_ok(gain)) {
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
        if (np_gain_ok(gain[i])) {
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
    if (p->have_seq) {
        uint8_t expect = (uint8_t)(p->last_seq + 1);
        if (out->seq != expect) {
            out->drops = (uint8_t)(out->seq - expect);
            p->drops += out->drops;
        }
    }
    p->last_seq = out->seq;
    p->have_seq = 1;
    /* Data format: 8 × int16 EEG, big-endian, ch 1→8. */
    for (i = 0; i < NP_NCHAN; i++) {
        out->uv[i] = scale_uv(i16be(p->buf + 2 + 2 * i), p->gain[i]);
    }
    /* P/N contact, bit 0 = channel 1. */
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
    int want, i, from;

    /* The board mode is the frame length. A 0xC0 inside the IMU floats
     * must not lock a 21-byte frame, and there is no 22-byte format. */
    want = (p->locked && p->frame_len > 0) ? p->frame_len
                                            : (p->board == NP_BOARD_KNIGHT_IMU ? NP_FRAME_IMU
                                                                               : NP_FRAME_EEG);

    if (p->have == 0) {
        if (b != NP_START) {
            return 0;
        }
        p->buf[0] = b;
        p->have = 1;
        return 0;
    }
    if (p->have >= NP_FRAME_MAX) {
        p->have = 0;
        p->locked = 0;
        p->resyncs++;
        if (b == NP_START) {
            p->buf[0] = b;
            p->have = 1;
        }
        return -1;
    }
    p->buf[p->have++] = b;
    if (p->have < want) {
        return 0;
    }
    if (p->buf[want - 1] == NP_END && decode_frame(p, want, out)) {
        p->frame_len = want;
        p->locked = 1;
        p->have = 0;
        return 1;
    }

    /* Missed the end marker. Keep a later start byte, including this one. */
    p->locked = 0;
    p->resyncs++;
    from = 0;
    for (i = 1; i < p->have; i++) {
        if (p->buf[i] == NP_START) {
            from = i;
            break;
        }
    }
    if (from > 0) {
        memmove(p->buf, p->buf + from, (size_t)(p->have - from));
        p->have -= from;
    } else {
        p->have = 0;
    }
    return -1;
}

/* Official command set: ≥1 s between commands (2 s after stream
 * before the first). Firmware readString() also needs ~1 s of silence
 * or a second token is glued on and dropped. Trailing newline lets a
 * readStringUntil('\n') return immediately. */
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

int np_fmt_chon(char *s, size_t n, int ch, int gain)
{
    return snprintf(s, n, "chon_%d_%d\n", ch, gain);
}

int np_fmt_choff(char *s, size_t n, int ch)
{
    return snprintf(s, n, "choff_%d\n", ch);
}

int np_fmt_rldadd(char *s, size_t n, int ch)
{
    return snprintf(s, n, "rldadd_%d\n", ch);
}

int np_fmt_rldremove(char *s, size_t n, int ch)
{
    return snprintf(s, n, "rldremove_%d\n", ch);
}

int np_cmd_chon(int fd, int ch, int gain)
{
    char s[32];
    np_fmt_chon(s, sizeof(s), ch, gain);
    return send_cmd(fd, s);
}

int np_cmd_choff(int fd, int ch)
{
    char s[32];
    np_fmt_choff(s, sizeof(s), ch);
    return send_cmd(fd, s);
}

int np_cmd_rldadd(int fd, int ch)
{
    char s[32];
    np_fmt_rldadd(s, sizeof(s), ch);
    return send_cmd(fd, s);
}

int np_cmd_rldremove(int fd, int ch)
{
    char s[32];
    np_fmt_rldremove(s, sizeof(s), ch);
    return send_cmd(fd, s);
}
