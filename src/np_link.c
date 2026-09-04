#define _GNU_SOURCE
#include "np_link.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define LINK_Q 64
#define CFG_MAX 1600

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_t thr;
static int running;
static int sock = -1;
static struct sockaddr_in peer;
static int http_port;
static char host[128];
static char token[NP_API_TOKEN];
static struct np_api_sample q[LINK_Q];
static int qh, qt;
static char cfg_json[CFG_MAX];
static int cfg_fresh;
static uint64_t last_frame_ms;
static np_link_sample_fn on_samp;
static np_link_cfg_fn on_cfg;

static uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000ull + (uint64_t)t.tv_nsec / 1000000ull;
}

int np_link_parse_dest(const char *dest, char *host_out, int hostn, int *http, int *udp)
{
    const char *slash, *col;
    int hlen, hp = 8765, up = 8766;
    if (!dest || !dest[0] || !host_out || hostn < 2) {
        return -1;
    }
    while (*dest == ' ') {
        dest++;
    }
    slash = strrchr(dest, '/');
    col = strrchr(dest, ':');
    if (slash && col && slash > col) {
        up = atoi(slash + 1);
        hp = atoi(col + 1);
        hlen = (int)(col - dest);
    } else if (col && col != dest && col[1] >= '0' && col[1] <= '9') {
        hp = atoi(col + 1);
        up = hp + 1;
        hlen = (int)(col - dest);
    } else {
        hlen = (int)strlen(dest);
    }
    if (hlen < 1 || hlen >= hostn) {
        return -1;
    }
    if (hp < 1 || hp > 65535 || up < 1 || up > 65535) {
        return -1;
    }
    memcpy(host_out, dest, (size_t)hlen);
    host_out[hlen] = 0;
    while (hlen > 0 && host_out[hlen - 1] == ' ') {
        host_out[--hlen] = 0;
    }
    if (!host_out[0]) {
        return -1;
    }
    if (http) {
        *http = hp;
    }
    if (udp) {
        *udp = up;
    }
    return 0;
}

static int resolve(const char *name, int port, struct sockaddr_in *out)
{
    struct addrinfo hints, *res = NULL;
    char ps[16];
    int er;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    snprintf(ps, sizeof(ps), "%d", port);
    er = getaddrinfo(name, ps, &hints, &res);
    if (er != 0 || !res) {
        return -1;
    }
    memcpy(out, res->ai_addr, sizeof(*out));
    freeaddrinfo(res);
    return 0;
}

static void cfg_get(void)
{
    int fd, n, tot = 0;
    struct sockaddr_in a;
    char req[256], buf[CFG_MAX + 256];
    const char *body;
    if (resolve(host, http_port, &a) != 0) {
        return;
    }
    a.sin_port = htons((uint16_t)http_port);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return;
    }
    {
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 400000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
        close(fd);
        return;
    }
    if (token[0]) {
        snprintf(req, sizeof(req),
                 "GET /cfg HTTP/1.0\r\nHost: x\r\nX-EXG-Token: %s\r\nConnection: close\r\n\r\n",
                 token);
    } else {
        snprintf(req, sizeof(req), "GET /cfg HTTP/1.0\r\nHost: x\r\nConnection: close\r\n\r\n");
    }
    if (send(fd, req, strlen(req), 0) < 0) {
        close(fd);
        return;
    }
    memset(buf, 0, sizeof(buf));
    while (tot < (int)sizeof(buf) - 1) {
        n = (int)recv(fd, buf + tot, sizeof(buf) - 1 - (size_t)tot, 0);
        if (n <= 0) {
            break;
        }
        tot += n;
    }
    close(fd);
    body = strstr(buf, "\r\n\r\n");
    if (!body) {
        return;
    }
    body += 4;
    pthread_mutex_lock(&mu);
    snprintf(cfg_json, sizeof(cfg_json), "%s", body);
    cfg_fresh = 1;
    pthread_mutex_unlock(&mu);
}

static void *link_thread(void *arg)
{
    uint64_t last_sub = 0, last_cfg = 0;
    (void)arg;
    while (running) {
        unsigned char buf[128];
        uint64_t now = now_ms();
        int n;
        if (now - last_sub >= 2000ull) {
            char sub[40];
            int slen;
            slen = snprintf(sub, sizeof(sub), "SUB1%s", token);
            if (slen < 4) {
                slen = 4;
            }
            sendto(sock, sub, (size_t)slen, 0, (struct sockaddr *)&peer, sizeof(peer));
            last_sub = now;
        }
        if (now - last_cfg >= 250ull) {
            cfg_get();
            last_cfg = now;
        }
        {
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 4000;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }
        n = (int)recvfrom(sock, buf, sizeof(buf), 0, NULL, NULL);
        if (n >= NP_API_FRAME) {
            struct np_api_sample s;
            if (np_api_unpack(buf, n, &s) == NP_API_FRAME) {
                pthread_mutex_lock(&mu);
                q[qh] = s;
                qh = (qh + 1) % LINK_Q;
                if (qh == qt) {
                    qt = (qt + 1) % LINK_Q;
                }
                last_frame_ms = now;
                pthread_mutex_unlock(&mu);
            }
        }
    }
    return NULL;
}

int np_link_start(const char *dest, const char *tok)
{
    int udp = 8766, tos = 0x10, on = 1;
    np_link_stop();
    memset(host, 0, sizeof(host));
    memset(token, 0, sizeof(token));
    http_port = 8765;
    if (np_link_parse_dest(dest, host, (int)sizeof(host), &http_port, &udp) != 0) {
        return -1;
    }
    if (tok && tok[0]) {
        snprintf(token, sizeof(token), "%s", tok);
    }
    if (resolve(host, udp, &peer) != 0) {
        return -1;
    }
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return -1;
    }
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    setsockopt(sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
    pthread_mutex_lock(&mu);
    qh = qt = 0;
    cfg_fresh = 0;
    last_frame_ms = 0;
    running = 1;
    pthread_mutex_unlock(&mu);
    if (pthread_create(&thr, NULL, link_thread, NULL) != 0) {
        close(sock);
        sock = -1;
        running = 0;
        return -1;
    }
    return 0;
}

void np_link_stop(void)
{
    if (running) {
        running = 0;
        pthread_join(thr, NULL);
    }
    if (sock >= 0) {
        close(sock);
        sock = -1;
    }
    pthread_mutex_lock(&mu);
    qh = qt = 0;
    cfg_fresh = 0;
    last_frame_ms = 0;
    pthread_mutex_unlock(&mu);
}

int np_link_on(void)
{
    return running && sock >= 0;
}

int np_link_alive(void)
{
    uint64_t last;
    pthread_mutex_lock(&mu);
    last = last_frame_ms;
    pthread_mutex_unlock(&mu);
    return last && now_ms() - last < 1500ull;
}

void np_link_set_hooks(np_link_sample_fn samp, np_link_cfg_fn cfg)
{
    on_samp = samp;
    on_cfg = cfg;
}

void np_link_poll(void)
{
    struct np_api_sample batch[32];
    char js[CFG_MAX];
    int n = 0, i, fresh = 0;
    pthread_mutex_lock(&mu);
    while (qt != qh && n < 32) {
        batch[n++] = q[qt];
        qt = (qt + 1) % LINK_Q;
    }
    if (cfg_fresh) {
        snprintf(js, sizeof(js), "%s", cfg_json);
        cfg_fresh = 0;
        fresh = 1;
    }
    pthread_mutex_unlock(&mu);
    for (i = 0; i < n; i++) {
        if (on_samp) {
            on_samp(&batch[i]);
        }
    }
    if (fresh && on_cfg) {
        on_cfg(js);
    }
}
