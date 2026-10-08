#define _GNU_SOURCE
#include "np_api.h"
#include "np_version.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* Share cooked µV as 68-byte little-endian EXG1. HTTP is control.
 * UDP and TCP carry frames. A short write finishes before the next one. */

#ifdef __ANDROID__
#include <android/log.h>
#define NP_API_LOG(...) __android_log_print(ANDROID_LOG_INFO, "exg-api", __VA_ARGS__)
#else
#define NP_API_LOG(...) ((void)0)
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define QN 256
#define MAX_HTTP 6
#define MAX_TCP 4
#define MAX_UDP 8
#define REQ_MAX 8192
#define JSON_MAX 2000
#define KIT_MAX 8192

struct http_cli {
    int fd;
    int stream; /* 0 request 1 binary EXG1 */
    int hdr_ok;
    int off;
    int hold_n; /* unsent tail of one EXG1 frame */
    unsigned char hold[NP_API_FRAME];
    char buf[REQ_MAX];
};

struct tcp_cli {
    int fd;
    int hold_n;
    unsigned char hold[NP_API_FRAME];
};

struct udp_sub {
    struct sockaddr_in a;
    uint32_t last_ms;
};

static struct np_api_cfg cfg;
static int running;
static pthread_t thr;
static int started;
static int http_fd = -1, udp_fd = -1, tcp_fd = -1;
static int wake_r = -1, wake_w = -1;

static pthread_mutex_t qmu = PTHREAD_MUTEX_INITIALIZER;
static struct np_api_sample q[QN];
static int qh, qt;
static struct np_api_sample latest;
static int have_latest;
static uint32_t seq;

static pthread_mutex_t cmu = PTHREAD_MUTEX_INITIALIZER;
static int op_r, op_w;
static int ops[16];
static int op_args[16];

static struct http_cli http_c[MAX_HTTP];
static struct tcp_cli tcp_c[MAX_TCP];
static struct udp_sub udp_s[MAX_UDP];
static struct sockaddr_in push_to;
static int have_push;
static int n_http_stream, n_tcp, n_udp;
static char self_ip[32];
static np_api_status_fn status_fn;
static np_api_view_fn view_fn;
static np_api_grant_fn grant_fn;
static np_api_kit_get_fn kit_get_fn;
static np_api_kit_put_fn kit_put_fn;
static np_api_pair_ask_fn pair_ask_fn;

static void close_fd(int *fd);

/* Monotonic milliseconds, truncated to uint32, so the value wraps. */
static uint32_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000u + (uint32_t)(t.tv_nsec / 1000000u));
}

/* Store 32 bits little-endian. */
static void put_u32le(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v);
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

/* Store 64 bits little-endian. */
static void put_u64le(unsigned char *p, uint64_t v)
{
    int i;
    for (i = 0; i < 8; i++) {
        p[i] = (unsigned char)(v >> (8 * i));
    }
}

/* Store a float as little-endian IEEE bits. The host is assumed little-endian. */
static void put_f32le(unsigned char *p, float f)
{
    union {
        float f;
        uint32_t u;
    } u;
    u.f = f;
    put_u32le(p, u.u);
}

/* Read a little-endian uint32. */
static uint32_t get_u32le(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* Read a little-endian uint64. */
static uint64_t get_u64le(const unsigned char *p)
{
    int i;
    uint64_t v = 0;
    for (i = 7; i >= 0; i--) {
        v = (v << 8) | p[i];
    }
    return v;
}

/* Read a little-endian float. The host is assumed little-endian. */
static float get_f32le(const unsigned char *p)
{
    union {
        float f;
        uint32_t u;
    } u;
    u.u = get_u32le(p);
    return u.f;
}

/* Write one 68-byte EXG1 frame: magic, seq, t_us, frames, nch, mask, clip, flags, eight µV floats, sps, id_score, id_best.
 * Returns 68, or 0 when dst or s is null or cap is below 68. Bytes 65..67 are zero. */
int np_api_pack(unsigned char *dst, int cap, const struct np_api_sample *s)
{
    int i;
    if (!dst || !s || cap < NP_API_FRAME) {
        return 0;
    }
    dst[0] = 'E';
    dst[1] = 'X';
    dst[2] = 'G';
    dst[3] = '1';
    put_u32le(dst + 4, s->seq);
    put_u64le(dst + 8, s->t_us);
    put_u32le(dst + 16, s->frames);
    dst[20] = s->nch;
    dst[21] = s->mask;
    dst[22] = s->clip;
    dst[23] = s->flags;
    for (i = 0; i < NP_NCHAN; i++) {
        put_f32le(dst + 24 + 4 * i, s->uv[i]);
    }
    put_f32le(dst + 56, s->sps);
    put_f32le(dst + 60, s->id_score);
    dst[64] = (unsigned char)s->id_best;
    dst[65] = 0;
    dst[66] = 0;
    dst[67] = 0;
    return NP_API_FRAME;
}

/* Read one EXG1 frame into s. Returns 68, or 0 on a short buffer, a null pointer, or a bad magic.
 * Bytes 65..67 are not checked. */
int np_api_unpack(const unsigned char *src, int n, struct np_api_sample *s)
{
    int i;
    if (!src || !s || n < NP_API_FRAME) {
        return 0;
    }
    if (src[0] != 'E' || src[1] != 'X' || src[2] != 'G' || src[3] != '1') {
        return 0;
    }
    memset(s, 0, sizeof(*s));
    s->seq = get_u32le(src + 4);
    s->t_us = get_u64le(src + 8);
    s->frames = get_u32le(src + 16);
    s->nch = src[20];
    s->mask = src[21];
    s->clip = src[22];
    s->flags = src[23];
    for (i = 0; i < NP_NCHAN; i++) {
        s->uv[i] = get_f32le(src + 24 + 4 * i);
    }
    s->sps = get_f32le(src + 56);
    s->id_score = get_f32le(src + 60);
    s->id_best = (int8_t)src[64];
    return NP_API_FRAME;
}

static uint64_t stamp_next_us;
static uint32_t stamp_last_fr;
static int stamp_ok;

/* Forget the burst grid. The next burst starts a new one. */
void np_api_stamp_reset(void)
{
    stamp_next_us = 0;
    stamp_last_fr = 0;
    stamp_ok = 0;
}

/* Space t_us by 1e6/sps µs, at least 1, and sps below 1 becomes 125. n below 1 writes start = now_us and step = 8000 and does not arm the grid.
 * A hole in frame0, or a stall over 150 ms, starts a new grid, and a late restart does not move a stamp already sent. */
void np_api_stamp_burst(uint32_t frame0, int n, int sps, uint64_t now_us, struct np_api_grid *out)
{
    uint64_t step, span, start;
    int hole, late;
    if (!out) {
        return;
    }
    if (n < 1) {
        out->start_us = now_us;
        out->step_us = 8000ull;
        return;
    }
    if (sps < 1) {
        sps = 125;
    }
    step = 1000000ull / (uint64_t)sps;
    if (step < 1ull) {
        step = 1ull;
    }
    span = (uint64_t)(n - 1) * step;
    hole = stamp_ok && frame0 != stamp_last_fr + 1u;
    late = stamp_ok && now_us > stamp_next_us + 150000ull;
    if (!stamp_ok || hole || late) {
        start = now_us > span ? now_us - span : 0;
        /* A late catch-up must not rewrite a stamp already sent. */
        if (late && start < stamp_next_us) {
            start = stamp_next_us;
        }
    } else {
        /* Same spacing across a USB read that was split in two. */
        start = stamp_next_us;
    }
    out->start_us = start;
    out->step_us = step;
    stamp_next_us = start + (uint64_t)n * step;
    stamp_last_fr = frame0 + (uint32_t)n - 1u;
    stamp_ok = 1;
}

/* Sharing off, LAN on (0.0.0.0), HTTP 8765, UDP 8766, TCP 8767, 125 Hz. A null cfg returns. */
void np_api_cfg_default(struct np_api_cfg *c)
{
    if (!c) {
        return;
    }
    memset(c, 0, sizeof(*c));
    c->on = 0;
    c->lan = 1;
    c->http = 8765;
    c->udp = 8766;
    c->tcp = 8767;
    c->hz = 125;
}

/* Set O_NONBLOCK. fd below 0 returns -1. Returns fd. */
static int nb(int fd)
{
    int fl;
    if (fd < 0) {
        return -1;
    }
    fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) {
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }
    return fd;
}

/* Set IPTOS_LOWDELAY and TCP_NODELAY. fd below 0 returns. TCP_NODELAY on a UDP socket is attempted too. */
static void sock_lowdelay(int fd)
{
    int tos = 0x10; /* IPTOS_LOWDELAY */
    int one = 1;
    if (fd < 0) {
        return;
    }
    setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

/* Open the wake pipe, both ends non-blocking. A second call does nothing. A failed pipe leaves the fds unset. */
static void wake_open(void)
{
    int p[2];
    if (wake_r >= 0) {
        return;
    }
    if (pipe(p) != 0) {
        return;
    }
    wake_r = nb(p[0]);
    wake_w = nb(p[1]);
}

/* Close both wake ends and store -1. */
static void wake_close(void)
{
    close_fd(&wake_r);
    close_fd(&wake_w);
}

/* Write one byte so the API thread wakes. A full pipe drops the error. */
static void wake_kick(void)
{
    char x = 1;
    if (wake_w >= 0) {
        (void)write(wake_w, &x, 1);
    }
}

/* Read the wake pipe until it would block. */
static void wake_drain(void)
{
    char b[32];
    if (wake_r < 0) {
        return;
    }
    while (read(wake_r, b, sizeof(b)) > 0) {
    }
}

/* Bind and listen, backlog 8, any address when ip is null, empty, or 0.0.0.0. Port outside 1..65535 returns -1.
 * The socket is non-blocking. Bind or listen failure returns -1. */
static int listen_tcp(const char *ip, int port)
{
    int fd, on = 1;
    struct sockaddr_in a;
    if (port <= 0 || port > 65535) {
        return -1;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    if (!ip || !ip[0] || !strcmp(ip, "0.0.0.0")) {
        a.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        a.sin_addr.s_addr = inet_addr(ip);
    }
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(fd, 8) < 0) {
        close(fd);
        return -1;
    }
    sock_lowdelay(fd);
    return nb(fd);
}

/* Bind a UDP socket with a 256 KiB send buffer. Same address and port rules as the TCP listener. Returns a non-blocking fd, or -1. */
static int bind_udp(const char *ip, int port)
{
    int fd, on = 1;
    struct sockaddr_in a;
    if (port <= 0 || port > 65535) {
        return -1;
    }
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    {
        int snd = 256 * 1024;
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &snd, sizeof(snd));
    }
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    if (!ip || !ip[0] || !strcmp(ip, "0.0.0.0")) {
        a.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        a.sin_addr.s_addr = inet_addr(ip);
    }
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        close(fd);
        return -1;
    }
    sock_lowdelay(fd);
    return nb(fd);
}

/* Close *fd when it is 0 or above, then store -1. A null pointer does nothing. */
static void close_fd(int *fd)
{
    if (fd && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

/* Set the label string to 0.0.0.0 or 127.0.0.1 from cfg.lan. No route is probed. */
static void pick_ip(void)
{
    /* Bind label only. Never probe a private unicast. */
    snprintf(self_ip, sizeof(self_ip), "%s", cfg.lan ? "0.0.0.0" : "127.0.0.1");
}

/* Dotted IPv4 host:port into out. No DNS. A bad string returns 0. Success returns 1. */
static int parse_push(const char *s, struct sockaddr_in *out)
{
    char host[64];
    const char *col;
    int port;
    if (!s || !s[0] || !out) {
        return 0;
    }
    col = strrchr(s, ':');
    if (!col) {
        return 0;
    }
    if ((size_t)(col - s) >= sizeof(host) || col == s) {
        return 0;
    }
    memcpy(host, s, (size_t)(col - s));
    host[col - s] = 0;
    port = atoi(col + 1);
    if (port <= 0 || port > 65535) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons((uint16_t)port);
    if (inet_aton(host, &out->sin_addr) == 0) {
        return 0;
    }
    return 1;
}

/* Close one HTTP client and clear its slot. A streaming client decrements the stream count. */
static void drop_http(int i)
{
    if (http_c[i].fd >= 0) {
        if (http_c[i].stream) {
            n_http_stream--;
            if (n_http_stream < 0) {
                n_http_stream = 0;
            }
        }
        close(http_c[i].fd);
    }
    memset(&http_c[i], 0, sizeof(http_c[i]));
    http_c[i].fd = -1;
}

/* Close one TCP client, clear its hold, and decrement the count. */
static void drop_tcp(int i)
{
    if (tcp_c[i].fd >= 0) {
        close(tcp_c[i].fd);
        n_tcp--;
        if (n_tcp < 0) {
            n_tcp = 0;
        }
    }
    tcp_c[i].fd = -1;
    tcp_c[i].hold_n = 0;
}

/* Close the listeners and every client. UDP subscriptions are cleared. */
static void sockets_close(void)
{
    int i;
    close_fd(&http_fd);
    close_fd(&udp_fd);
    close_fd(&tcp_fd);
    for (i = 0; i < MAX_HTTP; i++) {
        drop_http(i);
    }
    for (i = 0; i < MAX_TCP; i++) {
        drop_tcp(i);
    }
    memset(udp_s, 0, sizeof(udp_s));
    n_udp = 0;
    n_http_stream = 0;
    n_tcp = 0;
}

/* Bind HTTP, UDP, and TCP when each port is above 0. Port 0 is skipped. Success is any one bind. All three failing returns -1. */
static int sockets_open(void)
{
    const char *ip = cfg.lan ? "0.0.0.0" : "127.0.0.1";
    int i;
    for (i = 0; i < MAX_HTTP; i++) {
        http_c[i].fd = -1;
    }
    for (i = 0; i < MAX_TCP; i++) {
        tcp_c[i].fd = -1;
    }
    pick_ip();
    have_push = parse_push(cfg.push, &push_to);
    if (cfg.http > 0) {
        http_fd = listen_tcp(ip, cfg.http);
        if (http_fd < 0) {
            NP_API_LOG("http bind %s:%d failed", ip, cfg.http);
        }
    }
    if (cfg.udp > 0) {
        udp_fd = bind_udp(ip, cfg.udp);
        if (udp_fd < 0) {
            NP_API_LOG("udp bind %s:%d failed", ip, cfg.udp);
        }
    }
    if (cfg.tcp > 0) {
        tcp_fd = listen_tcp(ip, cfg.tcp);
        if (tcp_fd < 0) {
            NP_API_LOG("tcp bind %s:%d failed", ip, cfg.tcp);
        }
    }
    return (http_fd >= 0 || udp_fd >= 0 || tcp_fd >= 0) ? 0 : -1;
}

/* Push one control op. The ring holds 16. A full ring drops the new op and does not wake anyone. */
static void enqueue_op(int op, int arg)
{
    int n;
    pthread_mutex_lock(&cmu);
    n = (op_w + 1) & 15;
    if (n != op_r) {
        ops[op_w] = op;
        op_args[op_w] = arg;
        op_w = n;
    }
    pthread_mutex_unlock(&cmu);
}

/* Pop one control op. Returns 1 when a slot was filled, 0 when the ring is empty. Either pointer may be null. */
int np_api_take_op(int *op, int *arg)
{
    int have = 0;
    pthread_mutex_lock(&cmu);
    if (op_r != op_w) {
        if (op) {
            *op = ops[op_r];
        }
        if (arg) {
            *arg = op_args[op_r];
        }
        op_r = (op_r + 1) & 15;
        have = 1;
    }
    pthread_mutex_unlock(&cmu);
    return have;
}

/* Write all n bytes. EAGAIN polls POLLOUT for up to 80 ms. A zero-length send returns -1. */
static int send_all(int fd, const void *p, int n)
{
    const unsigned char *b = p;
    int off = 0;
    while (off < n) {
        struct pollfd pfd;
        int w = (int)send(fd, b + off, (size_t)(n - off), MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                pfd.fd = fd;
                pfd.events = POLLOUT;
                if (poll(&pfd, 1, 80) <= 0) {
                    return -1;
                }
                continue;
            }
            return -1;
        }
        if (w == 0) {
            return -1;
        }
        off += w;
    }
    return off;
}

/* One EXG1 frame, aligned. A short write stays in hold and is finished
 * before the next frame. 1 = this frame was accepted, 0 = still draining
 * the previous frame (this one is dropped), -1 = dead socket.
 * Never blocks the API thread. */
static int send_frame(int fd, unsigned char *hold, int *hold_n, const void *p, int n)
{
    const unsigned char *raw = p;
    if (!hold || !hold_n || n < 1 || n > NP_API_FRAME) {
        return -1;
    }
    if (*hold_n > 0) {
        int w = (int)send(fd, hold, (size_t)*hold_n, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            return -1;
        }
        if (w == 0) {
            return -1;
        }
        if (w < *hold_n) {
            memmove(hold, hold + w, (size_t)(*hold_n - w));
            *hold_n -= w;
            return 0;
        }
        *hold_n = 0;
    }
    {
        int w = (int)send(fd, raw, (size_t)n, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                memcpy(hold, raw, (size_t)n);
                *hold_n = n;
                return 1;
            }
            return -1;
        }
        if (w == 0) {
            return -1;
        }
        if (w < n) {
            memcpy(hold, raw + w, (size_t)(n - w));
            *hold_n = n - w;
        }
        return 1;
    }
}

/* Send one HTTP/1.1 reply with CORS and Content-Length. Codes 200, 204, and 401 use their reasons. Any other code is labeled Not Found.
 * A header that does not fit in 512 bytes sends nothing. */
static void http_reply(int fd, int code, const char *ctype, const char *body)
{
    char hdr[512];
    int bl = body ? (int)strlen(body) : 0;
    const char *reason = code == 200 ? "OK" : (code == 204 ? "No Content" : (code == 401 ? "Unauthorized" : "Not Found"));
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Access-Control-Allow-Headers: X-EXG-Token, Authorization, Content-Type\r\n"
                     "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                     "Cache-Control: no-store\r\nConnection: close\r\n"
                     "Content-Length: %d\r\n\r\n",
                     code, reason, ctype ? ctype : "text/plain", bl);
    if (n < 0 || n >= (int)sizeof(hdr)) {
        return;
    }
    send_all(fd, hdr, n);
    if (bl) {
        send_all(fd, body, bl);
    }
}

/* One JSON object for the sample. uv is eight µV values. The text is cut to n bytes. */
static void sample_json(const struct np_api_sample *s, char *out, int n)
{
    snprintf(out, (size_t)n,
             "{\"seq\":%u,\"t_us\":%llu,\"frames\":%u,\"nch\":%u,\"mask\":%u,"
             "\"clip\":%u,\"flags\":%u,\"sps\":%.2f,\"id_best\":%d,\"id_score\":%.4f,"
             "\"uv\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]}",
             s->seq, (unsigned long long)s->t_us, s->frames, s->nch, s->mask, s->clip,
             s->flags, (double)s->sps, (int)s->id_best, (double)s->id_score,
             (double)s->uv[0], (double)s->uv[1], (double)s->uv[2], (double)s->uv[3],
             (double)s->uv[4], (double)s->uv[5], (double)s->uv[6], (double)s->uv[7]);
}

/* Copy a token from token=, X-EXG-Token, or Authorization Bearer. Returns 1 when a token was stored. A null request returns 0. */
static int hdr_tok(const char *req, char *tok, int n)
{
    const char *p, *q, *qs;
    tok[0] = 0;
    if (!req) {
        return 0;
    }
    qs = strstr(req, "token=");
    if (qs && (qs == req || qs[-1] == '?' || qs[-1] == '&')) {
        qs += 6;
        q = qs;
        while (*q && *q != ' ' && *q != '&' && *q != '\r' && *q != '\n') {
            q++;
        }
        if (q > qs && (int)(q - qs) < n) {
            memcpy(tok, qs, (size_t)(q - qs));
            tok[q - qs] = 0;
            return 1;
        }
    }
    p = strstr(req, "X-EXG-Token:");
    if (!p) {
        p = strstr(req, "x-exg-token:");
    }
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++;
            while (*p == ' ') {
                p++;
            }
            q = p;
            while (*q && *q != '\r' && *q != '\n') {
                q++;
            }
            if (q > p && (int)(q - p) < n) {
                memcpy(tok, p, (size_t)(q - p));
                tok[q - p] = 0;
                return 1;
            }
        }
    }
    p = strstr(req, "Authorization:");
    if (p) {
        p = strstr(p, "Bearer ");
        if (p) {
            p += 7;
            q = p;
            while (*q && *q != '\r' && *q != '\n') {
                q++;
            }
            if (q > p && (int)(q - p) < n) {
                memcpy(tok, p, (size_t)(q - p));
                tok[q - p] = 0;
                return 1;
            }
        }
    }
    return tok[0] != 0;
}

/* 1 when the peer is 127.0.0.1. A failed getpeername returns 0. */
static int local_peer(int fd)
{
    struct sockaddr_in a;
    socklen_t sl = sizeof(a);
    if (getpeername(fd, (struct sockaddr *)&a, &sl) != 0) {
        return 0;
    }
    return a.sin_addr.s_addr == htonl(INADDR_LOOPBACK);
}

/* 1 for a localhost peer. Otherwise the header token must match cfg.token, or grant_fn must accept it.
 * An empty server token with no grant_fn accepts everyone. */
static int tok_ok(int fd, const char *req)
{
    char got[NP_API_TOKEN];
    got[0] = 0;
    if (local_peer(fd)) {
        return 1;
    }
    hdr_tok(req, got, sizeof(got));
    if (cfg.token[0] && got[0] && strcmp(got, cfg.token) == 0) {
        return 1;
    }
    if (grant_fn && got[0] && grant_fn(got)) {
        return 1;
    }
    if (!cfg.token[0] && !grant_fn) {
        return 1;
    }
    return 0;
}

/* Read key as a JSON number, true/false, lan/local, or a key= form. Returns 1 when *out was set. */
static int json_int(const char *body, const char *key, int *out)
{
    char pat[40];
    const char *p;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(body, pat);
    if (!p) {
        snprintf(pat, sizeof(pat), "%s=", key);
        p = strstr(body, pat);
        if (!p) {
            return 0;
        }
        p += strlen(pat);
    } else {
        p = strchr(p, ':');
        if (!p) {
            return 0;
        }
        p++;
    }
    while (*p == ' ' || *p == '"') {
        p++;
    }
    if (!(*p == '-' || (*p >= '0' && *p <= '9'))) {
        if (!strncmp(p, "true", 4)) {
            *out = 1;
            return 1;
        }
        if (!strncmp(p, "false", 5)) {
            *out = 0;
            return 1;
        }
        if (!strncmp(p, "lan", 3)) {
            *out = 1;
            return 1;
        }
        if (!strncmp(p, "local", 5)) {
            *out = 0;
            return 1;
        }
        return 0;
    }
    *out = atoi(p);
    return 1;
}

/* Copy the value, keeping only letters, digits, and _.- . A space becomes _. Returns 1 when any character was kept.
 * A null pointer or n below 2 returns 0. */
static int json_str(const char *body, const char *key, char *out, int n)
{
    char pat[40];
    const char *p, *q;
    int i, o;
    if (!body || !key || !out || n < 2) {
        return 0;
    }
    out[0] = 0;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(body, pat);
    if (p) {
        p = strchr(p + 1, ':');
        if (!p) {
            return 0;
        }
        p++;
        while (*p == ' ') {
            p++;
        }
        if (*p == '"') {
            p++;
            q = p;
            while (*q && *q != '"') {
                q++;
            }
        } else {
            q = p;
            while (*q && *q != ',' && *q != '}' && *q != ' ' && *q != '\r' && *q != '\n') {
                q++;
            }
        }
    } else {
        snprintf(pat, sizeof(pat), "%s=", key);
        p = strstr(body, pat);
        if (!p) {
            return 0;
        }
        p += strlen(pat);
        q = p;
        while (*q && *q != '&' && *q != ' ' && *q != '\r' && *q != '\n') {
            q++;
        }
    }
    o = 0;
    for (i = 0; p + i < q && o < n - 1; i++) {
        char c = p[i];
        if (c == ' ') {
            c = '_';
        }
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '.') {
            out[o++] = c;
        }
    }
    out[o] = 0;
    return out[0] != 0;
}

/* One HTTP request. /, /health, and /pair skip the token. GET covers those plus /index, /status, /sample, /cfg, /stream, and /kit.
 * POST covers /connect, /disconnect, /pause, /cfg, /kit, and /pair. OPTIONS is 204. /stream stays open. Other paths are closed by the reader after the reply. */
static void handle_req(struct http_cli *c)
{
    char path[128], method[8], body[JSON_MAX], js[JSON_MAX];
    const char *sp, *sp2, *hdrend, *b;
    int i, v, is_post;
    struct np_api_sample s;

    method[0] = 0;
    path[0] = 0;
    if (sscanf(c->buf, "%7s %127s", method, path) != 2) {
        http_reply(c->fd, 400, "text/plain", "bad request");
        return;
    }
    for (i = 0; path[i]; i++) {
        if (path[i] == '?') {
            path[i] = 0;
            break;
        }
    }
    is_post = !strcmp(method, "POST");
    if (!strcmp(method, "OPTIONS")) {
        http_reply(c->fd, 204, "text/plain", "");
        return;
    }
    if (strcmp(method, "GET") && !is_post) {
        http_reply(c->fd, 404, "text/plain", "no");
        return;
    }
    if (strcmp(path, "/") && strcmp(path, "/health") && strcmp(path, "/pair")
        && !tok_ok(c->fd, c->buf)) {
        http_reply(c->fd, 401, "application/json", "{\"ok\":false,\"err\":\"token\"}");
        return;
    }
    hdrend = strstr(c->buf, "\r\n\r\n");
    b = hdrend ? hdrend + 4 : "";
    snprintf(body, sizeof(body), "%s", b);

    if (!strcmp(path, "/") || !strcmp(path, "/index")) {
        snprintf(js, sizeof(js),
                 "{\"ok\":true,\"v\":\"" NP_APP_VER "\",\"api\":\"exg\","
                 "\"bind\":\"%s\",\"ip\":\"%s\",\"http\":%d,\"udp\":%d,\"tcp\":%d,"
                 "\"hz\":%d,\"token\":%s,\"push\":\"%s\","
                 "\"get\":[\"/health\",\"/status\",\"/sample\",\"/stream\",\"/cfg\",\"/kit\",\"/pair\"],"
                 "\"post\":[\"/connect\",\"/disconnect\",\"/pause\",\"/cfg\",\"/kit\",\"/pair\"],"
                 "\"frame\":\"EXG1 %d bytes LE cooked uV\"}",
                 cfg.lan ? "lan" : "local", self_ip, cfg.http, cfg.udp, cfg.tcp, cfg.hz,
                 cfg.token[0] ? "true" : "false", cfg.push, NP_API_FRAME);
        http_reply(c->fd, 200, "application/json", js);
        return;
    }
    if (!strcmp(path, "/health")) {
        snprintf(js, sizeof(js),
                 "{\"ok\":true,\"v\":\"" NP_APP_VER "\",\"on\":true,\"bind\":\"%s\","
                 "\"ip\":\"%s\",\"http\":%d,\"udp\":%d,\"tcp\":%d,\"hz\":%d,"
                 "\"clients\":{\"http\":%d,\"tcp\":%d,\"udp\":%d}}",
                 cfg.lan ? "lan" : "local", self_ip, cfg.http, cfg.udp, cfg.tcp, cfg.hz,
                 n_http_stream, n_tcp, n_udp);
        http_reply(c->fd, 200, "application/json", js);
        return;
    }
    if (!strcmp(path, "/status")) {
        if (status_fn) {
            status_fn(js, sizeof(js));
        } else {
            snprintf(js, sizeof(js), "{\"ok\":true}");
        }
        http_reply(c->fd, 200, "application/json", js);
        return;
    }
    if (!strcmp(path, "/sample")) {
        if (!np_api_latest(&s)) {
            http_reply(c->fd, 200, "application/json", "{\"ok\":true,\"have\":false}");
            return;
        }
        sample_json(&s, js, sizeof(js));
        http_reply(c->fd, 200, "application/json", js);
        return;
    }
    if (!strcmp(path, "/cfg") && !is_post) {
        char extra[1400];
        extra[0] = 0;
        if (view_fn) {
            view_fn(extra, (int)sizeof(extra));
        }
        snprintf(js, sizeof(js),
                 "{\"ok\":true,\"on\":%d,\"bind\":\"%s\",\"http\":%d,\"udp\":%d,"
                 "\"tcp\":%d,\"hz\":%d,\"token\":%s,\"push\":\"%s\"%s%s}",
                 cfg.on, cfg.lan ? "lan" : "local", cfg.http, cfg.udp, cfg.tcp, cfg.hz,
                 cfg.token[0] ? "true" : "false", cfg.push, extra[0] ? "," : "", extra);
        http_reply(c->fd, 200, "application/json", js);
        return;
    }
    if (!strcmp(path, "/stream")) {
        const char *hdr =
            "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
            "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\n"
            "X-EXG-Format: EXG1\r\nConnection: close\r\n\r\n";
        if (send_all(c->fd, hdr, (int)strlen(hdr)) < 0) {
            return;
        }
        c->stream = 1;
        c->hdr_ok = 1;
        n_http_stream++;
        return;
    }
    if (is_post && !strcmp(path, "/connect")) {
        enqueue_op(NP_API_OP_CONNECT, 1);
        http_reply(c->fd, 200, "application/json", "{\"ok\":true,\"op\":\"connect\"}");
        return;
    }
    if (is_post && !strcmp(path, "/disconnect")) {
        enqueue_op(NP_API_OP_DISC, 1);
        http_reply(c->fd, 200, "application/json", "{\"ok\":true,\"op\":\"disconnect\"}");
        return;
    }
    if (is_post && !strcmp(path, "/pause")) {
        enqueue_op(NP_API_OP_PAUSE, 1);
        http_reply(c->fd, 200, "application/json", "{\"ok\":true,\"op\":\"pause\"}");
        return;
    }
    if (is_post && !strcmp(path, "/cfg")) {
        if (json_int(body, "on", &v)) {
            enqueue_op(NP_API_OP_ON, v ? 1 : 0);
        }
        if (json_int(body, "bind", &v) || json_int(body, "lan", &v)) {
            enqueue_op(NP_API_OP_LAN, v ? 1 : 0);
        }
        if (json_int(body, "http", &v)) {
            enqueue_op(NP_API_OP_HTTP, v);
        }
        if (json_int(body, "udp", &v)) {
            enqueue_op(NP_API_OP_UDP, v);
        }
        if (json_int(body, "tcp", &v)) {
            enqueue_op(NP_API_OP_TCP, v);
        }
        if (json_int(body, "hz", &v)) {
            enqueue_op(NP_API_OP_HZ, v);
        }
        if (json_int(body, "notch", &v)) {
            enqueue_op(NP_API_OP_NOTCH, v);
        }
        if (json_int(body, "hp", &v)) {
            enqueue_op(NP_API_OP_HP, v);
        }
        if (json_int(body, "lp", &v)) {
            enqueue_op(NP_API_OP_LP, v);
        }
        if (json_int(body, "car", &v)) {
            enqueue_op(NP_API_OP_CAR, v ? 1 : 0);
        }
        if (json_int(body, "band", &v)) {
            enqueue_op(NP_API_OP_BAND, v);
        }
        http_reply(c->fd, 200, "application/json", "{\"ok\":true,\"op\":\"cfg\"}");
        return;
    }
    if (!strcmp(path, "/kit") && !is_post) {
        char kit[KIT_MAX];
        int kn = 0;
        if (kit_get_fn) {
            kn = kit_get_fn(kit, KIT_MAX);
        }
        if (kn < 1) {
            http_reply(c->fd, 404, "text/plain", "no kit");
            return;
        }
        kit[KIT_MAX - 1] = 0;
        http_reply(c->fd, 200, "text/plain", kit);
        return;
    }
    if (is_post && !strcmp(path, "/kit")) {
        if (kit_put_fn && kit_put_fn(body, (int)strlen(body)) == 0) {
            http_reply(c->fd, 200, "application/json", "{\"ok\":true,\"op\":\"kit\"}");
        } else {
            http_reply(c->fd, 404, "application/json", "{\"ok\":false,\"err\":\"kit\"}");
        }
        return;
    }
    if (!strcmp(path, "/pair")) {
        char name[32], grant[32];
        int st;
        name[0] = 0;
        grant[0] = 0;
        if (is_post) {
            json_str(body, "name", name, (int)sizeof(name));
            if (!name[0]) {
                json_str(c->buf, "name", name, (int)sizeof(name));
            }
            if (!name[0]) {
                snprintf(name, sizeof(name), "exg");
            }
        }
        if (!pair_ask_fn) {
            http_reply(c->fd, 200, "application/json",
                       "{\"ok\":true,\"state\":2,\"grant\":\"\"}");
            return;
        }
        st = pair_ask_fn(is_post ? name : "", grant, (int)sizeof(grant));
        if (st == 2) {
            snprintf(js, sizeof(js), "{\"ok\":true,\"state\":2,\"grant\":\"%s\"}", grant);
            http_reply(c->fd, 200, "application/json", js);
            return;
        }
        if (st == 3) {
            http_reply(c->fd, 200, "application/json", "{\"ok\":false,\"state\":3}");
            return;
        }
        snprintf(js, sizeof(js), "{\"ok\":true,\"state\":%d}", st);
        http_reply(c->fd, 200, "application/json", js);
        return;
    }
    (void)sp;
    (void)sp2;
    http_reply(c->fd, 404, "application/json", "{\"ok\":false,\"err\":\"no such path\"}");
}

/* Accept clients up to 6. A further client gets "busy" and is closed. */
static void accept_http(void)
{
    int fd, i;
    if (http_fd < 0) {
        return;
    }
    for (;;) {
        fd = accept(http_fd, NULL, NULL);
        if (fd < 0) {
            return;
        }
        nb(fd);
        for (i = 0; i < MAX_HTTP; i++) {
            if (http_c[i].fd < 0) {
                memset(&http_c[i], 0, sizeof(http_c[i]));
                http_c[i].fd = fd;
                break;
            }
        }
        if (i == MAX_HTTP) {
            http_reply(fd, 404, "text/plain", "busy");
            close(fd);
        }
    }
}

/* Accept raw EXG1 clients up to 4, with TCP_NODELAY. A further client is closed with no reply. */
static void accept_tcp(void)
{
    int fd, i, one = 1;
    if (tcp_fd < 0) {
        return;
    }
    for (;;) {
        fd = accept(tcp_fd, NULL, NULL);
        if (fd < 0) {
            return;
        }
        nb(fd);
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        for (i = 0; i < MAX_TCP; i++) {
            if (tcp_c[i].fd < 0) {
                tcp_c[i].fd = fd;
                tcp_c[i].hold_n = 0;
                n_tcp++;
                break;
            }
        }
        if (i == MAX_TCP) {
            close(fd);
        }
    }
}

/* A datagram of 12 or more bytes starting with PING gets a 20-byte PONG (echoed 8 bytes plus server realtime microseconds) and does not subscribe.
 * Any other datagram refreshes or adds a subscriber, at most 8. When grant_fn is set, the bytes after the first four must pass it. */
static void udp_hear(void)
{
    unsigned char buf[64];
    struct sockaddr_in a;
    socklen_t sl;
    int n, i, freei, oldest;
    uint32_t now;
    if (udp_fd < 0) {
        return;
    }
    now = now_ms();
    for (;;) {
        sl = sizeof(a);
        n = (int)recvfrom(udp_fd, buf, sizeof(buf), 0, (struct sockaddr *)&a, &sl);
        if (n < 0) {
            return;
        }
        if (n >= 12 && buf[0] == 'P' && buf[1] == 'I' && buf[2] == 'N' && buf[3] == 'G') {
            unsigned char pong[20];
            struct timespec ts;
            uint64_t srv;
            memcpy(pong, "PONG", 4);
            memcpy(pong + 4, buf + 4, 8);
            clock_gettime(CLOCK_REALTIME, &ts);
            srv = (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
            put_u64le(pong + 12, srv);
            sendto(udp_fd, pong, 20, 0, (struct sockaddr *)&a, sl);
            continue; /* PING is RTT only. Push dest carries the stream. */
        }
        freei = -1;
        oldest = 0;
        for (i = 0; i < MAX_UDP; i++) {
            if (udp_s[i].last_ms && udp_s[i].a.sin_addr.s_addr == a.sin_addr.s_addr &&
                udp_s[i].a.sin_port == a.sin_port) {
                udp_s[i].last_ms = now;
                freei = -2;
                break;
            }
            if (!udp_s[i].last_ms && freei < 0) {
                freei = i;
            }
            if (udp_s[i].last_ms && udp_s[i].last_ms < udp_s[oldest].last_ms) {
                oldest = i;
            }
        }
        if (grant_fn) {
            char gbuf[32];
            int glen = n > 4 ? n - 4 : 0;
            if (glen > 31) {
                glen = 31;
            }
            memcpy(gbuf, buf + 4, (size_t)glen);
            gbuf[glen] = 0;
            while (glen > 0 && (gbuf[glen - 1] == '\n' || gbuf[glen - 1] == '\r' ||
                                 gbuf[glen - 1] == ' ')) {
                gbuf[--glen] = 0;
            }
            if (!grant_fn(gbuf)) {
                continue;
            }
        }
        if (freei == -2) {
            continue;
        }
        if (freei < 0) {
            freei = oldest;
        }
        udp_s[freei].a = a;
        udp_s[freei].last_ms = now;
    }
}

/* Drop UDP subscribers silent for more than 8 seconds. Called when a frame is sent. */
static void count_udp(uint32_t now)
{
    int i, n = 0;
    for (i = 0; i < MAX_UDP; i++) {
        if (udp_s[i].last_ms && now - udp_s[i].last_ms > 8000) {
            memset(&udp_s[i], 0, sizeof(udp_s[i]));
        }
        if (udp_s[i].last_ms) {
            n++;
        }
    }
    n_udp = n;
}

/* Pack 68 bytes and send them. UDP uses sendto with no hold. TCP and HTTP streams finish a short write before the next frame, and a dead socket is dropped. */
static void emit_frame(const struct np_api_sample *s)
{
    unsigned char raw[NP_API_FRAME];
    int i, n;
    uint32_t now = now_ms();
    n = np_api_pack(raw, sizeof(raw), s);
    if (!n) {
        return;
    }
    count_udp(now);
    if (udp_fd >= 0) {
        for (i = 0; i < MAX_UDP; i++) {
            if (udp_s[i].last_ms) {
                sendto(udp_fd, raw, (size_t)n, 0, (struct sockaddr *)&udp_s[i].a,
                       sizeof(udp_s[i].a));
            }
        }
        if (have_push) {
            sendto(udp_fd, raw, (size_t)n, 0, (struct sockaddr *)&push_to, sizeof(push_to));
        }
    }
    for (i = 0; i < MAX_TCP; i++) {
        if (tcp_c[i].fd >= 0) {
            if (send_frame(tcp_c[i].fd, tcp_c[i].hold, &tcp_c[i].hold_n, raw, n) < 0) {
                drop_tcp(i);
            }
        }
    }
    for (i = 0; i < MAX_HTTP; i++) {
        if (http_c[i].fd < 0 || http_c[i].stream != 1) {
            continue;
        }
        if (send_frame(http_c[i].fd, http_c[i].hold, &http_c[i].hold_n, raw, n) < 0) {
            drop_http(i);
        }
    }
}

/* Send at most 32 queued samples. */
static void drain_q(void)
{
    struct np_api_sample batch[32];
    int n = 0, i;
    pthread_mutex_lock(&qmu);
    while (qt != qh && n < 32) {
        batch[n++] = q[qt];
        qt = (qt + 1) % QN;
    }
    pthread_mutex_unlock(&qmu);
    for (i = 0; i < n; i++) {
        emit_frame(&batch[i]);
    }
}

/* Read request bytes. A client already streaming is left alone. 8192 bytes with no header break drops the client.
 * A finished request that is not /stream is closed after the reply. */
static void read_http(void)
{
    int i;
    for (i = 0; i < MAX_HTTP; i++) {
        int r;
        if (http_c[i].fd < 0 || http_c[i].stream) {
            continue;
        }
        r = (int)recv(http_c[i].fd, http_c[i].buf + http_c[i].off,
                      sizeof(http_c[i].buf) - 1 - (size_t)http_c[i].off, 0);
        if (r < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                drop_http(i);
            }
            continue;
        }
        if (r == 0) {
            drop_http(i);
            continue;
        }
        http_c[i].off += r;
        http_c[i].buf[http_c[i].off] = 0;
        if (strstr(http_c[i].buf, "\r\n\r\n")) {
            handle_req(&http_c[i]);
            if (http_c[i].fd >= 0 && !http_c[i].stream) {
                drop_http(i);
            }
        } else if (http_c[i].off >= (int)sizeof(http_c[i].buf) - 1) {
            drop_http(i);
        }
    }
}

/* Poll listeners and idle HTTP clients for 20 ms, then accept, read, and drain the sample queue. Exit closes the sockets. */
static void *api_thread(void *arg)
{
    (void)arg;
    NP_API_LOG("listen bind=%s http=%d udp=%d tcp=%d hz=%d", cfg.lan ? "lan" : "local",
               cfg.http, cfg.udp, cfg.tcp, cfg.hz);
    while (running) {
        struct pollfd p[4 + MAX_HTTP];
        int np = 0, i;
        if (wake_r >= 0) {
            p[np].fd = wake_r;
            p[np].events = POLLIN;
            np++;
        }
        if (http_fd >= 0) {
            p[np].fd = http_fd;
            p[np].events = POLLIN;
            np++;
        }
        if (tcp_fd >= 0) {
            p[np].fd = tcp_fd;
            p[np].events = POLLIN;
            np++;
        }
        if (udp_fd >= 0) {
            p[np].fd = udp_fd;
            p[np].events = POLLIN;
            np++;
        }
        for (i = 0; i < MAX_HTTP && np < (int)(sizeof(p) / sizeof(p[0])); i++) {
            if (http_c[i].fd >= 0 && !http_c[i].stream) {
                p[np].fd = http_c[i].fd;
                p[np].events = POLLIN;
                np++;
            }
        }
        if (np) {
            poll(p, (nfds_t)np, 20);
        } else {
            usleep(1000);
        }
        wake_drain();
        accept_http();
        accept_tcp();
        udp_hear();
        read_http();
        drain_q();
    }
    sockets_close();
    return NULL;
}

/* on and lan become 0 or 1. A port outside 0..65535 returns to 8765, 8766, or 8767. hz is clamped to 1..500. */
static void np_api_cfg_clamp(struct np_api_cfg *c)
{
    if (!c) {
        return;
    }
    c->on = c->on ? 1 : 0;
    c->lan = c->lan ? 1 : 0;
    if (c->http < 0 || c->http > 65535) {
        c->http = 8765;
    }
    if (c->udp < 0 || c->udp > 65535) {
        c->udp = 8766;
    }
    if (c->tcp < 0 || c->tcp > 65535) {
        c->tcp = 8767;
    }
    if (c->hz < 1) {
        c->hz = 1;
    }
    if (c->hz > 500) {
        c->hz = 500;
    }
    c->token[NP_API_TOKEN - 1] = 0;
    c->push[NP_API_PUSH - 1] = 0;
}

/* 1 when on, lan, ports, hz, token, and push all match. */
static int cfg_same(const struct np_api_cfg *a, const struct np_api_cfg *b)
{
    return a->on == b->on && a->lan == b->lan && a->http == b->http && a->udp == b->udp &&
           a->tcp == b->tcp && a->hz == b->hz && !strcmp(a->token, b->token) &&
           !strcmp(a->push, b->push);
}

/* Join the thread when it was started, then close sockets and the wake pipe. A stop before start only clears running. */
void np_api_stop(void)
{
    if (!started) {
        running = 0;
        return;
    }
    running = 0;
    pthread_join(thr, NULL);
    started = 0;
    sockets_close();
    wake_close();
}

/* Stop, copy c, and clamp it. on = 0 stays stopped and returns 0. An unchanged running config returns 0.
 * Every bind failing returns -1 and forces on back to 0. A thread that will not start returns -1 with the sockets closed. */
int np_api_apply(const struct np_api_cfg *c)
{
    struct np_api_cfg next;
    if (!c) {
        return -1;
    }
    next = *c;
    np_api_cfg_clamp(&next);
    if (started && cfg_same(&cfg, &next)) {
        return 0;
    }
    np_api_stop();
    cfg = next;
    if (!cfg.on) {
        return 0;
    }
    wake_open();
    if (sockets_open() != 0) {
        NP_API_LOG("no port bound");
        sockets_close();
        wake_close();
        cfg.on = 0;
        return -1;
    }
    running = 1;
    if (pthread_create(&thr, NULL, api_thread, NULL) != 0) {
        running = 0;
        sockets_close();
        wake_close();
        return -1;
    }
    started = 1;
    return 0;
}

/* 1 when sharing is on and the thread has started. */
int np_api_on(void)
{
    return cfg.on && started;
}

/* The configured rate. A stored hz below 1 returns 125. */
int np_api_hz(void)
{
    return cfg.hz < 1 ? 125 : cfg.hz;
}

/* 1 when the bind label is the LAN address. */
int np_api_lan(void)
{
    return cfg.lan ? 1 : 0;
}

/* The HTTP port, which may be 0 when that listener is skipped. */
int np_api_http_port(void)
{
    return cfg.http;
}

/* The UDP port. */
int np_api_udp_port(void)
{
    return cfg.udp;
}

/* The TCP port. */
int np_api_tcp_port(void)
{
    return cfg.tcp;
}

/* Copy the token. A null out or n below 1 returns. */
void np_api_token(char *out, int n)
{
    if (!out || n < 1) {
        return;
    }
    snprintf(out, (size_t)n, "%s", cfg.token);
}

/* Copy the push host:port string. A null out or n below 1 returns. */
void np_api_push_dest(char *out, int n)
{
    if (!out || n < 1) {
        return;
    }
    snprintf(out, (size_t)n, "%s", cfg.push);
}

/* "not sharing EXG" when off. Otherwise a line with wifi or "this device", the three ports, and hz per second. */
void np_api_line(char *out, int n)
{
    if (!out || n < 1) {
        return;
    }
    if (!cfg.on) {
        snprintf(out, (size_t)n, "not sharing EXG");
        return;
    }
    snprintf(out, (size_t)n, "sharing EXG  %s  settings :%d  EXG :%d  spare :%d  %d/s",
             cfg.lan ? "wifi" : "this device", cfg.http, cfg.udp, cfg.tcp, cfg.hz);
}

/* Copy the sample, replace seq with the next number, and queue it. The queue holds 256 and drops the oldest when full. The wake pipe is kicked.
 * Off, or a null sample, returns without queueing. */
void np_api_push(const struct np_api_sample *s)
{
    int n;
    struct np_api_sample x;
    if (!s || !cfg.on) {
        return;
    }
    x = *s;
    pthread_mutex_lock(&qmu);
    seq++;
    x.seq = seq;
    latest = x;
    have_latest = 1;
    n = (qh + 1) % QN;
    if (n == qt) {
        qt = (qt + 1) % QN;
    }
    q[qh] = x;
    qh = n;
    pthread_mutex_unlock(&qmu);
    wake_kick();
}

/* Copy the last queued sample. Returns 1 when one exists, 0 when s is null or nothing has been queued. */
int np_api_latest(struct np_api_sample *s)
{
    int ok = 0;
    if (!s) {
        return 0;
    }
    pthread_mutex_lock(&qmu);
    if (have_latest) {
        *s = latest;
        ok = 1;
    }
    pthread_mutex_unlock(&qmu);
    return ok;
}

/* Store the /status callback. NULL is allowed. */
void np_api_set_status_fn(np_api_status_fn fn)
{
    status_fn = fn;
}

/* Store the /cfg extra-JSON callback. NULL is allowed. */
void np_api_set_view_fn(np_api_view_fn fn)
{
    view_fn = fn;
}

/* Store the token grant check. NULL means no grant list. */
void np_api_set_grant_fn(np_api_grant_fn fn)
{
    grant_fn = fn;
}

/* Store the /kit get and put callbacks. Either may be null. */
void np_api_set_kit_fn(np_api_kit_get_fn get, np_api_kit_put_fn put)
{
    kit_get_fn = get;
    kit_put_fn = put;
}

/* Store the /pair callback. NULL makes /pair answer state 2 with an empty grant. */
void np_api_set_pair_ask_fn(np_api_pair_ask_fn fn)
{
    pair_ask_fn = fn;
}
