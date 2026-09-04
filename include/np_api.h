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
    int hz; /* 1..125 */
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

void np_api_cfg_default(struct np_api_cfg *c);
int np_api_apply(const struct np_api_cfg *c);
void np_api_stop(void);
int np_api_on(void);
int np_api_hz(void);
int np_api_lan(void);
int np_api_http_port(void);
int np_api_udp_port(void);
int np_api_tcp_port(void);
void np_api_token(char *out, int n);
void np_api_push_dest(char *out, int n);
void np_api_line(char *out, int n);

void np_api_push(const struct np_api_sample *s);
int np_api_latest(struct np_api_sample *s);
int np_api_pack(unsigned char *dst, int cap, const struct np_api_sample *s);
int np_api_unpack(const unsigned char *src, int n, struct np_api_sample *s);

/* Host tick drains these. Returns 1 if an op was taken. */
int np_api_take_op(int *op, int *arg);

/* Optional live status JSON for GET /status. Thread may call this. */
typedef void (*np_api_status_fn)(char *out, int n);
void np_api_set_status_fn(np_api_status_fn fn);
/* Extra /cfg fields (no braces). Host leftover, colors, map. */
typedef void (*np_api_view_fn)(char *out, int n);
void np_api_set_view_fn(np_api_view_fn fn);

#endif
