#define _GNU_SOURCE
#include "np_api.h"

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
#define REQ_MAX 2048
#define JSON_MAX 2000

struct http_cli {
    int fd;
    int stream; /* 0 request 1 binary EXG1 */
    int hdr_ok;
    int off;
    char buf[REQ_MAX];
};

struct tcp_cli {
    int fd;
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

static void close_fd(int *fd);

static uint32_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000u + (uint32_t)(t.tv_nsec / 1000000u));
}

static void put_u32le(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v);
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void put_u64le(unsigned char *p, uint64_t v)
{
    int i;
    for (i = 0; i < 8; i++) {
        p[i] = (unsigned char)(v >> (8 * i));
    }
}

static void put_f32le(unsigned char *p, float f)
{
    union {
        float f;
        uint32_t u;
    } u;
    u.f = f;
    put_u32le(p, u.u);
}

static uint32_t get_u32le(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t get_u64le(const unsigned char *p)
{
    int i;
    uint64_t v = 0;
    for (i = 7; i >= 0; i--) {
        v = (v << 8) | p[i];
    }
    return v;
}

static float get_f32le(const unsigned char *p)
{
    union {
        float f;
        uint32_t u;
    } u;
    u.u = get_u32le(p);
    return u.f;
}

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

static void wake_close(void)
{
    close_fd(&wake_r);
    close_fd(&wake_w);
}

static void wake_kick(void)
{
    char x = 1;
    if (wake_w >= 0) {
        (void)write(wake_w, &x, 1);
    }
}

static void wake_drain(void)
{
    char b[32];
    if (wake_r < 0) {
        return;
    }
    while (read(wake_r, b, sizeof(b)) > 0) {
    }
}

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

static void close_fd(int *fd)
{
    if (fd && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static void pick_ip(void)
{
    /* Bind label only. Never probe a private unicast. */
    snprintf(self_ip, sizeof(self_ip), "%s", cfg.lan ? "0.0.0.0" : "127.0.0.1");
}

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
}

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

/* Stream path: never stall the API thread on a slow client. */
static int send_nb(int fd, const void *p, int n)
{
    int w = (int)send(fd, p, (size_t)n, MSG_NOSIGNAL);
    if (w < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
    return w == n ? n : 0;
}

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

static int local_peer(int fd)
{
    struct sockaddr_in a;
    socklen_t sl = sizeof(a);
    if (getpeername(fd, (struct sockaddr *)&a, &sl) != 0) {
        return 0;
    }
    return a.sin_addr.s_addr == htonl(INADDR_LOOPBACK);
}

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
    if (strcmp(path, "/") && strcmp(path, "/health") && !tok_ok(c->fd, c->buf)) {
        http_reply(c->fd, 401, "application/json", "{\"ok\":false,\"err\":\"token\"}");
        return;
    }
    hdrend = strstr(c->buf, "\r\n\r\n");
    b = hdrend ? hdrend + 4 : "";
    snprintf(body, sizeof(body), "%s", b);

    if (!strcmp(path, "/") || !strcmp(path, "/index")) {
        snprintf(js, sizeof(js),
                 "{\"ok\":true,\"v\":\"2.47\",\"api\":\"exg\","
                 "\"bind\":\"%s\",\"ip\":\"%s\",\"http\":%d,\"udp\":%d,\"tcp\":%d,"
                 "\"hz\":%d,\"token\":%s,\"push\":\"%s\","
                 "\"get\":[\"/health\",\"/status\",\"/sample\",\"/stream\",\"/cfg\"],"
                 "\"post\":[\"/connect\",\"/disconnect\",\"/pause\",\"/cfg\"],"
                 "\"frame\":\"EXG1 %d bytes LE cooked uV\"}",
                 cfg.lan ? "lan" : "local", self_ip, cfg.http, cfg.udp, cfg.tcp, cfg.hz,
                 cfg.token[0] ? "true" : "false", cfg.push, NP_API_FRAME);
        http_reply(c->fd, 200, "application/json", js);
        return;
    }
    if (!strcmp(path, "/health")) {
        snprintf(js, sizeof(js),
                 "{\"ok\":true,\"v\":\"2.47\",\"on\":true,\"bind\":\"%s\","
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
    (void)sp;
    (void)sp2;
    http_reply(c->fd, 404, "application/json", "{\"ok\":false,\"err\":\"no such path\"}");
}

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
                n_tcp++;
                break;
            }
        }
        if (i == MAX_TCP) {
            close(fd);
        }
    }
}

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
            if (send_nb(tcp_c[i].fd, raw, n) < 0) {
                drop_tcp(i);
            }
        }
    }
    for (i = 0; i < MAX_HTTP; i++) {
        if (http_c[i].fd < 0 || http_c[i].stream != 1) {
            continue;
        }
        if (send_nb(http_c[i].fd, raw, n) < 0) {
            drop_http(i);
        }
    }
}

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

static void *api_thread(void *arg)
{
    (void)arg;
    wake_open();
    sockets_open();
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
    if (c->hz > 125) {
        c->hz = 125;
    }
    c->token[NP_API_TOKEN - 1] = 0;
    c->push[NP_API_PUSH - 1] = 0;
}

static int cfg_same(const struct np_api_cfg *a, const struct np_api_cfg *b)
{
    return a->on == b->on && a->lan == b->lan && a->http == b->http && a->udp == b->udp &&
           a->tcp == b->tcp && a->hz == b->hz && !strcmp(a->token, b->token) &&
           !strcmp(a->push, b->push);
}

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
    running = 1;
    if (pthread_create(&thr, NULL, api_thread, NULL) != 0) {
        running = 0;
        return -1;
    }
    started = 1;
    return 0;
}

int np_api_on(void)
{
    return cfg.on && started;
}

int np_api_hz(void)
{
    return cfg.hz < 1 ? 125 : cfg.hz;
}

int np_api_lan(void)
{
    return cfg.lan ? 1 : 0;
}

int np_api_http_port(void)
{
    return cfg.http;
}

int np_api_udp_port(void)
{
    return cfg.udp;
}

int np_api_tcp_port(void)
{
    return cfg.tcp;
}

void np_api_token(char *out, int n)
{
    if (!out || n < 1) {
        return;
    }
    snprintf(out, (size_t)n, "%s", cfg.token);
}

void np_api_push_dest(char *out, int n)
{
    if (!out || n < 1) {
        return;
    }
    snprintf(out, (size_t)n, "%s", cfg.push);
}

void np_api_line(char *out, int n)
{
    if (!out || n < 1) {
        return;
    }
    if (!cfg.on) {
        snprintf(out, (size_t)n, "not sharing leftover");
        return;
    }
    snprintf(out, (size_t)n, "sharing leftover  %s  settings :%d  leftover :%d  spare :%d  %d/s",
             cfg.lan ? "wifi" : "this phone", cfg.http, cfg.udp, cfg.tcp, cfg.hz);
}

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

void np_api_set_status_fn(np_api_status_fn fn)
{
    status_fn = fn;
}

void np_api_set_view_fn(np_api_view_fn fn)
{
    view_fn = fn;
}

void np_api_set_grant_fn(np_api_grant_fn fn)
{
    grant_fn = fn;
}
