#define _GNU_SOURCE
#include "np_dsp.h"
#include "np_knight.h"
#include "np_ring.h"
#include "nplearn.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int fails;

static void expect(int cond, const char *name)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", name);
        fails++;
    } else {
        printf("ok   %s\n", name);
    }
}

static void put_i16be(unsigned char *p, int v)
{
    p[0] = (unsigned char)((v >> 8) & 0xFF);
    p[1] = (unsigned char)(v & 0xFF);
}

static void put_f32le(unsigned char *p, float f)
{
    union {
        float f;
        unsigned char c[4];
    } u;
    u.f = f;
    memcpy(p, u.c, 4);
}

static void make_imu_frame(unsigned char *f, uint8_t seq, const int raw[NP_NCHAN],
                           float ax)
{
    int i;
    memset(f, 0, 57);
    f[0] = NP_START;
    f[1] = seq;
    for (i = 0; i < NP_NCHAN; i++) {
        put_i16be(f + 2 + 2 * i, raw[i]);
    }
    f[18] = 0;
    f[19] = 0;
    put_f32le(f + 20, ax);
    put_f32le(f + 24, 0.f);
    put_f32le(f + 28, 1.f);
    f[56] = NP_END;
}

static float scale_uv_ref(int raw, int gain)
{
    return (4.0f / 32767.0f / (float)gain) * 1000000.0f * (float)raw / 79.57f;
}

static void test_cmds(void)
{
    char s[32];
    np_fmt_chon(s, sizeof(s), 1, 12);
    expect(strcmp(s, "chon_1_12\n") == 0, "fmt chon_1_12");
    np_fmt_chon(s, sizeof(s), 8, 24);
    expect(strcmp(s, "chon_8_24\n") == 0, "fmt chon_8_24");
    np_fmt_rldadd(s, sizeof(s), 3);
    expect(strcmp(s, "rldadd_3\n") == 0, "fmt rldadd_3");
    np_fmt_choff(s, sizeof(s), 2);
    expect(strcmp(s, "choff_2\n") == 0, "fmt choff_2");
}

static void test_parser(void)
{
    struct np_parser p;
    struct np_sample s;
    unsigned char fr[57];
    int raw[NP_NCHAN] = {1000, -1000, 0, 32767, -32767, 12, -12, 500};
    int i, nlock = 0, nout = 0;
    float want;

    make_imu_frame(fr, 7, raw, 0.5f);
    np_parser_init(&p, NP_BOARD_KNIGHT_IMU);
    for (i = 0; i < 57; i++) {
        int r = np_parser_feed(&p, fr[i], &s);
        if (r > 0) {
            nout++;
        }
        if (p.locked) {
            nlock = 1;
        }
    }
    expect(nout == 1 && nlock && p.frame_len == 57, "parser lock 57");
    expect(s.seq == 7 && s.imu == 1, "parser seq imu");
    want = scale_uv_ref(1000, 12);
    expect(fabsf(s.uv[0] - want) < 0.02f * (fabsf(want) + 1.f), "parser ch1 scale");
    expect(fabsf(s.acc[0] - 0.5f) < 1e-5f, "parser acc x");

    /* junk then a good frame — must resync */
    np_parser_init(&p, NP_BOARD_KNIGHT_IMU);
    nout = 0;
    np_parser_feed(&p, 0x11, &s);
    np_parser_feed(&p, 0x22, &s);
    for (i = 0; i < 57; i++) {
        if (np_parser_feed(&p, fr[i], &s) > 0) {
            nout++;
        }
    }
    expect(nout == 1, "parser resync after junk");
}

static void test_ring(void)
{
    struct np_ring r;
    struct np_sample s;
    float buf[16];
    uint32_t n;
    int i;

    memset(&s, 0, sizeof(s));
    np_ring_init(&r);
    for (i = 0; i < 10; i++) {
        s.uv[0] = (float)i;
        np_ring_push(&r, &s);
    }
    n = np_ring_copy(&r, 0, buf, 4);
    expect(n == 4 && buf[0] == 6.f && buf[3] == 9.f, "ring last-4");
}

static void synth(float *x, int n, float sps, float line_hz, float line_a, float sig_hz,
                  float sig_a)
{
    int i;
    for (i = 0; i < n; i++) {
        float t = (float)i / sps;
        x[i] = line_a * sinf(2.f * (float)M_PI * line_hz * t) +
               sig_a * sinf(2.f * (float)M_PI * sig_hz * t);
    }
}

static void test_auto_from_cal(void)
{
    float cal[256], live[256];
    float hz = 0.f, pin = 0.f, pout = 0.f;
    int i;
    struct np_notch nt;

    /* Table-top / headset-off: strong 50 Hz + tiny 10 Hz. */
    synth(cal, 256, 125.f, 50.f, 10.f, 10.f, 0.4f);
    expect(np_tone_hz(cal, 256, 125.f, &hz) == 0, "cal finds tone");
    expect(hz > 48.f && hz < 52.f, "cal tone near 50");

    synth(live, 256, 125.f, 50.f, 10.f, 10.f, 2.f);
    for (i = 0; i < 256; i++) {
        pin += live[i] * live[i];
    }
    np_notch_init(&nt, hz, 125.f, 30.f);
    for (i = 0; i < 256; i++) {
        live[i] = np_notch_step(&nt, live[i]);
        pout += live[i] * live[i];
    }
    expect(pout < 0.35f * pin, "AUTO IIR cuts cal tone");

    /* 60 Hz wide notch still bites at 125 SPS. */
    synth(live, 256, 125.f, 60.f, 8.f, 8.f, 1.f);
    pin = pout = 0.f;
    np_notch_init(&nt, 60.f, 125.f, 30.f);
    for (i = 0; i < 256; i++) {
        pin += 64.f;
        live[i] = np_notch_step(&nt, live[i]);
        pout += live[i] * live[i];
    }
    /* rebuild pin properly */
    synth(cal, 256, 125.f, 60.f, 8.f, 8.f, 1.f);
    pin = 0.f;
    np_notch_init(&nt, 60.f, 125.f, 30.f);
    pout = 0.f;
    for (i = 0; i < 256; i++) {
        pin += cal[i] * cal[i];
        cal[i] = np_notch_step(&nt, cal[i]);
        pout += cal[i] * cal[i];
    }
    expect(pout < 0.5f * pin, "60 Hz wide notch cuts");
}

static void test_nplearn(void)
{
    struct npl L;
    float wave[NPL_NCHAN][NPL_LEN];
    float rms[NPL_NCHAN];
    float src[128];
    int i;
    uint8_t mask = 0x01;

    npl_init(&L);
    for (i = 0; i < 128; i++) {
        src[i] = sinf(2.f * (float)M_PI * 10.f * (float)i / 125.f);
    }
    expect(npl_prep(wave[0], &rms[0], src, 128, 125.f, 50.f) == 0, "npl_prep");
    expect(npl_add(&L, "ten", wave, rms, mask) == 0, "npl_add");
    npl_score(&L, wave, rms, mask);
    expect(L.best == 0 && L.score[0] > 0.7f, "npl self-score");
}

static void test_replay_live_csv(void)
{
    FILE *f = fopen("tests/fixtures/live-table.csv", "r");
    char line[256];
    float acc[256];
    int n = 0, c;
    float hz = 0.f;
    if (!f) {
        printf("skip live csv replay\n");
        return;
    }
    memset(acc, 0, sizeof(acc));
    fgets(line, sizeof(line), f);
    while (n < 256 && fgets(line, sizeof(line), f)) {
        int idx;
        float v[8];
        if (sscanf(line, "%d,%f,%f,%f,%f,%f,%f,%f,%f", &idx, &v[0], &v[1], &v[2], &v[3], &v[4],
                   &v[5], &v[6], &v[7]) != 9) {
            continue;
        }
        for (c = 0; c < 8; c++) {
            acc[n] += v[c];
        }
        n++;
    }
    fclose(f);
    expect(n >= 128, "live csv has 128+ rows");
    expect(np_tone_hz(acc, n, 125.f, &hz) == 0, "replay live plate finds tone");
    expect(hz > 48.f && hz < 52.f, "replay live tone is 50 Hz mains");
}

static void test_disk_cal(void)
{
    FILE *f = fopen("exg-c.cal", "r");
    char line[128];
    int got_tone = 0, rows = 0;
    float hz = 0.f;
    if (!f) {
        printf("skip disk cal (no exg-c.cal)\n");
        return;
    }
    while (fgets(line, sizeof(line), f)) {
        int ch;
        float dc, rms, pk;
        if (sscanf(line, "tone_hz=%f", &hz) == 1) {
            got_tone = 1;
        } else if (sscanf(line, "%d,%f,%f,%f", &ch, &dc, &rms, &pk) == 4) {
            rows++;
        }
    }
    fclose(f);
    expect(got_tone && hz > 45.f && hz < 55.f, "on-disk cal tone ~50 Hz");
    expect(rows == 8, "on-disk cal 8 channels");
}

int main(void)
{
    test_cmds();
    test_parser();
    test_ring();
    test_auto_from_cal();
    test_nplearn();
    test_disk_cal();
    test_replay_live_csv();
    if (fails) {
        fprintf(stderr, "%d FAIL\n", fails);
        return 1;
    }
    printf("all mock tests passed\n");
    return 0;
}
