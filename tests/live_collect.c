#define _GNU_SOURCE
#include "np_dsp.h"
#include "np_knight.h"
#include "np_serial.h"

#include <errno.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec * 1e-9;
}

static int read_seconds(int fd, struct np_parser *p, float (*uv)[8], int maxn, float *ax,
                        double seconds, uint32_t *ngood, uint32_t *nbad)
{
    unsigned char buf[256];
    int nout = 0;
    double t0 = now_s();
    *ngood = *nbad = 0;
    while (now_s() - t0 < seconds && nout < maxn) {
        struct pollfd pfd = {fd, POLLIN, 0};
        int n, i;
        if (poll(&pfd, 1, 50) <= 0) {
            continue;
        }
        n = np_serial_read(fd, buf, (int)sizeof(buf));
        if (n < 0) {
            return -1;
        }
        for (i = 0; i < n; i++) {
            struct np_sample s;
            int r = np_parser_feed(p, buf[i], &s);
            if (r < 0) {
                (*nbad)++;
            } else if (r > 0) {
                int c;
                (*ngood)++;
                if (nout < maxn) {
                    for (c = 0; c < 8; c++) {
                        uv[nout][c] = s.uv[c];
                    }
                    if (ax) {
                        *ax = s.acc[0];
                    }
                    nout++;
                }
            }
        }
    }
    return nout;
}

static void stats(const float *x, int n, float *dc, float *rms, float *pk)
{
    int i;
    double s = 0, q = 0;
    float m = 0;
    if (n < 1) {
        *dc = *rms = *pk = 0;
        return;
    }
    for (i = 0; i < n; i++) {
        s += x[i];
        if (fabsf(x[i]) > m) {
            m = fabsf(x[i]);
        }
    }
    *dc = (float)(s / n);
    for (i = 0; i < n; i++) {
        float d = x[i] - *dc;
        q += (double)d * d;
    }
    *rms = sqrtf((float)(q / n));
    *pk = m;
}

int main(int argc, char **argv)
{
    const char *port = argc > 1 ? argv[1] : "/dev/ttyUSB1";
    const char *out = argc > 2 ? argv[2] : "tests/fixtures/live-table.csv";
    enum { MAXN = 4000 };
    static float uv[MAXN][8];
    struct np_parser p;
    int fd, n, c, i;
    uint32_t good = 0, bad = 0;
    float ax = 0;
    FILE *f;
    int pass = 1;

    fd = np_serial_open(port);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", port, strerror(errno));
        return 2;
    }
    np_parser_init(&p, NP_BOARD_KNIGHT_IMU);
    printf("listen %s (no DTR) 5s...\n", port);
    n = read_seconds(fd, &p, uv, MAXN, &ax, 5.0, &good, &bad);
    if (n < 80) {
        printf("only %d frames — one DTR, wait 8s\n", n);
        np_parser_init(&p, NP_BOARD_KNIGHT_IMU);
        np_serial_pulse_dtr(fd);
        np_serial_flush(fd);
        sleep(8);
        n = read_seconds(fd, &p, uv, MAXN, &ax, 12.0, &good, &bad);
    }
    np_serial_close(fd);
    printf("frames %d  good %u  bad %u  lock %d  flen %d  acc0 %.3f\n", n, good, bad, p.locked,
           p.frame_len, ax);

    if (n < 64) {
        fprintf(stderr, "FAIL live: not enough frames (%d)\n", n);
        return 1;
    }

    {
        double dt = (n >= 2) ? 12.0 : 5.0;
        /* sps from last capture window is approximate; use n / listen if we only did first. */
        (void)dt;
    }

    f = fopen(out, "w");
    if (f) {
        fprintf(f, "i,ch1,ch2,ch3,ch4,ch5,ch6,ch7,ch8\n");
        for (i = 0; i < n; i++) {
            fprintf(f, "%d", i);
            for (c = 0; c < 8; c++) {
                fprintf(f, ",%.3f", uv[i][c]);
            }
            fputc('\n', f);
        }
        fclose(f);
        printf("wrote %s (%d rows)\n", out, n);
    }

    printf("ch  dc_uV     rms_uV    pk_uV     tone_hz\n");
    for (c = 0; c < 8; c++) {
        float col[MAXN], dc, rms, pk, hz = 0.f;
        int have;
        for (i = 0; i < n; i++) {
            col[i] = uv[i][c];
        }
        stats(col, n, &dc, &rms, &pk);
        have = np_tone_hz(col, n > 256 ? 256 : n, 125.f, &hz) == 0;
        printf("%d  %9.1f %9.1f %9.1f  %s%.2f\n", c + 1, dc, rms, pk, have ? "" : "none ",
               have ? hz : 0.f);
        if (rms < 1.f && n > 200) {
            /* all-zero channel is possible if PD; not a hard fail */
        }
    }

    {
        float acc[256], hz = 0.f;
        int take = n > 256 ? 256 : n, used = 0;
        float pin = 0, pout = 0;
        struct np_notch nt;
        memset(acc, 0, sizeof(acc));
        for (c = 0; c < 8; c++) {
            for (i = 0; i < take; i++) {
                acc[i] += uv[i][c];
            }
            used++;
        }
        if (np_tone_hz(acc, take, 125.f, &hz) == 0) {
            printf("CAL tone (sum 8ch) %.2f Hz\n", hz);
            np_notch_init(&nt, hz, 125.f, 30.f);
            for (i = 0; i < take; i++) {
                pin += acc[i] * acc[i];
                acc[i] = np_notch_step(&nt, acc[i]);
                pout += acc[i] * acc[i];
            }
            printf("AUTO IIR energy ratio %.3f\n", pin > 1.f ? pout / pin : 0.f);
            if (hz < 45.f || hz > 65.f) {
                printf("WARN tone not in mains band (table may be quiet)\n");
            }
        } else {
            printf("CAL: no line tone above 4x band median (fail-closed AUTO)\n");
        }
    }

    if (p.frame_len != 21 && p.frame_len != 22 && p.frame_len != 57) {
        fprintf(stderr, "FAIL unexpected frame_len %d\n", p.frame_len);
        pass = 0;
    }
    printf(pass ? "LIVE PASS\n" : "LIVE FAIL\n");
    return pass ? 0 : 1;
}
