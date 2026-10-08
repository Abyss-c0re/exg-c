#ifndef NP_API_H
#define NP_API_H

#include "np_types.h"

#define NP_API_FRAME 68
#define NP_API_TOKEN 32
#define NP_API_PUSH 64

enum np_api_op {
    NP_API_OP_NONE = 0,
    NP_API_OP_CONNECT,
    NP_API_OP_DISC,
    NP_API_OP_PAUSE,
    NP_API_OP_NOTCH,
    NP_API_OP_HP,
    NP_API_OP_LP,
    NP_API_OP_CAR,
    NP_API_OP_BAND,
    NP_API_OP_HZ,
    NP_API_OP_LAN,
    NP_API_OP_ON,
    NP_API_OP_HTTP,
    NP_API_OP_UDP,
    NP_API_OP_TCP
};

struct np_api_cfg {
    int on;
    int lan;  /* 0 = 127.0.0.1, 1 = 0.0.0.0 */
    int http; /* 0 off */
    int udp;
    int tcp;
    int hz; /* 1..500 */
    char token[NP_API_TOKEN];
    char push[NP_API_PUSH]; /* optional host:port UDP dest */
};

struct np_api_sample {
    uint32_t seq;
    uint64_t t_us;
    uint32_t frames;
    uint8_t nch;
    uint8_t mask;
    uint8_t clip;
    uint8_t flags; /* bit0 connected, bit1 paused, bit2 id */
    float uv[NP_NCHAN];
    float sps;
    float id_score;
    int8_t id_best;
};

/* Sharing off, LAN on (0.0.0.0), HTTP 8765, UDP 8766, TCP 8767, 125 Hz. A null cfg returns. */
void np_api_cfg_default(struct np_api_cfg *c);
/* Stop, copy c, and clamp it. on = 0 stays stopped and returns 0. An unchanged running config returns 0. Every bind failing returns -1 and forces on back to 0. */
int np_api_apply(const struct np_api_cfg *c);
/* Join the thread when it was started, then close sockets and the wake pipe. A stop before start only clears running. */
void np_api_stop(void);
/* 1 when sharing is on and the thread has started. */
int np_api_on(void);
/* The configured rate. A stored hz below 1 returns 125. */
int np_api_hz(void);
/* 1 when the bind label is the LAN address. */
int np_api_lan(void);
/* The HTTP port, which may be 0 when that listener is skipped. */
int np_api_http_port(void);
/* The UDP port. */
int np_api_udp_port(void);
/* The TCP port. */
int np_api_tcp_port(void);
/* Copy the token. A null out or n below 1 returns. */
void np_api_token(char *out, int n);
/* Copy the push host:port string. A null out or n below 1 returns. */
void np_api_push_dest(char *out, int n);
/* "not sharing EXG" when off. Otherwise a line with wifi or "this device", the three ports, and hz per second. */
void np_api_line(char *out, int n);

/* Copy the sample, replace seq with the next number, and queue it. The queue holds 256 and drops the oldest when full. The wake pipe is kicked. Off, or a null sample, returns without queueing. */
void np_api_push(const struct np_api_sample *s);
/* Copy the last queued sample. Returns 1 when one exists, 0 when s is null or nothing has been queued. */
int np_api_latest(struct np_api_sample *s);
/* Write one 68-byte EXG1 frame: magic, seq, t_us, frames, nch, mask, clip, flags, eight µV floats, sps, id_score, id_best. Returns 68, or 0 when dst or s is null or cap is below 68. */
int np_api_pack(unsigned char *dst, int cap, const struct np_api_sample *s);
/* Read one EXG1 frame into s. Returns 68, or 0 on a short buffer, a null pointer, or a bad magic. Bytes 65..67 are not checked. */
int np_api_unpack(const unsigned char *src, int n, struct np_api_sample *s);

/* One USB read spread onto a 1/sps grid. Sample i is start_us + i*step_us.
 * A fresh burst ends on now_us. The next burst continues that grid unless
 * the frame counter jumped or the gap is longer than 150 ms. */
struct np_api_grid {
    uint64_t start_us;
    uint64_t step_us;
};

/* Forget the burst grid. The next burst starts a new one. */
void np_api_stamp_reset(void);
/* Space t_us by 1e6/sps µs, at least 1, and sps below 1 becomes 125. n below 1 writes start = now_us and step = 8000 and does not arm the grid. */
void np_api_stamp_burst(uint32_t frame0, int n, int sps, uint64_t now_us,
                        struct np_api_grid *out);

/* Host tick drains these. Returns 1 if an op was taken. */
int np_api_take_op(int *op, int *arg);

/* Optional live status JSON for GET /status. Thread may call this. */
typedef void (*np_api_status_fn)(char *out, int n);
/* Store the /status callback. NULL is allowed. */
void np_api_set_status_fn(np_api_status_fn fn);
/* Extra /cfg fields (no braces). Host EXG, colors, map. */
typedef void (*np_api_view_fn)(char *out, int n);
/* Store the /cfg extra-JSON callback. NULL is allowed. */
void np_api_set_view_fn(np_api_view_fn fn);
/* 1 if EXG may go to this grant. NULL = open (tests). */
typedef int (*np_api_grant_fn)(const char *grant);
/* Store the token grant check. NULL means no grant list. */
void np_api_set_grant_fn(np_api_grant_fn fn);
/* Writes the kit into out. A return below 1 means there is no kit. */
typedef int (*np_api_kit_get_fn)(char *out, int cap);
/* Replaces the kit from s[0..n). 0 accepts it. */
typedef int (*np_api_kit_put_fn)(const char *s, int n);
/* Store the /kit get and put callbacks. Either may be null. */
void np_api_set_kit_fn(np_api_kit_get_fn get, np_api_kit_put_fn put);
/* First LAN connect. 1 wait, 2 grant filled, 3 no. */
typedef int (*np_api_pair_ask_fn)(const char *name, char *grant, int gn);
/* Store the /pair callback. NULL makes /pair answer state 2 with an empty grant. */
void np_api_set_pair_ask_fn(np_api_pair_ask_fn fn);

#endif
