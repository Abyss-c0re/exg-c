#define _GNU_SOURCE
#include "np_api.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (uint64_t)t.tv_sec * 1000000ull + (uint64_t)t.tv_nsec / 1000ull;
}

static void put_u64le(unsigned char *p, uint64_t v)
{
    int i;
    for (i = 0; i < 8; i++) {
        p[i] = (unsigned char)(v >> (8 * i));
    }
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

static void usage(const char *a0)
{
    fprintf(stderr,
            "usage: %s [--bind IP] [--port N] [--ping HOST:PORT] [--seconds N]\n"
            "  C UDP receiver for EXG1. Default bind 0.0.0.0:8766\n"
            "  --ping sends PING and prints RTT. age_us is wall-clock minus frame t_us.\n",
            a0);
}

int main(int argc, char **argv)
{
    const char *bind_ip = "0.0.0.0";
    const char *ping = NULL;
    int port = 8766, seconds = 0, fd, n;
    unsigned int got = 0, gap = 0, last_seq = 0;
    int64_t age_sum = 0, age_max = 0, rtt_sum = 0, rtt_max = 0;
    unsigned int rtt_n = 0;
    uint64_t t0, last_ping = 0;
    struct sockaddr_in addr, ping_a;
    int have_ping = 0;

    for (n = 1; n < argc; n++) {
        if (!strcmp(argv[n], "--bind") && n + 1 < argc) {
            bind_ip = argv[++n];
        } else if (!strcmp(argv[n], "--port") && n + 1 < argc) {
            port = atoi(argv[++n]);
        } else if (!strcmp(argv[n], "--ping") && n + 1 < argc) {
            ping = argv[++n];
        } else if (!strcmp(argv[n], "--seconds") && n + 1 < argc) {
            seconds = atoi(argv[++n]);
        } else if (!strcmp(argv[n], "-h") || !strcmp(argv[n], "--help")) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }
    {
        int on = 1, tos = 0x10;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = inet_addr(bind_ip);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    if (ping && ping[0]) {
        char host[64];
        const char *col = strrchr(ping, ':');
        int pp;
        if (!col) {
            fprintf(stderr, "ping wants host:port\n");
            return 1;
        }
        memcpy(host, ping, (size_t)(col - ping));
        host[col - ping] = 0;
        pp = atoi(col + 1);
        memset(&ping_a, 0, sizeof(ping_a));
        ping_a.sin_family = AF_INET;
        ping_a.sin_port = htons((uint16_t)pp);
        ping_a.sin_addr.s_addr = inet_addr(host);
        have_ping = 1;
    }
    {
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    printf("exg-recv bind %s:%d  ping %s\n", bind_ip, port, ping ? ping : "(none)");
    t0 = now_us();
    for (;;) {
        unsigned char buf[128];
        struct sockaddr_in src;
        socklen_t sl = sizeof(src);
        uint64_t now = now_us();
        if (seconds > 0 && now - t0 >= (uint64_t)seconds * 1000000ull) {
            break;
        }
        if (have_ping && now - last_ping >= 500000ull) {
            unsigned char pingb[12];
            memcpy(pingb, "PING", 4);
            put_u64le(pingb + 4, now);
            sendto(fd, pingb, 12, 0, (struct sockaddr *)&ping_a, sizeof(ping_a));
            last_ping = now;
        }
        n = (int)recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&src, &sl);
        if (n < 0) {
            continue;
        }
        if (n >= 12 && buf[0] == 'P' && buf[1] == 'O' && buf[2] == 'N' && buf[3] == 'G') {
            uint64_t sent = get_u64le(buf + 4);
            int64_t rtt = (int64_t)now - (int64_t)sent;
            if (rtt < 0) {
                rtt = 0;
            }
            rtt_sum += rtt;
            rtt_n++;
            if (rtt > rtt_max) {
                rtt_max = rtt;
            }
            if (n >= 20) {
                uint64_t srv = get_u64le(buf + 12);
                int64_t off = (int64_t)srv - (int64_t)sent - rtt / 2;
                printf("rtt_us=%lld  max=%lld  n=%u  clk_off_us=%lld\n", (long long)rtt,
                       (long long)rtt_max, rtt_n, (long long)off);
            } else {
                printf("rtt_us=%lld  max=%lld  n=%u\n", (long long)rtt, (long long)rtt_max, rtt_n);
            }
            continue;
        }
        if (n >= NP_API_FRAME) {
            struct np_api_sample s;
            if (np_api_unpack(buf, n, &s) == NP_API_FRAME) {
                int64_t age = (int64_t)now - (int64_t)s.t_us;
                if (got && s.seq == last_seq) {
                    continue;
                }
                if (got && s.seq != last_seq + 1) {
                    gap += (s.seq > last_seq) ? (s.seq - last_seq - 1) : 1;
                }
                last_seq = s.seq;
                got++;
                age_sum += age < 0 ? 0 : age;
                if (age > age_max) {
                    age_max = age;
                }
                if ((got % 25u) == 0u) {
                    printf("seq=%u  age_us=%lld  mean_age=%lld  max_age=%lld  gap=%u  sps=%.1f\n",
                           s.seq, (long long)age, (long long)(age_sum / (int64_t)got),
                           (long long)age_max, gap, (double)s.sps);
                }
            }
        }
    }
    if (got) {
        printf("done frames=%u  mean_age_us=%lld  max_age_us=%lld  gap=%u", got,
               (long long)(age_sum / (int64_t)got), (long long)age_max, gap);
        if (rtt_n) {
            printf("  mean_rtt_us=%lld  max_rtt_us=%lld", (long long)(rtt_sum / (int64_t)rtt_n),
                   (long long)rtt_max);
        }
        putchar('\n');
    } else {
        printf("done frames=0\n");
    }
    close(fd);
    return 0;
}
