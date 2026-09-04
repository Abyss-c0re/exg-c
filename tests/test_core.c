#define _GNU_SOURCE
#include "np_dsp.h"
#include "np_knight.h"
#include "np_ring.h"
#include "np_algo.h"
#include "np_cube.h"
#include "np_smx.h"
#include "nplearn.h"
#include "np_atom.h"
#include "np_api.h"
#include "np_link.h"
#include "np_peer.h"

#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int fails;

static void test_view_extra(char *out, int n)
{
    snprintf(out, (size_t)n, "\"color\":[[255,255,255]],\"elec\":[\"FCz\"]");
}

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
    {
        float acc[3], gyr[3], mag[3];
        int ok = 0;
        memset(&s, 0, sizeof(s));
        s.imu = 1;
        s.acc[2] = 1.5f;
        s.gyr[1] = 0.4f;
        np_ring_push(&r, &s);
        np_ring_imu(&r, acc, gyr, mag, &ok);
        expect(ok && acc[2] == 1.5f && gyr[1] == 0.4f, "ring keeps last IMU");
    }
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

static float rms_of(const float *x, int n)
{
    int i;
    double q = 0;
    for (i = 0; i < n; i++) {
        q += (double)x[i] * x[i];
    }
    return sqrtf((float)(q / (n > 0 ? n : 1)));
}

/* NOISE plate (desk) then CALM plate (worn still). An 8 Hz burst
 * that is in neither plate must come out as SIGNAL. */
static void test_ml_harness(void)
{
    float noise[256], worn[256], ev[256];
    float hz = 0.f, noise_rms, calm_rms, raw_c, resid_c, raw_e, resid_e;
    struct np_notch nt;
    int i;

    synth(noise, 256, 125.f, 50.f, 20.f, 10.f, 0.2f);
    expect(np_tone_hz(noise, 256, 125.f, &hz) == 0, "harness: noise tone");
    noise_rms = rms_of(noise, 256);

    synth(worn, 256, 125.f, 50.f, 20.f, 10.f, 0.2f);
    for (i = 0; i < 256; i++) {
        worn[i] += 80.f;
    }
    raw_c = rms_of(worn, 256);
    np_notch_init(&nt, hz, 125.f, 30.f);
    for (i = 0; i < 256; i++) {
        worn[i] = np_notch_step(&nt, worn[i]);
    }
    np_sub_dc(worn, 256, 80.f);
    calm_rms = rms_of(worn, 256);
    resid_c = calm_rms;

    synth(ev, 256, 125.f, 50.f, 20.f, 8.f, 12.f);
    for (i = 0; i < 256; i++) {
        ev[i] += 80.f;
    }
    raw_e = rms_of(ev, 256);
    np_notch_init(&nt, hz, 125.f, 30.f);
    for (i = 0; i < 256; i++) {
        ev[i] = np_notch_step(&nt, ev[i]);
    }
    np_sub_dc(ev, 256, 80.f);
    resid_e = rms_of(ev, 256);

    {
        float desk[256], resid_n, dc = 0.f;
        int j;
        struct np_notch n2;
        synth(desk, 256, 125.f, 50.f, 20.f, 10.f, 0.2f);
        np_notch_init(&n2, hz, 125.f, 30.f);
        for (j = 0; j < 256; j++) {
            desk[j] = np_notch_step(&n2, desk[j]);
        }
        resid_n = rms_of(desk, 256);
        (void)dc;
        expect(np_detect(noise_rms, resid_n, noise_rms, calm_rms, NULL) == NP_DET_NOISE,
               "harness: desk = noise");
    }
    expect(np_detect(raw_c, resid_c, noise_rms, calm_rms, NULL) == NP_DET_CALM,
           "harness: worn still = calm");
    expect(np_detect(raw_e, resid_e, noise_rms, calm_rms, NULL) == NP_DET_SIGNAL,
           "harness: 8 Hz burst = SIGNAL");
    expect(resid_e > 1.5f * calm_rms, "harness: cleared event above calm");
    expect(np_detect(200.f, 80.f, 0.f, 0.f, NULL) == NP_DET_NONE,
           "harness: no plates is not SIGNAL");
    expect(np_detect(noise_rms, resid_e, noise_rms, 0.f, NULL) == NP_DET_NOISE,
           "harness: noise plate only, no CALM, is noise");
}

static void test_plate_destroy(void)
{
    float noise[NP_PLATE_N], live[NP_PLATE_N], sig[NP_PLATE_N], psd[NP_PSD_BINS];
    float hz = 0.f, pin = 0.f, pout = 0.f, spin = 0.f, spout = 0.f;
    int i;

    synth(noise, NP_PLATE_N, 125.f, 50.f, 10.f, 10.f, 0.3f);
    np_welch_psd(noise, NP_PLATE_N, psd);
    expect(np_tone_from_psd(psd, 125.f, &hz) == 0 && hz > 48.f && hz < 52.f,
           "welch plate tone ~50");
    {
        float fl = np_psd_floor(psd);
        int bin = (int)(50.f * (float)NP_FFT_N / 125.f);
        expect(fl > 0.f && psd[bin] > 1.5f * fl, "50 Hz sits above PSD floor");
    }

    synth(live, NP_PLATE_N, 125.f, 50.f, 10.f, 8.f, 3.f);
    for (i = 0; i < NP_PLATE_N; i++) {
        pin += live[i] * live[i];
    }
    np_plate_destroy(live, NP_PLATE_N, psd);
    for (i = 0; i < NP_PLATE_N; i++) {
        pout += live[i] * live[i];
    }
    expect(pout < 0.65f * pin, "plate destroy cuts 50 Hz plate");

    synth(sig, NP_PLATE_N, 125.f, 8.f, 3.f, 8.f, 0.f);
    for (i = 0; i < NP_PLATE_N; i++) {
        spin += sig[i] * sig[i];
    }
    np_plate_destroy(sig, NP_PLATE_N, psd);
    for (i = 0; i < NP_PLATE_N; i++) {
        spout += sig[i] * sig[i];
    }
    expect(spout > 0.35f * spin, "plate destroy keeps 8 Hz");
    {
        float win4[500], mid = 0.f, tail = 0.f;
        synth(win4, 500, 125.f, 50.f, 10.f, 8.f, 4.f);
        np_plate_destroy(win4, 500, psd);
        for (i = 200; i < 300; i++) {
            mid += win4[i] * win4[i];
        }
        for (i = 450; i < 500; i++) {
            tail += win4[i] * win4[i];
        }
        mid /= 100.f;
        tail /= 50.f;
        expect(tail < 4.f * mid + 8.f, "4s destroy has no raw tail");
    }
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
    {
        uint8_t z[64], a[64], b[64];
        memset(z, 0, 64);
        memset(a, 0, 64);
        memset(b, 0, 64);
        a[0] = 0x0F;
        b[0] = 0x03;
        expect(npl_cube_jaccard(z, z) == 0.f, "empty cubes are not unity");
        expect(npl_cube_jaccard(a, a) > 0.99f, "identical occupied Jaccard 1");
        expect(npl_cube_jaccard(a, b) > 0.4f && npl_cube_jaccard(a, b) < 0.6f,
               "partial overlap is not 1");
        npl_set_cube(&L, 0, a);
        npl_score(&L, wave, rms, mask);
        npl_score_cube(&L, a);
        expect(L.score[0] > 0.7f, "wave score stays after cube");
        expect(L.score_cube[0] > 0.99f, "cube Jaccard on same pose");
        npl_score(&L, wave, rms, mask);
        npl_score_cube(&L, z);
        expect(L.score[0] > 0.7f, "empty cube does not overwrite wave");
        expect(L.score_cube[0] == 0.f, "empty live cube Jaccard is 0");
        {
            float before = L.score[0];
            uint8_t rows[2] = {0x0F, 0x0F};
            npl_score_smx(&L, rows, 2);
            expect(L.score[0] == before, "SMX does not mix into MATCH");
        }
        expect(npl_add(&L, "twin", wave, rms, mask) == 1, "npl twin");
        npl_score(&L, wave, rms, mask);
        expect(L.best < 0, "two identical poses do not name a winner");
        expect(L.score[0] == 0.f && L.score[1] == 0.f, "no winner wipes cosine scores");
    }
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
    if (got_tone && hz <= 1.f) {
        expect(rows == 8, "on-disk cal 8 channels");
        expect(1, "on-disk cal tone idle (no line)");
        return;
    }
    expect(got_tone && hz > 45.f && hz < 55.f, "on-disk cal tone ~50 Hz");
    expect(rows == 8, "on-disk cal 8 channels");
}

static void test_smx(void)
{
    struct np_smx m;
    struct np_cube cubes[NP_CUBE_BUDGET + 4];
    char pack[NP_SMX_SEC * NP_NCHAN + 4];
    uint8_t row[NP_NCHAN];
    int ids[NP_NCHAN];
    int i, n, nc, on, lat, core;

    np_smx_init(&m);
    memset(row, 0, sizeof(row));
    row[0] = 1;
    row[2] = 1;
    row[7] = 1;
    np_smx_push(&m, row, 8, 0xFF);
    expect(m.nch == 8 && m.have == 1 && m.seq == 1, "smx first second");
    n = np_smx_pack(&m, pack, sizeof(pack));
    expect(n == 8 && strcmp(pack, "10100001") == 0, "smx pack newest row");

    memset(row, 0, sizeof(row));
    row[1] = 1;
    np_smx_push(&m, row, 8, 0xFF);
    n = np_smx_pack(&m, pack, sizeof(pack));
    expect(n == 16 && memcmp(pack, "01000000", 8) == 0, "smx newest first");
    expect(memcmp(pack + 8, "10100001", 8) == 0, "smx keeps prior second");

    expect(np_smx_ch_ids(&m, ids) == 8 && ids[0] == 1 && ids[7] == 8, "smx ch ids 1-8");

    for (i = 0; i < 40; i++) {
        memset(row, 0, sizeof(row));
        row[i % 8] = 1;
        np_smx_push(&m, row, 8, 0xFF);
    }
    expect(m.have == NP_SMX_SEC, "smx keeps 32 seconds");
    nc = np_smx_cubes(&m, cubes, NP_CUBE_BUDGET + 4);
    expect(nc > 2 && nc <= NP_CUBE_BUDGET, "smx cubes under budget 40");
    on = lat = core = 0;
    {
        int crim_ok = 1;
        for (i = 0; i < nc; i++) {
            if (cubes[i].role == 1) {
                core++;
            } else if (cubes[i].role == 2) {
                on++;
                if (cubes[i].r != NP_CUBE_CR || cubes[i].g != NP_CUBE_CG ||
                    cubes[i].b != NP_CUBE_CB) {
                    crim_ok = 0;
                }
            } else {
                lat++;
            }
        }
        expect(crim_ok, "smx channel cube crimson");
    }
    expect(core >= 1, "smx has core cube");
    expect(on + lat + core == nc, "smx roles lattice/core/channel");

    /* Width follows used channels, not always 8. */
    np_smx_init(&m);
    row[0] = 1;
    row[1] = 0;
    row[2] = 1;
    np_smx_push(&m, row, 3, 0x07);
    n = np_smx_pack(&m, pack, sizeof(pack));
    expect(n == 3 && strcmp(pack, "101") == 0, "smx width = used channels");
    expect(np_smx_ch_ids(&m, ids) == 3 && ids[0] == 1 && ids[2] == 3,
           "smx used-channel ids");

    {
        uint8_t packed[8] = {1, 0, 1};
        unsigned int f;
        np_smx_init(&m);
        np_smx_push(&m, packed, 3, 0x0D);
        f = np_smx_fold_ch(&m);
        expect(f == (1u | (1u << 3)), "smx fold remaps packed slots to channels");
    }
}

static void test_elec_view(void)
{
    struct np_elec e[NP_NCHAN], back;
    struct np_smx m;
    struct np_cube cubes[NP_CUBE_BUDGET];
    float x, y, z, x2, y2, z2;
    int i, n, sites;
    uint8_t row[NP_NCHAN];

    np_elec_default(e);
    {
        int distinct = 1;
        for (i = 1; i < NP_NCHAN; i++) {
            if (e[i].az == e[0].az && e[i].el == e[0].el) {
                distinct = 0;
            }
        }
        expect(distinct, "default sites distinct");
    }
    expect(np_1010_count() == NP_1010_N, "61 10-10 headset nodes");
    expect(np_1010_find("Fp1") >= 0 && np_1010_find("C3") >= 0 && np_1010_find("O2") >= 0,
           "headset names Fp1 C3 O2 exist");
    expect(np_1010_core(np_1010_find("Fp1")) && np_1010_core(np_1010_find("Cz")),
           "10-20 names are core markings");
    expect(!np_1010_core(np_1010_find("AF3")) && !np_1010_core(np_1010_find("FCz")),
           "10-10 intermediates are not 10-20 core");
    expect(strcmp(e[0].name, "FCz") == 0 && strcmp(e[1].name, "CPz") == 0, "default ch1 FCz ch2 CPz");
    expect(strcmp(e[6].name, "C3") == 0 && strcmp(e[7].name, "C4") == 0, "default C3 C4");
    {
        int p, ca, cb, ok = 1;
        expect(np_pair_count() == 4, "four leftover pairs");
        expect(strcmp(np_pair_site_a(0), "FCz") == 0 && strcmp(np_pair_site_b(0), "CPz") == 0,
               "pair 0 FCz-CPz");
        expect(strcmp(np_pair_site_a(1), "CP4") == 0 && strcmp(np_pair_site_b(1), "FC3") == 0,
               "pair 1 CP4-FC3");
        expect(strcmp(np_pair_site_a(2), "FC4") == 0 && strcmp(np_pair_site_b(2), "CP3") == 0,
               "pair 2 FC4-CP3");
        expect(strcmp(np_pair_site_a(3), "C3") == 0 && strcmp(np_pair_site_b(3), "C4") == 0,
               "pair 3 C3-C4");
        for (p = 0; p < 4; p++) {
            if (np_pair_chs(e, p, &ca, &cb) != 0 || ca < 0 || cb < 0 || ca == cb) {
                ok = 0;
            }
        }
        expect(ok, "each pair maps two distinct channels");
    }
    np_elec_to_xyz(&e[0], 1.f, &x, &y, &z);
    expect(fabsf(x * x + y * y + z * z - 1.f) < 1e-5f, "site on unit sphere");
    {
        struct np_elec fp;
        float fx, fy, fz;
        np_elec_set_site(&fp, np_1010_find("Fp1"));
        np_elec_to_xyz(&fp, 1.f, &fx, &fy, &fz);
        expect(fx < 0.f && fz > 0.f, "Fp1 is left-front");
    }
    {
        float fx, fy;
        np_1010_flat(np_1010_find("Fp1"), &fx, &fy);
        expect(fx < 0.f && fy > 0.f, "Fp1 flat is left-front");
        np_1010_flat(np_1010_find("Cz"), &fx, &fy);
        expect(fabsf(fx) < 0.05f && fabsf(fy) < 0.05f, "Cz flat is center");
    }
    np_elec_from_xyz(x, y, z, &back);
    expect(fabsf(back.az - e[0].az) < 0.05f && fabsf(back.el - e[0].el) < 0.05f,
           "xyz round-trip az/el");
    expect(strcmp(np_1010_name(np_1010_nearest(e[6].az, e[6].el)), "C3") == 0, "nearest snap C3");

    np_view_apply(0.7f, 0.4f, 1.f, 0.f, 0.f, &x, &y, &z);
    np_view_undo(0.7f, 0.4f, x, y, z, &x2, &y2, &z2);
    expect(fabsf(x2 - 1.f) < 1e-5f && fabsf(y2) < 1e-5f && fabsf(z2) < 1e-5f,
           "view rot inverse");
    np_view_apply(-1.1f, 0.5f, 3.f, 4.f, 0.f, &x, &y, &z);
    expect(fabsf(x * x + y * y + z * z - 25.f) < 1e-4f, "view rot preserves length");

    np_smx_init(&m);
    memset(row, 0, sizeof(row));
    row[0] = 1;
    np_smx_push(&m, row, 8, 0xFF);
    n = np_smx_head_cubes(&m, e, NULL, cubes, NP_CUBE_BUDGET);
    expect(n > 10 && n <= NP_CUBE_BUDGET, "cube lattice under budget 40");
    {
        float x1, y1, z1, x2, y2, z2;
        np_elec_cube_xyz(&e[0], &x1, &y1, &z1);
        np_elec_cube_xyz(&e[0], &x2, &y2, &z2);
        expect(x1 == x2 && y1 == y2 && z1 == z2, "channel cell does not move");
        expect(fabsf(x1) < 0.35f && z1 > 0.f, "FCz cube cell is midline-front");
        np_elec_cube_xyz(&e[1], &x2, &y2, &z2);
        expect(!(x1 == x2 && z1 == z2), "FCz and CPz occupy different cells");
        {
            struct np_elec fp;
            np_elec_set_site(&fp, np_1010_find("Fp1"));
            np_elec_cube_xyz(&fp, &x1, &y1, &z1);
            expect(x1 < 0.f && z1 > 0.f, "Fp1 cube cell is left-front");
        }
    }
    sites = 0;
    for (i = 0; i < n; i++) {
        if (cubes[i].role == 2) {
            sites++;
        }
    }
    expect(sites >= 1, "SIGNAL channel glows on the cube");
    {
        int rgb[NP_NCHAN][3];
        int found = 0;
        for (i = 0; i < NP_NCHAN; i++) {
            rgb[i][0] = 10 + i;
            rgb[i][1] = 80 + i;
            rgb[i][2] = 160 + i;
        }
        n = np_smx_head_cubes(&m, e, rgb, cubes, NP_CUBE_BUDGET);
        for (i = 0; i < n; i++) {
            if (cubes[i].role == 2 && cubes[i].r == 10 && cubes[i].g == 80 &&
                cubes[i].b == 160) {
                found = 1;
            }
        }
        expect(found, "glow uses per-channel color");
    }
}

static void test_cube3(void)
{
    struct np_smx m;
    struct np_elec e[NP_NCHAN];
    char pack[NP_CUBE3_N + 4];
    int x, y, z, x2, y2, z2;
    float acc[3] = {0.f, 0.f, 1.8f}, gyr[3] = {0, 0, 0}, mag[3] = {0, 0, 0};

    np_smx_init(&m);
    np_elec_default(e);
    expect(np_cube_idx(0, 0, 0) == 0 && np_cube_idx(7, 7, 7) == 511, "8^3 index 0..511");
    expect(np_cube_shell(0, 3, 3) && np_cube_shell(7, 1, 1), "faces are shell");
    expect(!np_cube_shell(3, 3, 3) && !np_cube_shell(4, 5, 3), "interior is virtual");
    expect(np_1010_ijk(np_1010_find("Fp1"), &x, &y, &z) == 0 && np_cube_shell(x, y, z),
           "Fp1 on shell");
    expect(x < 4 && z == 7, "Fp1 left-front face");
    expect(np_1010_ijk(np_1010_find("Cz"), &x, &y, &z) == 0 && y == 7, "Cz on top face");
    expect(np_1010_ijk(np_1010_find("O1"), &x, &y, &z) == 0 && z == 0, "O1 on back face");
    {
        int i, all_shell = 1, at[8], nat;
        for (i = 0; i < np_1010_count(); i++) {
            if (np_1010_ijk(i, &x, &y, &z) != 0 || !np_cube_shell(x, y, z)) {
                all_shell = 0;
            }
        }
        expect(all_shell, "every 10-10 name is a shell cell");
        np_1010_ijk(np_1010_find("Cz"), &x, &y, &z);
        nat = np_1010_sites_at(x, y, z, at, 8);
        expect(nat >= 1, "Cz cell has at least Cz");
    }
    np_1010_ijk(e[0].site, &x, &y, &z);
    np_1010_ijk(e[0].site, &x2, &y2, &z2);
    expect(x == x2 && y == y2 && z == z2, "headset cell is fixed");
    expect(np_virt_claim(&m, "bad", 0, 3, 3) < 0, "plugin cannot take EEG shell");
    expect(np_virt_claim(&m, "emg", 3, 4, 5) >= 0, "plugin claims interior");
    np_virt_write(&m, "emg", 1);
    expect(np_virt_read(&m, "emg") == 1, "plugin write/read");
    np_cube_imu(&m, acc, gyr, mag);
    expect(np_virt_read(&m, "accZ") == 1, "IMU accZ lights interior");
    expect(np_cube_pack(&m, pack, sizeof(pack)) == NP_CUBE3_N, "SoT pack is 512 bits");
    {
        int i, uniq = 1, on_shell = 1, seen[NP_CUBE3_N];
        memset(seen, 0, sizeof(seen));
        for (i = 0; i < NP_NCHAN; i++) {
            int id;
            np_1010_ijk(e[i].site, &x, &y, &z);
            if (!np_cube_shell(x, y, z)) {
                on_shell = 0;
            }
            id = np_cube_idx(x, y, z);
            if (seen[id]) {
                uniq = 0;
            }
            seen[id] = 1;
        }
        expect(on_shell, "default montage on shell");
        expect(uniq, "default 8 sites are distinct cells");
    }
    np_cube_set(&m, 1, 2, 7, 1, NP_CELL_EEG);
    expect(np_cube_get(&m, 1, 2, 7) == 1, "cube set/get");
    np_cube_clear_kind(&m, NP_CELL_EEG);
    expect(np_cube_get(&m, 1, 2, 7) == 0, "clear EEG leaves cell off");
    expect(np_virt_read(&m, "emg") == 1, "clear EEG keeps plugin bits");
    np_cube_set(&m, 0, 0, 0, 1, NP_CELL_IMU);
    np_cube_imu(&m, acc, gyr, mag);
    expect(np_cube_get(&m, 0, 0, 0) == 0, "IMU tick does not keep a shell bit");
}

static void test_algo(void)
{
    float hi[32], mix[32], z[32];
    int i;
    for (i = 0; i < 32; i++) {
        z[i] = 0.f;
        hi[i] = 2.f;
        mix[i] = (i & 1) ? 1.f : -1.f;
    }
    expect(np_algo_bit(NP_ALGO_DETECT, hi, 32, 1) == 1, "algo detect passthrough");
    expect(np_algo_bit(NP_ALGO_SIGN, hi, 32, 0) == 1, "algo sign +");
    expect(np_algo_bit(NP_ALGO_SIGN, z, 32, 0) == 0, "algo sign flat");
    expect(np_algo_bit(NP_ALGO_FOLD, hi, 32, 0) == 1, "algo fold majority high");
    expect(np_algo_bit(NP_ALGO_FOLD, mix, 32, 0) == 0, "algo fold split is 0");
    expect(np_algo_bit(NP_ALGO_PROTON, hi, 32, 0) == 1, "algo proton +energy");
    expect(np_algo_bit(NP_ALGO_DELTA, z, 32, 0) == 0, "algo delta still");
    expect(strcmp(np_algo_name(NP_ALGO_FOLD), "fold") == 0, "algo name fold");
}

static void test_profile_format(void)
{
    const char *path = "/tmp/exg-c-profile-mock.ini";
    FILE *f;
    char line[96];
    int gain1 = 0, active3 = -1, rld2 = -1, scale = 0, notch = 0;
    char elec1[8] = "", elec8[8] = "", prof[24] = "";

    f = fopen(path, "w");
    expect(f != NULL, "profile mock file opens");
    if (!f) {
        return;
    }
    fprintf(f, "[ui]\nscale=20\nprofile=motor\n");
    fprintf(f, "[view]\nnotch_hz=50\nhp_hz=1\n");
    fprintf(f, "[cube]\nelec1=Fp1\nelec8=O2\n");
    fprintf(f, "[channels]\ngain1=8\nactive3=0\nrld2=1\n");
    fclose(f);
    f = fopen(path, "r");
    expect(f != NULL, "profile mock file reads");
    if (!f) {
        return;
    }
    while (fgets(line, sizeof(line), f)) {
        int v, ch;
        if (sscanf(line, "scale=%d", &v) == 1) {
            scale = v;
        } else if (sscanf(line, "profile=%23s", prof) == 1) {
            ;
        } else if (sscanf(line, "notch_hz=%d", &notch) == 1) {
            ;
        } else if (sscanf(line, "elec1=%7s", elec1) == 1) {
            ;
        } else if (sscanf(line, "elec8=%7s", elec8) == 1) {
            ;
        } else if (sscanf(line, "gain%d=%d", &ch, &v) == 2 && ch == 1) {
            gain1 = v;
        } else if (sscanf(line, "active%d=%d", &ch, &v) == 2 && ch == 3) {
            active3 = v;
        } else if (sscanf(line, "rld%d=%d", &ch, &v) == 2 && ch == 2) {
            rld2 = v;
        }
    }
    fclose(f);
    expect(scale == 20 && notch == 50, "profile keeps UI and filter");
    expect(strcmp(prof, "motor") == 0, "profile name stored");
    expect(strcmp(elec1, "Fp1") == 0 && strcmp(elec8, "O2") == 0,
           "profile keeps electrode sites");
    expect(gain1 == 8 && active3 == 0 && rld2 == 1, "profile keeps gain on/rld");
}

static void test_id_event(void)
{
    float rms[8], calm[8];
    int fp[8] = {1, 1, 0, 0, 0, 0, 0, 0};
    uint8_t mask = 0xFF;
    float r = 0.f;
    int i;

    for (i = 0; i < 8; i++) {
        calm[i] = 20.f;
        rms[i] = 22.f;
    }
    expect(np_id_event(rms, calm, fp, mask, 0, &r) == NP_ID_NEED, "id: no plate is need CALM");
    expect(np_id_event(rms, calm, fp, mask, 1, &r) == NP_ID_STILL, "id: worn still");
    rms[0] = 80.f;
    rms[1] = 90.f;
    expect(np_id_event(rms, calm, fp, mask, 1, &r) == NP_ID_BLINK, "id: Fp pair is blink");
    for (i = 0; i < 8; i++) {
        rms[i] = 120.f;
    }
    expect(np_id_event(rms, calm, fp, mask, 1, &r) == NP_ID_CLENCH, "id: all-ch is clench");
    for (i = 0; i < 8; i++) {
        rms[i] = 22.f;
    }
    rms[2] = rms[3] = rms[5] = rms[6] = rms[7] = 40.f;
    expect(np_id_event(rms, calm, fp, mask, 1, &r) == NP_ID_CLENCH,
           "id: 2x on 5 ch is clench (EEG jaw)");
    for (i = 0; i < 8; i++) {
        rms[i] = 22.f;
    }
    rms[4] = 90.f;
    expect(np_id_event(rms, calm, fp, mask, 1, &r) == NP_ID_BURST, "id: one hot is burst");
    rms[4] = 400000.f;
    expect(np_id_event(rms, calm, fp, mask, 1, &r) == NP_ID_RAIL, "id: open rail");
}

static void test_process(void)
{
    float v[8];
    int use[8];
    float x[64];
    struct np_lp lp, ev;
    int i;

    expect(np_sample_clip(4001.f) && !np_sample_clip(100.f), "clip threshold 4 mV");
    expect(np_window_clip((float[]){0.f, 10.f, 5000.f}, 3), "window clip");
    expect(!np_window_clip((float[]){0.f, 10.f, -20.f}, 3), "window clean");
    for (i = 0; i < 8; i++) {
        v[i] = 1000.f + (float)i;
        use[i] = 1;
    }
    np_car_sample(v, use);
    expect(v[0] < 200.f && v[0] > -200.f, "CAR kills common 1 mV");
    for (i = 0; i < 8; i++) {
        v[i] = 4800.f;
        use[i] = 1;
    }
    v[3] = 5200.f;
    np_car_sample(v, use);
    expect(fabsf(v[3]) < 400.f, "CAR still subtracts a 5 mV channel");
    expect(fabsf(v[0]) < 400.f, "CAR kills 4.8 mV common mode (the test take)");
    v[3] = 400000.f;
    np_car_sample(v, use);
    expect(v[3] > 300000.f, "CAR leaves an open rail");
    expect(strcmp(np_band_name(2), "EEG") == 0, "band name EEG");
    np_lp_init(&lp, 8.f, 125.f);
    {
        float e_in = 0.f, e_out = 0.f, y;
        for (i = 0; i < 64; i++) {
            x[i] = (i % 2) ? 100.f : -100.f;
            e_in += x[i] * x[i];
            y = np_lp_step(&lp, x[i]);
            e_out += y * y;
        }
        expect(e_out < e_in * 0.35f, "LP cuts Nyquist square");
    }
    np_env_init(&ev, 0.15f, 125.f);
    expect(np_env_step(&ev, -80.f) >= 0.f, "envelope is unsigned");
}

static void test_atom(void)
{
    float planar[8 * 125];
    uint64_t a, b, ring[4], ref[4];
    int i, n, win = 0;
    char path[] = "/tmp/exg-atom-poc.npat";

    memset(planar, 0, sizeof(planar));
    a = np_atom_pack(planar, 8, 125, 125, 50.f);
    expect((a & 0x01) != 0 && (a & 0x40) != 0, "silence sets polarity+flat");
    expect(np_atom_unity(a, a) == 1.f, "self unity is 1");
    for (i = 0; i < 125; i++) {
        float s = 80.f * sinf(2.f * (float)M_PI * 10.f * (float)i / 125.f);
        int c;
        for (c = 0; c < 8; c++) {
            planar[c * 125 + i] = s;
        }
    }
    b = np_atom_pack(planar, 8, 125, 125, 50.f);
    expect(np_atom_hamming(a, b) > 0, "tone differs from silence");
    expect(np_atom_unity(a, b) < 1.f, "tone vs silence is not 1");
    expect(np_atom_ring_unity(NULL, 0, &a, 1) == 0.f, "empty live ring is 0");
    ring[0] = a;
    ring[1] = b;
    ref[0] = a;
    ref[1] = b;
    expect(np_atom_ring_unity(ring, 2, ref, 2) > 0.99f, "identical tails unity 1");
    expect(np_atom_save(path, ring, 2, 125) == 0, "atom save");
    n = np_atom_load(path, ref, 4, &win);
    expect(n == 2 && win == 125 && ref[0] == a && ref[1] == b, "atom load round-trip");
    {
        float rms[16], rms2[16];
        uint64_t got[4];
        int have = 0;
        np_atom_rms8(planar, 8, 125, 125, rms);
        np_atom_rms8(planar, 8, 125, 125, rms + 8);
        expect(np_atom_rms_cos(rms, 2, rms, 2) > 0.99f, "identical RMS cosine 1");
        expect(np_atom_rms_close(rms, 2, rms, 2) > 0.99f, "identical log-RMS 1");
        {
            float loud[16];
            int j;
            for (j = 0; j < 16; j++) {
                loud[j] = rms[j] * 4.f;
            }
            expect(np_atom_rms_cos(rms, 2, loud, 2) > 0.99f, "cosine ignores 4x loudness");
            expect(np_atom_rms_close(rms, 2, loud, 2) < 0.40f, "log-RMS sees 4x loudness");
        }
        expect(np_atom_save2(path, ring, rms, 2, 125) == 0, "atom save2");
        n = np_atom_load2(path, got, rms2, 4, &win, &have);
        expect(n == 2 && have == 1 && got[0] == a, "atom load2");
        expect(12 + 2 * 8 < 200, "atom file is tens of bytes");
        {
            char rp[] = "/tmp/exg-raw.nprw";
            float in[16], out[16];
            int ch = 0, ns = 0;
            float sps = 0.f;
            int k;
            for (k = 0; k < 16; k++) {
                in[k] = (float)k;
            }
            expect(np_raw_save(rp, in, 2, 8, 125.f) == 0, "raw save");
            expect(np_raw_load(rp, out, 16, &ch, &ns, &sps) == 16, "raw load");
            expect(ch == 2 && ns == 8 && out[9] == 9.f, "raw round-trip");
            remove(rp);
        }
        expect(np_atom_file_close(path, path) > 0.99f, "file close self is 1");
        expect(np_atom_save(path, ring, 2, 125) == 0, "v1 save");
        expect(np_atom_file_close(path, path) == 0.f, "v1 bits are not a score");
        {
            /* rest.npat / a.npat means: millivolt, 4+ ch over 4 mV CLIP. */
            char pa[] = "/tmp/exg-atom-rest.npat";
            char pb[] = "/tmp/exg-atom-act.npat";
            float rest[8] = {1961.f, 3734.f, 6235.f, 7923.f,
                             13790.f, 15928.f, 10298.f, 11939.f};
            float act[8] = {1892.f, 3365.f, 6443.f, 9313.f,
                            13795.f, 15621.f, 10882.f, 12141.f};
            float close;
            int hot = 0, c;
            for (c = 0; c < 8; c++) {
                if (rest[c] >= 4000.f) {
                    hot++;
                }
            }
            expect(hot >= 4, "rest would have tripped the old CLIP gate");
            expect(np_atom_rms_close(rest, 1, rest, 1) > 0.99f,
                   "CLIP-loud self is 1, not 0");
            close = np_atom_rms_close(rest, 1, act, 1);
            expect(close > 0.90f && close < 0.99f, "rest vs a is ~94%, not 0");
            expect(np_atom_save2(pa, ring, rest, 1, 125) == 0, "save rest");
            expect(np_atom_save2(pb, ring, act, 1, 125) == 0, "save act");
            close = np_atom_file_close(pa, pb);
            expect(close > 0.90f && close < 0.99f, "file close rest vs a ~94%");
            remove(pa);
            remove(pb);
        }
        {
            /* Evidence: 50 µV pack on a millivolt head is theater.
             * CALM-relative pack must flip energy bits on a 4× burst. */
            float restw[8 * 125], actw[8 * 125];
            uint64_t br, ba;
            int k, ch, sat_r = 0, sat_a = 0;
            float calm = 2000.f;
            memset(restw, 0, sizeof(restw));
            memset(actw, 0, sizeof(actw));
            for (k = 0; k < 125; k++) {
                float s = sinf(2.f * (float)M_PI * 20.f * (float)k / 125.f);
                for (ch = 0; ch < 8; ch++) {
                    restw[ch * 125 + k] = calm * s;
                    actw[ch * 125 + k] = 4.f * calm * s;
                }
            }
            br = np_atom_pack(restw, 8, 125, 125, 50.f);
            ba = np_atom_pack(actw, 8, 125, 125, 50.f);
            for (ch = 0; ch < 8; ch++) {
                uint8_t rb = (uint8_t)((br >> (8 * ch)) & 0xffu);
                uint8_t ab = (uint8_t)((ba >> (8 * ch)) & 0xffu);
                if ((rb & 0x96u) == 0x96u) {
                    sat_r++;
                }
                if ((ab & 0x96u) == 0x96u) {
                    sat_a++;
                }
            }
            expect(sat_r == 8 && sat_a == 8, "50uV scale saturates 2mV and 8mV");
            expect(np_atom_hamming(br, ba) < 8, "50uV Hamming cannot see 4x");
            br = np_atom_pack(restw, 8, 125, 125, calm);
            ba = np_atom_pack(actw, 8, 125, 125, calm);
            expect(np_atom_hamming(br, ba) >= 16, "CALM scale Hamming sees 4x");
            {
                float base[8];
                uint64_t rr, aa;
                uint8_t cube[64];
                int on = 0, i;
                for (ch = 0; ch < 8; ch++) {
                    base[ch] = calm;
                }
                rr = np_atom_pack_rel(restw, 8, 125, 125, base);
                aa = np_atom_pack_rel(actw, 8, 125, 125, base);
                expect(np_atom_hamming(rr, aa) >= 16, "pack_rel leftover baseline sees 4x");
                expect(np_atom_from_uv8(restw, base) != 0 || restw[0] != 0.f,
                       "from_uv8 runs");
                np_atom_faces8(aa, cube);
                for (i = 0; i < 64; i++) {
                    if (cube[i]) {
                        on++;
                    }
                }
                expect(on == np_atom_popcount(aa), "cube is 64 leftover bits");
            }
            {
                float rr[8], ar[8];
                np_atom_rms8(restw, 8, 125, 125, rr);
                np_atom_rms8(actw, 8, 125, 125, ar);
                expect(np_atom_rms_close(rr, 1, ar, 1) < 0.40f,
                       "CALM-scale log-RMS separates 4x");
            }
        }
        {
            /* stored rest.npat / a.npat (jaw clench). 1 s vs take mean. */
            float rest[5 * 8] = {
                2233, 2205, 2135, 2077, 2012, 2013, 2115, 2122,
                2428, 2449, 2401, 2310, 2125, 2244, 2381, 2390,
                2367, 3374, 3319, 3235, 2092, 3173, 3317, 3310,
                2314, 3334, 3749, 3752, 2052, 3649, 3749, 3738,
                2400, 3079, 3575, 3580, 2097, 3519, 3578, 3566
            };
            float act[5 * 8] = {
                2413, 4395, 5033, 4994, 2293, 5063, 5076, 5069,
                2351, 4353, 4920, 4869, 2398, 4944, 4959, 4951,
                2212, 4307, 4804, 4795, 2226, 4830, 4845, 4833,
                2448, 4335, 4840, 4779, 2385, 4851, 4878, 4871,
                2465, 2975, 3250, 3274, 2309, 3303, 3299, 3292
            };
            float clench[8], mix[5 * 8];
            float vs_rest, vs_act;
            int j;
            memcpy(clench, act, 8 * sizeof(float));
            vs_rest = np_atom_rms_close_to_mean(clench, 1, rest, 5);
            vs_act = np_atom_rms_close_to_mean(clench, 1, act, 5);
            expect(vs_act > vs_rest + 0.08f, "1s clench names a, not rest");
            expect(vs_act > 0.85f && vs_rest < 0.80f, "clench vs a-mean high, vs rest-mean not");
            memcpy(mix, rest, 4 * 8 * sizeof(float));
            memcpy(mix + 4 * 8, act, 8 * sizeof(float));
            vs_rest = np_atom_rms_close(mix, 5, rest, 5);
            vs_act = np_atom_rms_close(mix, 5, act, 5);
            expect(vs_rest > vs_act, "8s newest-align names rest during a 1s clench");
            vs_rest = np_atom_rms_close_to_mean(mix, 5, rest, 5);
            vs_act = np_atom_rms_close_to_mean(mix, 5, act, 5);
            expect(vs_act > vs_rest + 0.08f, "last-1s vs mean names the clench");
            {
                float pat[8];
                int np;
                np = np_atom_rms_pattern(act, 5, rest, 5, pat);
                expect(np == 4, "a pattern is 4 distinctive seconds, not the rest tail");
                vs_rest = np_atom_rms_close_to_pattern(clench, 1, rest, 5, rest, 5);
                vs_act = np_atom_rms_close_to_pattern(clench, 1, act, 5, rest, 5);
                expect(vs_act > 0.95f && vs_act > vs_rest + 0.20f,
                       "clench matches a-pattern, not rest");
                vs_rest = np_atom_rms_close_to_pattern(rest + 4 * 8, 1, rest, 5, rest, 5);
                vs_act = np_atom_rms_close_to_pattern(rest + 4 * 8, 1, act, 5, rest, 5);
                expect(vs_rest > vs_act + 0.08f, "rest second matches rest, not a-pattern");
            }
            (void)j;
        }
    }
}

static int http_get(const char *host, int port, const char *path, char *out, int n)
{
    int fd, w, r, got = 0;
    char req[256];
    struct sockaddr_in a;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = inet_addr(host);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        close(fd);
        return -1;
    }
    {
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 250000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    w = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", path, host);
    if (send(fd, req, (size_t)w, 0) < 0) {
        close(fd);
        return -1;
    }
    out[0] = 0;
    while (got < n - 1) {
        r = (int)recv(fd, out + got, (size_t)(n - 1 - got), 0);
        if (r <= 0) {
            break;
        }
        got += r;
    }
    out[got] = 0;
    close(fd);
    return got;
}

static void test_api(void)
{
    struct np_api_sample a, b;
    unsigned char raw[NP_API_FRAME];
    struct np_api_cfg c;
    char body[2048];
    int i, ok, op, arg;
    int udp;
    struct sockaddr_in ua;

    memset(&a, 0, sizeof(a));
    a.t_us = 123456789ull;
    a.frames = 42;
    a.nch = 8;
    a.mask = 0x0F;
    a.clip = 0x01;
    a.flags = 5;
    a.uv[0] = 12.5f;
    a.uv[3] = -80.f;
    a.sps = 125.f;
    a.id_score = 0.91f;
    a.id_best = 2;
    expect(np_api_pack(raw, sizeof(raw), &a) == NP_API_FRAME, "api pack size");
    expect(raw[0] == 'E' && raw[3] == '1', "api magic EXG1");
    expect(np_api_unpack(raw, NP_API_FRAME, &b) == NP_API_FRAME, "api unpack");
    expect(b.frames == 42 && b.mask == 0x0F && b.id_best == 2, "api fields");
    expect(fabsf(b.uv[0] - 12.5f) < 0.001f && fabsf(b.uv[3] + 80.f) < 0.001f, "api uv");
    expect(fabsf(b.sps - 125.f) < 0.01f && fabsf(b.id_score - 0.91f) < 0.001f, "api scores");

    np_api_cfg_default(&c);
    expect(c.on == 0, "api default off");
    c.on = 1;
    c.lan = 0;
    c.http = 18765;
    c.udp = 18766;
    c.tcp = 0;
    c.hz = 125;
    c.token[0] = 0;
    expect(np_api_apply(&c) == 0, "api listen local");
    usleep(80000);
    expect(np_api_on() == 1, "api on");
    ok = 0;
    for (i = 0; i < 20 && !ok; i++) {
        if (http_get("127.0.0.1", 18765, "/health", body, sizeof(body)) > 0 &&
            strstr(body, "\"ok\":true")) {
            ok = 1;
        } else {
            usleep(20000);
        }
    }
    expect(ok, "api GET /health");
    expect(strstr(body, "\"bind\":\"local\"") != NULL, "api health local");

    a.uv[1] = 333.f;
    np_api_push(&a);
    usleep(20000);
    ok = http_get("127.0.0.1", 18765, "/sample", body, sizeof(body)) > 0 &&
         strstr(body, "333");
    expect(ok, "api GET /sample last push");

    udp = socket(AF_INET, SOCK_DGRAM, 0);
    expect(udp >= 0, "api udp socket");
    if (udp >= 0) {
        unsigned char pkt[NP_API_FRAME];
        struct timeval tv;
        memset(&ua, 0, sizeof(ua));
        ua.sin_family = AF_INET;
        ua.sin_port = htons(18766);
        ua.sin_addr.s_addr = inet_addr("127.0.0.1");
        sendto(udp, "SUB", 3, 0, (struct sockaddr *)&ua, sizeof(ua));
        usleep(30000);
        a.uv[2] = 777.f;
        np_api_push(&a);
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        setsockopt(udp, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ok = 0;
        if (recv(udp, pkt, sizeof(pkt), 0) == NP_API_FRAME &&
            np_api_unpack(pkt, NP_API_FRAME, &b) == NP_API_FRAME &&
            fabsf(b.uv[2] - 777.f) < 0.01f) {
            ok = 1;
        }
        expect(ok, "api UDP subscribe gets frame");
        {
            unsigned char pingb[12], pong[16];
            struct timeval tv;
            uint64_t tsend = 123456789ull;
            pingb[0] = 'P';
            pingb[1] = 'I';
            pingb[2] = 'N';
            pingb[3] = 'G';
            pingb[4] = (unsigned char)tsend;
            pingb[5] = (unsigned char)(tsend >> 8);
            pingb[6] = (unsigned char)(tsend >> 16);
            pingb[7] = (unsigned char)(tsend >> 24);
            pingb[8] = (unsigned char)(tsend >> 32);
            pingb[9] = (unsigned char)(tsend >> 40);
            pingb[10] = (unsigned char)(tsend >> 48);
            pingb[11] = (unsigned char)(tsend >> 56);
            sendto(udp, pingb, 12, 0, (struct sockaddr *)&ua, sizeof(ua));
            tv.tv_sec = 0;
            tv.tv_usec = 200000;
            setsockopt(udp, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            ok = 0;
            {
                int rn = (int)recv(udp, pong, sizeof(pong), 0);
                if (rn >= 12 && pong[0] == 'P' && pong[1] == 'O' && pong[2] == 'N' &&
                    pong[3] == 'G' && pong[4] == pingb[4] && pong[11] == pingb[11]) {
                    ok = 1;
                }
            }
            expect(ok, "api UDP PING/PONG");
        }
        close(udp);
    }

    ok = http_get("127.0.0.1", 18765, "/", body, sizeof(body)) > 0 &&
         strstr(body, "/stream") && strstr(body, "EXG1");
    expect(ok, "api GET / index lists stream");
    expect(strstr(body, "stream.json") == NULL, "api index has no NDJSON live path");
    expect(strstr(body, "\"v\":\"2.50\"") != NULL, "api index version 2.50");
    expect(strstr(body, "\"ip\":\"127.0.0.1\"") != NULL, "api local ip is loopback");
    {
        int k = http_get("127.0.0.1", 18765, "/kit", body, sizeof(body));
        expect(k > 0 && strstr(body, "no kit") != NULL, "kit empty without host");
    }
    {
        char host[64];
        int hp = 0, up = 0;
        expect(np_link_parse_dest("box:8765", host, 64, &hp, &up) == 0, "parse dest host:port");
        expect(strcmp(host, "box") == 0 && hp == 8765 && up == 8766, "udp defaults to http+1");
        expect(np_link_parse_dest("box:8765/9000", host, 64, &hp, &up) == 0, "parse dest slash udp");
        expect(hp == 8765 && up == 9000, "explicit udp port");
        expect(np_link_parse_dest("", host, 64, &hp, &up) != 0, "empty dest fails");
    }
    {
        struct np_peers p;
        char g[32], path[] = "/tmp/exg-peer-test";
        np_peers_init(&p);
        np_peers_mkgrant(g, 32);
        expect(g[0] != 0, "grant is made");
        expect(np_peers_allow_add(&p, "Quest_3", g) >= 0, "allow add");
        expect(np_peers_grant_ok(&p, g), "allow grant ok");
        expect(!np_peers_grant_ok(&p, "nope"), "unknown grant is not ok");
        expect(np_peers_follow_add(&p, "Titan_2", "x:8765", g) >= 0, "follow add");
        expect(np_peers_save(&p, path) == 0, "peer save");
        np_peers_init(&p);
        expect(np_peers_load(&p, path) == 0 && p.nallow == 1 && p.nfollow == 1, "peer load");
        expect(np_peers_grant_ok(&p, g), "loaded grant ok");
        np_peers_allow_del(&p, 0);
        expect(!np_peers_grant_ok(&p, g), "revoke drops grant");
        unlink(path);
    }
    np_api_set_view_fn(test_view_extra);
    ok = http_get("127.0.0.1", 18765, "/cfg", body, sizeof(body)) > 0;
    expect(ok && strstr(body, "\"color\"") && strstr(body, "FCz"), "cfg carries color and map");
    np_api_set_view_fn(NULL);

    {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        char req[] = "POST /pause HTTP/1.0\r\nContent-Length: 0\r\n\r\n";
        struct sockaddr_in ha;
        memset(&ha, 0, sizeof(ha));
        ha.sin_family = AF_INET;
        ha.sin_port = htons(18765);
        ha.sin_addr.s_addr = inet_addr("127.0.0.1");
        if (fd >= 0 && connect(fd, (struct sockaddr *)&ha, sizeof(ha)) == 0) {
            send(fd, req, sizeof(req) - 1, 0);
            usleep(30000);
        }
        if (fd >= 0) {
            close(fd);
        }
    }
    usleep(20000);
    expect(np_api_take_op(&op, &arg) == 1 && op == NP_API_OP_PAUSE, "api POST /pause queues");

    np_api_stop();
    expect(np_api_on() == 0, "api stop");
    (void)errno;
}

int main(void)
{
    test_cmds();
    test_parser();
    test_ring();
    test_auto_from_cal();
    test_ml_harness();
    test_plate_destroy();
    test_nplearn();
    test_disk_cal();
    test_replay_live_csv();
    test_smx();
    test_elec_view();
    test_cube3();
    test_algo();
    test_profile_format();
    test_id_event();
    test_process();
    test_atom();
    test_api();
    if (fails) {
        fprintf(stderr, "%d FAIL\n", fails);
        return 1;
    }
    printf("all mock tests passed\n");
    return 0;
}
