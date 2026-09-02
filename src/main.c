#define _GNU_SOURCE
#include "np_dsp.h"
#include "np_font.h"
#include "np_knight.h"
#include "nplearn.h"
#include "np_ring.h"
#include "np_serial.h"
#include "np_algo.h"
#include "np_cube.h"
#include "np_smx.h"
#include "np_host.h"
#include "np_atom.h"
#ifdef NP_ANDROID_UI
#include "sdl2_min.h"
#include <android/log.h>
#define NP_ALOG(...) __android_log_print(ANDROID_LOG_INFO, "exg-c", __VA_ARGS__)
#elif defined(__ANDROID__)
#include "SDL.h"
#include "SDL_system.h"
#include <android/log.h>
#define NP_ALOG(...) __android_log_print(ANDROID_LOG_INFO, "exg-c", __VA_ARGS__)
#else
#include "sdl2_min.h"
#define NP_ALOG(...) ((void)0)
#endif

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#ifndef __ANDROID__
#include <grp.h>
#endif
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <pthread.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poll.h>
#include <unistd.h>

#define WIN_W 1280
#define WIN_H 800
#define SIDE_W 300
#define STATUS_H 28
#define FFT_H 96
#define LEARN_H 108
#ifdef __ANDROID__
#define NP_TOUCH 1
#else
#define NP_TOUCH 0
#endif
#define REC_MS 4000
#define LEARN_S 1.0f
#define WAVE_TOP 8
#define OPEN_UV 3000.f
#define QMAX 48
#define NP_PROF_NAME 24
#define NP_MAX_PROF 16
#define CMD_CHON 1
#define CMD_CHOFF 2
#define CMD_RLDADD 3
#define CMD_RLDRM 4

static int win_w = WIN_W, win_h = WIN_H;

struct np_app {
    int fd;
    int running;
    int connected;
    int want_connect;
    enum np_board board;
    int port_i;
    int nports;
    char ports[NP_MAX_PORTS][NP_MAX_PATH];
    int active[NP_NCHAN];
    int rld[NP_NCHAN];
    int gain[NP_NCHAN];
    int recording;
    FILE *csv;
    char csv_path[NP_MAX_PATH];
    char status[160];
    int status_ok;
    struct np_parser parser;
    struct np_ring ring;
    pthread_t thr;
    pthread_t en_thr;
    pthread_t cmd_thr;
    int en_running;
    pthread_mutex_t mu;
    pthread_mutex_t qmu;
    pthread_cond_t qcv;
    int qh, qt;
    struct {
        int op, ch, gain;
    } q[QMAX];
    /* visualization / sampling */
    int window_s;
    int autoscale;
    int og;
    int scale_uv;
    int notch_hz;
    int hp_hz;
    int lp_hz;
    int car;
    int envelope;
    int band;
    int grid;
    int paused;
    int show_uv;
    int detrend;
    float sps;
    uint32_t sps_n;
    struct timespec sps_t;
    struct np_hp hp[NP_NCHAN];
    struct np_notch notch[NP_NCHAN];
    struct np_lp lp[NP_NCHAN];
    struct np_lp env[NP_NCHAN];
    struct npl learn;
    int typing;
    char namebuf[NPL_NAME];
    struct {
        int have;
        uint32_t n;
        float dc[NP_NCHAN], rms[NP_NCHAN], pk[NP_NCHAN];
    } off, on, cal, calm;
    int cal_arm;
    int cal_cut;
    float cal_hz; /* line tone from noise plate; 0 = none */
    float noise_psd[NP_PSD_BINS];
    int noise_psd_ok;
    uint32_t rec_t0;
    uint32_t saved_t0;
    uint64_t stall_tot;
    int stall_n;
    uint32_t stall_t;
    int recover_n;
    int tab;
    int ui_scale; /* tenths: 10, 15, 20 → 1.0x 1.5x 2.0x */
    int pref_w, pref_h;
    int chrgb[NP_NCHAN][3];
    pthread_mutex_t csv_mu;
    pthread_mutex_t parse_mu;
    struct np_smx smx;
    char cube_ack[48];
    int cube_ok;
    float cube_yaw, cube_pitch;
    float cube_zoom;
    int cube_view; /* 0 viz (crimson lattice)  1 map (10-10 assign) */
    int site_focus; /* 10-10 index */
    int virt_focus; /* IMU / plugin slot */
    struct np_elec elec[NP_NCHAN];
    int elec_sel; /* 0..7 or -1 */
    int algo;     /* NP_ALGO_* 0/1 fold for cube + learn */
    char prof[NP_PROF_NAME];
    char profiles[NP_MAX_PROF][NP_PROF_NAME];
    int nprof;
    int typing_prof;
    int side_scroll;
    int atom_on;
    uint64_t atom_live[NP_ATOM_RING];
    int atom_n;
    int atom_wr;
    uint64_t atom_ref[NP_ATOM_RING];
    int atom_ref_n;
    char atom_ref_name[NP_ATOM_NAME];
    float atom_unity;
    unsigned int atom_seq;
    char atom_a[NP_ATOM_NAME];
    char atom_b[NP_ATOM_NAME];
    float atom_ab;
};

static struct np_app g;
static SDL_Window *Win;
static SDL_Renderer *R;
static int sidew(void)
{
    if (NP_TOUCH) {
        int w = win_w * 36 / 100;
        if (w < 300) {
            w = 300;
        }
        if (w > 380) {
            w = 380;
        }
        if (w > win_w / 2) {
            w = win_w / 2;
        }
        return w;
    }
    return SIDE_W;
}
static int statush(void)
{
    return NP_TOUCH ? 42 : STATUS_H;
}
static int btnh(void)
{
    return NP_TOUCH ? 40 : 22;
}
static int rowh(void)
{
    return NP_TOUCH ? 48 : 26;
}
static int learnh(void)
{
    if (NP_TOUCH) {
        return win_h < 560 ? 150 : 168;
    }
    if (win_h < 520) {
        return 88;
    }
    return LEARN_H;
}
static int ffth(void)
{
    if (win_h < 520) {
        return 56;
    }
    if (win_h < 640) {
        return 72;
    }
    return FFT_H;
}
static float ui_f(void)
{
    int t = g.ui_scale;
    if (t != 10 && t != 15 && t != 20) {
        t = 15;
    }
    return (float)t / 10.f;
}
static void set_status(int ok, const char *fmt, ...);
static void typing_set(int on);
static void apply_filt(int ch, float *buf, uint32_t n);
static void cook_all(float buf[NP_NCHAN][NP_RING], uint32_t nn[NP_NCHAN], uint32_t want);
static void band_apply(int band);
static void present_cube(int x, int y, int w, int h);
static void ch_stats(const float *buf, uint32_t n, float *dc, float *rms, float *pk);
static void cmd_push(int op, int ch, int gain);
static void cfg_save(void);
static const int SCALE_UV[] = {50, 100, 200, 500, 1000, 5000};
#define NSCALE 6
static const int WIN_S[] = {1, 2, 4, 8};
#define NWINS 4
static const int WINPREF[][2] = {{1280, 800}, {1440, 900}, {1600, 1000}, {1920, 1080}};
#define NWINPREF 4

/* Do not cook filter poles from a lagged measured rate (46 SPS makes
 * a 50 Hz notch sit past Nyquist and a 60 Hz notch is already there). */
static uint32_t view_copy(int ch, float *dst, uint32_t n);
static float design_sps(void)
{
    if (g.sps >= 100.f && g.sps <= 160.f) {
        return g.sps;
    }
    return (float)NP_DEFAULT_SPS;
}

static uint32_t plate_want(void)
{
    uint32_t w = (uint32_t)(g.window_s * design_sps());
    if (w < (uint32_t)NP_PLATE_N) {
        w = (uint32_t)NP_PLATE_N;
    }
    if (w > NP_RING) {
        w = NP_RING;
    }
    return w;
}

static float notch_hz_eff(void)
{
    if (g.notch_hz < 0) {
        return g.cal_hz;
    }
    return (float)g.notch_hz;
}

static uint64_t live_seen;
static int live_sig = -1;
static uint32_t live_wr;
static float live_ch[NP_NCHAN][NP_RING];
static int last_clip[NP_NCHAN];
static uint32_t clean_t;
static uint64_t clean_seen;
static uint32_t clean_n[NP_NCHAN];
static float clean_ch[NP_NCHAN][NP_RING];

static int filt_sig(void)
{
    return g.hp_hz + (g.notch_hz + 3) * 97 + (int)(notch_hz_eff() * 10.f) + g.cal_cut * 10007 +
           g.lp_hz * 13 + g.car * 17 + g.envelope * 19 + g.band * 23;
}

static void filt_reset(void)
{
    int i;
    float sps = design_sps();
    float nh = notch_hz_eff();
    for (i = 0; i < NP_NCHAN; i++) {
        np_hp_init(&g.hp[i], (float)g.hp_hz, sps);
        /* AUTO uses the cal tone as a cheap IIR — not a per-frame LS fit. */
        np_notch_init(&g.notch[i], nh > 1.f ? nh : 0.f, sps, 30.f);
        np_lp_init(&g.lp[i], (float)g.lp_hz, sps);
        np_env_init(&g.env[i], 0.15f, sps);
    }
    live_seen = 0;
    live_wr = 0;
    live_sig = -1;
    clean_t = 0;
    clean_seen = 0;
}

static char g_files_dir[NP_MAX_PATH];

void np_set_files_dir(const char *p)
{
    if (p && p[0]) {
        snprintf(g_files_dir, sizeof(g_files_dir), "%s", p);
    }
}

static void np_mkdir_p(const char *path)
{
    char buf[NP_MAX_PATH];
    char *s;
    if (!path || !path[0]) {
        return;
    }
    snprintf(buf, sizeof(buf), "%s", path);
    for (s = buf + 1; *s; s++) {
        if (*s == '/') {
            *s = 0;
            mkdir(buf, 0755);
            *s = '/';
        }
    }
    mkdir(buf, 0755);
}

static void np_cfg_root(char *out, size_t n)
{
    if (g_files_dir[0]) {
        snprintf(out, n, "%s", g_files_dir);
        return;
    }
#if defined(__ANDROID__) && !defined(NP_ANDROID_UI)
    {
        const char *p = SDL_AndroidGetInternalStoragePath();
        if (p && p[0]) {
            snprintf(out, n, "%s", p);
            return;
        }
    }
#endif
#ifdef __ANDROID__
    {
        const char *h = getenv("HOME");
        if (h && h[0]) {
            snprintf(out, n, "%s", h);
            return;
        }
    }
#else
    {
        const char *h = getenv("HOME");
        if (h && h[0]) {
            snprintf(out, n, "%s/.config", h);
            return;
        }
    }
#endif
    snprintf(out, n, ".");
}

static void cfg_path(char *out, size_t n)
{
    char root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    snprintf(out, n, "%s/exg-c.ini", root);
}

static void learn_path(char *out, size_t n)
{
    char root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    mkdir(root, 0755);
    snprintf(out, n, "%s/exg-c.learn", root);
}

static void learn_persist(void)
{
    char path[NP_MAX_PATH];
    learn_path(path, sizeof(path));
    npl_save(&g.learn, path);
}

static void atom_sanitize(char *dst, int n, const char *src)
{
    int i = 0, o = 0;
    if (!dst || n < 2) {
        return;
    }
    if (!src) {
        dst[0] = 0;
        return;
    }
    for (i = 0; src[i] && o < n - 1; i++) {
        char c = src[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_') {
            dst[o++] = c;
        }
    }
    dst[o] = 0;
}

static void atom_dir(char *out, int n)
{
    char root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    mkdir(root, 0755);
    snprintf(out, (size_t)n, "%s/exg-c/atoms", root);
    np_mkdir_p(out);
}

static int atom_path(char *out, int n, const char *name)
{
    char dir[NP_MAX_PATH], safe[NP_ATOM_NAME];
    atom_sanitize(safe, (int)sizeof(safe), name);
    if (!safe[0]) {
        return -1;
    }
    atom_dir(dir, (int)sizeof(dir));
    snprintf(out, (size_t)n, "%s/%s.npat", dir, safe);
    return 0;
}

static void atom_flatten(uint64_t *dst, int *n)
{
    int i, start;
    *n = g.atom_n;
    if (*n < 1) {
        return;
    }
    start = g.atom_n < NP_ATOM_RING ? 0 : g.atom_wr;
    for (i = 0; i < *n; i++) {
        dst[i] = g.atom_live[(start + i) % NP_ATOM_RING];
    }
}

static void atom_score(void)
{
    uint64_t live[NP_ATOM_RING];
    int n = 0;
    if (g.atom_ref_n < 1 || g.atom_n < 1) {
        g.atom_unity = 0.f;
        return;
    }
    atom_flatten(live, &n);
    g.atom_unity = np_atom_ring_unity(live, n, g.atom_ref, g.atom_ref_n);
}

static void atom_tick(void)
{
    static uint64_t last;
    struct timespec ts;
    uint64_t now;
    float planar[NP_NCHAN * NP_ATOM_WIN];
    float scale;
    int c, got = 0;
    uint32_t want;

    if (!g.atom_on) {
        last = 0;
        return;
    }
    if (!g.connected || (g.sps > 0.f && g.sps < 80.f)) {
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000ull);
    if (last && now - last < 1000ull) {
        return;
    }
    last = now;
    want = (uint32_t)NP_ATOM_WIN;
    memset(planar, 0, sizeof(planar));
    for (c = 0; c < NP_NCHAN; c++) {
        float buf[NP_ATOM_WIN];
        uint32_t n;
        if (!g.active[c]) {
            continue;
        }
        n = np_ring_copy(&g.ring, c, buf, want);
        if (n < 32) {
            continue;
        }
        np_detrend(buf, (int)n);
        memcpy(planar + c * NP_ATOM_WIN, buf, (size_t)n * sizeof(float));
        got++;
    }
    if (got < 1) {
        return;
    }
    /* CubalC 50 µV. CALM plate here is millivolts — that scale makes every
     * window look flat. Envelope (EMG band) would kill zc/rise too. */
    scale = NP_ATOM_SCALE;
    {
        uint64_t bits = np_atom_pack(planar, NP_NCHAN, NP_ATOM_WIN, NP_ATOM_WIN, scale);
        g.atom_live[g.atom_wr] = bits;
        g.atom_wr = (g.atom_wr + 1) % NP_ATOM_RING;
        if (g.atom_n < NP_ATOM_RING) {
            g.atom_n++;
        }
        g.atom_seq++;
    }
    atom_score();
}

static uint8_t rec_smx[NPL_SMX_SEC];
static int rec_smx_n;

static uint8_t learn_fold_byte(uint8_t bits[NP_NCHAN])
{
    int c;
    uint8_t fold = 0;
    uint32_t want = (uint32_t)(2.f * design_sps());
    memset(bits, 0, NP_NCHAN);
    if (want < 32) {
        want = 32;
    }
    if (want > NP_RING) {
        want = NP_RING;
    }
    for (c = 0; c < NP_NCHAN; c++) {
        float buf[NP_RING], dc = 0, rms = 0, pk = 0, raw = 0, rr = 0;
        uint32_t n;
        int det;
        if (!g.active[c]) {
            continue;
        }
        n = np_ring_copy(&g.ring, c, buf, want);
        if (n < 16) {
            continue;
        }
        ch_stats(buf, n, &dc, &rms, &pk);
        raw = rms;
        apply_filt(c, buf, n);
        ch_stats(buf, n, &dc, &rms, &pk);
        det = np_detect(raw > 1.f ? raw : rms, rms, g.cal.have ? g.cal.rms[c] : 0.f,
                        g.calm.have ? g.calm.rms[c] : 0.f, &rr);
        bits[c] = (uint8_t)np_algo_bit(g.algo, buf, (int)n, det == NP_DET_SIGNAL);
        if (bits[c]) {
            fold |= (uint8_t)(1u << c);
        }
    }
    return fold;
}

static void learn_write_cube(const uint8_t bits[NP_NCHAN], uint8_t cube[64])
{
    int c, ix, iy, iz;
    np_cube_clear_kind(&g.smx, NP_CELL_EEG);
    for (c = 0; c < NP_NCHAN; c++) {
        if (g.elec[c].site >= 0) {
            np_1010_ijk(g.elec[c].site, &ix, &iy, &iz);
            np_cube_set(&g.smx, ix, iy, iz, bits[c] ? 1 : 0, NP_CELL_EEG);
        }
    }
    np_cube_pack_bin(&g.smx, cube);
}

static int site_is_fp(int ch)
{
    const char *n;
    if (ch < 0 || ch >= NP_NCHAN) {
        return 0;
    }
    if (g.elec[ch].site < 0) {
        return ch == 0 || ch == 1;
    }
    n = np_1010_name(g.elec[ch].site);
    return n && n[0] == 'F' && n[1] == 'p';
}

/* Last ~0.5 s vs worn CALM. This is the identifier — not the 64-sample template. */
static int stream_id(float *ratio)
{
    float rms[NP_NCHAN], calm[NP_NCHAN];
    float buf[NP_NCHAN][NP_RING];
    uint32_t nn[NP_NCHAN];
    int fp[NP_NCHAN];
    uint8_t mask = 0;
    int c, nclip = 0;
    uint32_t want = (uint32_t)(0.50f * design_sps());

    if (ratio) {
        *ratio = 0.f;
    }
    if (g.connected && g.sps > 0.f && g.sps < 80.f) {
        return NP_ID_NONE;
    }
    if (want < 32) {
        want = 32;
    }
    memset(rms, 0, sizeof(rms));
    memset(calm, 0, sizeof(calm));
    memset(fp, 0, sizeof(fp));
    memset(nn, 0, sizeof(nn));
    for (c = 0; c < NP_NCHAN; c++) {
        if (!g.active[c]) {
            continue;
        }
        nn[c] = np_ring_copy(&g.ring, c, buf[c], want);
        if (nn[c] >= 16 && np_window_clip(buf[c], (int)nn[c])) {
            nclip++;
        }
    }
    if (nclip >= 1) {
        if (ratio) {
            *ratio = (float)nclip;
        }
        return NP_ID_CLIP;
    }
    cook_all(buf, nn, want);
    for (c = 0; c < NP_NCHAN; c++) {
        float dc = 0, pk = 0;
        if (!g.active[c] || nn[c] < 16) {
            continue;
        }
        ch_stats(buf[c], nn[c], &dc, &rms[c], &pk);
        calm[c] = g.calm.have ? g.calm.rms[c] : 0.f;
        fp[c] = site_is_fp(c);
        mask |= (uint8_t)(1u << c);
    }
    return np_id_event(rms, calm, fp, mask, g.calm.have, ratio);
}

static void id_label(char *out, int n)
{
    float r = 0.f;
    int id = stream_id(&r);
    if (g.connected && g.sps > 0.f && g.sps < 80.f) {
        snprintf(out, (size_t)n, "ID warming %.0f sps", (double)g.sps);
        return;
    }
    if (id == NP_ID_NEED) {
        snprintf(out, (size_t)n, "ID need CALM");
    } else if (id == NP_ID_RAIL) {
        snprintf(out, (size_t)n, "ID rail");
    } else if (id == NP_ID_CLIP) {
        snprintf(out, (size_t)n, "ID CLIP");
    } else if (id == NP_ID_NONE) {
        snprintf(out, (size_t)n, "ID —");
    } else {
        snprintf(out, (size_t)n, "ID %s %.1fx", np_id_name(id), (double)r);
    }
}

/* One second around the gesture — not the plot window. Packing 8 s of
 * mixed still+noise is why MATCH could not tell a blink from leftover. */
static int learn_capture(float wave[NPL_NCHAN][NPL_LEN], float rms[NPL_NCHAN], uint8_t *mask)
{
    int c, have = 0, clip = 0;
    float sps = g.sps > 1.f ? g.sps : (float)NP_DEFAULT_SPS;
    uint32_t want = (uint32_t)(LEARN_S * sps);
    float notch = notch_hz_eff();
    float buf[NP_NCHAN][NP_RING];
    uint32_t nn[NP_NCHAN];
    *mask = 0;
    memset(wave, 0, (size_t)NPL_NCHAN * NPL_LEN * sizeof(float));
    memset(rms, 0, NPL_NCHAN * sizeof(float));
    if (want < 32) {
        want = 32;
    }
    if (want > NP_RING) {
        want = NP_RING;
    }
    for (c = 0; c < NPL_NCHAN; c++) {
        nn[c] = 0;
        if (!g.active[c]) {
            continue;
        }
        have = 1;
        nn[c] = np_ring_copy(&g.ring, c, buf[c], want);
        if (nn[c] >= 16 && np_window_clip(buf[c], (int)nn[c])) {
            clip = 1;
        }
    }
    if (clip) {
        return -3;
    }
    cook_all(buf, nn, want);
    for (c = 0; c < NPL_NCHAN; c++) {
        if (nn[c] < 16) {
            continue;
        }
        if (npl_prep(wave[c], &rms[c], buf[c], (int)nn[c], sps, notch) == 0) {
            *mask |= (uint8_t)(1u << c);
        }
    }
    if (*mask) {
        return 0;
    }
    return have ? -2 : -1;
}

static void learn_hold_tick(void);

static void learn_tick(void)
{
    static uint32_t last;
    float wave[NPL_NCHAN][NPL_LEN], rms[NPL_NCHAN];
    uint8_t mask;
    uint32_t now = SDL_GetTicks();
    learn_hold_tick();
    if (g.connected && g.sps > 0.f && g.sps < 80.f) {
        g.learn.best = -1;
        return;
    }
    if (!g.learn.match || g.learn.n <= 0) {
        g.learn.best = -1;
        return;
    }
    if (last && now - last < 100) {
        return;
    }
    last = now;
    if (learn_capture(wave, rms, &mask) != 0) {
        g.learn.best = -1;
        return;
    }
    npl_score(&g.learn, wave, rms, mask);
    {
        uint8_t cube[64], bits[NP_NCHAN], rows[NPL_SMX_SEC];
        int ids[NP_NCHAN], nid, t, ns;
        (void)learn_fold_byte(bits);
        learn_write_cube(bits, cube);
        npl_score_cube(&g.learn, cube);
        nid = np_smx_ch_ids(&g.smx, ids);
        ns = (int)g.smx.have;
        if (ns > NPL_SMX_SEC) {
            ns = NPL_SMX_SEC;
        }
        for (t = 0; t < ns; t++) {
            int row = (int)((g.smx.wr - (uint32_t)ns + (uint32_t)t) % NP_SMX_SEC);
            uint8_t f = 0;
            int k;
            for (k = 0; k < nid; k++) {
                if (g.smx.bit[row][k]) {
                    f |= (uint8_t)(1u << (ids[k] - 1));
                }
            }
            rows[t] = f;
        }
        if (ns > 0) {
            npl_score_smx(&g.learn, rows, ns);
        }
    }
}

static void learn_save_named(void)
{
    float wave[NPL_NCHAN][NPL_LEN], rms[NPL_NCHAN];
    uint8_t mask;
    int r, err;
    if (!g.namebuf[0]) {
        set_status(0, "type a name, then Save");
        return;
    }
    err = learn_capture(wave, rms, &mask);
    if (err == -1) {
        set_status(0, "no samples yet - wait for the stream");
        return;
    }
    if (err == -3) {
        set_status(0, "CLIP — sat. Don't record a rail.");
        return;
    }
    if (err != 0) {
        set_status(0, "turn a channel ON and wait one window");
        return;
    }
    r = npl_add(&g.learn, g.namebuf, wave, rms, mask);
    if (r >= 0) {
        uint8_t cube[64], bits[NP_NCHAN], fold;
        fold = learn_fold_byte(bits);
        learn_write_cube(bits, cube);
        npl_set_cube(&g.learn, r, cube);
        if (rec_smx_n < 1) {
            rec_smx[0] = fold;
            rec_smx_n = 1;
        }
        npl_set_smx(&g.learn, r, rec_smx, rec_smx_n, fold);
        rec_smx_n = 0;
    }
    if (r == -2) {
        set_status(0, "learn full (%d)", NPL_MAX);
        return;
    }
    if (r < 0) {
        set_status(0, "learn add failed");
        return;
    }
    learn_persist();
    g.learn.match = 1;
    {
        int nc = 0, b;
        for (b = 0; b < NPL_NCHAN; b++) {
            if (mask & (uint8_t)(1u << b)) {
                nc++;
            }
        }
        g.saved_t0 = SDL_GetTicks();
        g.rec_t0 = 0;
        {
            int rail = 0, c;
            for (c = 0; c < NPL_NCHAN; c++) {
                if ((mask & (uint8_t)(1u << c)) && rms[c] > 250000.f) {
                    rail = 1;
                }
            }
            if (rail) {
                set_status(1, "saved '%s'  %d ch  (open/rail)", g.namebuf, nc);
            } else {
                char id[40];
                id_label(id, sizeof(id));
                set_status(1, "saved '%s'  %d ch  1s snap  %s", g.namebuf, nc, id);
            }
        }
    }
}

static void learn_start_hold(void)
{
    if (!g.namebuf[0]) {
        typing_set(1);
        set_status(0, "step 1: type a name, then Record");
        return;
    }
    if (!g.connected) {
        set_status(0, "connect the board first");
        return;
    }
    rec_smx_n = 0;
    g.rec_t0 = SDL_GetTicks();
    if (!g.rec_t0) {
        g.rec_t0 = 1;
    }
    {
        uint8_t bits[NP_NCHAN], fold;
        fold = learn_fold_byte(bits);
        rec_smx[rec_smx_n++] = fold;
    }
    set_status(1, "do a blink or jaw clench now  ('%s')", g.namebuf);
}

static void learn_hold_tick(void)
{
    uint32_t now;
    int dt, id;
    float ratio = 0.f;
    if (!g.rec_t0) {
        return;
    }
    now = SDL_GetTicks();
    dt = (int)(now - g.rec_t0);
    id = stream_id(&ratio);
    if (g.connected && g.sps > 0.f && g.sps < 80.f) {
        if (dt >= (int)REC_MS) {
            g.rec_t0 = 0;
            set_status(0, "still enabling — wait for 125 sps, then Record");
        }
        return;
    }
    if (id == NP_ID_CLIP) {
        if (dt >= (int)REC_MS) {
            g.rec_t0 = 0;
            set_status(0, "CLIP — sat. Don't record that.");
        }
        return;
    }
    if (dt >= 280 &&
        (id == NP_ID_BLINK || id == NP_ID_CLENCH || id == NP_ID_BURST)) {
        g.rec_t0 = 0;
        learn_save_named();
        return;
    }
    if (dt >= (int)REC_MS) {
        g.rec_t0 = 0;
        learn_save_named();
        if (id == NP_ID_STILL || id == NP_ID_NEED) {
            set_status(0, "saved leftover — no burst. Blink hard or clench.");
        }
    }
}

static void typing_set(int on)
{
    g.typing = on;
    if (!on) {
        g.typing_prof = 0;
    }
    if (on) {
        SDL_StartTextInput();
    } else {
        SDL_StopTextInput();
    }
}

static void cfg_save(void);
static int cfg_write(const char *path);
static int cfg_read(const char *path);
static void prof_scan(void);
static void prof_apply(void);

static void prof_dir(char *out, size_t n)
{
    char root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    snprintf(out, n, "%s/exg-c/profiles", root);
}

static int prof_ok_name(const char *s)
{
    int n = 0;
    if (!s || !s[0]) {
        return 0;
    }
    for (; *s; s++, n++) {
        unsigned char c = (unsigned char)*s;
        if (!(isalnum(c) || c == '-' || c == '_')) {
            return 0;
        }
        if (n >= NP_PROF_NAME - 1) {
            return 0;
        }
    }
    return 1;
}

static void prof_file(const char *name, char *out, size_t n)
{
    char dir[192];
    prof_dir(dir, sizeof(dir));
    snprintf(out, n, "%s/%.23s.ini", dir, name ? name : "default");
}

static int cfg_write(const char *path)
{
    FILE *f;
    int i;
    if (!path || !path[0]) {
        return -1;
    }
    f = fopen(path, "w");
    if (!f) {
        return -1;
    }
    fprintf(f, "[ui]\n");
    fprintf(f, "scale=%d\n", g.ui_scale);
    fprintf(f, "width=%d\n", g.pref_w);
    fprintf(f, "height=%d\n", g.pref_h);
    if (g.prof[0]) {
        fprintf(f, "profile=%s\n", g.prof);
    }
    fprintf(f, "\n[view]\n");
    fprintf(f, "window_s=%d\n", g.window_s);
    fprintf(f, "autoscale=%d\n", g.autoscale);
    fprintf(f, "og=%d\n", g.og);
    fprintf(f, "scale_uv=%d\n", g.scale_uv);
    fprintf(f, "notch_hz=%d\n", g.notch_hz);
    fprintf(f, "hp_hz=%d\n", g.hp_hz);
    fprintf(f, "lp_hz=%d\n", g.lp_hz);
    fprintf(f, "car=%d\n", g.car ? 1 : 0);
    fprintf(f, "envelope=%d\n", g.envelope ? 1 : 0);
    fprintf(f, "band=%d\n", g.band);
    fprintf(f, "grid=%d\n", g.grid);
    fprintf(f, "show_uv=%d\n", g.show_uv);
    fprintf(f, "detrend=%d\n", g.detrend);
    fprintf(f, "cal_cut=%d\n", g.cal_cut);
    fprintf(f, "board=%d\n", (int)g.board);
    fprintf(f, "algo=%d\n", g.algo);
    fprintf(f, "\n[cube]\n");
    fprintf(f, "yaw=%.4f\n", (double)g.cube_yaw);
    fprintf(f, "pitch=%.4f\n", (double)g.cube_pitch);
    fprintf(f, "zoom=%.2f\n", (double)g.cube_zoom);
    fprintf(f, "view=%d\n", g.cube_view ? 1 : 0);
    for (i = 0; i < NP_NCHAN; i++) {
        if (g.elec[i].name[0]) {
            fprintf(f, "elec%d=%s\n", i + 1, g.elec[i].name);
        } else {
            fprintf(f, "elec%d=%.2f,%.2f\n", i + 1, (double)g.elec[i].az, (double)g.elec[i].el);
        }
    }
    fprintf(f, "\n[channels]\n");
    for (i = 0; i < NP_NCHAN; i++) {
        fprintf(f, "gain%d=%d\n", i + 1, g.gain[i]);
        fprintf(f, "color%d=%d,%d,%d\n", i + 1, g.chrgb[i][0], g.chrgb[i][1], g.chrgb[i][2]);
        fprintf(f, "active%d=%d\n", i + 1, g.active[i] ? 1 : 0);
        fprintf(f, "rld%d=%d\n", i + 1, g.rld[i] ? 1 : 0);
    }
    fclose(f);
    return 0;
}

static int cfg_read(const char *path)
{
    FILE *f;
    char line[96];
    if (!path || !path[0]) {
        return -1;
    }
    f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    while (fgets(line, sizeof(line), f)) {
        int v;
        float fa, fb;
        char ename[24];
        if (sscanf(line, "window_s=%d", &v) == 1) {
            g.window_s = v;
        } else if (sscanf(line, "autoscale=%d", &v) == 1) {
            g.autoscale = v;
        } else if (sscanf(line, "og=%d", &v) == 1) {
            g.og = v;
        } else if (sscanf(line, "scale_uv=%d", &v) == 1) {
            g.scale_uv = v;
        } else if (sscanf(line, "notch_hz=%d", &v) == 1) {
            g.notch_hz = v;
        } else if (sscanf(line, "hp_hz=%d", &v) == 1) {
            g.hp_hz = v;
        } else if (sscanf(line, "lp_hz=%d", &v) == 1) {
            g.lp_hz = v;
        } else if (sscanf(line, "car=%d", &v) == 1) {
            g.car = v ? 1 : 0;
        } else if (sscanf(line, "envelope=%d", &v) == 1) {
            g.envelope = v ? 1 : 0;
        } else if (sscanf(line, "band=%d", &v) == 1 && v >= 0 && v < NP_BAND_N) {
            g.band = v;
        } else if (sscanf(line, "grid=%d", &v) == 1) {
            g.grid = v;
        } else if (sscanf(line, "show_uv=%d", &v) == 1) {
            g.show_uv = v;
        } else if (sscanf(line, "detrend=%d", &v) == 1) {
            g.detrend = v;
        } else if (sscanf(line, "cal_cut=%d", &v) == 1) {
            g.cal_cut = v;
        } else if (sscanf(line, "board=%d", &v) == 1) {
            g.board = v ? NP_BOARD_KNIGHT_IMU : NP_BOARD_KNIGHT;
        } else if (sscanf(line, "algo=%d", &v) == 1 && v >= 0 && v < NP_ALGO_N) {
            g.algo = v;
        } else if (sscanf(line, "scale=%d", &v) == 1 && v >= 1) {
            if (v == 1) {
                g.ui_scale = 10;
            } else if (v == 2) {
                g.ui_scale = 15;
            } else if (v == 3) {
                g.ui_scale = 20;
            } else if (v == 10 || v == 15 || v == 20) {
                g.ui_scale = v;
            }
        } else if (sscanf(line, "width=%d", &v) == 1 && v >= 800) {
            g.pref_w = v;
        } else if (sscanf(line, "height=%d", &v) == 1 && v >= 560) {
            g.pref_h = v;
        } else if (sscanf(line, "profile=%23s", ename) == 1 && prof_ok_name(ename)) {
            snprintf(g.prof, sizeof(g.prof), "%s", ename);
        } else if (sscanf(line, "yaw=%f", &fa) == 1) {
            g.cube_yaw = fa;
        } else if (sscanf(line, "zoom=%f", &fa) == 1 && fa >= 0.7f && fa <= 2.8f) {
            g.cube_zoom = fa;
        } else if (sscanf(line, "view=%d", &v) == 1 && (v == 0 || v == 1)) {
            g.cube_view = v;
        } else if (sscanf(line, "pitch=%f", &fa) == 1) {
            g.cube_pitch = fa;
        } else if (sscanf(line, "elec%d=%f,%f", &v, &fa, &fb) == 3 && v >= 1 && v <= NP_NCHAN) {
            g.elec[v - 1].az = fa;
            g.elec[v - 1].el = fb;
            np_elec_set_site(&g.elec[v - 1], np_1010_nearest(fa, fb));
        } else if (sscanf(line, "elec%d=%7s", &v, ename) == 2 && v >= 1 && v <= NP_NCHAN) {
            int s = np_1010_find(ename);
            if (s >= 0) {
                np_elec_set_site(&g.elec[v - 1], s);
            }
        } else {
            int ch, gn, r, gc, b;
            if (sscanf(line, "gain%d=%d", &ch, &gn) == 2 && ch >= 1 && ch <= NP_NCHAN &&
                gn >= 1) {
                g.gain[ch - 1] = gn;
            } else if (sscanf(line, "color%d=%d,%d,%d", &ch, &r, &gc, &b) == 4 && ch >= 1 &&
                       ch <= NP_NCHAN) {
                g.chrgb[ch - 1][0] = r;
                g.chrgb[ch - 1][1] = gc;
                g.chrgb[ch - 1][2] = b;
            } else if (sscanf(line, "active%d=%d", &ch, &v) == 2 && ch >= 1 && ch <= NP_NCHAN) {
                g.active[ch - 1] = v ? 1 : 0;
            } else if (sscanf(line, "rld%d=%d", &ch, &v) == 2 && ch >= 1 && ch <= NP_NCHAN) {
                g.rld[ch - 1] = v ? 1 : 0;
            }
        }
    }
    fclose(f);
    if (g.window_s < 1) {
        g.window_s = 2;
    }
    if (g.scale_uv < 20) {
        g.scale_uv = 200;
    }
    if (g.ui_scale != 10 && g.ui_scale != 15 && g.ui_scale != 20) {
        g.ui_scale = 15;
    }
    if (g.pref_w < 800) {
        g.pref_w = WIN_W;
    }
    if (g.pref_h < 560) {
        g.pref_h = WIN_H;
    }
    return 0;
}

static void cfg_save(void)
{
    char path[NP_MAX_PATH], root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    mkdir(root, 0755);
    cfg_path(path, sizeof(path));
    cfg_write(path);
}

static void cfg_load(void)
{
    char path[NP_MAX_PATH];
    cfg_path(path, sizeof(path));
    if (cfg_read(path) == 0) {
        return;
    }
    {
        const char *h = getenv("HOME");
        if (h && h[0]) {
            snprintf(path, sizeof(path), "%s/.config/exg-c.conf", h);
            cfg_read(path);
        }
    }
}

static void prof_scan(void)
{
    char dir[NP_MAX_PATH];
    DIR *d;
    struct dirent *e;
    g.nprof = 0;
    prof_dir(dir, sizeof(dir));
    d = opendir(dir);
    if (!d) {
        return;
    }
    while ((e = readdir(d)) != NULL && g.nprof < NP_MAX_PROF) {
        size_t n = strlen(e->d_name);
        if (n < 5 || strcmp(e->d_name + n - 4, ".ini") != 0) {
            continue;
        }
        if (n - 4 >= NP_PROF_NAME) {
            continue;
        }
        memcpy(g.profiles[g.nprof], e->d_name, n - 4);
        g.profiles[g.nprof][n - 4] = 0;
        if (prof_ok_name(g.profiles[g.nprof])) {
            g.nprof++;
        }
    }
    closedir(d);
}

static void prof_apply(void)
{
    int c;
    filt_reset();
    if (Win) {
        SDL_SetWindowSize(Win, g.pref_w, g.pref_h);
    }
    pthread_mutex_lock(&g.parse_mu);
    np_parser_set_gains(&g.parser, g.gain);
    pthread_mutex_unlock(&g.parse_mu);
    if (!g.connected) {
        return;
    }
    for (c = 0; c < NP_NCHAN; c++) {
        if (g.active[c]) {
            cmd_push(CMD_CHON, c + 1, g.gain[c]);
        } else {
            cmd_push(CMD_CHOFF, c + 1, 0);
        }
        cmd_push(g.rld[c] ? CMD_RLDADD : CMD_RLDRM, c + 1, 0);
    }
}

static void prof_save(void)
{
    char dir[NP_MAX_PATH], path[NP_MAX_PATH], parent[NP_MAX_PATH];
    if (!prof_ok_name(g.prof)) {
        set_status(0, "type a profile name (letters, digits, - _)");
        return;
    }
    {
        char root[NP_MAX_PATH];
        np_cfg_root(root, sizeof(root));
        mkdir(root, 0755);
        snprintf(parent, sizeof(parent), "%s/exg-c", root);
        mkdir(parent, 0755);
    }
    prof_dir(dir, sizeof(dir));
    mkdir(dir, 0755);
    prof_file(g.prof, path, sizeof(path));
    if (cfg_write(path) != 0) {
        set_status(0, "cannot write profile %s", g.prof);
        return;
    }
    cfg_save();
    prof_scan();
    set_status(1, "saved profile '%s'", g.prof);
}

static void prof_load(void)
{
    char path[NP_MAX_PATH];
    if (!prof_ok_name(g.prof)) {
        prof_scan();
        if (g.nprof > 0) {
            snprintf(g.prof, sizeof(g.prof), "%s", g.profiles[0]);
        } else {
            set_status(0, "no profile name - type one or Save first");
            return;
        }
    }
    prof_file(g.prof, path, sizeof(path));
    if (cfg_read(path) != 0) {
        set_status(0, "no profile '%s'", g.prof);
        return;
    }
    prof_apply();
    cfg_save();
    set_status(1, "loaded profile '%s'", g.prof);
}

static void prof_cycle(void)
{
    int i, next = 0;
    prof_scan();
    if (g.nprof <= 0) {
        set_status(0, "no saved profiles yet");
        return;
    }
    for (i = 0; i < g.nprof; i++) {
        if (strcmp(g.profiles[i], g.prof) == 0) {
            next = (i + 1) % g.nprof;
            break;
        }
    }
    snprintf(g.prof, sizeof(g.prof), "%s", g.profiles[next]);
    set_status(1, "profile '%s'  (%d/%d)  click Load", g.prof, next + 1, g.nprof);
}

/* Snapshot path (learn / CAL). Own poles — must not smash the live IIR. */
static void apply_filt(int ch, float *buf, uint32_t n)
{
    uint32_t i;
    float sps = design_sps();
    float nh = 0.f;
    struct np_hp hp;
    struct np_notch nt;

    if (g.notch_hz != 0) {
        nh = notch_hz_eff();
    } else if (g.cal_cut && g.cal_hz > 1.f) {
        nh = g.cal_hz;
    }
    if (g.hp_hz <= 0 && nh <= 1.f && !(g.cal_cut && g.calm.have)) {
        return;
    }
    np_hp_init(&hp, (float)g.hp_hz, sps);
    np_notch_init(&nt, nh > 1.f ? nh : 0.f, sps, 30.f);
    for (i = 0; i < n; i++) {
        float v = buf[i];
        if (g.hp_hz > 0) {
            v = np_hp_step(&hp, v);
        }
        if (nh > 1.f) {
            v = np_notch_step(&nt, v);
        }
        buf[i] = v;
    }
    if (g.cal_cut && g.noise_psd_ok && n >= (uint32_t)NP_FFT_N) {
        if (n > 512) {
            np_plate_destroy(buf + (n - 512), 512, g.noise_psd);
        } else {
            np_plate_destroy(buf, (int)n, g.noise_psd);
        }
    }
    if (g.cal_cut && g.calm.have) {
        np_sub_dc(buf, (int)n, g.calm.dc[ch]);
    }
}

static void cook_all(float buf[NP_NCHAN][NP_RING], uint32_t nn[NP_NCHAN], uint32_t want)
{
    int c, t;
    uint32_t nmax = 0;

    (void)want;
    for (c = 0; c < NP_NCHAN; c++) {
        if (!g.active[c] || nn[c] < 16) {
            continue;
        }
        apply_filt(c, buf[c], nn[c]);
        if (nn[c] > nmax) {
            nmax = nn[c];
        }
    }
    if (g.car && nmax > 0) {
        for (t = 0; t < (int)nmax; t++) {
            float v[NP_NCHAN];
            int use[NP_NCHAN];
            for (c = 0; c < NP_NCHAN; c++) {
                use[c] = g.active[c] && nn[c] > (uint32_t)t;
                v[c] = use[c] ? buf[c][t] : 0.f;
            }
            np_car_sample(v, use);
            for (c = 0; c < NP_NCHAN; c++) {
                if (use[c]) {
                    buf[c][t] = v[c];
                }
            }
        }
    }
    for (c = 0; c < NP_NCHAN; c++) {
        uint32_t i;
        if (nn[c] < 16) {
            continue;
        }
        if (g.lp_hz > 0) {
            struct np_lp lp;
            np_lp_init(&lp, (float)g.lp_hz, design_sps());
            for (i = 0; i < nn[c]; i++) {
                buf[c][i] = np_lp_step(&lp, buf[c][i]);
            }
        }
        if (g.detrend) {
            np_detrend(buf[c], (int)nn[c]);
        }
        if (g.envelope) {
            struct np_lp ev;
            np_env_init(&ev, 0.15f, design_sps());
            for (i = 0; i < nn[c]; i++) {
                buf[c][i] = np_env_step(&ev, buf[c][i]);
            }
        }
    }
}

static void band_apply(int band)
{
    if (band < 0 || band >= NP_BAND_N) {
        band = 0;
    }
    g.band = band;
    if (band == NP_BAND_RAW) {
        g.notch_hz = 0;
        g.hp_hz = 0;
        g.lp_hz = 0;
        g.car = 0;
        g.envelope = 0;
        g.detrend = 0;
    } else if (band == NP_BAND_LINE) {
        g.notch_hz = g.cal_hz > 1.f ? -1 : 50;
        g.hp_hz = 1;
        g.lp_hz = 0;
        g.car = 1;
        g.envelope = 0;
        g.detrend = 1;
        if (g.cal.have) {
            g.cal_cut = 1;
        }
    } else if (band == NP_BAND_EEG) {
        g.notch_hz = g.cal_hz > 1.f ? -1 : 50;
        g.hp_hz = 1;
        g.lp_hz = 40;
        g.car = 1;
        g.envelope = 0;
        g.detrend = 1;
        if (g.cal.have) {
            g.cal_cut = 1;
        }
    } else {
        g.notch_hz = 50;
        g.hp_hz = 20;
        g.lp_hz = 0;
        g.car = 0;
        g.envelope = 1;
        g.detrend = 1;
    }
    filt_reset();
    cfg_save();
}

/* One IIR step per new sample. Display copies this. Never re-filter the window. */
static void live_sync(void)
{
    uint64_t tot = 0;
    uint32_t need, c, i;
    int sig = filt_sig();
    float nh = 0.f;
    float tmp[NP_NCHAN][NP_RING];
    uint32_t got[NP_NCHAN];

    np_ring_stats(&g.ring, &tot, NULL, NULL);
    if (sig != live_sig) {
        filt_reset();
        live_sig = filt_sig();
        live_seen = tot > 16 ? tot - 16 : 0;
    }
    if (tot < live_seen) {
        live_seen = 0;
        live_wr = 0;
    }
    need = (uint32_t)(tot - live_seen);
    if (need == 0) {
        return;
    }
    if (need > NP_RING) {
        need = NP_RING;
        live_seen = tot - need;
    }
    if (g.notch_hz != 0) {
        nh = notch_hz_eff();
    } else if (g.cal_cut && g.cal_hz > 1.f) {
        nh = g.cal_hz;
    }
    for (c = 0; c < NP_NCHAN; c++) {
        got[c] = np_ring_copy(&g.ring, c, tmp[c], need);
    }
    for (i = 0; i < need; i++) {
        float v[NP_NCHAN];
        int use[NP_NCHAN];
        for (c = 0; c < NP_NCHAN; c++) {
            float x = (i < got[c]) ? tmp[c][i] : 0.f;
            if (g.hp_hz > 0) {
                x = np_hp_step(&g.hp[c], x);
            }
            if (nh > 1.f) {
                x = np_notch_step(&g.notch[c], x);
            }
            v[c] = x;
            use[c] = g.active[c] && i < got[c];
        }
        if (g.car) {
            np_car_sample(v, use);
        }
        for (c = 0; c < NP_NCHAN; c++) {
            if (g.lp_hz > 0) {
                v[c] = np_lp_step(&g.lp[c], v[c]);
            }
            if (g.envelope) {
                v[c] = np_env_step(&g.env[c], v[c]);
            }
            live_ch[c][(live_wr + i) % NP_RING] = v[c];
        }
    }
    live_wr += need;
    live_seen = tot;
}

static uint32_t live_copy(int ch, float *dst, uint32_t n)
{
    uint32_t have, i, start;
    live_sync();
    have = live_seen < NP_RING ? (uint32_t)live_seen : NP_RING;
    if (n > have) {
        n = have;
    }
    start = (live_wr + NP_RING - n) % NP_RING;
    for (i = 0; i < n; i++) {
        dst[i] = live_ch[ch][(start + i) % NP_RING];
    }
    return n;
}

/* CLEAN STFT at ~12 Hz, not 60×8. Plot uses the last cooked window. */
static uint32_t view_copy(int ch, float *dst, uint32_t n)
{
    uint32_t got, c;
    uint64_t tot = 0;
    uint32_t now;

    got = live_copy(ch, dst, n);
    if (!(g.cal_cut && g.noise_psd_ok && got >= (uint32_t)NP_FFT_N)) {
        if (g.cal_cut && g.calm.have && got > 0) {
            np_sub_dc(dst, (int)got, g.calm.dc[ch]);
        }
        return got;
    }
    np_ring_stats(&g.ring, &tot, NULL, NULL);
    now = SDL_GetTicks();
    if (clean_t == 0 || now - clean_t >= 80 || tot != clean_seen) {
        for (c = 0; c < NP_NCHAN; c++) {
            uint32_t m = live_copy(c, clean_ch[c], n);
            if (m >= (uint32_t)NP_FFT_N) {
                np_plate_destroy(clean_ch[c], (int)m, g.noise_psd);
            }
            if (g.calm.have && m > 0) {
                np_sub_dc(clean_ch[c], (int)m, g.calm.dc[c]);
            }
            clean_n[c] = m;
        }
        clean_t = now;
        clean_seen = tot;
    }
    if (clean_n[ch] > 0) {
        if (got > clean_n[ch]) {
            got = clean_n[ch];
        }
        memcpy(dst, clean_ch[ch], got * sizeof(float));
    }
    return got;
}

static volatile int cube_busy;

static const char *cube_url(void)
{
    const char *u = getenv("NP_CUBE_URL");
    if (u && u[0]) {
        return u;
    }
    return "off";
}

/* Best-effort POST. Short timeouts. Bits only. */
static int cube_post_json(const char *base, const char *path, const char *json, char *resp,
                          int rcap)
{
    char host[128], req[1400], hdr[256];
    const char *p;
    int port = 80, fd = -1, n, blen, woff, got = 0;
    struct addrinfo hints, *ai = NULL;
    struct pollfd pfd;
    char portstr[12];

    if (resp && rcap > 0) {
        resp[0] = 0;
    }
    if (!base || !json || !path) {
        return 0;
    }
    if (!strncmp(base, "off", 3) || !strcmp(base, "0")) {
        return 0;
    }
    p = base;
    if (!strncmp(p, "http://", 7)) {
        p += 7;
    }
    {
        const char *col = strchr(p, ':');
        const char *sl = strchr(p, '/');
        size_t hl;
        if (col && (!sl || col < sl)) {
            hl = (size_t)(col - p);
            port = atoi(col + 1);
            if (port <= 0) {
                port = 17333;
            }
        } else {
            hl = sl ? (size_t)(sl - p) : strlen(p);
        }
        if (hl >= sizeof(host) || hl == 0) {
            return 0;
        }
        memcpy(host, p, hl);
        host[hl] = 0;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (getaddrinfo(host, portstr, &hints, &ai) != 0) {
        return 0;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        freeaddrinfo(ai);
        return 0;
    }
    {
        int fl = fcntl(fd, F_GETFL, 0);
        if (fl >= 0) {
            fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        }
    }
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) < 0 && errno != EINPROGRESS) {
        freeaddrinfo(ai);
        close(fd);
        return 0;
    }
    freeaddrinfo(ai);
    pfd.fd = fd;
    pfd.events = POLLOUT;
    if (poll(&pfd, 1, 200) <= 0) {
        close(fd);
        return 0;
    }
    {
        int err = 0;
        socklen_t el = sizeof(err);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) < 0 || err) {
            close(fd);
            return 0;
        }
    }
    blen = (int)strlen(json);
    n = snprintf(hdr, sizeof(hdr),
                 "POST %s HTTP/1.0\r\nHost: %s:%d\r\nContent-Type: application/json\r\n"
                 "Content-Length: %d\r\nConnection: close\r\n\r\n",
                 path, host, port, blen);
    if (n < 0 || n + blen >= (int)sizeof(req)) {
        close(fd);
        return 0;
    }
    memcpy(req, hdr, (size_t)n);
    memcpy(req + n, json, (size_t)blen);
    n += blen;
    woff = 0;
    while (woff < n) {
        int w;
        pfd.events = POLLOUT;
        if (poll(&pfd, 1, 200) <= 0) {
            close(fd);
            return 0;
        }
        w = (int)write(fd, req + woff, (size_t)(n - woff));
        if (w < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            close(fd);
            return 0;
        }
        woff += w;
    }
    if (resp && rcap > 1) {
        pfd.events = POLLIN;
        while (got < rcap - 1) {
            int r;
            if (poll(&pfd, 1, 200) <= 0) {
                break;
            }
            r = (int)read(fd, resp + got, (size_t)(rcap - 1 - got));
            if (r <= 0) {
                break;
            }
            got += r;
        }
        resp[got] = 0;
    }
    close(fd);
    return 1;
}

static void *cube_post_thr(void *arg)
{
    char *json = arg;
    char resp[320];
    const char *url = cube_url();
    int ok;

    memset(resp, 0, sizeof(resp));
    ok = cube_post_json(url, "/v1/coord", json, resp, sizeof(resp));
    pthread_mutex_lock(&g.mu);
    if (ok && strstr(resp, "\"stored\":true")) {
        snprintf(g.cube_ack, sizeof(g.cube_ack), "cube stored");
        g.cube_ok = 1;
    } else if (ok && strstr(resp, "\"ok\":true")) {
        snprintf(g.cube_ack, sizeof(g.cube_ack), "cube ack");
        g.cube_ok = 1;
    } else {
        snprintf(g.cube_ack, sizeof(g.cube_ack), "cube offline");
        g.cube_ok = 0;
    }
    pthread_mutex_unlock(&g.mu);
    cube_busy = 0;
    free(json);
    return NULL;
}

static void cube_offer(void)
{
    char bits[NP_CUBE3_N + 4];
    char *json;
    int n, cap;
    pthread_t th;
    pthread_attr_t at;
    const char *url = cube_url();

    if (!strncmp(url, "off", 3) || !strcmp(url, "0")) {
        snprintf(g.cube_ack, sizeof(g.cube_ack), "offer off");
        g.cube_ok = 0;
        return;
    }
    if (cube_busy) {
        return;
    }
    n = np_cube_pack(&g.smx, bits, sizeof(bits));
    cap = n + 280;
    json = malloc((size_t)cap);
    if (!json) {
        return;
    }
    snprintf(json, (size_t)cap,
             "{\"plate\":\"NEXUS_COORD v1 | from=exg-c | type=smx | topic=channel_stim | "
             "seq=%u | unity=1.0 | hold_flash=1 | share=state_matrix_only | pii=0 | "
             "n=8 | cells=512 | nch=%u | have=%u | sot_bits=%s |\"}",
             g.smx.seq, (unsigned)g.smx.nch, g.smx.have, bits);
    cube_busy = 1;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &at, cube_post_thr, json) != 0) {
        cube_busy = 0;
        free(json);
    }
    pthread_attr_destroy(&at);
}

static void smx_tick(void)
{
    static uint64_t last;
    struct timespec ts;
    uint64_t now;
    uint8_t bits[NP_NCHAN];
    uint8_t mask = 0;
    int nch = 0, c;
    uint32_t want;
    float buf[NP_RING];

    if (!g.connected) {
        last = 0;
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000ull);
    if (last && now - last < 1000ull) {
        return;
    }
    last = now;

    want = (uint32_t)design_sps();
    if (want < 32) {
        want = 32;
    }
    if (want > NP_RING) {
        want = NP_RING;
    }
    memset(bits, 0, sizeof(bits));
    for (c = 0; c < NP_NCHAN; c++) {
        uint32_t n;
        float dc = 0, rms = 0, pk = 0, raw_rms = 0, rr = 0;
        int det;
        if (!g.active[c]) {
            continue;
        }
        mask |= (uint8_t)(1u << c);
        n = np_ring_copy(&g.ring, c, buf, want);
        if (n >= 16) {
            ch_stats(buf, n, &dc, &rms, &pk);
            raw_rms = rms;
            n = view_copy(c, buf, n);
            ch_stats(buf, n, &dc, &rms, &pk);
            det = np_detect(raw_rms > 1.f ? raw_rms : rms, rms,
                            g.cal.have ? g.cal.rms[c] : 0.f,
                            g.calm.have ? g.calm.rms[c] : 0.f, &rr);
            bits[nch] = (uint8_t)np_algo_bit(g.algo, buf, (int)n, det == NP_DET_SIGNAL);
        }
        nch++;
    }
    if (nch > 0) {
        int ix, iy, iz;
        float acc[3], gyr[3], mag[3];
        int imu_ok = 0, used = 0;
        np_smx_push(&g.smx, bits, nch, mask);
        np_cube_clear_kind(&g.smx, NP_CELL_EEG);
        for (c = 0; c < NP_NCHAN; c++) {
            if (!g.active[c]) {
                continue;
            }
            if (g.elec[c].site >= 0) {
                np_1010_ijk(g.elec[c].site, &ix, &iy, &iz);
                np_cube_set(&g.smx, ix, iy, iz, bits[used] ? 1 : 0, NP_CELL_EEG);
            }
            used++;
        }
        np_ring_imu(&g.ring, acc, gyr, mag, &imu_ok);
        if (imu_ok) {
            np_cube_imu(&g.smx, acc, gyr, mag);
        }
        cube_offer();
        if (g.rec_t0 && rec_smx_n < NPL_SMX_SEC) {
            uint8_t fold = 0;
            int used = 0;
            for (c = 0; c < NP_NCHAN; c++) {
                if (!g.active[c]) {
                    continue;
                }
                if (bits[used]) {
                    fold |= (uint8_t)(1u << c);
                }
                used++;
            }
            rec_smx[rec_smx_n++] = fold;
        }
    }
}

static int in_group(gid_t gid)
{
    int n = getgroups(0, NULL);
    gid_t *list;
    int i, ok = 0;
    if (getgid() == gid || getegid() == gid) {
        return 1;
    }
    if (n <= 0) {
        return 0;
    }
    list = calloc((size_t)n, sizeof(*list));
    if (!list) {
        return 0;
    }
    n = getgroups(n, list);
    for (i = 0; i < n; i++) {
        if (list[i] == gid) {
            ok = 1;
        }
    }
    free(list);
    return ok;
}

static void ensure_dialout(int argc, char **argv)
{
#ifdef __ANDROID__
    (void)argc;
    (void)argv;
    return;
#else
    struct group *gr = getgrnam("dialout");
    char cmd[2048];
    int i, off = 0;
    if (!gr || in_group(gr->gr_gid) || getenv("NP_EXG_NOSG")) {
        return;
    }
    cmd[0] = 0;
    for (i = 0; i < argc && off < (int)sizeof(cmd) - 8; i++) {
        const char *p = argv[i] ? argv[i] : "";
        if (i) {
            cmd[off++] = ' ';
        }
        cmd[off++] = '\'';
        while (*p && off < (int)sizeof(cmd) - 6) {
            if (*p == '\'') {
                off += snprintf(cmd + off, sizeof(cmd) - (size_t)off, "'\\''");
            } else {
                cmd[off++] = *p;
            }
            p++;
        }
        cmd[off++] = '\'';
        cmd[off] = 0;
    }
    setenv("NP_EXG_NOSG", "1", 1);
    execlp("sg", "sg", "dialout", "-c", cmd, (char *)NULL);
#endif
}

static void set_status(int ok, const char *fmt, ...)
{
    va_list ap;
    pthread_mutex_lock(&g.mu);
    g.status_ok = ok;
    va_start(ap, fmt);
    vsnprintf(g.status, sizeof(g.status), fmt, ap);
    va_end(ap);
    pthread_mutex_unlock(&g.mu);
}

static void cmd_push(int op, int ch, int gain)
{
    int n;
    pthread_mutex_lock(&g.qmu);
    n = (g.qh + 1) % QMAX;
    if (n != g.qt) {
        g.q[g.qh].op = op;
        g.q[g.qh].ch = ch;
        g.q[g.qh].gain = gain;
        g.qh = n;
        pthread_cond_signal(&g.qcv);
    }
    pthread_mutex_unlock(&g.qmu);
}

static int cmd_pending(void)
{
    int n;
    pthread_mutex_lock(&g.qmu);
    n = (g.qh - g.qt + QMAX) % QMAX;
    pthread_mutex_unlock(&g.qmu);
    return n;
}

static void cmd_drain(int timeout_ms)
{
    int waits = timeout_ms / 50;
    while (waits-- > 0 && g.connected && cmd_pending() > 0) {
        usleep(50000);
    }
}

static void *cmd_thread(void *arg)
{
    (void)arg;
    while (g.running) {
        int op = 0, ch = 0, gain = 12;
        pthread_mutex_lock(&g.qmu);
        while (g.running && g.qh == g.qt) {
            pthread_cond_wait(&g.qcv, &g.qmu);
        }
        if (!g.running) {
            pthread_mutex_unlock(&g.qmu);
            break;
        }
        op = g.q[g.qt].op;
        ch = g.q[g.qt].ch;
        gain = g.q[g.qt].gain;
        g.qt = (g.qt + 1) % QMAX;
        pthread_mutex_unlock(&g.qmu);
        if (g.fd < 0 || !g.connected) {
            continue;
        }
        if (op == CMD_CHON) {
            set_status(1, "enable ch%d", ch);
            pthread_mutex_lock(&g.parse_mu);
            np_parser_set_gain(&g.parser, ch, gain);
            pthread_mutex_unlock(&g.parse_mu);
            np_cmd_chon(g.fd, ch, gain);
        } else if (op == CMD_CHOFF) {
            np_cmd_choff(g.fd, ch);
        } else if (op == CMD_RLDADD) {
            np_cmd_rldadd(g.fd, ch);
        } else if (op == CMD_RLDRM) {
            np_cmd_rldremove(g.fd, ch);
        }
    }
    return NULL;
}

static void *reader_thread(void *arg)
{
    unsigned char buf[256];
    (void)arg;
    while (g.running && g.connected && g.fd >= 0) {
        int n, i;
#ifdef __ANDROID__
        /* USB is a JNI bulk transfer, not a real fd. poll() on the dummy
         * handle never sees Knight bytes, so the board looked dead. */
        n = np_serial_read(g.fd, buf, (int)sizeof(buf));
        if (n == 0) {
            usleep(2000);
            continue;
        }
#else
        {
            struct pollfd pfd = {g.fd, POLLIN, 0};
            if (poll(&pfd, 1, 8) <= 0) {
                continue;
            }
        }
        n = np_serial_read(g.fd, buf, (int)sizeof(buf));
#endif
        if (n < 0) {
            set_status(0, "serial read failed");
            break;
        }
        if (n == 0) {
            continue;
        }
        for (i = 0; i < n; i++) {
            struct np_sample s;
            int r, locked;
            pthread_mutex_lock(&g.parse_mu);
            r = np_parser_feed(&g.parser, buf[i], &s);
            locked = g.parser.locked;
            pthread_mutex_unlock(&g.parse_mu);
            if (r < 0) {
                if (locked) {
                    pthread_mutex_lock(&g.ring.mu);
                    g.ring.bad++;
                    pthread_mutex_unlock(&g.ring.mu);
                }
            } else if (r > 0) {
                struct timespec now;
                np_ring_push(&g.ring, &s);
                g.sps_n++;
                clock_gettime(CLOCK_MONOTONIC, &now);
                if (g.sps_t.tv_sec == 0) {
                    g.sps_t = now;
                } else {
                    double dt = (double)(now.tv_sec - g.sps_t.tv_sec) +
                        (now.tv_nsec - g.sps_t.tv_nsec) / 1e9;
                    if (dt >= 1.0) {
                        g.sps = (float)g.sps_n / (float)dt;
                        g.sps_n = 0;
                        g.sps_t = now;
                    }
                }
                if (g.recording) {
                    pthread_mutex_lock(&g.csv_mu);
                    if (g.csv) {
                        int c;
                        struct timespec ts;
                        clock_gettime(CLOCK_REALTIME, &ts);
                        fprintf(g.csv, "%ld.%09ld,%u", (long)ts.tv_sec, ts.tv_nsec, s.seq);
                        for (c = 0; c < NP_NCHAN; c++) {
                            fprintf(g.csv, ",%.3f", s.uv[c]);
                        }
                        fprintf(g.csv, ",%u,%u\n", s.loff_p, s.loff_n);
                    }
                    pthread_mutex_unlock(&g.csv_mu);
                }
            }
        }
    }
    return NULL;
}

/* Wait until tot grows. Skip DTR only if the acquire atom is already
 * locked and still producing frames. Never init the parser unlocked. */
static int wait_live(int frames, int tries, int gap_us)
{
    int w;
    uint64_t tot = 0, last = 0;
    int grew = 0, locked = 0;
    for (w = 0; w < tries && g.connected; w++) {
        np_ring_stats(&g.ring, &tot, NULL, NULL);
        pthread_mutex_lock(&g.parse_mu);
        locked = g.parser.locked;
        pthread_mutex_unlock(&g.parse_mu);
        if (tot > last) {
            grew++;
            last = tot;
        }
        if (locked && (int)tot >= frames && grew >= 1) {
            return 1;
        }
        usleep(gap_us);
    }
    return 0;
}

static void parser_rearm(void)
{
    pthread_mutex_lock(&g.parse_mu);
    np_parser_init(&g.parser, g.board);
    np_parser_set_gains(&g.parser, g.gain);
    pthread_mutex_unlock(&g.parse_mu);
}

static void *enable_thread(void *arg)
{
    int c;
    (void)arg;
    set_status(1, "waiting for stream...");
#ifdef __ANDROID__
    if (!wait_live(20, 40, 100000)) {
        set_status(1, "uart idle - one board kick");
        np_serial_pulse_dtr(g.fd);
        np_serial_flush(g.fd);
    }
#endif
    if (!wait_live(50, 120, 100000)) {
        set_status(0, "no live stream - tap Connect again");
        g.en_running = 0;
        return NULL;
    }
    set_status(1, "enabling channels...");
    for (c = 0; c < NP_NCHAN && g.connected; c++) {
        if (g.active[c]) {
            cmd_push(CMD_CHON, c + 1, g.gain[c]);
        }
    }
    cmd_drain(25000);
    for (c = 0; c < NP_NCHAN && g.connected; c++) {
        if (g.active[c] && g.rld[c]) {
            cmd_push(CMD_RLDADD, c + 1, 0);
        }
    }
    cmd_drain(25000);
    if (g.connected) {
        set_status(1, "connected %s", g.nports ? g.ports[g.port_i] : "");
    }
    g.en_running = 0;
    return NULL;
}

static void stream_recover(void)
{
    if (!g.connected || g.fd < 0 || g.en_running) {
        return;
    }
#ifdef __ANDROID__
    /* DTR reset loops keep the Nano in the bootloader. Never pulse
     * unless we already had frames (a real stall, not "never started"). */
    if (g.stall_tot < 10) {
        set_status(0, "usb open, waiting for Knight frames...");
        return;
    }
#endif
    if (g.recover_n >= 3) {
        set_status(0, "stream dead (3 resets) - click Disconnect/Connect");
        return;
    }
    g.recover_n++;
    set_status(0, "stream stalled - board reset %d/3", g.recover_n);
    parser_rearm();
#ifndef __ANDROID__
    np_serial_pulse_dtr(g.fd);
    np_serial_flush(g.fd);
#endif
    g.en_running = 1;
    if (pthread_create(&g.en_thr, NULL, enable_thread, NULL) == 0) {
        pthread_detach(g.en_thr);
    } else {
        g.en_running = 0;
    }
}

static void do_connect(void)
{
    const char *path;
    if (g.connected) {
        return;
    }
    g.nports = np_list_ports(g.ports, NP_MAX_PORTS);
    if (g.nports <= 0) {
        set_status(0, NP_TOUCH ? "no USB serial (plug Knight / grant USB)"
                               : "no /dev/ttyUSB* or /dev/ttyACM*");
        return;
    }
    if (g.port_i >= g.nports) {
        g.port_i = 0;
    }
    path = g.ports[g.port_i];
    g.fd = np_serial_open(path);
    if (g.fd < 0) {
        set_status(0, "open %s: %s", path, strerror(errno));
        return;
    }
    np_parser_init(&g.parser, g.board);
    np_parser_set_gains(&g.parser, g.gain);
    filt_reset();
#ifndef __ANDROID__
    /* Android: do not DTR-reset on the UI thread. The Knight is already
     * running; a pulse blacks the GL surface and reboots the Nano. */
    np_serial_pulse_dtr(g.fd);
    np_serial_flush(g.fd);
#endif
    g.connected = 1;
    g.stall_t = SDL_GetTicks();
    g.stall_n = 0;
    if (pthread_create(&g.thr, NULL, reader_thread, NULL) != 0) {
        np_serial_close(g.fd);
        g.fd = -1;
        g.connected = 0;
        set_status(0, "thread create failed");
        return;
    }
    g.en_running = 1;
    g.recover_n = 0;
    g.stall_n = 0;
    g.stall_tot = 0;
    if (pthread_create(&g.en_thr, NULL, enable_thread, NULL) != 0) {
        g.en_running = 0;
        set_status(0, "connected, but enable thread failed");
        return;
    }
    pthread_detach(g.en_thr);
    set_status(1, "connected %s - enabling channels", path);
}

static void do_disconnect(void)
{
    if (!g.connected) {
        return;
    }
    g.connected = 0;
    /* enable_thread checks g.connected and exits; reader joins */
    pthread_join(g.thr, NULL);
    np_serial_close(g.fd);
    g.fd = -1;
    g.recording = 0;
    pthread_mutex_lock(&g.csv_mu);
    if (g.csv) {
        fclose(g.csv);
        g.csv = NULL;
    }
    pthread_mutex_unlock(&g.csv_mu);
    set_status(1, "disconnected");
}

static void toggle_record(void)
{
    if (!g.connected) {
        set_status(0, "connect before record");
        return;
    }
    if (g.recording) {
        g.recording = 0;
        pthread_mutex_lock(&g.csv_mu);
        if (g.csv) {
            fclose(g.csv);
            g.csv = NULL;
        }
        pthread_mutex_unlock(&g.csv_mu);
        set_status(1, "stopped %s", g.csv_path);
        return;
    }
    {
        time_t t = time(NULL);
        struct tm tm;
        FILE *f;
        localtime_r(&t, &tm);
        {
            char stamp[40], root[NP_MAX_PATH];
            strftime(stamp, sizeof(stamp), "knight-%Y%m%d-%H%M%S.csv", &tm);
            np_cfg_root(root, sizeof(root));
            mkdir(root, 0755);
            snprintf(g.csv_path, sizeof(g.csv_path), "%s/%s", root, stamp);
        }
        f = fopen(g.csv_path, "w");
        if (!f) {
            set_status(0, "cannot write %s", g.csv_path);
            return;
        }
        setvbuf(f, NULL, _IOFBF, 8192);
        fprintf(f, "time,seq,ch1,ch2,ch3,ch4,ch5,ch6,ch7,ch8,loff_p,loff_n\n");
        pthread_mutex_lock(&g.csv_mu);
        g.csv = f;
        pthread_mutex_unlock(&g.csv_mu);
        g.recording = 1;
        set_status(1, "recording %s", g.csv_path);
    }
}

/* ---------- drawing ---------- */

static void fill(int x, int y, int w, int h, int r, int gcol, int b)
{
    SDL_Rect rc = {x, y, w, h};
    SDL_SetRenderDrawColor(R, (Uint8)r, (Uint8)gcol, (Uint8)b, 255);
    SDL_RenderFillRect(R, &rc);
}

static void glyph(int x, int y, char ch, int r, int gcol, int b, int s)
{
    int row, col;
    unsigned char c = (unsigned char)ch;
    if (c < 32 || c > 127) {
        c = '?';
    }
    for (row = 0; row < 7; row++) {
        unsigned char bits = np_font5x7[c - 32][row];
        for (col = 0; col < 5; col++) {
            if (bits & (1 << (4 - col))) {
                fill(x + col * s, y + row * s, s, s, r, gcol, b);
            }
        }
    }
}

static void text(int x, int y, const char *s, int r, int gcol, int b, int sc)
{
    if (sc < 1) {
        sc = 1;
    }
    while (*s) {
        glyph(x, y, *s, r, gcol, b, sc);
        x += 6 * sc;
        s++;
    }
}

struct hit {
    SDL_Rect r;
    int kind; /* 1 connect 2 disc 3 rec 4 port 5 board 6 act 7 rld 8 gain */
    int ch;
};

static struct hit hits[128];
int nhits;
static int side_need;
static int side_pin_h;
static int hit_in_body;
static void btn(int x, int y, int w, int h, const char *label, int on, int kind, int ch,
                int r, int gcol, int b);

static int side_view(void)
{
    int h = win_h - statush();
    return h > 40 ? h : 40;
}

static void side_clamp(void)
{
    int max = side_need - side_view();
    if (max < 0) {
        max = 0;
    }
    if (g.side_scroll > max) {
        g.side_scroll = max;
    }
    if (g.side_scroll < 0) {
        g.side_scroll = 0;
    }
}

static int in_side(int x, int y)
{
    return x >= win_w - sidew() && y >= 0 && y < side_view();
}

static void side_end(int x, int y)
{
    int view = side_view();
    SDL_RenderSetClipRect(R, NULL);
    /* Room for the always-visible up/down row if the pane overflows. */
    side_need = y + g.side_scroll + 28;
    side_clamp();
    if (side_need > view) {
        int track = view - 28;
        int span = side_need - view;
        int bh = track * view / side_need;
        int by;
        if (bh < 16) {
            bh = 16;
        }
        by = 2 + (span > 0 ? g.side_scroll * (track - bh) / span : 0);
        fill(x + sidew() - 5, by, 3, bh, 200, 40, 56);
        {
            int th = NP_TOUCH ? 36 : 20;
            btn(x + 12, view - th - 4, 130, th, "up", 0, 45, 0, 36, 40, 48);
            btn(x + 146, view - th - 4, 130, th, "down", 0, 46, 0, 36, 40, 48);
        }
    }
}

static void add_hit(int x, int y, int w, int h, int kind, int ch)
{
    if (nhits >= 128) {
        return;
    }
    if (x >= win_w - sidew()) {
        int top = hit_in_body ? side_pin_h : 0;
        if (y + h <= top || y >= side_view()) {
            return;
        }
    }
    hits[nhits].r.x = x;
    hits[nhits].r.y = y;
    hits[nhits].r.w = w;
    hits[nhits].r.h = h;
    hits[nhits].kind = kind;
    hits[nhits].ch = ch;
    nhits++;
}

static int inside(const SDL_Rect *r, int x, int y)
{
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

static void btn(int x, int y, int w, int h, const char *label, int on, int kind, int ch,
                int r, int gcol, int b)
{
    fill(x, y, w, h, r, gcol, b);
    fill(x + 1, y + 1, w - 2, 1, 70, 74, 82);
    text(x + 8, y + (h - 7) / 2, label, on ? 240 : 200, on ? 240 : 200, on ? 240 : 210, 1);
    add_hit(x, y, w, h, kind, ch);
}

static const int CHCOL[NP_NCHAN][3] = {
    {80, 200, 255}, {255, 180, 70}, {120, 220, 140}, {240, 110, 140},
    {180, 150, 255}, {255, 230, 90}, {90, 230, 210}, {230, 140, 255},
};
static const int PALETTE[][3] = {
    {80, 200, 255}, {255, 180, 70}, {120, 220, 140}, {240, 110, 140},
    {180, 150, 255}, {255, 230, 90}, {90, 230, 210}, {230, 140, 255},
    {255, 90, 90}, {90, 255, 140}, {255, 255, 255}, {255, 140, 40},
};
#define NPAL 12

static void chcol_cycle(int c)
{
    int i, k;
    if (c < 0 || c >= NP_NCHAN) {
        return;
    }
    k = 0;
    for (i = 0; i < NPAL; i++) {
        if (g.chrgb[c][0] == PALETTE[i][0] && g.chrgb[c][1] == PALETTE[i][1] &&
            g.chrgb[c][2] == PALETTE[i][2]) {
            k = (i + 1) % NPAL;
            break;
        }
    }
    g.chrgb[c][0] = PALETTE[k][0];
    g.chrgb[c][1] = PALETTE[k][1];
    g.chrgb[c][2] = PALETTE[k][2];
}

/* Open-input / rail is not EEG. Autoscale of ±0.13 V looks like a brainwave. */
#define Q_OFF 0
#define Q_ZERO 1
#define Q_LEADOFF 2
#define Q_OPEN 3
#define Q_LIVE 4

static int ch_quality(int c, const float *buf, uint32_t n, uint8_t lp, uint8_t ln)
{
    uint32_t i;
    float mx = 0.f;
    uint8_t bit = (uint8_t)(1u << c);

    if (!g.active[c]) {
        return Q_OFF;
    }
    if (n < 4) {
        return Q_ZERO;
    }
    /* ADS1299 LOFF_STATP/N: 1 = lead-off when the comparator is on.
     * If both bytes stay 0 the firmware never enabled lead-off current
     * and we fall through to amplitude. */
    if ((lp | ln) != 0 && ((lp & bit) || (ln & bit))) {
        return Q_LEADOFF;
    }
    for (i = 0; i < n; i++) {
        float a = fabsf(buf[i]);
        if (a > mx) {
            mx = a;
        }
    }
    if (mx < 0.5f) {
        return Q_ZERO;
    }
    /* Near ADS1299 full-scale only. 3 mV is not "no skin" — EMG and
     * a worn but noisy headset both exceed that. */
    if (mx > 250000.f) {
        return Q_OPEN;
    }
    return Q_LIVE;
}

static void ch_stats(const float *buf, uint32_t n, float *dc, float *rms, float *pk)
{
    uint32_t i;
    double s = 0, e = 0;
    float p = 0.f;
    *dc = 0;
    *rms = 0;
    *pk = 0;
    if (!n) {
        return;
    }
    for (i = 0; i < n; i++) {
        float a = fabsf(buf[i]);
        s += buf[i];
        e += (double)buf[i] * (double)buf[i];
        if (a > p) {
            p = a;
        }
    }
    *dc = (float)(s / (double)n);
    *rms = sqrtf((float)(e / (double)n));
    *pk = p;
}

static void cal_path(char *out, size_t n)
{
    char root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    np_mkdir_p(root);
    snprintf(out, n, "%s/exg-c.cal", root);
}

static int cal_save(void)
{
    char path[NP_MAX_PATH];
    FILE *f;
    int c;
    time_t t = time(NULL);
    cal_path(path, sizeof(path));
    f = fopen(path, "w");
    if (!f) {
        return -1;
    }
    fprintf(f, "# exg-c plates: NOISE (desk/off) then CALM (worn still)\n");
    fprintf(f, "# time %ld  noise_n %u  calm %d\n", (long)t, g.cal.n, g.calm.have);
    fprintf(f, "tone_hz=%.3f\n", g.cal_hz);
    if (g.noise_psd_ok) {
        int k;
        fprintf(f, "psd");
        for (k = 0; k < NP_PSD_BINS; k++) {
            fprintf(f, " %.5g", g.noise_psd[k]);
        }
        fputc('\n', f);
    }
    fprintf(f, "ch,dc_uV,rms_uV,pk_uV\n");
    for (c = 0; c < NP_NCHAN; c++) {
        fprintf(f, "%d,%.3f,%.3f,%.3f\n", c + 1, g.cal.dc[c], g.cal.rms[c], g.cal.pk[c]);
    }
    if (g.calm.have) {
        for (c = 0; c < NP_NCHAN; c++) {
            fprintf(f, "calm%d=%.3f,%.3f,%.3f\n", c + 1, g.calm.dc[c], g.calm.rms[c],
                    g.calm.pk[c]);
        }
    }
    fclose(f);
    return 0;
}

static int cal_load(void)
{
    char path[NP_MAX_PATH], line[128];
    FILE *f;
    int got = 0;
    cal_path(path, sizeof(path));
    f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    memset(&g.cal, 0, sizeof(g.cal));
    while (fgets(line, sizeof(line), f)) {
        int ch;
        float dc, rms, pk;
        if (line[0] == '#' || (line[0] == 'c' && line[1] == 'h')) {
            continue;
        }
        if (sscanf(line, "tone_hz=%f", &dc) == 1) {
            g.cal_hz = dc;
            continue;
        }
        if (!strncmp(line, "psd ", 4) || !strncmp(line, "psd\t", 4)) {
            const char *s = line + 3;
            int k = 0;
            g.noise_psd_ok = 0;
            memset(g.noise_psd, 0, sizeof(g.noise_psd));
            while (k < NP_PSD_BINS) {
                char *end = NULL;
                float v = strtof(s, &end);
                if (end == s) {
                    break;
                }
                g.noise_psd[k++] = v;
                s = end;
            }
            if (k >= 8) {
                g.noise_psd_ok = 1;
            }
            continue;
        }
        if (sscanf(line, "calm%d=%f,%f,%f", &ch, &dc, &rms, &pk) == 4 && ch >= 1 &&
            ch <= NP_NCHAN) {
            g.calm.dc[ch - 1] = dc;
            g.calm.rms[ch - 1] = rms;
            g.calm.pk[ch - 1] = pk;
            g.calm.have = 1;
        } else if (sscanf(line, "%d,%f,%f,%f", &ch, &dc, &rms, &pk) == 4 && ch >= 1 &&
                   ch <= NP_NCHAN) {
            g.cal.dc[ch - 1] = dc;
            g.cal.rms[ch - 1] = rms;
            g.cal.pk[ch - 1] = pk;
            got++;
        }
    }
    fclose(f);
    if (got > 0) {
        g.cal.have = 1;
        g.cal.n = 1;
    }
    return got > 0 ? 0 : -1;
}

static void cal_capture(void)
{
    int c;
    uint32_t want = plate_want();
    memset(&g.cal, 0, sizeof(g.cal));
    g.cal_hz = 0.f;
    g.noise_psd_ok = 0;
    memset(g.noise_psd, 0, sizeof(g.noise_psd));
    {
        float acc[NP_PLATE_N];
        int accn = 0, used = 0;
        memset(acc, 0, sizeof(acc));
        for (c = 0; c < NP_NCHAN; c++) {
            float buf[NP_RING];
            uint32_t n = np_ring_copy(&g.ring, c, buf, want);
            uint32_t i, take;
            ch_stats(buf, n, &g.cal.dc[c], &g.cal.rms[c], &g.cal.pk[c]);
            if (n > g.cal.n) {
                g.cal.n = n;
            }
            take = n > (uint32_t)NP_PLATE_N ? (uint32_t)NP_PLATE_N : n;
            if (take < 32) {
                continue;
            }
            if (accn < (int)take) {
                accn = (int)take;
            }
            for (i = 0; i < take; i++) {
                acc[i] += buf[i];
            }
            used++;
        }
        if (used > 0 && accn >= NP_FFT_N) {
            float hz = 0.f;
            np_welch_psd(acc, accn, g.noise_psd);
            g.noise_psd_ok = 1;
            if (np_tone_from_psd(g.noise_psd, design_sps(), &hz) == 0) {
                g.cal_hz = hz;
            } else if (np_tone_hz(acc, accn > NP_FFT_N ? NP_FFT_N : accn, design_sps(),
                                 &hz) == 0) {
                g.cal_hz = hz;
            }
        } else if (used > 0) {
            float hz = 0.f;
            if (np_tone_hz(acc, accn, design_sps(), &hz) == 0) {
                g.cal_hz = hz;
            }
        }
    }
    g.cal.have = 1;
    g.cal_arm = 0;
    if (cal_save() != 0) {
        set_status(0, "calibrate captured but could not write exg-c.cal");
        return;
    }
    if (g.cal_hz > 1.f) {
        set_status(1, "NOISE plate  ch1 rms %.0f uV  line %.1f Hz", g.cal.rms[0], g.cal_hz);
    } else {
        set_status(1, "NOISE plate  ch1 rms %.0f uV  no line tone", g.cal.rms[0]);
    }
}

static void calm_capture(void)
{
    int c;
    uint32_t want = plate_want();
    if (!g.cal.have) {
        set_status(0, "NOISE first (desk / headset off, then OK)");
        return;
    }
    memset(&g.calm, 0, sizeof(g.calm));
    for (c = 0; c < NP_NCHAN; c++) {
        float buf[NP_RING], dc, rms, pk;
        uint32_t n = np_ring_copy(&g.ring, c, buf, want);
        if (n < 16) {
            continue;
        }
        if (g.cal_hz > 1.f) {
            struct np_notch nt;
            uint32_t i;
            np_notch_init(&nt, g.cal_hz, design_sps(), 30.f);
            for (i = 0; i < n; i++) {
                buf[i] = np_notch_step(&nt, buf[i]);
            }
        }
        ch_stats(buf, n, &dc, &rms, &pk);
        g.calm.dc[c] = dc;
        np_sub_dc(buf, (int)n, dc);
        ch_stats(buf, n, &dc, &g.calm.rms[c], &g.calm.pk[c]);
        if (n > g.calm.n) {
            g.calm.n = n;
        }
    }
    g.calm.have = 1;
    g.cal_cut = 1;
    if (cal_save() != 0) {
        set_status(0, "calm captured but could not write exg-c.cal");
        return;
    }
    set_status(1, "CALM plate  ch1 resid %.0f uV  CLEAN on", g.calm.rms[0]);
}

static int live_vs_cal(float *ratio_out)
{
    int c, nbase = 0, nchg = 0;
    float rmax = 0.f;
    uint32_t want = (uint32_t)(g.window_s * (g.sps > 1.f ? g.sps : NP_DEFAULT_SPS));
    if (want < 32) {
        want = 32;
    }
    if (want > NP_RING) {
        want = NP_RING;
    }
    for (c = 0; c < NP_NCHAN; c++) {
        float buf[NP_RING], dc, rms, pk, r;
        uint32_t n;
        if (!g.active[c] || !g.cal.have || g.cal.rms[c] < 1.f) {
            continue;
        }
        n = np_ring_copy(&g.ring, c, buf, want);
        ch_stats(buf, n, &dc, &rms, &pk);
        r = rms / g.cal.rms[c];
        if (r > rmax) {
            rmax = r;
        }
        if (r < 0.70f || r > 1.40f) {
            nchg++;
        } else {
            nbase++;
        }
    }
    if (ratio_out) {
        *ratio_out = rmax;
    }
    return nchg > 0 ? nchg : (nbase ? 0 : -1);
}

static void wear_check(void)
{
    float r = 0.f;
    int nchg;
    if (!g.cal.have) {
        set_status(0, "CAL first (headset off, then OK)");
        return;
    }
    nchg = live_vs_cal(&r);
    if (nchg > 0) {
        set_status(1, "analog CHANGED vs cal (max %.2fx) - %d ch left baseline", r, nchg);
    } else {
        set_status(0, "still at baseline (%.2fx)", r > 0.f ? r : 1.f);
    }
}

static void draw_waves(int x, int y, int w, int h)
{
    int c;
    uint8_t lp = 0, ln = 0;
    fill(x, y, w, h, 10, 12, 16);
    np_ring_loff(&g.ring, &lp, &ln);
    for (c = 0; c < NP_NCHAN; c++) {
        float buf[NP_RING];
        static float snap[NP_NCHAN][NP_RING];
        static uint32_t snapn[NP_NCHAN];
        uint32_t want = (uint32_t)(g.window_s * design_sps());
        int y0, y1b, row_h, mid, q, gated = 0;
        uint32_t i;
        uint32_t n = 0;
        char lab[36];
        float last, peak;
        float pre_min = 0.f, pre_max = 0.f;
        int cr, cg, cb, dim = 0;

        if (want < 32) {
            want = 32;
        }
        if (want > NP_RING) {
            want = NP_RING;
        }
        y0 = y + (c * h) / NP_NCHAN;
        y1b = y + ((c + 1) * h) / NP_NCHAN;
        row_h = y1b - y0;
        mid = (y0 + y1b) / 2;
        {
            float dc = 0, rms = 0, pk = 0, r = 0.f, raw_rms = 0.f;
            if (!g.paused) {
                n = np_ring_copy(&g.ring, c, buf, want);
                ch_stats(buf, n, &dc, &rms, &pk);
                raw_rms = rms;
                if (g.cal.have && g.cal_cut && !g.calm.have && g.cal.rms[c] > 1.f && n >= 4) {
                    r = rms / g.cal.rms[c];
                    if (r > 0.85f && r < 1.18f) {
                        gated = 1;
                    }
                }
                if (!gated) {
                    snapn[c] = n;
                    memcpy(snap[c], buf, n * sizeof(float));
                } else if (snapn[c] < 4 && n >= 4) {
                    snapn[c] = n;
                    memcpy(snap[c], buf, n * sizeof(float));
                }
            }
            if ((gated || g.paused) && snapn[c] >= 4) {
                n = snapn[c];
                memcpy(buf, snap[c], n * sizeof(float));
                ch_stats(buf, n, &dc, &rms, &pk);
            }
            if (n > 0) {
                uint32_t k;
                pre_min = pre_max = buf[0];
                for (k = 1; k < n; k++) {
                    if (buf[k] < pre_min) {
                        pre_min = buf[k];
                    }
                    if (buf[k] > pre_max) {
                        pre_max = buf[k];
                    }
                }
            }
            q = ch_quality(c, buf, n, lp, ln);
            if (q != Q_OFF && n >= 4 && !gated) {
                n = view_copy(c, buf, n);
                if (g.detrend) {
                    np_detrend(buf, (int)n);
                }
                ch_stats(buf, n, &dc, &rms, &pk);
            }
            last = n ? buf[n - 1] : 0.f;
            fill(x, y1b - 1, w, 1, 32, 36, 44);
            snprintf(lab, sizeof(lab), "%s", g.elec[c].name[0] ? g.elec[c].name : "?");
            text(x + 4, y0 + 4, lab, g.chrgb[c][0], g.chrgb[c][1], g.chrgb[c][2], 1);
            if (g.show_uv) {
                if (rms >= 1000.f) {
                    snprintf(lab, sizeof(lab), "%+.0f  rms %.1f mV", last, rms / 1000.f);
                } else {
                    snprintf(lab, sizeof(lab), "%+.0f  rms %.0f uV", last, rms);
                }
                text(x + 40, y0 + 4, lab, 170, 176, 186, 1);
            }
            if (q == Q_OFF) {
                text(x + 220, y0 + 4, "off", 110, 114, 124, 1);
            } else if (q == Q_LEADOFF) {
                snprintf(lab, sizeof(lab), "lead-off %s%s", (lp & (1u << c)) ? "P" : "",
                         (ln & (1u << c)) ? "N" : "");
                text(x + 220, y0 + 4, lab, 200, 90, 80, 1);
            } else if (gated) {
                text(x + 220, y0 + 4, "FROZEN", 200, 140, 70, 1);
            } else if (np_window_clip(buf, (int)n)) {
                text(x + 220, y0 + 4, "CLIP", 230, 80, 70, 1);
            } else if (g.cal_cut && g.cal.have) {
                float rr = 0.f;
                int det = np_detect(raw_rms > 1.f ? raw_rms : rms, rms, g.cal.rms[c],
                                    g.calm.have ? g.calm.rms[c] : 0.f, &rr);
                if (det == NP_DET_SIGNAL) {
                    snprintf(lab, sizeof(lab), "SIGNAL %.1fx", rr);
                    text(x + 220, y0 + 4, lab, 80, 230, 120, 1);
                } else if (det == NP_DET_CALM) {
                    text(x + 220, y0 + 4, "calm", 140, 180, 160, 1);
                } else if (det == NP_DET_NOISE) {
                    text(x + 220, y0 + 4, "noise", 210, 150, 80, 1);
                }
            } else if (g.cal.have && g.cal.rms[c] > 1.f) {
                snprintf(lab, sizeof(lab), "vs cal %.2fx", r > 0.f ? r : rms / g.cal.rms[c]);
                text(x + 220, y0 + 4, lab, 80, 210, 140, 1);
            }
            if (gated) {
                dim = 1;
            }
        }
        if (q == Q_OFF || n < 4) {
            continue;
        }
        if (g.grid) {
            int gx;
            SDL_SetRenderDrawColor(R, 28, 32, 40, 255);
            SDL_RenderDrawLine(R, x, mid, x + w, mid);
            for (gx = 1; gx < 4; gx++) {
                int xx = x + gx * w / 4;
                SDL_RenderDrawLine(R, xx, y0 + 1, xx, y1b - 2);
            }
        }
        peak = (float)g.scale_uv;
        if (peak < 20.f) {
            peak = 200.f;
        }
        {
            float omin = 0.f, omax = 0.f, ospan = 0.f;
            if (g.og && n > 0) {
                /* Keep the pre-notch span so cutting 60 Hz shrinks the
                 * strip. Post-filter min-max restretches leftovers to
                 * the same pixels and looks like off. */
                if (g.notch_hz != 0) {
                    omin = pre_min;
                    omax = pre_max;
                } else {
                    omin = omax = buf[0];
                    for (i = 1; i < n; i++) {
                        if (buf[i] < omin) {
                            omin = buf[i];
                        }
                        if (buf[i] > omax) {
                            omax = buf[i];
                        }
                    }
                }
                ospan = omax - omin;
                if (ospan < 8.f) {
                    ospan = 8.f;
                }
            } else if (g.autoscale && q == Q_LIVE) {
                peak = 8.f;
                for (i = 0; i < n; i++) {
                    float a = fabsf(buf[i]);
                    if (a > peak) {
                        peak = a;
                    }
                }
                /* Do not cap at 2 mV — a 4 s AUTO window is often mV EMG. */
                if (peak > 200000.f) {
                    peak = 200000.f;
                }
            }
            dim = (!g.og && (q == Q_OPEN || q == Q_LEADOFF));
            cr = dim ? g.chrgb[c][0] / 3 : g.chrgb[c][0];
            cg = dim ? g.chrgb[c][1] / 3 : g.chrgb[c][1];
            cb = dim ? g.chrgb[c][2] / 3 : g.chrgb[c][2];
            SDL_SetRenderDrawColor(R, (Uint8)cr, (Uint8)cg, (Uint8)cb, 255);
            for (i = 1; i < n; i++) {
                int x1 = x + (int)((i - 1) * (w - 1) / (n - 1));
                int x2 = x + (int)(i * (w - 1) / (n - 1));
                int py1, py2;
                if (g.og) {
                    float u1 = (buf[i - 1] - omin) / ospan;
                    float u2 = (buf[i] - omin) / ospan;
                    py1 = y1b - 3 - (int)(u1 * (row_h - 8));
                    py2 = y1b - 3 - (int)(u2 * (row_h - 8));
                } else {
                    py1 = mid - (int)(buf[i - 1] / peak * (row_h * 0.40f));
                    py2 = mid - (int)(buf[i] / peak * (row_h * 0.40f));
                }
            if (py1 < y0 + 2) {
                py1 = y0 + 2;
            }
            if (py1 > y1b - 3) {
                py1 = y1b - 3;
            }
            if (py2 < y0 + 2) {
                py2 = y0 + 2;
            }
            if (py2 > y1b - 3) {
                py2 = y1b - 3;
            }
            SDL_RenderDrawLine(R, x1, py1, x2, py2);
            }
        }
    }
}

enum { FFT_STRIP_N = 128, FFT_STRIP_BINS = 64 };

static float fft_hold[FFT_STRIP_BINS];
static uint32_t fft_t;
static int fft_used, fft_open, fft_peak_hz;

static void fft_refresh(void)
{
    enum { N = FFT_STRIP_N };
    float mag[FFT_STRIP_BINS];
    float acc[N];
    int c, i, used = 0, open_n = 0, peak_i = 1;
    uint8_t lp = 0, ln = 0;
    uint32_t now = SDL_GetTicks();
    if (fft_t && now - fft_t < 80) {
        return;
    }
    memset(mag, 0, sizeof(mag));
    np_ring_loff(&g.ring, &lp, &ln);
    for (c = 0; c < NP_NCHAN; c++) {
        float buf[N];
        uint32_t n;
        int q;
        if (!g.active[c]) {
            continue;
        }
        n = view_copy(c, buf, N);
        if (n < N) {
            continue;
        }
        q = ch_quality(c, buf, n, lp, ln);
        if (g.cal.have && g.cal_cut && g.cal.rms[c] > 1.f) {
            float dc, rms, pk;
            ch_stats(buf, n, &dc, &rms, &pk);
            if (rms / g.cal.rms[c] > 0.70f && rms / g.cal.rms[c] < 1.40f) {
                open_n++;
                continue;
            }
        }
        if (q != Q_LIVE) {
            open_n++;
        }
        np_detrend(buf, (int)n);
        for (i = 0; i < (int)n; i++) {
            float win = 0.5f - 0.5f * cosf(2.f * (float)M_PI * (float)i / (float)(n - 1));
            acc[i] = buf[i] * win;
        }
        {
            float m[N / 2];
            np_fft_mag(acc, N, m);
            for (i = 0; i < N / 2; i++) {
                mag[i] += m[i];
            }
            used++;
        }
    }
    memcpy(fft_hold, mag, sizeof(fft_hold));
    fft_t = now;
    fft_used = used;
    fft_open = open_n;
    {
        float peak = 1e-12f;
        int sps = g.sps > 1.f ? (int)(g.sps + 0.5f) : NP_DEFAULT_SPS;
        for (i = 1; i < FFT_STRIP_BINS; i++) {
            if (mag[i] > peak) {
                peak = mag[i];
                peak_i = i;
            }
        }
        fft_peak_hz = (peak_i * sps) / N;
    }
}

static void draw_fft(int x, int y, int w, int h)
{
    int i, used, open_n;
    char cap[56];
    fft_refresh();
    used = fft_used;
    open_n = fft_open;
    fill(x, y, w, h, 10, 12, 16);
    if (open_n && used == open_n) {
        snprintf(cap, sizeof(cap), "FFT  open");
    } else {
        snprintf(cap, sizeof(cap), "FFT");
    }
    text(x + 6, y + 4, cap, 160, 168, 180, 1);
    if (used) {
        float peak = 1e-12f;
        int bins = FFT_STRIP_BINS;
        int plot_y = y + 16;
        int plot_h = h - 22;
        int bar_w;
        for (i = 1; i < bins; i++) {
            if (fft_hold[i] > peak) {
                peak = fft_hold[i];
            }
        }
        bar_w = w / (bins - 1);
        if (bar_w < 1) {
            bar_w = 1;
        }
        {
            int mark_hz = g.notch_hz == 60 ? 60 : 50;
            int sps = g.sps > 1.f ? (int)(g.sps + 0.5f) : NP_DEFAULT_SPS;
            int mark = (mark_hz * FFT_STRIP_N) / sps;
            if (mark < 1) {
                mark = 1;
            }
            if (mark > bins - 1) {
                mark = bins - 1;
            }
            for (i = 1; i < bins; i++) {
                int bh = (int)(fft_hold[i] / peak * (plot_h - 2));
                int bx = x + (i - 1) * (w - 1) / (bins - 1);
                if (bh < 1) {
                    bh = 1;
                }
                if (i == mark) {
                    fill(bx, plot_y, bar_w < 2 ? 2 : bar_w, plot_h, 80, 16, 18);
                    fill(bx, plot_y + plot_h - bh, bar_w, bh, 224, 80, 80);
                } else {
                    fill(bx, plot_y + plot_h - bh, bar_w, bh, 70, 170, 230);
                }
            }
        }
        {
            char hz[24];
            int mark_hz = g.notch_hz == 60 ? 60 : 50;
            snprintf(hz, sizeof(hz), "%d Hz", fft_peak_hz);
            text(x + w - 52, y + 4, hz, 180, 200, 210, 1);
            snprintf(hz, sizeof(hz), "%d Hz", mark_hz);
            text(x + w / 2 - 16, y + 4, hz, 224, 80, 80, 1);
        }
    }
}

static void fill_tri(int x0, int y0, int x1, int y1, int x2, int y2, int r, int gc, int b)
{
    int i;
    if (y0 > y1) {
        int t = x0;
        x0 = x1;
        x1 = t;
        t = y0;
        y0 = y1;
        y1 = t;
    }
    if (y0 > y2) {
        int t = x0;
        x0 = x2;
        x2 = t;
        t = y0;
        y0 = y2;
        y2 = t;
    }
    if (y1 > y2) {
        int t = x1;
        x1 = x2;
        x2 = t;
        t = y1;
        y1 = y2;
        y2 = t;
    }
    if (y2 == y0) {
        return;
    }
    SDL_SetRenderDrawColor(R, (Uint8)r, (Uint8)gc, (Uint8)b, 255);
    for (i = y0; i <= y2; i++) {
        int xa, xb;
        if (i <= y1) {
            xa = y1 == y0 ? x0 : x0 + (x1 - x0) * (i - y0) / (y1 - y0);
            xb = x0 + (x2 - x0) * (i - y0) / (y2 - y0);
        } else {
            xa = y2 == y1 ? x1 : x1 + (x2 - x1) * (i - y1) / (y2 - y1);
            xb = x0 + (x2 - x0) * (i - y0) / (y2 - y0);
        }
        if (xa > xb) {
            int t = xa;
            xa = xb;
            xb = t;
        }
        SDL_RenderDrawLine(R, xa, i, xb, i);
    }
}

static int s_cube_ox, s_cube_oy, s_cube_vx, s_cube_vy, s_cube_vw, s_cube_vh;
static int s_cube_px, s_cube_py; /* screen origin of the cube panel */
static float s_cube_k;
static SDL_Texture *cube_tex;
static int cube_tw, cube_th;
static uint32_t cube_baked_seq;
static float cube_baked_yaw, cube_baked_pitch, cube_baked_zoom;
static int cube_baked_site, cube_baked_virt, cube_baked_sel, cube_baked_view;
static int cube_baked_sites[NP_NCHAN];
static uint32_t cube_bake_t;
static void present_cube(int x, int y, int w, int h);
static int s_elec_sx[NP_NCHAN], s_elec_sy[NP_NCHAN];
static int s_node_sx[NP_1010_N], s_node_sy[NP_1010_N];
static int s_map_sx[NP_1010_N], s_map_sy[NP_1010_N];
static int s_map_x, s_map_y, s_map_w, s_map_h;
static int s_cube_drag; /* 0 none 1 spin 2 place */
static int s_cube_lx, s_cube_ly, s_cube_dirty;

static void cube_zoom_clamp(void)
{
    if (g.cube_zoom < 0.70f) {
        g.cube_zoom = 0.70f;
    }
    if (g.cube_zoom > 2.80f) {
        g.cube_zoom = 2.80f;
    }
}

static void cube_zoom_by(int dir)
{
    g.cube_zoom += dir > 0 ? 0.20f : -0.20f;
    cube_zoom_clamp();
}

static void cube_site_by(int dir)
{
    int n = np_1010_count();
    if (n < 1) {
        return;
    }
    g.site_focus += dir;
    while (g.site_focus < 0) {
        g.site_focus += n;
    }
    g.site_focus %= n;
}

static int cube_virt_n(void)
{
    int i, n = 0;
    for (i = 0; i < NP_VIRT_MAX; i++) {
        if (g.smx.virt[i].used) {
            n++;
        }
    }
    return n;
}

static int cube_virt_slot(int focus)
{
    int i, n = 0;
    for (i = 0; i < NP_VIRT_MAX; i++) {
        if (!g.smx.virt[i].used) {
            continue;
        }
        if (n == focus) {
            return i;
        }
        n++;
    }
    return -1;
}

static void cube_virt_by(int dir)
{
    int n = cube_virt_n();
    if (n < 1) {
        g.virt_focus = 0;
        return;
    }
    g.virt_focus += dir;
    while (g.virt_focus < 0) {
        g.virt_focus += n;
    }
    g.virt_focus %= n;
}

static void cube_assign_focus(void)
{
    int ix, iy, iz, share[8], ns, i, other = -1;
    if (g.site_focus < 0 || g.site_focus >= np_1010_count()) {
        return;
    }
    if (g.elec_sel < 0 || g.elec_sel >= NP_NCHAN) {
        g.elec_sel = 0;
    }
    np_elec_set_site(&g.elec[g.elec_sel], g.site_focus);
    cfg_save();
    np_1010_ijk(g.site_focus, &ix, &iy, &iz);
    ns = np_1010_sites_at(ix, iy, iz, share, 8);
    for (i = 0; i < NP_NCHAN; i++) {
        if (i != g.elec_sel && g.elec[i].site == g.site_focus) {
            other = i;
        }
    }
    if (other >= 0) {
        set_status(1, "ch%d @ %s  cell %d,%d,%d  also ch%d", g.elec_sel + 1,
                   np_1010_name(g.site_focus), ix, iy, iz, other + 1);
    } else if (ns > 1) {
        set_status(1, "ch%d @ %s  cell %d,%d,%d  (%d names on this cell)", g.elec_sel + 1,
                   np_1010_name(g.site_focus), ix, iy, iz, ns);
    } else {
        set_status(1, "ch%d @ %s  cell %d,%d,%d", g.elec_sel + 1, np_1010_name(g.site_focus),
                   ix, iy, iz);
    }
}

static float viz_auto_yaw;
static uint32_t viz_t0, viz_last;
static uint8_t viz_prev[NP_CUBE3_N];
static float viz_imp[NP_CUBE3_N];

static float viz_t(void)
{
    if (!viz_t0) {
        viz_t0 = SDL_GetTicks();
    }
    return (SDL_GetTicks() - viz_t0) / 1000.f;
}

static void viz_tick(void)
{
    uint32_t now = SDL_GetTicks();
    float dt = viz_last ? (now - viz_last) / 1000.f : 0.016f;
    if (dt > 0.08f) {
        dt = 0.016f;
    }
    viz_last = now;
    if (!viz_t0) {
        viz_t0 = now;
    }
    if (s_cube_drag != 1) {
        viz_auto_yaw += 0.148f * dt;
    }
}

static void cam_pt(float x, float y, float z, int *sx, int *sy, float *depth)
{
    float vx, vy, vz, yaw = g.cube_yaw, yy = y;
    if (g.cube_view == 0) {
        yaw += viz_auto_yaw;
        yy += 0.06f * sinf(viz_t() * 1.4f);
    }
    np_view_apply(yaw, g.cube_pitch, x, yy, z, &vx, &vy, &vz);
    if (g.cube_view == 0) {
        float f = 3.6f / (3.6f + vz + 2.2f);
        vx *= f;
        vy *= f;
    }
    if (sx) {
        *sx = s_cube_ox + (int)(vx * s_cube_k);
    }
    if (sy) {
        *sy = s_cube_oy - (int)(vy * s_cube_k);
    }
    if (depth) {
        *depth = vz;
    }
}

static void fill_disk(int cx, int cy, int r, int cr, int cg, int cb)
{
    int y;
    if (r < 1) {
        r = 1;
    }
    for (y = -r; y <= r; y++) {
        int xw = (int)sqrtf((float)(r * r - y * y));
        fill(cx - xw, cy + y, xw * 2 + 1, 1, cr, cg, cb);
    }
}

static int cube_farther(const void *a, const void *b)
{
    const struct np_cube *ca = a, *cb = b;
    float ax, ay, az, bx, by, bz;
    np_view_apply(g.cube_yaw, g.cube_pitch, ca->x, ca->y, ca->z, &ax, &ay, &az);
    np_view_apply(g.cube_yaw, g.cube_pitch, cb->x, cb->y, cb->z, &bx, &by, &bz);
    if (az < bz) {
        return -1;
    }
    if (az > bz) {
        return 1;
    }
    return 0;
}

static void draw_iso_cube(const struct np_cube *c)
{
    int p[8][2];
    float h = c->s * 0.5f;
    int i, cr, cg, cb, dim;
    const float vx[8] = {-1, 1, 1, -1, -1, 1, 1, -1};
    const float vy[8] = {1, 1, 1, 1, -1, -1, -1, -1};
    const float vz[8] = {-1, -1, 1, 1, -1, -1, 1, 1};

    dim = c->a < 120 ? 1 : 0;
    cr = dim ? c->r / 3 : c->r;
    cg = dim ? c->g / 3 : c->g;
    cb = dim ? c->b / 3 : c->b;
    if (cr < 8) {
        cr = 8;
    }
    for (i = 0; i < 8; i++) {
        cam_pt(c->x + vx[i] * h, c->y + vy[i] * h, c->z + vz[i] * h, &p[i][0], &p[i][1], NULL);
    }
    /* Dim cells are wire only — filled scanlines starve the USB reader. */
    if (!dim) {
        fill_tri(p[0][0], p[0][1], p[1][0], p[1][1], p[2][0], p[2][1],
                 cr, cg + 18 > 255 ? 255 : cg + 18, cb);
        fill_tri(p[0][0], p[0][1], p[2][0], p[2][1], p[3][0], p[3][1],
                 cr, cg + 18 > 255 ? 255 : cg + 18, cb);
        fill_tri(p[3][0], p[3][1], p[2][0], p[2][1], p[6][0], p[6][1], cr * 2 / 3, cg * 2 / 3,
                 cb * 2 / 3);
        fill_tri(p[3][0], p[3][1], p[6][0], p[6][1], p[7][0], p[7][1], cr * 2 / 3, cg * 2 / 3,
                 cb * 2 / 3);
        fill_tri(p[1][0], p[1][1], p[5][0], p[5][1], p[6][0], p[6][1], cr / 2, cg / 2, cb / 2);
        fill_tri(p[1][0], p[1][1], p[6][0], p[6][1], p[2][0], p[2][1], cr / 2, cg / 2, cb / 2);
    }
    SDL_SetRenderDrawColor(R, 18, 6, 10, 255);
    SDL_RenderDrawLine(R, p[0][0], p[0][1], p[1][0], p[1][1]);
    SDL_RenderDrawLine(R, p[1][0], p[1][1], p[2][0], p[2][1]);
    SDL_RenderDrawLine(R, p[2][0], p[2][1], p[3][0], p[3][1]);
    SDL_RenderDrawLine(R, p[3][0], p[3][1], p[0][0], p[0][1]);
    SDL_RenderDrawLine(R, p[3][0], p[3][1], p[7][0], p[7][1]);
    SDL_RenderDrawLine(R, p[2][0], p[2][1], p[6][0], p[6][1]);
    SDL_RenderDrawLine(R, p[1][0], p[1][1], p[5][0], p[5][1]);
    SDL_RenderDrawLine(R, p[7][0], p[7][1], p[6][0], p[6][1]);
    SDL_RenderDrawLine(R, p[6][0], p[6][1], p[5][0], p[5][1]);
}

static int cube_in_spin(int mx, int my)
{
    mx -= s_cube_px;
    my -= s_cube_py;
    return mx >= s_cube_vx && my >= s_cube_vy && mx < s_cube_vx + s_cube_vw &&
           my < s_cube_vy + s_cube_vh;
}

static int cube_hit_elec(int mx, int my)
{
    int i, best = -1, bd = 24 * 24;
    mx -= s_cube_px;
    my -= s_cube_py;
    for (i = 0; i < NP_NCHAN; i++) {
        int dx = mx - s_elec_sx[i], dy = my - s_elec_sy[i], d;
        if (s_elec_sx[i] < -10000) {
            continue;
        }
        d = dx * dx + dy * dy;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

static int cube_hit_node(int mx, int my)
{
    int i, best = -1, reach, bd;
    mx -= s_cube_px;
    my -= s_cube_py;
    reach = 14 + (int)(10.f * g.cube_zoom);
    bd = reach * reach;
    for (i = 0; i < NP_1010_N; i++) {
        int dx = mx - s_node_sx[i], dy = my - s_node_sy[i], d;
        if (s_node_sx[i] < -10000) {
            continue;
        }
        d = dx * dx + dy * dy;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

static int cube_hit_map(int mx, int my)
{
    int i, best = -1, bd = 16 * 16;
    mx -= s_cube_px;
    my -= s_cube_py;
    if (mx < s_map_x || my < s_map_y || mx >= s_map_x + s_map_w || my >= s_map_y + s_map_h) {
        return -1;
    }
    for (i = 0; i < NP_1010_N; i++) {
        int dx = mx - s_map_sx[i], dy = my - s_map_sy[i], d;
        d = dx * dx + dy * dy;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

static int cube_pointer_down(int mx, int my)
{
    int hit, node, c, mapped;
    if (g.tab != 2) {
        return 0;
    }
    mapped = cube_hit_map(mx, my);
    if (mapped >= 0) {
        g.site_focus = mapped;
        if (g.elec_sel >= 0) {
            cube_assign_focus();
        } else {
            set_status(1, "site %s  - pick a channel, then Assign", np_1010_name(mapped));
        }
        return 1;
    }
    if (!cube_in_spin(mx, my)) {
        return 0;
    }
    hit = cube_hit_elec(mx, my);
    if (hit >= 0) {
        g.elec_sel = hit;
        if (g.elec[hit].site >= 0) {
            g.site_focus = g.elec[hit].site;
        }
        set_status(1, "ch%d %s", hit + 1, g.elec[hit].name[0] ? g.elec[hit].name : "?");
        return 1;
    }
    node = cube_hit_node(mx, my);
    if (node >= 0) {
        g.site_focus = node;
        if (g.elec_sel >= 0) {
            cube_assign_focus();
            return 1;
        }
        for (c = 0; c < NP_NCHAN; c++) {
            if (g.elec[c].site == node) {
                g.elec_sel = c;
                set_status(1, "ch%d %s", c + 1, np_1010_name(node));
                return 1;
            }
        }
        set_status(1, "site %s  - pick a channel, then Assign", np_1010_name(node));
        return 1;
    }
    s_cube_drag = 1;
    s_cube_lx = mx;
    s_cube_ly = my;
    return 1;
}

static void cube_pointer_move(int mx, int my)
{
    if (s_cube_drag == 1) {
        g.cube_yaw += (float)(mx - s_cube_lx) * 0.010f;
        g.cube_pitch += (float)(s_cube_ly - my) * 0.010f;
        if (g.cube_pitch > 1.20f) {
            g.cube_pitch = 1.20f;
        }
        if (g.cube_pitch < -0.35f) {
            g.cube_pitch = -0.35f;
        }
        s_cube_lx = mx;
        s_cube_ly = my;
    }
}

static void cube_pointer_up(void)
{
    s_cube_drag = 0;
    s_cube_dirty = 0;
}

static void draw_cube_wire(void)
{
    const float p[8][3] = {{-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1},
                           {-1, -1, 1},  {1, -1, 1},  {-1, 1, 1},  {1, 1, 1}};
    const int e[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7},
                          {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    int i;
    if (g.cube_view == 0) {
        SDL_SetRenderDrawColor(R, 140, 5, 13, 200);
    } else {
        SDL_SetRenderDrawColor(R, 90, 18, 28, 255);
    }
    for (i = 0; i < 12; i++) {
        int x0, y0, x1, y1;
        cam_pt(p[e[i][0]][0], p[e[i][0]][1], p[e[i][0]][2], &x0, &y0, NULL);
        cam_pt(p[e[i][1]][0], p[e[i][1]][1], p[e[i][1]][2], &x1, &y1, NULL);
        SDL_RenderDrawLine(R, x0, y0, x1, y1);
    }
}

struct viz_dot {
    float depth;
    int sx, sy, r, on, imp;
};

static int viz_dot_farther(const void *a, const void *b)
{
    const struct viz_dot *da = a, *db = b;
    if (da->depth < db->depth) {
        return -1;
    }
    if (da->depth > db->depth) {
        return 1;
    }
    return 0;
}

static void draw_cube_lattice(void)
{
    struct viz_dot dots[NP_CUBE3_N];
    int ix, iy, iz, n = 0, i, on_n = 0;
    float pulse = 0.5f + 0.5f * sinf(viz_t() * 3.2f);

    for (i = 0; i < NP_CUBE3_N; i++) {
        int on = g.smx.cube[i] ? 1 : 0;
        if (on) {
            on_n++;
        }
        if (on && !viz_prev[i]) {
            viz_imp[i] = 1.f;
        } else {
            viz_imp[i] *= 0.88f;
            if (viz_imp[i] < 0.02f) {
                viz_imp[i] = 0.f;
            }
        }
        viz_prev[i] = (uint8_t)on;
    }
    for (iz = 0; iz < 8; iz++) {
        for (iy = 0; iy < 8; iy++) {
            for (ix = 0; ix < 8; ix++) {
                int idx = np_cube_idx(ix, iy, iz);
                int shell = np_cube_shell(ix, iy, iz);
                int on = g.smx.cube[idx] ? 1 : 0;
                float wx, wy, wz, depth = 0.f;
                int sx, sy, edge;
                if (!shell && !on && viz_imp[idx] < 0.08f) {
                    continue;
                }
                np_ijk_world(ix, iy, iz, &wx, &wy, &wz);
                cam_pt(wx, wy, wz, &sx, &sy, &depth);
                edge = (ix == 0 || ix == 7) + (iy == 0 || iy == 7) + (iz == 0 || iz == 7);
                dots[n].depth = depth;
                dots[n].sx = sx;
                dots[n].sy = sy;
                dots[n].on = on || viz_imp[idx] > 0.08f;
                dots[n].imp = viz_imp[idx] > 0.08f;
                if (dots[n].on) {
                    dots[n].r = 3 + (int)(g.cube_zoom * 3.f) + (dots[n].imp ? 2 : 0);
                } else {
                    dots[n].r = (edge >= 2 ? 2 : 1) + (pulse > 0.7f && edge >= 2 ? 1 : 0);
                }
                n++;
            }
        }
    }
    if (n > 1) {
        qsort(dots, (size_t)n, sizeof(dots[0]), viz_dot_farther);
    }
    (void)on_n;
    for (i = 0; i < n; i++) {
        if (dots[i].on) {
            fill_disk(dots[i].sx, dots[i].sy, dots[i].r, 255, 20, 26);
            if (dots[i].imp) {
                float ang = viz_t() * 40.f + (float)i;
                int jx = (int)(sinf(ang) * 10.f);
                int jy = (int)(cosf(ang * 1.3f) * 10.f);
                SDL_SetRenderDrawColor(R, 255, 13, 20, 255);
                SDL_RenderDrawLine(R, dots[i].sx, dots[i].sy, dots[i].sx + jx, dots[i].sy + jy);
            }
        } else {
            fill_disk(dots[i].sx, dots[i].sy, dots[i].r, 180, 12, 16);
        }
    }
}

static void draw_cube(int x, int y, int w, int h)
{
    struct np_cube cells[NP_CUBE_BUDGET];
    int n, i, c, ids[NP_NCHAN], nid;
    char lab[96];

    int map_h = (g.cube_view == 1 && h > 300) ? 148 : (g.cube_view == 1 ? 118 : 0);
    int sot_h = 18;
    fill(x, y, w, h, g.cube_view == 0 ? 6 : 4, 0, g.cube_view == 0 ? 1 : 6);
    nid = np_smx_ch_ids(&g.smx, ids);
    cube_zoom_clamp();
    if (g.site_focus < 0 || g.site_focus >= np_1010_count()) {
        g.site_focus = 0;
    }
    s_cube_vx = x;
    s_cube_vy = y;
    s_cube_vw = w;
    s_cube_vh = h - map_h - sot_h;
    if (s_cube_vh < 90) {
        map_h = 100;
        s_cube_vh = h - map_h - sot_h;
    }
    s_cube_ox = x + w / 2;
    s_cube_oy = y + s_cube_vh / 2 + 8;
    s_cube_k = (float)(w < s_cube_vh ? w : s_cube_vh) / 2.35f * g.cube_zoom;
    if (s_cube_k < 50.f) {
        s_cube_k = 50.f;
    }
    if (g.cube_view == 0) {
        viz_tick();
    }
    draw_cube_wire();
    if (g.cube_view == 0) {
        int csx, csy;
        draw_cube_lattice();
        cam_pt(0.f, 0.f, 0.f, &csx, &csy, NULL);
        fill_disk(csx, csy, 4 + (int)(g.cube_zoom * 3.f), 255, 20, 26);
        snprintf(lab, sizeof(lab), "viz  seq %u  %s  drag", g.smx.seq, np_algo_name(g.algo));
        text(x + 8, y + 6, lab, 255, 20, 26, 1);
        s_map_x = s_map_y = s_map_w = s_map_h = 0;
        for (i = 0; i < NP_1010_N; i++) {
            s_node_sx[i] = s_node_sy[i] = -20000;
        }
        for (c = 0; c < NP_NCHAN; c++) {
            float cx, cy, cz;
            int sx, sy;
            char nlab[12];
            np_elec_cube_xyz(&g.elec[c], &cx, &cy, &cz);
            cam_pt(cx, cy, cz, &sx, &sy, NULL);
            s_elec_sx[c] = sx;
            s_elec_sy[c] = sy;
            snprintf(nlab, sizeof(nlab), "%d %s", c + 1,
                     g.elec[c].name[0] ? g.elec[c].name : "?");
            text(sx - (int)strlen(nlab) * 3, sy - 18, nlab, g.chrgb[c][0], g.chrgb[c][1],
                 g.chrgb[c][2], 1);
        }
        goto cube_sot;
    }

    /* map — same 10-10 cells as before. Viz is the lattice above. */
    n = np_smx_head_cubes(&g.smx, g.elec, g.chrgb, cells, NP_CUBE_BUDGET);
    if (n > 1) {
        qsort(cells, (size_t)n, sizeof(cells[0]), cube_farther);
    }
    for (i = 0; i < n; i++) {
        draw_iso_cube(&cells[i]);
    }

    /* Highlight the focused 10-10 cell — SoT is the name, not the blob. */
    {
        int fx, fy, fz;
        struct np_cube hi;
        np_1010_ijk(g.site_focus, &fx, &fy, &fz);
        np_ijk_world(fx, fy, fz, &hi.x, &hi.y, &hi.z);
        hi.s = 0.30f;
        hi.r = 255;
        hi.g = 210;
        hi.b = 70;
        hi.a = 255;
        hi.role = 2;
        draw_iso_cube(&hi);
        {
            int vs = cube_virt_slot(g.virt_focus);
            if (vs >= 0) {
                np_ijk_world(g.smx.virt[vs].x, g.smx.virt[vs].y, g.smx.virt[vs].z, &hi.x, &hi.y,
                             &hi.z);
                hi.s = 0.24f;
                hi.r = 40;
                hi.g = 200;
                hi.b = 220;
                draw_iso_cube(&hi);
            }
        }
    }

    /* 10-10 names on the cube. Core + assigned + focus always; all names when zoomed. */
    for (i = 0; i < NP_1010_N; i++) {
        float cx, cy, cz, depth = 0.f;
        int sx, sy, taken = -1, show;
        np_1010_cube_xyz(i, &cx, &cy, &cz);
        cam_pt(cx, cy, cz, &sx, &sy, &depth);
        s_node_sx[i] = sx;
        s_node_sy[i] = sy;
        for (c = 0; c < NP_NCHAN; c++) {
            if (g.elec[c].site == i) {
                taken = c;
                break;
            }
        }
        show = (i == g.site_focus) || taken >= 0 || np_1010_core(i);
        if (show && depth > -0.25f) {
            const char *nm = np_1010_name(i);
            int bright = (i == g.site_focus);
            text(sx - (int)strlen(nm) * 3, sy + 6, nm,
                 bright ? 255 : (taken >= 0 ? g.chrgb[taken][0] : 160),
                 bright ? 220 : (taken >= 0 ? g.chrgb[taken][1] : 40),
                 bright ? 80 : (taken >= 0 ? g.chrgb[taken][2] : 50), 1);
        }
    }
    for (c = 0; c < NP_NCHAN; c++) {
        float cx, cy, cz;
        int sx, sy;
        char nlab[12];
        np_elec_cube_xyz(&g.elec[c], &cx, &cy, &cz);
        cam_pt(cx, cy, cz, &sx, &sy, NULL);
        s_elec_sx[c] = sx;
        s_elec_sy[c] = sy;
        snprintf(nlab, sizeof(nlab), "%d %s", c + 1,
                 g.elec[c].name[0] ? g.elec[c].name : "?");
        if (c == g.elec_sel) {
            fill(sx - 20, sy - 18, 40, 12, 0, 0, 0);
        }
        text(sx - (int)strlen(nlab) * 3, sy - 18, nlab,
             c == g.elec_sel ? 255 : g.chrgb[c][0],
             c == g.elec_sel ? 230 : g.chrgb[c][1],
             c == g.elec_sel ? 230 : g.chrgb[c][2], 1);
    }

    {
        int fx, fy, fz;
        np_1010_ijk(g.site_focus, &fx, &fy, &fz);
        snprintf(lab, sizeof(lab), "find %s  cell %d,%d,%d  zoom %.1fx   +/-  [ ] site  Enter assign",
                 np_1010_name(g.site_focus), fx, fy, fz, (double)g.cube_zoom);
        text(x + 8, y + 6, lab, NP_CUBE_CR, NP_CUBE_CG, NP_CUBE_CB, 1);
    }
    if (g.elec_sel >= 0) {
        snprintf(lab, sizeof(lab), "ch%d now %s   pick a 10-10 on the map or Assign",
                 g.elec_sel + 1, g.elec[g.elec_sel].name[0] ? g.elec[g.elec_sel].name : "?");
        text(x + 8, y + 18, lab, 200, 160, 80, 1);
    }

    /* Flat 10-10 — same markings as the headset. This is how you find a cell. */
    {
        int mx0 = x + 10, my0 = y + s_cube_vh + 4, mw = w - 20, mh = map_h - 8;
        int i;
        s_map_x = mx0;
        s_map_y = my0;
        s_map_w = mw;
        s_map_h = mh;
        fill(mx0, my0, mw, mh, 8, 6, 10);
        text(mx0 + 4, my0 + 3, "10-10  (nose up)   [ ] next site   Enter assign", 140, 40, 50,
             1);
        for (i = 0; i < NP_1010_N; i++) {
            float fx, fy;
            int sx, sy, taken = -1, r;
            np_1010_flat(i, &fx, &fy);
            sx = mx0 + mw / 2 + (int)(fx * (float)(mw / 2 - 16));
            sy = my0 + mh / 2 - (int)(fy * (float)(mh / 2 - 16));
            s_map_sx[i] = sx;
            s_map_sy[i] = sy;
            for (c = 0; c < NP_NCHAN; c++) {
                if (g.elec[c].site == i) {
                    taken = c;
                    break;
                }
            }
            r = (i == g.site_focus) ? 5 : (taken >= 0 || np_1010_core(i) ? 3 : 2);
            fill(sx - r, sy - r, r * 2, r * 2,
                 i == g.site_focus ? 255 : (taken >= 0 ? g.chrgb[taken][0] : 90),
                 i == g.site_focus ? 210 : (taken >= 0 ? g.chrgb[taken][1] : 24),
                 i == g.site_focus ? 70 : (taken >= 0 ? g.chrgb[taken][2] : 32));
            if (i == g.site_focus || taken >= 0 || np_1010_core(i)) {
                text(sx + 5, sy - 3, np_1010_name(i),
                     i == g.site_focus ? 255 : 200, i == g.site_focus ? 220 : 180,
                     i == g.site_focus ? 90 : 190, 1);
            }
        }
    }

cube_sot:
    /* Current second — SoT strip. */
    {
        int cell = (w - 16) / 8, gy = y + h - 18;
        if (cell > 40) {
            cell = 40;
        }
        if (cell < 16) {
            cell = 16;
        }
        fill(x, gy, w, 18, 4, 3, 6);
        for (c = 0; c < NP_NCHAN; c++) {
            int on = 0, k;
            if (g.smx.have) {
                int row = (int)((g.smx.wr - 1) % NP_SMX_SEC);
                for (k = 0; k < nid; k++) {
                    if (ids[k] == c + 1) {
                        on = g.smx.bit[row][k];
                        break;
                    }
                }
            }
            if (g.cube_view == 0) {
                fill(x + 8 + c * cell, gy + 5, cell - 3, 10,
                     on ? NP_CUBE_CR : 40, on ? NP_CUBE_CG : 8, on ? NP_CUBE_CB : 14);
                text(x + 8 + c * cell, gy + 6, g.elec[c].name[0] ? g.elec[c].name : "?",
                     on ? 255 : 120, on ? 80 : 40, on ? 100 : 50, 1);
            } else {
                fill(x + 8 + c * cell, gy + 5, cell - 3, 10,
                     on ? g.chrgb[c][0] : g.chrgb[c][0] / 4,
                     on ? g.chrgb[c][1] : g.chrgb[c][1] / 4,
                     on ? g.chrgb[c][2] : g.chrgb[c][2] / 4);
                text(x + 8 + c * cell, gy + 6, g.elec[c].name[0] ? g.elec[c].name : "?",
                     on ? 255 : 160, on ? 240 : 90, on ? 240 : 100, 1);
            }
        }
    }
}

static const char *port_short(void)
{
    const char *p = g.nports ? g.ports[g.port_i] : "";
    if (!p[0]) {
        return "(no port)";
    }
    if (p[0] == '/' && p[1] == 'd' && p[2] == 'e' && p[3] == 'v' && p[4] == '/') {
        return p + 5;
    }
    return p;
}

static int draw_channels(int x, int y)
{
    int c, bh = btnh(), rh = rowh();
    char line[8], gn[8];
    text(x + 12, y, "Channels 1-8", 140, 148, 160, 1);
    y += NP_TOUCH ? 18 : 14;
    for (c = 0; c < NP_NCHAN; c++) {
        int col = c / 4;
        int row = c % 4;
        int bx = x + 10 + col * 145;
        int by = y + row * rh;
        snprintf(line, sizeof(line), "%s", g.elec[c].name[0] ? g.elec[c].name : "?");
        text(bx, by + (bh - 7) / 2, line, g.chrgb[c][0], g.chrgb[c][1], g.chrgb[c][2], 1);
        btn(bx + 26, by, 36, bh, g.active[c] ? "ON" : "off", g.active[c], 6, c,
            g.active[c] ? 28 : 40, g.active[c] ? 90 : 42, g.active[c] ? 60 : 50);
        btn(bx + 64, by, 36, bh, g.rld[c] ? "RLD" : "rld", g.rld[c], 7, c,
            g.rld[c] ? 50 : 40, g.rld[c] ? 70 : 42, g.rld[c] ? 110 : 50);
        snprintf(gn, sizeof(gn), "g%d", g.gain[c]);
        btn(bx + 102, by, 40, bh, gn, 1, 8, c, 40, 42, 52);
    }
    return y + 4 * rh + 8;
}

static int draw_view_block(int x, int y)
{
    char b[40];
    int bh = btnh(), rh = rowh();
    text(x + 12, y, "View", 140, 148, 160, 1);
    y += NP_TOUCH ? 16 : 13;
    snprintf(b, sizeof(b), "win %ds", g.window_s);
    btn(x + 12, y, 136, bh, b, 1, 9, 0, 36, 40, 48);
    if (g.og) {
        snprintf(b, sizeof(b), "scale fit");
    } else if (g.autoscale) {
        snprintf(b, sizeof(b), "scale AUTO");
    } else {
        snprintf(b, sizeof(b), "scale +-%duV", g.scale_uv);
    }
    btn(x + 152, y, 136, bh, b, 1, 10, 0, 36, 40, 48);
    y += rh;
    if (g.notch_hz < 0) {
        if (g.cal_hz > 1.f) {
            snprintf(b, sizeof(b), "notch AUTO %.1f", (double)g.cal_hz);
        } else {
            snprintf(b, sizeof(b), "notch AUTO");
        }
    } else if (g.notch_hz) {
        snprintf(b, sizeof(b), (float)g.notch_hz > design_sps() * 0.42f
                                   ? "notch %d wide"
                                   : "notch %dHz",
                 g.notch_hz);
    } else {
        snprintf(b, sizeof(b), "notch off");
    }
    btn(x + 12, y, 136, bh, b, g.notch_hz != 0, 11, 0, 36, 40, 48);
    if (g.hp_hz) {
        snprintf(b, sizeof(b), "hp %dHz", g.hp_hz);
    } else {
        snprintf(b, sizeof(b), "hp off");
    }
    btn(x + 152, y, 136, bh, b, g.hp_hz != 0, 12, 0, 36, 40, 48);
    y += rh;
    btn(x + 12, y, 136, bh, g.grid ? "grid on" : "grid off", g.grid, 13, 0, 36, 40, 48);
    btn(x + 152, y, 136, bh, g.paused ? "PAUSED" : "live", !g.paused, 14, 0,
        g.paused ? 110 : 36, g.paused ? 50 : 40, g.paused ? 40 : 48);
    y += rh;
    btn(x + 12, y, 136, bh, g.show_uv ? "uV on" : "uV off", g.show_uv, 15, 0, 36, 40, 48);
    btn(x + 152, y, 136, bh, g.detrend ? "detrend" : "raw DC", g.detrend, 21, 0, 36, 40, 48);
    y += rh;
    snprintf(b, sizeof(b), "band %s", np_band_name(g.band));
    btn(x + 12, y, 136, bh, b, g.band != 0, 60, 0, 36, 40, 48);
    btn(x + 152, y, 136, bh, g.car ? "CAR on" : "CAR off", g.car, 61, 0, 36, 40, 48);
    y += rh;
    if (g.lp_hz) {
        snprintf(b, sizeof(b), "lp %dHz", g.lp_hz);
    } else {
        snprintf(b, sizeof(b), "lp off");
    }
    btn(x + 12, y, 136, bh, b, g.lp_hz != 0, 62, 0, 36, 40, 48);
    btn(x + 152, y, 136, bh, g.envelope ? "envelope" : "wave", g.envelope, 63, 0, 36, 40, 48);
    return y + rh;
}

static void draw_side(int x)
{
    int y = 8;
    int c;
    int bh = btnh(), rh = rowh();
    char live[48];
    const char *port = port_short();
    const char *bname = g.board == NP_BOARD_KNIGHT_IMU ? "8-ch + IMU" : "8-ch EXG";

    side_clamp();
    hit_in_body = 0;

    /* Pinned — always on the first screen. Tabs before chrome. */
    text(x + 12, y, "exg-c", 240, 242, 248, 2);
    if (g.connected) {
        if (g.board == NP_BOARD_KNIGHT_IMU) {
            float ax = 0, ay = 0, az = 0;
            pthread_mutex_lock(&g.ring.mu);
            if (g.ring.wr) {
                uint32_t w = (g.ring.wr - 1) % NP_RING;
                ax = g.ring.acc[0][w];
                ay = g.ring.acc[1][w];
                az = g.ring.acc[2][w];
            }
            pthread_mutex_unlock(&g.ring.mu);
            snprintf(live, sizeof(live), "%.1f %.1f %.1f", ax, ay, az);
        } else {
            snprintf(live, sizeof(live), "%.0f sps",
                     g.sps > 1.f ? g.sps : (float)NP_DEFAULT_SPS);
        }
        text(x + 90, y + 4, live, 180, 200, 120, 1);
    } else {
        text(x + 90, y + 4, "offline", 120, 128, 140, 1);
    }
    y += NP_TOUCH ? 24 : 20;
    btn(x + 12, y, 84, bh, "Main", g.tab == 0, 30, 0, g.tab == 0 ? 36 : 28, g.tab == 0 ? 50 : 32,
        44);
    btn(x + 100, y, 84, bh, "Cube", g.tab == 2, 36, 0, g.tab == 2 ? 70 : 28, g.tab == 2 ? 22 : 32,
        g.tab == 2 ? 32 : 44);
    btn(x + 188, y, 88, bh, "Settings", g.tab == 1, 31, 0, g.tab == 1 ? 36 : 28,
        g.tab == 1 ? 50 : 32, 44);
    y += rh;
    btn(x + 12, y, 168, bh, port, 1, 4, 0, 32, 36, 44);
    if (!g.connected) {
        btn(x + 184, y, 92, bh, "Connect", 1, 1, 0, 30, 110, 80);
    } else {
        btn(x + 184, y, 92, bh, "Disconnect", 1, 2, 0, 120, 40, 48);
    }
    y += rh;
    btn(x + 12, y, 168, bh, bname, 1, 5, 0, 32, 36, 44);
    btn(x + 184, y, 92, bh, g.recording ? "Stop CSV" : "CSV", g.recording, 3, 0,
        g.recording ? 110 : 36, g.recording ? 50 : 40, g.recording ? 40 : 52);
    y += rh;
    btn(x + 12, y, 168, bh, g.atom_on ? "Stop take" : "Take", g.atom_on, 64, 0,
        g.atom_on ? 110 : 36, g.atom_on ? 40 : 40, g.atom_on ? 40 : 48);
    btn(x + 184, y, 92, bh, "keep", 1, 65, 0, 36, 40, 48);
    y += rh;
    if (g.atom_on || g.atom_n || g.atom_ref_n) {
        char al[56];
        np_host_atom_line(al, (int)sizeof(al));
        text(x + 12, y, al, 200, 80, 90, 1);
        y += NP_TOUCH ? 18 : 14;
    }
    y += 2;
    side_pin_h = y;
    {
        SDL_Rect clip;
        clip.x = x;
        clip.y = side_pin_h;
        clip.w = sidew();
        clip.h = side_view() - side_pin_h;
        if (clip.h < 1) {
            clip.h = 1;
        }
        SDL_RenderSetClipRect(R, &clip);
    }
    hit_in_body = 1;
    y -= g.side_scroll;
    if (g.tab == 2) {
        char b[48];
        snprintf(b, sizeof(b), "SMX  %u ch  %us", (unsigned)g.smx.nch, g.smx.have);
        text(x + 12, y, b, NP_CUBE_CR, NP_CUBE_CG, NP_CUBE_CB, 1);
        y += 14;
        pthread_mutex_lock(&g.mu);
        snprintf(b, sizeof(b), "%s", g.cube_ack[0] ? g.cube_ack : "offer off");
        pthread_mutex_unlock(&g.mu);
        text(x + 12, y, b, g.cube_ok ? 80 : 160, g.cube_ok ? 200 : 120, g.cube_ok ? 120 : 80, 1);
        y += 16;
        btn(x + 12, y, 130, bh, "viz", g.cube_view == 0, 55, 0,
            g.cube_view == 0 ? 90 : 28, g.cube_view == 0 ? 16 : 32, g.cube_view == 0 ? 24 : 42);
        btn(x + 146, y, 130, bh, "map", g.cube_view == 1, 56, 0,
            g.cube_view == 1 ? 70 : 28, g.cube_view == 1 ? 28 : 32, g.cube_view == 1 ? 20 : 42);
        y += rh;
        if (g.cube_view == 0) {
            text(x + 12, y, "crimson  last sample  spin / +/-", 100, 108, 116, 1);
            y += 16;
            {
                char zb[24];
                snprintf(zb, sizeof(zb), "zoom %.1fx", (double)g.cube_zoom);
                btn(x + 12, y, 88, bh, "-", 0, 47, 0, 36, 40, 48);
                btn(x + 104, y, 80, bh, zb, 0, 0, 0, 28, 32, 40);
                btn(x + 188, y, 88, bh, "+", 0, 48, 0, 36, 40, 48);
            }
            y += rh;
            btn(x + 12, y, 130, bh, "front", 0, 38, 0, 32, 36, 44);
            btn(x + 146, y, 130, bh, "default 8", 0, 39, 0, 32, 36, 44);
            y += rh;
            snprintf(b, sizeof(b), "algo %s", np_algo_name(g.algo));
            btn(x + 12, y, sidew() - 24, bh, b, g.algo != 0, 44, 0, 36, 40, 48);
            y += rh;
            side_end(x, y);
            return;
        }
        text(x + 12, y, "ch, find site, Assign", 100, 108, 116, 1);
        y += 14;
        for (c = 0; c < NP_NCHAN; c++) {
            int col = c / 4, row = c % 4;
            int bx = x + 12 + col * 140;
            int by = y + row * rh;
            snprintf(b, sizeof(b), "%d %s", c + 1, g.elec[c].name[0] ? g.elec[c].name : "?");
            btn(bx, by, 132, bh, b, g.elec_sel == c, 37, c,
                g.elec_sel == c ? 80 : 28, g.elec_sel == c ? 20 : 32,
                g.elec_sel == c ? 34 : 42);
        }
        y += 4 * rh + 8;
        {
            char zb[24];
            snprintf(zb, sizeof(zb), "zoom %.1fx", (double)g.cube_zoom);
            btn(x + 12, y, 88, bh, "-", 0, 47, 0, 36, 40, 48);
            btn(x + 104, y, 80, bh, zb, 0, 0, 0, 28, 32, 40);
            btn(x + 188, y, 88, bh, "+", 0, 48, 0, 36, 40, 48);
        }
        y += rh;
        {
            int ix = 0, iy = 0, iz = 0;
            char sb[40];
            np_1010_ijk(g.site_focus, &ix, &iy, &iz);
            snprintf(sb, sizeof(sb), "%s  %d,%d,%d", np_1010_name(g.site_focus), ix, iy, iz);
            btn(x + 12, y, 44, bh, "<", 0, 49, 0, 36, 40, 48);
            btn(x + 60, y, 156, bh, sb, 1, 51, 0, 70, 28, 32);
            btn(x + 220, y, 56, bh, ">", 0, 50, 0, 36, 40, 48);
        }
        y += rh;
        btn(x + 12, y, sidew() - 24, bh, "Assign to selected ch", 0, 51, 0, 28, 80, 48);
        y += rh;
        {
            int vs = cube_virt_slot(g.virt_focus);
            char vb[40];
            if (vs >= 0) {
                snprintf(vb, sizeof(vb), "sensor %s  %u,%u,%u", g.smx.virt[vs].name,
                         g.smx.virt[vs].x, g.smx.virt[vs].y, g.smx.virt[vs].z);
            } else {
                snprintf(vb, sizeof(vb), "sensor  (none)");
            }
            btn(x + 12, y, 44, bh, "<", 0, 52, 0, 36, 40, 48);
            btn(x + 60, y, 156, bh, vb, 0, 0, 0, 28, 36, 42);
            btn(x + 220, y, 56, bh, ">", 0, 53, 0, 36, 40, 48);
        }
        y += rh;
        btn(x + 12, y, 130, bh, "front", 0, 38, 0, 32, 36, 44);
        btn(x + 146, y, 130, bh, "default 8", 0, 39, 0, 32, 36, 44);
        y += rh;
        btn(x + 12, y, 130, bh, "Save profile", 0, 41, 0, 28, 80, 48);
        btn(x + 146, y, 130, bh, "Load profile", 0, 42, 0, 28, 80, 48);
        y += rh;
        snprintf(b, sizeof(b), "algo %s", np_algo_name(g.algo));
        btn(x + 12, y, sidew() - 24, bh, b, g.algo != 0, 44, 0, 36, 40, 48);
        y += rh;
        side_end(x, y);
        return;
    }
    if (g.tab == 1) {
        char b[48];
        text(x + 12, y, "Profile  (name, then Save)", 140, 148, 160, 1);
        y += 14;
        fill(x + 12, y, sidew() - 24, bh, g.typing_prof ? 46 : 28, g.typing_prof ? 56 : 32,
             g.typing_prof ? 70 : 42);
        {
            char shown[NP_PROF_NAME + 2];
            if (g.prof[0]) {
                snprintf(shown, sizeof(shown), "%s%s", g.prof, g.typing_prof ? "_" : "");
            } else {
                snprintf(shown, sizeof(shown), "%s", g.typing_prof ? "_" : "e.g. motor");
            }
            text(x + 18, y + (bh - 7) / 2, shown, g.prof[0] ? 240 : 130, 240, 246, 1);
        }
        add_hit(x + 12, y, sidew() - 24, bh, 40, 0);
        y += rh + 2;
        btn(x + 12, y, 88, bh, "Save", 0, 41, 0, 28, 90, 52);
        btn(x + 104, y, 88, bh, "Load", 0, 42, 0, 28, 90, 52);
        btn(x + 196, y, 80, bh, g.nprof ? "next" : "none", 0, 43, 0, 36, 40, 48);
        y += rh + 2;
        text(x + 12, y, "keeps UI, gain, filters, sites", 100, 108, 116, 1);
        y += 16;
        snprintf(b, sizeof(b), "algo %s", np_algo_name(g.algo));
        btn(x + 12, y, sidew() - 24, bh, b, g.algo != 0, 44, 0, 36, 40, 48);
        y += rh + 2;
        text(x + 12, y, "UI", 140, 148, 160, 1);
        y += 14;
        snprintf(b, sizeof(b), "UI %.1fx", (double)ui_f());
        btn(x + 12, y, 136, bh, b, 1, 32, 0, 36, 40, 48);
        if (NP_TOUCH) {
            btn(x + 152, y, 136, bh, "fullscreen", 1, 0, 0, 36, 40, 48);
        } else {
            snprintf(b, sizeof(b), "%dx%d", g.pref_w, g.pref_h);
            btn(x + 152, y, 136, bh, b, 1, 33, 0, 36, 40, 48);
        }
        y += rh + 2;
        text(x + 12, y, "Channel colors (click)", 140, 148, 160, 1);
        y += 14;
        for (c = 0; c < NP_NCHAN; c++) {
            int col = c / 4, row = c % 4;
            int bx = x + 12 + col * 140;
            int by = y + row * rh;
            snprintf(b, sizeof(b), "%s", g.elec[c].name[0] ? g.elec[c].name : "?");
            btn(bx, by, 128, bh, b, 1, 34, c, g.chrgb[c][0] / 3, g.chrgb[c][1] / 3,
                g.chrgb[c][2] / 3);
        }
        y += 4 * rh + 8;
        text(x + 12, y, NP_TOUCH ? "app files / exg-c/profiles" : "~/.config/exg-c/profiles/",
             100, 108, 116, 1);
        y += 16;
        side_end(x, y);
        return;
    }
    y = draw_view_block(x, y);
    y = draw_channels(x, y);
    side_end(x, y);
}

static void draw_learn(int x, int y, int w, int h)
{
    char lab[80];
    int i, bx, bw, nshow;
    uint32_t now = SDL_GetTicks();
    int holding = 0, left_ms = 0;
    int saved_ago = g.saved_t0 ? (int)(now - g.saved_t0) : 99999;

    if (g.rec_t0) {
        int dt = (int)(now - g.rec_t0);
        if (dt < (int)REC_MS) {
            holding = 1;
            left_ms = (int)REC_MS - dt;
        }
    }

    fill(x, y, w, h, 12, 14, 18);
    if (NP_TOUCH) {
        int bh = 36;
        text(x + 6, y + 8, "LEARN", 200, 210, 220, 1);
        fill(x + 64, y + 4, 180, bh, g.typing ? 46 : 28, g.typing ? 56 : 32, g.typing ? 70 : 42);
        {
            char shown[NPL_NAME + 2];
            if (g.namebuf[0]) {
                snprintf(shown, sizeof(shown), "%s%s", g.namebuf, g.typing ? "_" : "");
            } else {
                snprintf(shown, sizeof(shown), "%s", g.typing ? "_" : "type a name");
            }
            text(x + 70, y + 4 + (bh - 7) / 2, shown, g.namebuf[0] ? 240 : 130, 240, 246, 1);
        }
        add_hit(x + 64, y + 4, 180, bh, 16, 0);
        if (holding) {
            snprintf(lab, sizeof(lab), "HOLD %.1fs", left_ms / 1000.f);
            btn(x + 250, y + 4, 120, bh, lab, 1, 17, 0, 120, 70, 30);
        } else {
            btn(x + 250, y + 4, 120, bh, g.namebuf[0] ? "Record matrix" : "need name",
                g.namebuf[0], 17, 0, g.namebuf[0] ? 28 : 36, g.namebuf[0] ? 100 : 42,
                g.namebuf[0] ? 70 : 50);
        }
        btn(x + 376, y + 4, 70, bh, g.learn.match ? "MATCH" : "match", g.learn.match, 18, 0,
            g.learn.match ? 28 : 40, g.learn.match ? 90 : 42, g.learn.match ? 70 : 50);
        btn(x + 452, y + 4, 48, bh, "del", 0, 19, 0, 90, 42, 46);
        btn(x + 6, y + 46, 56, 32, "NOISE", g.cal_arm || g.cal.have, 25, 0,
            g.cal_arm ? 110 : 32, g.cal_arm ? 70 : 36, 40);
        btn(x + 66, y + 46, 36, 32, "OK", g.cal_arm, 26, 0, g.cal_arm ? 28 : 36,
            g.cal_arm ? 100 : 38, g.cal_arm ? 70 : 46);
        btn(x + 106, y + 46, 56, 32, "CALM", g.calm.have, 35, 0, g.calm.have ? 28 : 36,
            g.calm.have ? 80 : 38, g.calm.have ? 70 : 46);
        btn(x + 166, y + 46, 48, 32, g.cal_cut ? "CLN" : "cln", g.cal_cut && g.cal.have, 27, 0,
            g.cal_cut ? 28 : 36, g.cal_cut ? 80 : 38, g.cal_cut ? 70 : 46);
        if (g.cal_arm) {
            text(x + 222, y + 56, "desk / off, then OK", 230, 190, 90, 1);
        } else if (g.cal.have && g.calm.have) {
            snprintf(lab, sizeof(lab), "noise %.0fHz  calm %.0f uV%s", g.cal_hz, g.calm.rms[0],
                     g.cal_cut ? "  CLEAN" : "");
            text(x + 222, y + 56, lab, 140, 180, 150, 1);
        } else if (g.cal.have) {
            text(x + 222, y + 56, "wear headset, sit still, CALM", 100, 120, 110, 1);
        } else {
            text(x + 222, y + 56, "NOISE = desk plate   then CALM on head", 90, 96, 104, 1);
        }
        nshow = g.learn.n > 8 ? 8 : g.learn.n;
        if (nshow > 0) {
            bw = (w - 12) / nshow;
            if (bw > 140) {
                bw = 140;
            }
            if (bw < 72) {
                bw = 72;
            }
            for (i = 0; i < nshow; i++) {
                int on = (i == g.learn.sel);
                int hit = (g.learn.match && i == g.learn.best && g.learn.score[i] > 0.55f);
                int bar, pct;
                bx = x + 6 + i * bw;
                fill(bx, y + 84, bw - 5, 40, on ? 42 : 22, hit ? 50 : (on ? 50 : 26),
                     hit ? 42 : 32);
                text(bx + 4, y + 88, g.learn.s[i].name, 230, 232, 236, 1);
                pct = (int)(g.learn.score[i] * 100.f);
                snprintf(lab, sizeof(lab), "%d%%  c%d", pct,
                         (int)(g.learn.score_cube[i] * 100.f));
                text(bx + 4, y + 100, lab, hit ? 80 : 160, hit ? 230 : 180, hit ? 140 : 150, 1);
                bar = (int)(g.learn.score[i] * (bw - 14));
                if (bar < 2) {
                    bar = 2;
                }
                fill(bx + 4, y + 114, bar, 6, hit ? 60 : 50, hit ? 190 : 90, 90);
                add_hit(bx, y + 84, bw - 5, 40, 20, i);
            }
        } else {
            text(x + 8, y + 90, "no samples yet", 90, 96, 104, 1);
        }
        return;
    }
    text(x + 6, y + 5, "LEARN", 200, 210, 220, 1);

    text(x + 52, y + 6, "1.", 100, 140, 180, 1);
    fill(x + 70, y + 3, 168, 20, g.typing ? 46 : 28, g.typing ? 56 : 32, g.typing ? 70 : 42);
    {
        char shown[NPL_NAME + 2];
        if (g.namebuf[0]) {
            snprintf(shown, sizeof(shown), "%s%s", g.namebuf, g.typing ? "_" : "");
        } else {
            snprintf(shown, sizeof(shown), "%s", g.typing ? "_" : "type a name");
        }
        text(x + 76, y + 8, shown, g.namebuf[0] ? 240 : 130, 240, 246, 1);
    }
    add_hit(x + 70, y + 3, 168, 20, 16, 0);

    text(x + 246, y + 6, "2.", 100, 140, 180, 1);
    if (holding) {
        snprintf(lab, sizeof(lab), "HOLD %.1fs", left_ms / 1000.f);
        btn(x + 264, y + 2, 100, 22, lab, 1, 17, 0, 120, 70, 30);
    } else {
        btn(x + 264, y + 2, 100, 22, g.namebuf[0] ? "Record matrix" : "need name", g.namebuf[0], 17,
            0, g.namebuf[0] ? 28 : 36, g.namebuf[0] ? 100 : 42, g.namebuf[0] ? 70 : 50);
    }
    btn(x + 370, y + 2, 58, 22, g.learn.match ? "MATCH" : "match", g.learn.match, 18, 0,
        g.learn.match ? 28 : 40, g.learn.match ? 90 : 42, g.learn.match ? 70 : 50);
    btn(x + 432, y + 2, 40, 22, "del", 0, 19, 0, 90, 42, 46);

    if (holding) {
        int barw = w - 490;
        int fillw;
        if (barw < 40) {
            barw = 40;
        }
        fillw = (int)((REC_MS - left_ms) * (barw - 4) / REC_MS);
        fill(x + 480, y + 6, barw, 14, 40, 32, 20);
        fill(x + 482, y + 8, fillw > 0 ? fillw : 1, 10, 230, 160, 50);
        text(x + 484, y + 8, "blink or clench", 20, 16, 10, 1);
    } else if (saved_ago < 2500) {
        snprintf(lab, sizeof(lab), "saved '%s'  -  again or watch match", g.namebuf);
        text(x + 480, y + 8, lab, 80, 220, 140, 1);
    } else if (g.learn.best >= 0 && g.learn.match && g.learn.n) {
        int pct = (int)(g.learn.score[g.learn.best] * 100.f);
        int cpct = (int)(g.learn.score_cube[g.learn.best] * 100.f);
        snprintf(lab, sizeof(lab), "now: %s  %d%%  cube %d%%", g.learn.s[g.learn.best].name, pct,
                 cpct);
        text(x + 480, y + 8, lab, pct >= 55 ? 80 : 200, pct >= 55 ? 230 : 170, 120, 1);
    } else if (!g.namebuf[0]) {
        text(x + 480, y + 8, "name, then Record — blink or clench", 120, 128, 140, 1);
    } else {
        char id[40];
        id_label(id, sizeof(id));
        text(x + 480, y + 8, id, 140, 180, 150, 1);
    }

    nshow = g.learn.n > 8 ? 8 : g.learn.n;
    if (g.cal.have && g.cal_cut && nshow <= 0) {
        float rr = 0.f;
        int nchg = live_vs_cal(&rr);
        if (nchg == 0) {
            text(x + 8, y + 32, "still at baseline", 210, 150, 80, 1);
        } else if (nchg > 0) {
            snprintf(lab, sizeof(lab), "%d ch left baseline (%.2fx)", nchg, rr);
            text(x + 8, y + 32, lab, 80, 210, 140, 1);
        }
    } else if (nshow <= 0) {
        text(x + 8, y + 32, "no samples yet", 90, 96, 104, 1);
    }
    if (nshow > 0) {
        bw = (w - 12) / nshow;
        if (bw > 140) {
            bw = 140;
        }
        if (bw < 72) {
            bw = 72;
        }
        for (i = 0; i < nshow; i++) {
            int on = (i == g.learn.sel);
            int hit = (g.learn.match && i == g.learn.best && g.learn.score[i] > 0.55f);
            int bar, pct;
            bx = x + 6 + i * bw;
            fill(bx, y + 28, bw - 5, 36, on ? 42 : 22, hit ? 50 : (on ? 50 : 26),
                 hit ? 42 : 32);
            text(bx + 4, y + 31, g.learn.s[i].name, 230, 232, 236, 1);
            pct = (int)(g.learn.score[i] * 100.f);
            snprintf(lab, sizeof(lab), "%d%%  c%d", pct, (int)(g.learn.score_cube[i] * 100.f));
            text(bx + 4, y + 42, lab, hit ? 80 : 160, hit ? 230 : 180, hit ? 140 : 150, 1);
            bar = (int)(g.learn.score[i] * (bw - 14));
            if (bar < 2) {
                bar = 2;
            }
            fill(bx + 4, y + 54, bar, 6, hit ? 60 : 50, hit ? 190 : 90, 90);
            add_hit(bx, y + 28, bw - 5, 36, 20, i);
        }
    }

    btn(x + 6, y + h - 22, 48, 18, "NOISE", g.cal_arm || g.cal.have, 25, 0,
        g.cal_arm ? 110 : 32, g.cal_arm ? 70 : 36, 40);
    btn(x + 58, y + h - 22, 28, 18, "OK", g.cal_arm, 26, 0, g.cal_arm ? 28 : 36,
        g.cal_arm ? 100 : 38, g.cal_arm ? 70 : 46);
    btn(x + 90, y + h - 22, 44, 18, "CALM", g.calm.have, 35, 0, g.calm.have ? 28 : 36,
        g.calm.have ? 80 : 38, g.calm.have ? 70 : 46);
    btn(x + 138, y + h - 22, 40, 18, g.cal_cut ? "CLN" : "cln",
        g.cal_cut && g.cal.have, 27, 0, g.cal_cut ? 28 : 36, g.cal_cut ? 80 : 38,
        g.cal_cut ? 70 : 46);
    if (g.cal_arm) {
        text(x + 186, y + h - 18, "desk / off, then OK", 230, 190, 90, 1);
    } else if (g.cal.have && g.calm.have) {
        snprintf(lab, sizeof(lab), "noise %.0fHz  calm %.0f uV%s", g.cal_hz,
                 g.calm.rms[0], g.cal_cut ? "  CLEAN" : "");
        text(x + 186, y + h - 18, lab, 140, 180, 150, 1);
    } else if (g.cal.have) {
        text(x + 186, y + h - 18, "wear headset, sit still, CALM", 100, 120, 110, 1);
    } else {
        text(x + 186, y + h - 18, "NOISE = desk plate   then CALM on head", 90, 96, 104, 1);
    }
}


static void refresh_title(void)
{
    static char last[96];
    char t[96];
    if (!Win) {
        return;
    }
    if (g.connected) {
        snprintf(t, sizeof(t), "exg-c  %s  %.0f sps", port_short(),
                 g.sps > 1.f ? g.sps : (float)NP_DEFAULT_SPS);
    } else {
        snprintf(t, sizeof(t), "exg-c");
    }
    if (strcmp(last, t) != 0) {
        snprintf(last, sizeof(last), "%s", t);
        SDL_SetWindowTitle(Win, t);
    }
}

static void draw_status(void)
{
    char st[240];
    uint64_t tot = 0;
    uint32_t good = 0, bad = 0;
    uint8_t lp = 0, ln = 0;
    np_ring_stats(&g.ring, &tot, &good, &bad);
    np_ring_loff(&g.ring, &lp, &ln);
    pthread_mutex_lock(&g.mu);
    if (g.connected) {
        snprintf(st, sizeof(st),
                 "%s   %.0f sps   %llu frames   bad %u   %s   loff %02X/%02X%s",
                 g.status, g.sps > 1.f ? g.sps : (float)NP_DEFAULT_SPS,
                 (unsigned long long)tot, bad, g.parser.locked ? "lock" : "sync", lp, ln,
                 g.paused ? "   PAUSE" : "");
    } else {
        snprintf(st, sizeof(st), "%s", g.status);
    }
    pthread_mutex_unlock(&g.mu);
    fill(0, win_h - statush(), win_w, statush(), 16, 18, 22);
    text(12, win_h - statush() + (statush() - 8) / 2, st, g.status_ok ? 80 : 240,
         g.status_ok ? 210 : 90,
         g.status_ok ? 120 : 90, 1);
    refresh_title();
}

static void next_gain(int ch)
{
    int i;
    for (i = 0; i < NP_NGAINS; i++) {
        if (NP_GAINS[i] == g.gain[ch]) {
            g.gain[ch] = NP_GAINS[(i + 1) % NP_NGAINS];
            break;
        }
    }
}

static void click(int x, int y)
{
    int i;
    for (i = 0; i < nhits; i++) {
        if (!inside(&hits[i].r, x, y)) {
            continue;
        }
        if (hits[i].r.x >= win_w - sidew() && !in_side(x, y)) {
            continue;
        }
        switch (hits[i].kind) {
        case 1:
            do_connect();
            break;
        case 2:
            do_disconnect();
            break;
        case 3:
            toggle_record();
            break;
        case 4:
            g.nports = np_list_ports(g.ports, NP_MAX_PORTS);
            if (g.nports) {
                g.port_i = (g.port_i + 1) % g.nports;
            }
            break;
        case 5:
            if (!g.connected) {
                g.board = g.board == NP_BOARD_KNIGHT ? NP_BOARD_KNIGHT_IMU : NP_BOARD_KNIGHT;
            }
            break;
        case 6:
            g.active[hits[i].ch] = !g.active[hits[i].ch];
            if (g.connected) {
                if (g.active[hits[i].ch]) {
                    cmd_push(CMD_CHON, hits[i].ch + 1, g.gain[hits[i].ch]);
                } else {
                    cmd_push(CMD_CHOFF, hits[i].ch + 1, 0);
                }
            }
            break;
        case 7:
            g.rld[hits[i].ch] = !g.rld[hits[i].ch];
            if (g.connected) {
                cmd_push(g.rld[hits[i].ch] ? CMD_RLDADD : CMD_RLDRM, hits[i].ch + 1, 0);
            }
            break;
        case 8:
            next_gain(hits[i].ch);
            pthread_mutex_lock(&g.parse_mu);
            np_parser_set_gain(&g.parser, hits[i].ch + 1, g.gain[hits[i].ch]);
            pthread_mutex_unlock(&g.parse_mu);
            if (g.connected && g.active[hits[i].ch]) {
                cmd_push(CMD_CHON, hits[i].ch + 1, g.gain[hits[i].ch]);
            }
            cfg_save();
            break;
        case 9:
            {
                int k;
                for (k = 0; k < NWINS; k++) {
                    if (WIN_S[k] == g.window_s) {
                        g.window_s = WIN_S[(k + 1) % NWINS];
                        break;
                    }
                }
                cfg_save();
            }
            break;
        case 10:
            if (g.og) {
                g.og = 0;
                g.autoscale = 0;
                g.scale_uv = 200;
            } else if (g.autoscale) {
                g.autoscale = 0;
                g.og = 1;
            } else {
                int k, found = 0;
                for (k = 0; k < NSCALE; k++) {
                    if (SCALE_UV[k] == g.scale_uv) {
                        if (k + 1 == NSCALE) {
                            g.autoscale = 1;
                        } else {
                            g.scale_uv = SCALE_UV[k + 1];
                        }
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    g.autoscale = 1;
                }
            }
            cfg_save();
            break;
        case 11:
            g.notch_hz = g.notch_hz == 0 ? 50 : (g.notch_hz == 50 ? 60 : (g.notch_hz == 60 ? -1 : 0));
            filt_reset();
            cfg_save();
            break;
        case 12:
            np_host_cycle_hp();
            break;
        case 60:
            np_host_cycle_band();
            break;
        case 61:
            np_host_toggle_car();
            break;
        case 62:
            np_host_cycle_lp();
            break;
        case 63:
            np_host_toggle_envelope();
            break;
        case 64:
            if (g.atom_on) {
                if (np_host_atom_stop() >= 1 && g.namebuf[0]) {
                    np_host_atom_save();
                } else {
                    np_host_atom_discard();
                }
            } else {
                np_host_atom_start();
            }
            break;
        case 65:
            np_host_atom_save();
            break;
        case 66:
            np_host_atom_load();
            break;
        case 13:
            g.grid = !g.grid;
            cfg_save();
            break;
        case 14:
            g.paused = !g.paused;
            break;
        case 15:
            g.show_uv = !g.show_uv;
            cfg_save();
            break;
        case 21:
            g.detrend = !g.detrend;
            cfg_save();
            break;
        case 16:
            g.typing_prof = 0;
            typing_set(1);
            return;
        case 40:
            g.typing_prof = 1;
            typing_set(1);
            return;
        case 41:
            typing_set(0);
            prof_save();
            break;
        case 42:
            typing_set(0);
            prof_load();
            break;
        case 43:
            typing_set(0);
            prof_cycle();
            break;
        case 44:
            np_host_cycle_algo();
            break;
        case 17:
            typing_set(0);
            if (g.rec_t0) {
                g.rec_t0 = 0;
                set_status(1, "record cancelled");
            } else {
                learn_start_hold();
            }
            break;
        case 18:
            g.learn.match = !g.learn.match;
            if (!g.learn.match) {
                g.learn.best = -1;
            }
            break;
        case 19:
            if (g.learn.sel >= 0) {
                char gone[NPL_NAME];
                snprintf(gone, sizeof(gone), "%s", g.learn.s[g.learn.sel].name);
                npl_del(&g.learn, g.learn.sel);
                learn_persist();
                set_status(1, "deleted '%s'", gone);
            }
            break;
        case 20:
            g.learn.sel = hits[i].ch;
            snprintf(g.namebuf, sizeof(g.namebuf), "%s", g.learn.s[g.learn.sel].name);
            break;
        case 25:
            g.cal_arm = 1;
            set_status(1, "NOISE: board on desk / headset off, then OK");
            break;
        case 26:
            if (!g.cal_arm) {
                set_status(0, "click CAL first, then remove headset, then OK");
            } else {
                cal_capture();
                g.cal_cut = 1;
                cfg_save();
            }
            break;
        case 27:
            g.cal_cut = !g.cal_cut;
            cfg_save();
            set_status(1, g.cal_cut ? "CLEAN on - noise+calm removed" : "CLEAN off - raw plot");
            break;
        case 28:
            wear_check();
            break;
        case 35:
            calm_capture();
            break;
        case 30:
            g.tab = 0;
            g.side_scroll = 0;
            break;
        case 31:
            g.tab = 1;
            g.side_scroll = 0;
            break;
        case 36:
            g.tab = 2;
            g.side_scroll = 0;
            break;
        case 45:
            g.side_scroll -= 48;
            side_clamp();
            break;
        case 46:
            g.side_scroll += 48;
            side_clamp();
            break;
        case 37:
            g.elec_sel = hits[i].ch;
            if (g.elec[g.elec_sel].site >= 0) {
                g.site_focus = g.elec[g.elec_sel].site;
            }
            set_status(1, "ch%d @ %s", g.elec_sel + 1,
                       g.elec[g.elec_sel].name[0] ? g.elec[g.elec_sel].name : "?");
            break;
        case 38:
            g.cube_yaw = 0.55f;
            g.cube_pitch = 0.40f;
            set_status(1, "front");
            break;
        case 39:
            np_elec_default(g.elec);
            cfg_save();
            set_status(1, "default  Fp1 Fp2 C3 C4 P3 P4 O1 O2");
            break;
        case 47:
            cube_zoom_by(-1);
            break;
        case 48:
            cube_zoom_by(1);
            break;
        case 49:
            cube_site_by(-1);
            break;
        case 50:
            cube_site_by(1);
            break;
        case 51:
            cube_assign_focus();
            break;
        case 52:
            cube_virt_by(-1);
            break;
        case 55:
            g.cube_view = 0;
            cfg_save();
            set_status(1, "viz  crimson lattice");
            break;
        case 56:
            g.cube_view = 1;
            cfg_save();
            set_status(1, "map  assign 10-10 sites");
            break;
        case 53:
            cube_virt_by(1);
            {
                int vs = cube_virt_slot(g.virt_focus);
                if (vs >= 0) {
                    set_status(1, "sensor %s  cell %u,%u,%u", g.smx.virt[vs].name,
                               g.smx.virt[vs].x, g.smx.virt[vs].y, g.smx.virt[vs].z);
                }
            }
            break;
        case 32:
            if (g.ui_scale == 10) {
                g.ui_scale = 15;
            } else if (g.ui_scale == 15) {
                g.ui_scale = 20;
            } else {
                g.ui_scale = 10;
            }
            cfg_save();
            break;
        case 33:
            {
                int k, found = 0;
                for (k = 0; k < NWINPREF; k++) {
                    if (WINPREF[k][0] == g.pref_w && WINPREF[k][1] == g.pref_h) {
                        k = (k + 1) % NWINPREF;
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    k = 0;
                }
                g.pref_w = WINPREF[k][0];
                g.pref_h = WINPREF[k][1];
                if (Win) {
                    SDL_SetWindowSize(Win, g.pref_w, g.pref_h);
                }
                cfg_save();
            }
            break;
        case 34:
            chcol_cycle(hits[i].ch);
            cfg_save();
            break;
        default:
            break;
        }
        return;
    }
    typing_set(0);
}

static void frame(void)
{
    int plot_w = win_w - sidew() - 24;
    int wave_h = win_h - WAVE_TOP - learnh() - ffth() - 10 - statush();
    if (wave_h < NP_NCHAN * 28) {
        wave_h = NP_NCHAN * 28;
    }
    nhits = 0;
    smx_tick();
    atom_tick();
    SDL_SetRenderDrawColor(R, 22, 24, 30, 255);
    SDL_RenderClear(R);
    fill(0, 0, win_w, win_h, 22, 24, 30);
    fill(win_w - sidew(), 0, sidew(), win_h, 26, 28, 34);
    if (g.tab == 2) {
        present_cube(12, WAVE_TOP, plot_w, win_h - WAVE_TOP - statush() - 8);
    } else {
        draw_waves(12, WAVE_TOP, plot_w, wave_h);
        draw_learn(12, WAVE_TOP + wave_h + 4, plot_w, learnh());
        draw_fft(12, WAVE_TOP + wave_h + learnh() + 8, plot_w, ffth());
    }
    draw_side(win_w - sidew());
    draw_status();
}

static int cube_need_bake(int w, int h)
{
    int c;
    if (!cube_tex || cube_tw != w || cube_th != h) {
        return 1;
    }
    if (cube_baked_seq != g.smx.seq) {
        return 1;
    }
    if (cube_baked_yaw != g.cube_yaw || cube_baked_pitch != g.cube_pitch ||
        cube_baked_zoom != g.cube_zoom) {
        if (s_cube_drag == 1 && cube_bake_t && SDL_GetTicks() - cube_bake_t < 80) {
            return 0;
        }
        return 1;
    }
    if (cube_baked_site != g.site_focus || cube_baked_virt != g.virt_focus ||
        cube_baked_sel != g.elec_sel || cube_baked_view != g.cube_view) {
        return 1;
    }
    for (c = 0; c < NP_NCHAN; c++) {
        if (cube_baked_sites[c] != g.elec[c].site) {
            return 1;
        }
    }
    return 0;
}

static void cube_remember(int w, int h)
{
    int c;
    cube_tw = w;
    cube_th = h;
    cube_baked_seq = g.smx.seq;
    cube_baked_yaw = g.cube_yaw;
    cube_baked_pitch = g.cube_pitch;
    cube_baked_zoom = g.cube_zoom;
    cube_baked_site = g.site_focus;
    cube_baked_virt = g.virt_focus;
    cube_baked_sel = g.elec_sel;
    cube_baked_view = g.cube_view;
    for (c = 0; c < NP_NCHAN; c++) {
        cube_baked_sites[c] = g.elec[c].site;
    }
}

/* Algocube is sample SoT. Bake on new SMX second or view change, blit the rest. */
static void present_cube(int x, int y, int w, int h)
{
    if (w < 8 || h < 8) {
        return;
    }
    /* Viz levitates — draw live. Map stays a baked texture. */
    if (g.cube_view == 0) {
        s_cube_px = 0;
        s_cube_py = 0;
        draw_cube(x, y, w, h);
        return;
    }
    if (cube_need_bake(w, h)) {
        if (cube_tex && (cube_tw != w || cube_th != h)) {
            SDL_DestroyTexture(cube_tex);
            cube_tex = NULL;
        }
        if (!cube_tex) {
            cube_tex = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
                                         w, h);
        }
        if (cube_tex && SDL_SetRenderTarget(R, cube_tex) != 0) {
            SDL_DestroyTexture(cube_tex);
            cube_tex = NULL;
        }
        if (cube_tex) {
            float s = ui_f();
            SDL_RenderSetScale(R, 1.f, 1.f);
            SDL_RenderSetClipRect(R, NULL);
            draw_cube(0, 0, w, h);
            SDL_SetRenderTarget(R, NULL);
            SDL_RenderSetScale(R, s, s);
            s_cube_px = x;
            s_cube_py = y;
            cube_remember(w, h);
            cube_bake_t = SDL_GetTicks();
        } else {
            s_cube_px = 0;
            s_cube_py = 0;
            draw_cube(x, y, w, h);
            return;
        }
    }
    {
        SDL_Rect dst;
        dst.x = x;
        dst.y = y;
        dst.w = w;
        dst.h = h;
        SDL_RenderCopy(R, cube_tex, NULL, &dst);
    }
}

#ifndef NP_ANDROID_UI
static int gfx_make_renderer(SDL_Window *win)
{
    if (cube_tex) {
        SDL_DestroyTexture(cube_tex);
        cube_tex = NULL;
    }
    if (R) {
        SDL_DestroyRenderer(R);
        R = NULL;
    }
    if (!win) {
        return -1;
    }
    R = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!R) {
        NP_ALOG("accel renderer failed: %s", SDL_GetError());
        R = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!R) {
        NP_ALOG("software renderer failed: %s", SDL_GetError());
        return -1;
    }
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    NP_ALOG("renderer ok");
    return 0;
}

static int run_gui(void)
{
    SDL_Window *win;
    SDL_Event ev;
    int live = 1;

#ifdef __ANDROID__
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
#else
    setenv("SDL_VIDEODRIVER", "x11", 0);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    SDL_SetHint("SDL_RENDER_SCALE_QUALITY", "0");
#endif
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
#ifdef __ANDROID__
    win = SDL_CreateWindow("exg-c", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 0, 0,
                           SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE |
                               SDL_WINDOW_FULLSCREEN_DESKTOP);
    {
        int pw = 1280, ph = 720;
        if (win) {
            SDL_GetWindowSize(win, &pw, &ph);
        }
        win_w = pw;
        win_h = ph;
        g.pref_w = pw;
        g.pref_h = ph;
    }
#else
    win = SDL_CreateWindow("exg-c", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           g.pref_w, g.pref_h, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    win_w = g.pref_w;
    win_h = g.pref_h;
#endif
    Win = win;
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    if (gfx_make_renderer(win) != 0) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    g.nports = np_list_ports(g.ports, NP_MAX_PORTS);
    if (g.nports > 0) {
        do_connect();
    } else {
        set_status(1, "idle - plug board, click Connect");
    }

    while (live) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                live = 0;
#ifdef __ANDROID__
            } else if (ev.type == SDL_RENDER_DEVICE_RESET) {
                /* Only a lost GL context needs a new renderer.
                 * SIZE_CHANGED/FOCUS during USB connect was wiping the
                 * surface and leaving a black screen. */
                gfx_make_renderer(win);
#endif
            } else if (ev.type == SDL_TEXTINPUT && g.typing) {
                const char *s = ev.text.text;
                char *dst = g.typing_prof ? g.prof : g.namebuf;
                int cap = g.typing_prof ? NP_PROF_NAME : NPL_NAME;
                int n = (int)strlen(dst);
                while (*s && n < cap - 1) {
                    unsigned char c = (unsigned char)*s++;
                    if (g.typing_prof) {
                        if (isalnum(c) || c == '-' || c == '_') {
                            dst[n++] = (char)c;
                        }
                    } else if (c >= 32 && c < 127 && c != '/' && c != '\\') {
                        dst[n++] = (char)c;
                    }
                }
                dst[n] = 0;
            } else if (ev.type == SDL_KEYDOWN) {
                int k = ev.key.keysym.sym;
                if (g.typing) {
                    char *dst = g.typing_prof ? g.prof : g.namebuf;
                    if (k == SDLK_ESCAPE) {
                        typing_set(0);
                    } else if (k == SDLK_RETURN) {
                        int was_prof = g.typing_prof;
                        typing_set(0);
                        if (was_prof) {
                            prof_save();
                        } else {
                            learn_start_hold();
                        }
                    } else if (k == SDLK_BACKSPACE) {
                        int n = (int)strlen(dst);
                        if (n > 0) {
                            dst[n - 1] = 0;
                        }
                    }
                } else if (k == SDLK_ESCAPE || k == SDLK_q) {
                    live = 0;
                } else if (k == SDLK_c) {
                    do_connect();
                } else if (k == SDLK_d) {
                    do_disconnect();
                } else if (k == SDLK_r) {
                    toggle_record();
                } else if (k == SDLK_SPACE) {
                    g.paused = !g.paused;
                } else if (k == SDLK_TAB) {
                    g.nports = np_list_ports(g.ports, NP_MAX_PORTS);
                    if (g.nports) {
                        g.port_i = (g.port_i + 1) % g.nports;
                    }
                } else if (k == SDLK_m) {
                    g.tab = 0;
                    g.side_scroll = 0;
                } else if (k == SDLK_s) {
                    g.tab = 1;
                    g.side_scroll = 0;
                } else if (k == SDLK_b) {
                    g.tab = 2;
                    g.side_scroll = 0;
                } else if (g.tab == 2 && k == SDLK_v) {
                    g.cube_view = g.cube_view ? 0 : 1;
                    cfg_save();
                    set_status(1, g.cube_view ? "map  assign 10-10 sites"
                                              : "viz  crimson lattice");
                } else if (k == SDLK_UP || k == SDLK_PAGEUP) {
                    g.side_scroll -= 48;
                    side_clamp();
                } else if (k == SDLK_DOWN || k == SDLK_PAGEDOWN) {
                    g.side_scroll += 48;
                    side_clamp();
                } else if (g.tab == 2 && (k == SDLK_MINUS || k == SDLK_KP_MINUS)) {
                    cube_zoom_by(-1);
                } else if (g.tab == 2 &&
                           (k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS)) {
                    cube_zoom_by(1);
                } else if (g.tab == 2 && k == SDLK_LEFT) {
                    cube_site_by(-1);
                } else if (g.tab == 2 && k == SDLK_RIGHT) {
                    cube_site_by(1);
                } else if (g.tab == 2 && k == SDLK_RETURN) {
                    cube_assign_focus();
                } else if (g.tab == 2 && k == SDLK_COMMA) {
                    cube_virt_by(-1);
                } else if (g.tab == 2 && k == SDLK_PERIOD) {
                    cube_virt_by(1);
                } else if (k >= SDLK_1 && k <= SDLK_8) {
                    int ch = k - SDLK_1;
                    if (g.tab == 2) {
                        g.elec_sel = ch;
                        if (g.elec[ch].site >= 0) {
                            g.site_focus = g.elec[ch].site;
                        }
                        set_status(1, "ch%d %s", ch + 1,
                                   g.elec[ch].name[0] ? g.elec[ch].name : "?");
                    } else {
                        g.active[ch] = !g.active[ch];
                        if (g.connected) {
                            cmd_push(g.active[ch] ? CMD_CHON : CMD_CHOFF, ch + 1,
                                     g.gain[ch]);
                        }
                    }
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float s = ui_f();
                int mx = (int)(ev.button.x / s), my = (int)(ev.button.y / s);
                int hi, on_hit = 0;
                for (hi = 0; hi < nhits; hi++) {
                    if (inside(&hits[hi].r, mx, my)) {
                        on_hit = 1;
                        break;
                    }
                }
                if (on_hit) {
                    click(mx, my);
                } else if (!cube_pointer_down(mx, my)) {
                    click(mx, my);
                }
            } else if (ev.type == SDL_MOUSEMOTION) {
                float s = ui_f();
                cube_pointer_move((int)(ev.motion.x / s), (int)(ev.motion.y / s));
            } else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
                cube_pointer_up();
            } else if (ev.type == SDL_MOUSEWHEEL) {
                float s = ui_f();
                int mx, my, dy;
                SDL_GetMouseState(&mx, &my);
                mx = (int)(mx / s);
                my = (int)(my / s);
                dy = ev.wheel.y;
                if (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                    dy = -dy;
                }
                if (mx >= win_w - sidew()) {
                    g.side_scroll -= dy * 36;
                    side_clamp();
                } else if (g.tab == 2 && cube_in_spin(mx, my)) {
                    cube_zoom_by(dy);
                }
            }
        }
        learn_tick();
        if (g.connected && !g.en_running) {
            uint64_t tot = 0;
            uint32_t now = SDL_GetTicks();
            np_ring_stats(&g.ring, &tot, NULL, NULL);
            if (tot != g.stall_tot) {
                g.stall_tot = tot;
                g.stall_t = now;
                g.stall_n = 0;
                if (g.recover_n && tot > 50) {
                    g.recover_n = 0;
                }
            } else if (g.stall_t && now - g.stall_t > 3000) {
                if (!g.stall_n) {
                    set_status(0, "stream stalled at %llu frames", (unsigned long long)tot);
                    g.stall_n = 1;
                }
                if (now - g.stall_t > 4000) {
                    g.stall_t = now;
                    stream_recover();
                }
            }
        }
        if (!R) {
            gfx_make_renderer(win);
            if (!R) {
                SDL_Delay(16);
                continue;
            }
        }
        {
            int pw, ph;
            float s = ui_f();
            SDL_GetWindowSize(win, &pw, &ph);
            if (pw < 2 || ph < 2) {
                SDL_Delay(16);
                continue;
            }
            /* Clear the real backbuffer first so a previous scale cannot
             * leave a second copy in the margin. */
            SDL_RenderSetScale(R, 1.f, 1.f);
            SDL_SetRenderDrawColor(R, 22, 24, 30, 255);
            SDL_RenderClear(R);
            SDL_RenderSetScale(R, s, s);
            win_w = (int)(pw / s);
            win_h = (int)(ph / s);
            if (win_w < (NP_TOUCH ? 480 : 640)) {
                win_w = NP_TOUCH ? 480 : 640;
            }
            if (win_h < (NP_TOUCH ? 300 : 400)) {
                win_h = NP_TOUCH ? 300 : 400;
            }
        }
        frame();
        SDL_RenderPresent(R);
    }
    g.running = 0;
    pthread_cond_signal(&g.qcv);
    pthread_join(g.cmd_thr, NULL);
    do_disconnect();
    if (cube_tex) {
        SDL_DestroyTexture(cube_tex);
        cube_tex = NULL;
    }
    SDL_DestroyRenderer(R);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
#endif /* !NP_ANDROID_UI */

static int run_cli(const char *port, int seconds)
{
    time_t end = time(NULL) + seconds;
    uint64_t tot;
    uint32_t good, bad;
    int i;

    if (port) {
        snprintf(g.ports[0], NP_MAX_PATH, "%s", port);
        g.nports = 1;
        g.port_i = 0;
    }
    do_connect();
    if (!g.connected) {
        fprintf(stderr, "%s\n", g.status);
        return 1;
    }
    printf("reading %s for %ds...\n", g.ports[g.port_i], seconds);
    while (time(NULL) < end && g.connected) {
        usleep(100000);
        np_ring_stats(&g.ring, &tot, &good, &bad);
        printf("\rframes %llu  good %u  bad %u   %s", (unsigned long long)tot, good, bad,
               g.status);
        fflush(stdout);
    }
    putchar('\n');
    {
        uint8_t lp = 0, ln = 0;
        np_ring_loff(&g.ring, &lp, &ln);
        printf("loff_p=0x%02X loff_n=0x%02X  (bit0=ch1 ... bit7=ch8)\n", lp, ln);
    }
    for (i = 0; i < NP_NCHAN; i++) {
        float b[16];
        uint32_t n = np_ring_copy(&g.ring, i, b, 16);
        printf("ch%d last %u samples:", i + 1, n);
        if (n) {
            printf(" %.1f uV", b[n - 1]);
        }
        putchar('\n');
    }
    do_disconnect();
    return 0;
}

static int host_ready;

int np_host_start(const char *files_dir)
{
    int i;
    if (host_ready) {
        return 0;
    }
    np_set_files_dir(files_dir);
    memset(&g, 0, sizeof(g));
    g.fd = -1;
    g.running = 1;
    g.board = NP_BOARD_KNIGHT_IMU;
    g.window_s = 2;
    g.autoscale = 0;
    g.og = 0;
    g.scale_uv = 200;
    g.notch_hz = 50;
    g.hp_hz = 1;
    g.lp_hz = 0;
    g.car = 0;
    g.envelope = 0;
    g.band = 0;
    g.detrend = 0;
    g.cal_cut = 1;
    g.grid = 1;
    g.show_uv = 1;
    g.ui_scale = 15;
    g.pref_w = 1280;
    g.pref_h = 720;
    g.cube_yaw = 0.55f;
    g.cube_pitch = 0.40f;
    g.cube_zoom = 1.0f;
    snprintf(g.prof, sizeof(g.prof), "default");
    np_elec_default(g.elec);
    for (i = 0; i < NP_NCHAN; i++) {
        g.gain[i] = 12;
        g.active[i] = 1;
        g.rld[i] = 1;
        g.chrgb[i][0] = CHCOL[i][0];
        g.chrgb[i][1] = CHCOL[i][1];
        g.chrgb[i][2] = CHCOL[i][2];
    }
    {
        char root[NP_MAX_PATH];
        np_cfg_root(root, sizeof(root));
        np_mkdir_p(root);
        np_mkdir_p(root);
        {
            char pdir[NP_MAX_PATH];
            snprintf(pdir, sizeof(pdir), "%s/exg-c/profiles", root);
            np_mkdir_p(pdir);
        }
    }
    cfg_load();
    prof_scan();
    filt_reset();
    pthread_mutex_init(&g.mu, NULL);
    pthread_mutex_init(&g.qmu, NULL);
    pthread_mutex_init(&g.csv_mu, NULL);
    pthread_mutex_init(&g.parse_mu, NULL);
    pthread_cond_init(&g.qcv, NULL);
    np_ring_init(&g.ring);
    np_smx_init(&g.smx);
    snprintf(g.cube_ack, sizeof(g.cube_ack), "offer off");
    npl_init(&g.learn);
    {
        char lp[NP_MAX_PATH];
        learn_path(lp, sizeof(lp));
        npl_load(&g.learn, lp);
    }
    cal_load();
    if (pthread_create(&g.cmd_thr, NULL, cmd_thread, NULL) != 0) {
        return -1;
    }
    g.nports = np_list_ports(g.ports, NP_MAX_PORTS);
    host_ready = 1;
    set_status(1, g.nports ? "ready - tap Connect" : "plug Knight, grant USB, tap Connect");
    return 0;
}

void np_host_shutdown(void)
{
    if (!host_ready) {
        return;
    }
    g.running = 0;
    pthread_cond_signal(&g.qcv);
    pthread_join(g.cmd_thr, NULL);
    do_disconnect();
    host_ready = 0;
}

static void live_snap(void)
{
    static uint32_t last;
    static uint64_t last_tot;
    uint32_t now = SDL_GetTicks(), want;
    char root[NP_MAX_PATH], path[NP_MAX_PATH], id[48];
    FILE *f;
    int c;
    uint64_t tot = 0;
    float raw[NP_NCHAN][256];
    uint32_t n0 = 0;

    if (!g.connected) {
        return;
    }
    if (last && now - last < 800) {
        return;
    }
    last = now;
    np_ring_stats(&g.ring, &tot, NULL, NULL);
    np_cfg_root(root, sizeof(root));
    snprintf(path, sizeof(path), "%s/live-snap.txt", root);
    f = fopen(path, "w");
    if (!f) {
        return;
    }
    id_label(id, sizeof(id));
    fprintf(f, "t_ms=%u frames=%llu dframes=%llu sps=%.1f %s\n", now,
            (unsigned long long)tot, (unsigned long long)(tot - last_tot),
            g.sps > 1.f ? g.sps : 0.f, id);
    last_tot = tot;
    want = (uint32_t)(0.50f * design_sps());
    if (want > 256) {
        want = 256;
    }
    fprintf(f, "ch,dc,rms,pk,uniq,n\n");
    for (c = 0; c < NP_NCHAN; c++) {
        float dc = 0, rms = 0, pk = 0;
        uint32_t n, i, uniq = 1;
        n = np_ring_copy(&g.ring, c, raw[c], want);
        if (c == 0) {
            n0 = n;
        }
        ch_stats(raw[c], n, &dc, &rms, &pk);
        for (i = 1; i < n; i++) {
            if (raw[c][i] != raw[c][i - 1]) {
                uniq++;
            }
        }
        fprintf(f, "%d,%.3f,%.3f,%.3f,%u,%u\n", c + 1, dc, rms, pk, uniq, n);
    }
    fclose(f);
    snprintf(path, sizeof(path), "%s/live-snap.csv", root);
    f = fopen(path, "w");
    if (!f) {
        return;
    }
    fprintf(f, "i,ch1,ch2,ch3,ch4,ch5,ch6,ch7,ch8\n");
    for (c = 0; (uint32_t)c < n0; c++) {
        fprintf(f, "%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", c, raw[0][c],
                raw[1][c], raw[2][c], raw[3][c], raw[4][c], raw[5][c], raw[6][c],
                raw[7][c]);
    }
    fclose(f);
    {
        float ratio = 0.f;
        int ev = stream_id(&ratio);
        static float best;
        if (g.sps >= 80.f && (ev == NP_ID_CLENCH || ev == NP_ID_BURST) &&
            ratio >= 2.50f && ratio > best) {
            best = ratio;
            snprintf(path, sizeof(path), "%s/clench-live.txt", root);
            f = fopen(path, "w");
            if (f) {
                fprintf(f, "t_ms=%u frames=%llu sps=%.1f %s ratio=%.2f\n", now,
                        (unsigned long long)tot, g.sps > 1.f ? g.sps : 0.f, id,
                        (double)ratio);
                fclose(f);
            }
            snprintf(path, sizeof(path), "%s/clench-live.csv", root);
            f = fopen(path, "w");
            if (f) {
                fprintf(f, "i,ch1,ch2,ch3,ch4,ch5,ch6,ch7,ch8\n");
                for (c = 0; (uint32_t)c < n0; c++) {
                    fprintf(f, "%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", c,
                            raw[0][c], raw[1][c], raw[2][c], raw[3][c], raw[4][c],
                            raw[5][c], raw[6][c], raw[7][c]);
                }
                fclose(f);
            }
        }
    }
}

void np_host_tick(void)
{
    if (!host_ready) {
        return;
    }
    smx_tick();
    atom_tick();
    learn_tick();
    live_snap();
    if (g.connected && !g.en_running) {
        uint64_t tot = 0;
        uint32_t now = SDL_GetTicks();
        np_ring_stats(&g.ring, &tot, NULL, NULL);
        if (tot != g.stall_tot) {
            g.stall_tot = tot;
            g.stall_t = now;
            g.stall_n = 0;
            if (g.recover_n && tot > 50) {
                g.recover_n = 0;
            }
        } else if (g.stall_t && now - g.stall_t > 4000) {
            stream_recover();
            g.stall_t = now;
        }
    }
}

int np_host_connect(void)
{
    do_connect();
    return g.connected;
}
void np_host_disconnect(void)
{
    do_disconnect();
}
int np_host_connected(void)
{
    return g.connected;
}
void np_host_status(char *out, int n)
{
    pthread_mutex_lock(&g.mu);
    snprintf(out, (size_t)n, "%s", g.status);
    pthread_mutex_unlock(&g.mu);
}
int np_host_status_ok(void)
{
    return g.status_ok;
}
float np_host_sps(void)
{
    return g.sps > 1.f ? g.sps : 0.f;
}
unsigned int np_host_frames(void)
{
    uint64_t tot = 0;
    np_ring_stats(&g.ring, &tot, NULL, NULL);
    return (unsigned int)tot;
}
int np_host_copy_wave(int ch, float *dst, int max)
{
    uint32_t want;
    if (ch < 0 || ch >= NP_NCHAN || !dst || max < 8) {
        return 0;
    }
    want = (uint32_t)(g.window_s * design_sps());
    if (want < 32) {
        want = 32;
    }
    if (want > (uint32_t)max) {
        want = (uint32_t)max;
    }
    {
        int n = (int)view_copy(ch, dst, want);
        if (g.detrend && n > 4) {
            np_detrend(dst, n);
        }
        last_clip[ch] = np_window_clip(dst, n);
        return n;
    }
}
int np_host_scale_uv(void)
{
    return g.scale_uv;
}
void np_host_cycle_scale(void)
{
    int k;
    for (k = 0; k < NSCALE; k++) {
        if (SCALE_UV[k] == g.scale_uv) {
            g.scale_uv = SCALE_UV[(k + 1) % NSCALE];
            cfg_save();
            return;
        }
    }
    g.scale_uv = 200;
}
void np_host_set_scale_uv(int uv)
{
    if (uv <= 0) {
        g.autoscale = 1;
        g.og = 0;
    } else {
        g.scale_uv = uv;
        g.autoscale = 0;
        g.og = 0;
    }
    cfg_save();
}
int np_host_window_s(void)
{
    return g.window_s < 1 ? 2 : g.window_s;
}
void np_host_cycle_window(void)
{
    int k;
    for (k = 0; k < NWINS; k++) {
        if (WIN_S[k] == g.window_s) {
            g.window_s = WIN_S[(k + 1) % NWINS];
            cfg_save();
            return;
        }
    }
    g.window_s = 2;
    cfg_save();
}
void np_host_set_window_s(int s)
{
    if (s < 1) {
        s = 1;
    }
    if (s > 8) {
        s = 8;
    }
    g.window_s = s;
    cfg_save();
}
int np_host_paused(void)
{
    return g.paused;
}
void np_host_toggle_pause(void)
{
    g.paused = !g.paused;
}
void np_host_set_active(int ch, int on)
{
    if (ch < 0 || ch >= NP_NCHAN) {
        return;
    }
    g.active[ch] = on ? 1 : 0;
    if (g.connected) {
        cmd_push(g.active[ch] ? CMD_CHON : CMD_CHOFF, ch + 1, g.gain[ch]);
    }
}
void np_host_set_rld(int ch, int on)
{
    if (ch < 0 || ch >= NP_NCHAN) {
        return;
    }
    g.rld[ch] = on ? 1 : 0;
    if (g.connected) {
        cmd_push(g.rld[ch] ? CMD_RLDADD : CMD_RLDRM, ch + 1, 0);
    }
}
void np_host_cycle_gain(int ch)
{
    if (ch < 0 || ch >= NP_NCHAN) {
        return;
    }
    next_gain(ch);
    pthread_mutex_lock(&g.parse_mu);
    np_parser_set_gain(&g.parser, ch + 1, g.gain[ch]);
    pthread_mutex_unlock(&g.parse_mu);
    if (g.connected && g.active[ch]) {
        cmd_push(CMD_CHON, ch + 1, g.gain[ch]);
    }
    cfg_save();
}
void np_host_set_gain(int ch, int gain)
{
    int i, ok = 0;
    if (ch < 0 || ch >= NP_NCHAN) {
        return;
    }
    for (i = 0; i < NP_NGAINS; i++) {
        if (NP_GAINS[i] == gain) {
            ok = 1;
            break;
        }
    }
    if (!ok) {
        return;
    }
    g.gain[ch] = gain;
    pthread_mutex_lock(&g.parse_mu);
    np_parser_set_gain(&g.parser, ch + 1, g.gain[ch]);
    pthread_mutex_unlock(&g.parse_mu);
    if (g.connected && g.active[ch]) {
        cmd_push(CMD_CHON, ch + 1, g.gain[ch]);
    }
    cfg_save();
}
int np_host_active(int ch)
{
    return (ch >= 0 && ch < NP_NCHAN) ? g.active[ch] : 0;
}
int np_host_rld(int ch)
{
    return (ch >= 0 && ch < NP_NCHAN) ? g.rld[ch] : 0;
}
int np_host_gain(int ch)
{
    return (ch >= 0 && ch < NP_NCHAN) ? g.gain[ch] : 12;
}
void np_host_color(int ch, int *r, int *gcol, int *b)
{
    if (ch < 0 || ch >= NP_NCHAN) {
        return;
    }
    if (r) {
        *r = g.chrgb[ch][0];
    }
    if (gcol) {
        *gcol = g.chrgb[ch][1];
    }
    if (b) {
        *b = g.chrgb[ch][2];
    }
}
void np_host_cycle_color(int ch)
{
    chcol_cycle(ch);
    cfg_save();
}
void np_host_set_color(int ch, int r, int gc, int b)
{
    if (ch < 0 || ch >= NP_NCHAN) {
        return;
    }
    if (r < 0) {
        r = 0;
    }
    if (r > 255) {
        r = 255;
    }
    if (gc < 0) {
        gc = 0;
    }
    if (gc > 255) {
        gc = 255;
    }
    if (b < 0) {
        b = 0;
    }
    if (b > 255) {
        b = 255;
    }
    g.chrgb[ch][0] = r;
    g.chrgb[ch][1] = gc;
    g.chrgb[ch][2] = b;
    cfg_save();
}
void np_host_noise_arm(void)
{
    g.cal_arm = 1;
    set_status(1, "NOISE: board on desk / headset off, then OK");
}
void np_host_noise_ok(void)
{
    if (!g.cal_arm) {
        set_status(0, "tap NOISE first, then OK");
        return;
    }
    cal_capture();
    g.cal_cut = 1;
    cfg_save();
}
void np_host_calm(void)
{
    calm_capture();
}
void np_host_toggle_clean(void)
{
    g.cal_cut = !g.cal_cut;
    cfg_save();
}
int np_host_cal_have(void)
{
    return g.cal.have;
}
int np_host_calm_have(void)
{
    return g.calm.have;
}
int np_host_clean(void)
{
    return g.cal_cut;
}
void np_host_set_name(const char *s)
{
    snprintf(g.namebuf, sizeof(g.namebuf), "%s", s ? s : "");
}
void np_host_get_name(char *out, int n)
{
    snprintf(out, (size_t)n, "%s", g.namebuf);
}
void np_host_record(void)
{
    if (g.rec_t0) {
        g.rec_t0 = 0;
        set_status(1, "record cancelled");
    } else {
        learn_start_hold();
    }
}
void np_host_toggle_match(void)
{
    g.learn.match = !g.learn.match;
    if (!g.learn.match) {
        g.learn.best = -1;
        set_status(1, "MATCH off — scores frozen");
    } else if (g.learn.n < 1) {
        set_status(0, "MATCH on — Record a pose first");
    } else {
        set_status(1, "MATCH on — scoring %d pose%s", g.learn.n,
                   g.learn.n == 1 ? "" : "s");
    }
}
int np_host_match(void)
{
    return g.learn.match;
}
int np_host_learn_n(void)
{
    return g.learn.n;
}
int np_host_learn_best(void)
{
    return g.learn.best;
}
void np_host_learn_name(int i, char *out, int n)
{
    if (i < 0 || i >= g.learn.n) {
        out[0] = 0;
        return;
    }
    snprintf(out, (size_t)n, "%s", g.learn.s[i].name);
}
float np_host_learn_score(int i)
{
    if (i < 0 || i >= g.learn.n) {
        return 0.f;
    }
    return g.learn.score[i];
}
float np_host_learn_score_cube(int i)
{
    if (i < 0 || i >= g.learn.n) {
        return 0.f;
    }
    return g.learn.score_cube[i];
}
int np_host_learn_sel(void)
{
    return g.learn.sel;
}
void np_host_learn_select(int i)
{
    if (i < 0 || i >= g.learn.n) {
        return;
    }
    g.learn.sel = i;
    snprintf(g.namebuf, sizeof(g.namebuf), "%s", g.learn.s[i].name);
}
void np_host_learn_del(int i)
{
    if (i < 0 || i >= g.learn.n) {
        return;
    }
    npl_del(&g.learn, i);
    learn_persist();
    set_status(1, "deleted pose");
}
void np_host_set_profile(const char *s)
{
    if (s && prof_ok_name(s)) {
        snprintf(g.prof, sizeof(g.prof), "%s", s);
    }
}
void np_host_get_profile(char *out, int n)
{
    snprintf(out, (size_t)n, "%s", g.prof);
}
int np_host_prof_save(void)
{
    prof_save();
    return prof_ok_name(g.prof) ? 0 : -1;
}
int np_host_prof_load(void)
{
    prof_load();
    return 0;
}
int np_host_prof_count(void)
{
    prof_scan();
    return g.nprof;
}
void np_host_prof_at(int i, char *out, int n)
{
    prof_scan();
    if (i < 0 || i >= g.nprof) {
        out[0] = 0;
        return;
    }
    snprintf(out, (size_t)n, "%s", g.profiles[i]);
}
void np_host_ports(char *out, int n)
{
    int i, off = 0;
    g.nports = np_list_ports(g.ports, NP_MAX_PORTS);
    out[0] = 0;
    for (i = 0; i < g.nports && off < n - 2; i++) {
        off += snprintf(out + off, (size_t)(n - off), "%s%s", i ? "\n" : "", g.ports[i]);
    }
}
void np_host_cycle_port(void)
{
    g.nports = np_list_ports(g.ports, NP_MAX_PORTS);
    if (g.nports) {
        g.port_i = (g.port_i + 1) % g.nports;
    }
}
void np_host_set_port_i(int i)
{
    g.nports = np_list_ports(g.ports, NP_MAX_PORTS);
    if (g.nports < 1) {
        return;
    }
    if (i < 0) {
        i = 0;
    }
    if (i >= g.nports) {
        i = g.nports - 1;
    }
    g.port_i = i;
}
void np_host_copy_cube(unsigned char dst[512])
{
    memcpy(dst, g.smx.cube, 512);
}
int np_host_notch(void)
{
    return g.notch_hz;
}
int np_host_hp(void)
{
    return g.hp_hz;
}
void np_host_cycle_notch(void)
{
    if (g.notch_hz == 50) {
        g.notch_hz = 60;
    } else if (g.notch_hz == 60) {
        g.notch_hz = 0;
    } else if (g.notch_hz == 0) {
        g.notch_hz = -1;
    } else {
        g.notch_hz = 50;
    }
    filt_reset();
    cfg_save();
}
void np_host_set_notch(int hz)
{
    if (hz != 0 && hz != 50 && hz != 60 && hz != -1) {
        hz = 50;
    }
    g.notch_hz = hz;
    filt_reset();
    cfg_save();
}
void np_host_cycle_hp(void)
{
    static const int hp[] = {0, 1, 2, 5, 20};
    int k;
    for (k = 0; k < 5; k++) {
        if (hp[k] == g.hp_hz) {
            g.hp_hz = hp[(k + 1) % 5];
            filt_reset();
            cfg_save();
            return;
        }
    }
    g.hp_hz = 1;
}
void np_host_set_hp(int hz)
{
    if (hz != 0 && hz != 1 && hz != 2 && hz != 5 && hz != 20) {
        hz = 1;
    }
    g.hp_hz = hz;
    filt_reset();
    cfg_save();
}
int np_host_lp(void)
{
    return g.lp_hz;
}
void np_host_cycle_lp(void)
{
    static const int lp[] = {0, 20, 40};
    int k;
    for (k = 0; k < 3; k++) {
        if (lp[k] == g.lp_hz) {
            g.lp_hz = lp[(k + 1) % 3];
            filt_reset();
            cfg_save();
            return;
        }
    }
    g.lp_hz = 0;
}
void np_host_set_lp(int hz)
{
    if (hz != 0 && hz != 20 && hz != 40) {
        hz = 0;
    }
    g.lp_hz = hz;
    filt_reset();
    cfg_save();
}
int np_host_car(void)
{
    return g.car;
}
void np_host_toggle_car(void)
{
    g.car = !g.car;
    filt_reset();
    cfg_save();
}
int np_host_detrend(void)
{
    return g.detrend;
}
void np_host_toggle_detrend(void)
{
    g.detrend = !g.detrend;
    cfg_save();
}
int np_host_envelope(void)
{
    return g.envelope;
}
void np_host_toggle_envelope(void)
{
    g.envelope = !g.envelope;
    filt_reset();
    cfg_save();
}
int np_host_band(void)
{
    return g.band;
}
void np_host_cycle_band(void)
{
    band_apply((g.band + 1) % NP_BAND_N);
}
void np_host_set_band(int band)
{
    band_apply(band);
}
int np_host_ch_clip(int ch)
{
    if (ch < 0 || ch >= NP_NCHAN) {
        return 0;
    }
    return last_clip[ch];
}

int np_host_algo(void)
{
    if (g.algo < 0 || g.algo >= NP_ALGO_N) {
        return 0;
    }
    return g.algo;
}
void np_host_cycle_algo(void)
{
    g.algo = (g.algo + 1) % NP_ALGO_N;
    cfg_save();
    set_status(1, "algo %s  — cube node is 0 or 1", np_algo_name(g.algo));
}
void np_host_set_algo(int id)
{
    if (id < 0 || id >= NP_ALGO_N) {
        id = 0;
    }
    g.algo = id;
    cfg_save();
    set_status(1, "algo %s  — cube node is 0 or 1", np_algo_name(g.algo));
}
void np_host_algo_name(char *out, int n)
{
    snprintf(out, (size_t)n, "%s", np_algo_name(np_host_algo()));
}

int np_host_cube_view(void)
{
    return g.cube_view ? 1 : 0;
}
void np_host_set_cube_view(int map)
{
    g.cube_view = map ? 1 : 0;
    cfg_save();
    set_status(1, g.cube_view ? "map  assign 10-10 sites" : "viz  crimson lattice");
}
void np_host_cube_spin(float dyaw, float dpitch)
{
    g.cube_yaw += dyaw;
    g.cube_pitch += dpitch;
    if (g.cube_pitch > 1.20f) {
        g.cube_pitch = 1.20f;
    }
    if (g.cube_pitch < -0.35f) {
        g.cube_pitch = -0.35f;
    }
}
void np_host_cube_zoom(int dir)
{
    cube_zoom_by(dir);
    cfg_save();
}
void np_host_cube_front(void)
{
    g.cube_yaw = 0.55f;
    g.cube_pitch = 0.40f;
    g.cube_zoom = 1.0f;
    cfg_save();
    set_status(1, "front");
}
int np_host_elec_sel(void)
{
    return g.elec_sel;
}
void np_host_set_elec_sel(int ch)
{
    if (ch < 0 || ch >= NP_NCHAN) {
        return;
    }
    g.elec_sel = ch;
    if (g.elec[ch].site >= 0) {
        g.site_focus = g.elec[ch].site;
    }
    set_status(1, "ch%d %s", ch + 1, g.elec[ch].name[0] ? g.elec[ch].name : "?");
}
void np_host_elec_label(int ch, char *out, int n)
{
    if (ch < 0 || ch >= NP_NCHAN) {
        out[0] = 0;
        return;
    }
    snprintf(out, (size_t)n, "%d %s", ch + 1, g.elec[ch].name[0] ? g.elec[ch].name : "?");
}
void np_host_elec_name(int ch, char *out, int n)
{
    if (!out || n < 2) {
        return;
    }
    if (ch < 0 || ch >= NP_NCHAN) {
        out[0] = 0;
        return;
    }
    snprintf(out, (size_t)n, "%s", g.elec[ch].name[0] ? g.elec[ch].name : "?");
}
int np_host_elec_site(int ch)
{
    return (ch >= 0 && ch < NP_NCHAN) ? g.elec[ch].site : -1;
}
void np_host_elec_xyz(int ch, float *x, float *y, float *z)
{
    if (ch < 0 || ch >= NP_NCHAN) {
        return;
    }
    np_elec_cube_xyz(&g.elec[ch], x, y, z);
}
int np_host_site_focus(void)
{
    return g.site_focus;
}
void np_host_site_step(int dir)
{
    cube_site_by(dir);
}
void np_host_assign_site(int site)
{
    if (site >= 0 && site < np_1010_count()) {
        g.site_focus = site;
    }
    cube_assign_focus();
}
int np_host_site_n(void)
{
    return np_1010_count();
}
void np_host_site_name(int i, char *out, int n)
{
    const char *s = np_1010_name(i);
    snprintf(out, (size_t)n, "%s", s ? s : "");
}
int np_host_site_core(int i)
{
    return np_1010_core(i);
}
int np_host_site_ch(int i)
{
    int c;
    for (c = 0; c < NP_NCHAN; c++) {
        if (g.elec[c].site == i) {
            return c;
        }
    }
    return -1;
}
void np_host_site_flat(int i, float *fx, float *fy)
{
    np_1010_flat(i, fx, fy);
}
void np_host_site_xyz(int i, float *x, float *y, float *z)
{
    np_1010_cube_xyz(i, x, y, z);
}
int np_host_site_ijk(int i, int *x, int *y, int *z)
{
    return np_1010_ijk(i, x, y, z);
}
int np_host_viz_cells(float *xyz, float *size, int *rgba, int cap)
{
    struct np_cube cells[NP_CUBE_BUDGET];
    int n, i;
    if (!xyz || !size || !rgba || cap < 1) {
        return 0;
    }
    n = np_smx_head_cubes(&g.smx, g.elec, g.chrgb, cells, cap);
    for (i = 0; i < n; i++) {
        xyz[i * 3] = cells[i].x;
        xyz[i * 3 + 1] = cells[i].y;
        xyz[i * 3 + 2] = cells[i].z;
        size[i] = cells[i].s;
        rgba[i] = (cells[i].a << 24) | (cells[i].r << 16) | (cells[i].g << 8) | cells[i].b;
    }
    /* Live 8^3 ON bits — otherwise viz is eight dim electrode cells. */
    {
        int ix, iy, iz;
        for (iz = 0; iz < 8 && n < cap; iz++) {
            for (iy = 0; iy < 8 && n < cap; iy++) {
                for (ix = 0; ix < 8 && n < cap; ix++) {
                    float wx, wy, wz;
                    int k, dup = 0;
                    if (!np_cube_get(&g.smx, ix, iy, iz)) {
                        continue;
                    }
                    np_ijk_world(ix, iy, iz, &wx, &wy, &wz);
                    for (k = 0; k < n; k++) {
                        float dx = xyz[k * 3] - wx, dy = xyz[k * 3 + 1] - wy,
                              dz = xyz[k * 3 + 2] - wz;
                        if (dx * dx + dy * dy + dz * dz < 0.04f) {
                            dup = 1;
                            break;
                        }
                    }
                    if (dup) {
                        continue;
                    }
                    xyz[n * 3] = wx;
                    xyz[n * 3 + 1] = wy;
                    xyz[n * 3 + 2] = wz;
                    size[n] = 0.24f;
                    rgba[n] = (230 << 24) | (242 << 16) | (38 << 8) | 71;
                    n++;
                }
            }
        }
    }
    return n;
}

unsigned int np_host_smx_seq(void)
{
    return g.smx.seq;
}

unsigned int np_host_smx_fold(void)
{
    unsigned int f = 0;
    int c, row;
    if (g.smx.have < 1) {
        return 0;
    }
    row = (int)((g.smx.wr - 1) % NP_SMX_SEC);
    for (c = 0; c < NP_NCHAN; c++) {
        if (g.smx.bit[row][c]) {
            f |= 1u << c;
        }
    }
    return f;
}

int np_host_prof_export(const char *path)
{
    return cfg_write(path);
}

int np_host_prof_import(const char *path)
{
    if (cfg_read(path) != 0) {
        set_status(0, "cannot read profile file");
        return -1;
    }
    prof_apply();
    cfg_save();
    set_status(1, "loaded profile file");
    return 0;
}

void np_host_id(char *out, int n)
{
    if (!out || n < 4) {
        return;
    }
    if (!host_ready) {
        snprintf(out, (size_t)n, "ID —");
        return;
    }
    id_label(out, n);
}

int np_host_rec_ms(void)
{
    uint32_t now;
    int dt;
    if (!g.rec_t0) {
        return 0;
    }
    now = SDL_GetTicks();
    dt = (int)(now - g.rec_t0);
    if (dt >= (int)REC_MS) {
        return 0;
    }
    return (int)REC_MS - dt;
}

int np_host_csv(void)
{
    return g.recording;
}

void np_host_toggle_csv(void)
{
    toggle_record();
}

int np_host_fft(float *dst, int max, int *peak_hz)
{
    int n;
    fft_refresh();
    n = max < FFT_STRIP_BINS ? max : FFT_STRIP_BINS;
    if (n < 0) {
        n = 0;
    }
    if (dst && n > 0) {
        memcpy(dst, fft_hold, (size_t)n * sizeof(float));
    }
    if (peak_hz) {
        *peak_hz = fft_peak_hz;
    }
    return n;
}

void np_host_atom_discard(void)
{
    g.atom_on = 0;
    g.atom_n = 0;
    g.atom_wr = 0;
    set_status(1, "take discarded");
}

void np_host_atom_start(void)
{
    g.atom_n = 0;
    g.atom_wr = 0;
    g.atom_on = 1;
    set_status(1, "take running — watch the plot, then Stop");
}

int np_host_atom_stop(void)
{
    int n = g.atom_n;
    g.atom_on = 0;
    if (n < 1) {
        set_status(0, "take empty — hold at least 1 s");
    } else {
        set_status(1, "take %d s — name it to keep", n);
    }
    return n;
}

void np_host_toggle_atom(void)
{
    if (g.atom_on) {
        np_host_atom_stop();
    } else {
        np_host_atom_start();
    }
}

int np_host_atom(void)
{
    return g.atom_on;
}

int np_host_atom_n(void)
{
    return g.atom_n;
}

int np_host_atom_save(void)
{
    char path[NP_MAX_PATH];
    uint64_t live[NP_ATOM_RING];
    int n = 0;
    if (atom_path(path, (int)sizeof(path), g.namebuf) != 0) {
        set_status(0, "ATOM need a name");
        return -1;
    }
    atom_flatten(live, &n);
    if (n < 1) {
        set_status(0, "ATOM empty — wait for 1 s folds");
        return -1;
    }
    if (np_atom_save(path, live, n, NP_ATOM_WIN) != 0) {
        set_status(0, "ATOM cannot write %s", path);
        return -1;
    }
    memcpy(g.atom_ref, live, (size_t)n * sizeof(uint64_t));
    g.atom_ref_n = n;
    atom_sanitize(g.atom_ref_name, (int)sizeof(g.atom_ref_name), g.namebuf);
    snprintf(g.atom_a, sizeof(g.atom_a), "%s", g.atom_ref_name);
    g.atom_b[0] = 0;
    g.atom_ab = 0.f;
    atom_score();
    g.atom_n = 0;
    g.atom_wr = 0;
    set_status(1, "kept %s  %d s — tap another take to compare", g.atom_ref_name, n);
    return 0;
}

int np_host_atom_load(void)
{
    char path[NP_MAX_PATH];
    int n, win = 0;
    if (atom_path(path, (int)sizeof(path), g.namebuf) != 0) {
        set_status(0, "ATOM need a name to compare");
        return -1;
    }
    n = np_atom_load(path, g.atom_ref, NP_ATOM_RING, &win);
    if (n < 1) {
        set_status(0, "ATOM no chain '%s'", g.namebuf);
        return -1;
    }
    g.atom_ref_n = n;
    atom_sanitize(g.atom_ref_name, (int)sizeof(g.atom_ref_name), g.namebuf);
    atom_score();
    set_status(1, "ATOM vs %s  %d atoms  unity %.0f%%", g.atom_ref_name, n,
               (double)(g.atom_unity * 100.f));
    return 0;
}

float np_host_atom_unity(void)
{
    return g.atom_unity;
}

void np_host_atom_line(char *out, int n)
{
    if (!out || n < 4) {
        return;
    }
    if (g.atom_on) {
        snprintf(out, (size_t)n, "recording %d s", g.atom_n);
    } else if (g.atom_a[0] && g.atom_b[0]) {
        snprintf(out, (size_t)n, "%s vs %s  %.0f%%", g.atom_a, g.atom_b,
                 (double)(g.atom_ab * 100.f));
    } else if (g.atom_a[0]) {
        snprintf(out, (size_t)n, "vs %s — tap another take", g.atom_a);
    } else {
        out[0] = 0;
    }
}

void np_host_atom_ref(char *out, int n)
{
    if (!out || n < 2) {
        return;
    }
    snprintf(out, (size_t)n, "%s", g.atom_ref_name);
}

#define NP_ATOM_MAX 32
static char atom_listed[NP_ATOM_MAX][NP_ATOM_NAME];
static int atom_listed_n;

static int atom_rescan(void)
{
    DIR *d;
    char dir[NP_MAX_PATH];
    struct dirent *e;
    int n = 0, i, j;
    atom_dir(dir, (int)sizeof(dir));
    d = opendir(dir);
    if (!d) {
        atom_listed_n = 0;
        return 0;
    }
    while ((e = readdir(d)) != NULL && n < NP_ATOM_MAX) {
        size_t L = strlen(e->d_name);
        int len;
        if (L < 6 || strcmp(e->d_name + L - 5, ".npat") != 0) {
            continue;
        }
        len = (int)L - 5;
        if (len >= NP_ATOM_NAME) {
            len = NP_ATOM_NAME - 1;
        }
        memcpy(atom_listed[n], e->d_name, (size_t)len);
        atom_listed[n][len] = 0;
        n++;
    }
    closedir(d);
    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            if (strcmp(atom_listed[i], atom_listed[j]) > 0) {
                char tmp[NP_ATOM_NAME];
                memcpy(tmp, atom_listed[i], NP_ATOM_NAME);
                memcpy(atom_listed[i], atom_listed[j], NP_ATOM_NAME);
                memcpy(atom_listed[j], tmp, NP_ATOM_NAME);
            }
        }
    }
    atom_listed_n = n;
    return n;
}

int np_host_atom_count(void)
{
    return atom_rescan();
}

void np_host_atom_at(int i, char *out, int n)
{
    if (!out || n < 2) {
        return;
    }
    if (i < 0 || i >= atom_listed_n) {
        out[0] = 0;
        return;
    }
    snprintf(out, (size_t)n, "%s", atom_listed[i]);
}

int np_host_atom_secs(int i)
{
    char path[NP_MAX_PATH];
    uint64_t tmp[NP_ATOM_RING];
    int win = 0;
    if (i < 0 || i >= atom_listed_n) {
        return 0;
    }
    if (atom_path(path, (int)sizeof(path), atom_listed[i]) != 0) {
        return 0;
    }
    return np_atom_load(path, tmp, NP_ATOM_RING, &win);
}

int np_host_atom_select(int i)
{
    if (i < 0 || i >= atom_rescan()) {
        return -1;
    }
    snprintf(g.namebuf, sizeof(g.namebuf), "%s", atom_listed[i]);
    return np_host_atom_load();
}

void np_host_atom_del(int i)
{
    char path[NP_MAX_PATH];
    if (i < 0 || i >= atom_rescan()) {
        return;
    }
    if (atom_path(path, (int)sizeof(path), atom_listed[i]) != 0) {
        return;
    }
    if (strcmp(g.atom_ref_name, atom_listed[i]) == 0) {
        g.atom_ref_n = 0;
        g.atom_ref_name[0] = 0;
        g.atom_unity = 0.f;
    }
    if (strcmp(g.atom_a, atom_listed[i]) == 0) {
        g.atom_a[0] = 0;
        g.atom_ab = 0.f;
    }
    if (strcmp(g.atom_b, atom_listed[i]) == 0) {
        g.atom_b[0] = 0;
        g.atom_ab = 0.f;
    }
    unlink(path);
    set_status(1, "deleted take %s", atom_listed[i]);
    atom_rescan();
}

static void atom_pair_score(void)
{
    uint64_t aa[NP_ATOM_RING], bb[NP_ATOM_RING];
    char pa[NP_MAX_PATH], pb[NP_MAX_PATH];
    int na, nb, wa = 0, wb = 0;
    g.atom_ab = 0.f;
    if (!g.atom_a[0] || !g.atom_b[0]) {
        return;
    }
    if (atom_path(pa, (int)sizeof(pa), g.atom_a) != 0 ||
        atom_path(pb, (int)sizeof(pb), g.atom_b) != 0) {
        return;
    }
    na = np_atom_load(pa, aa, NP_ATOM_RING, &wa);
    nb = np_atom_load(pb, bb, NP_ATOM_RING, &wb);
    if (na < 1 || nb < 1) {
        return;
    }
    g.atom_ab = np_atom_ring_unity(aa, na, bb, nb);
}

void np_host_atom_pick(int i)
{
    const char *name;
    if (i < 0 || i >= atom_rescan()) {
        return;
    }
    name = atom_listed[i];
    if (!g.atom_a[0] || (g.atom_a[0] && g.atom_b[0])) {
        snprintf(g.atom_a, sizeof(g.atom_a), "%s", name);
        g.atom_b[0] = 0;
        g.atom_ab = 0.f;
        snprintf(g.namebuf, sizeof(g.namebuf), "%s", name);
        np_host_atom_load();
        set_status(1, "%s — tap another take to compare", name);
        return;
    }
    if (strcmp(g.atom_a, name) == 0) {
        g.atom_a[0] = 0;
        g.atom_b[0] = 0;
        g.atom_ab = 0.f;
        set_status(1, "compare cleared");
        return;
    }
    snprintf(g.atom_b, sizeof(g.atom_b), "%s", name);
    atom_pair_score();
    set_status(1, "%s vs %s  %.0f%%", g.atom_a, g.atom_b, (double)(g.atom_ab * 100.f));
}

void np_host_atom_pair(char *out, int n)
{
    if (!out || n < 4) {
        return;
    }
    if (g.atom_a[0] && g.atom_b[0]) {
        snprintf(out, (size_t)n, "%s vs %s  %.0f%%", g.atom_a, g.atom_b,
                 (double)(g.atom_ab * 100.f));
    } else if (g.atom_a[0]) {
        snprintf(out, (size_t)n, "%s — tap a second take", g.atom_a);
    } else {
        snprintf(out, (size_t)n, "tap two takes to compare");
    }
}

void np_host_atom_slot_a(char *out, int n)
{
    if (out && n > 1) {
        snprintf(out, (size_t)n, "%s", g.atom_a);
    }
}

void np_host_atom_slot_b(char *out, int n)
{
    if (out && n > 1) {
        snprintf(out, (size_t)n, "%s", g.atom_b);
    }
}

int np_host_imu(float acc[3], float gyr[3], float mag[3])
{
    int ok = 0;
    np_ring_imu(&g.ring, acc, gyr, mag, &ok);
    return ok;
}
int np_host_board_imu(void)
{
    return g.board == NP_BOARD_KNIGHT_IMU;
}
void np_host_cycle_board(void)
{
    if (g.connected) {
        set_status(0, "disconnect before switching IMU / EXG");
        return;
    }
    g.board = g.board == NP_BOARD_KNIGHT ? NP_BOARD_KNIGHT_IMU : NP_BOARD_KNIGHT;
    cfg_save();
    set_status(1, g.board == NP_BOARD_KNIGHT_IMU ? "8-ch + IMU" : "8-ch EXG");
}
void np_host_set_board_imu(int imu)
{
    if (g.connected) {
        set_status(0, "disconnect before switching IMU / EXG");
        return;
    }
    g.board = imu ? NP_BOARD_KNIGHT_IMU : NP_BOARD_KNIGHT;
    cfg_save();
    set_status(1, g.board == NP_BOARD_KNIGHT_IMU ? "8-ch + IMU" : "8-ch EXG");
}
int np_host_ui_scale(void)
{
    if (g.ui_scale != 10 && g.ui_scale != 15 && g.ui_scale != 20) {
        return 15;
    }
    return g.ui_scale;
}
void np_host_cycle_ui_scale(void)
{
    if (g.ui_scale == 10) {
        g.ui_scale = 15;
    } else if (g.ui_scale == 15) {
        g.ui_scale = 20;
    } else {
        g.ui_scale = 10;
    }
    cfg_save();
}
void np_host_set_ui_scale(int tenths)
{
    if (tenths != 10 && tenths != 15 && tenths != 20) {
        tenths = 15;
    }
    g.ui_scale = tenths;
    cfg_save();
}

static void usage(const char *a0)
{
    fprintf(stderr,
            "usage: %s [--cli] [--port PATH] [--imu] [--seconds N]\n"
            "  GUI: click Connect, toggle channels / RLD / gain, Record CSV\n"
            "  keys: c connect  d disconnect  r record  space pause  1-8 channel  Tab port  q quit\n"
            "  learn: click name, type, Enter/Save. MATCH scores live vs saved samples.\n",
            a0);
}

int main(int argc, char **argv)
{
    int i, cli = 0, seconds = 8;
    const char *port = NULL;

    ensure_dialout(argc, argv);

    memset(&g, 0, sizeof(g));
    g.fd = -1;
    g.running = 1;
    g.board = NP_BOARD_KNIGHT_IMU;
    g.window_s = 2;
    g.autoscale = 0;
    g.og = 0;
    g.scale_uv = 200;
    g.notch_hz = 50;
    g.hp_hz = 1;
    g.lp_hz = 0;
    g.car = 0;
    g.envelope = 0;
    g.band = 0;
    g.detrend = 0;
    g.cal_cut = 1;
    g.grid = 1;
    g.show_uv = 1;
#ifdef __ANDROID__
    g.ui_scale = 15;
    g.pref_w = 1280;
    g.pref_h = 720;
#else
    g.ui_scale = 15;
    g.pref_w = 1920;
    g.pref_h = 1080;
#endif
    g.cube_yaw = 0.55f;
    g.cube_pitch = 0.40f;
    g.cube_zoom = 1.0f;
    g.cube_view = 0;
    g.site_focus = 0;
    g.virt_focus = 0;
    g.elec_sel = 0;
    np_elec_default(g.elec);
    snprintf(g.prof, sizeof(g.prof), "default");
    for (i = 0; i < NP_NCHAN; i++) {
        g.gain[i] = 12;
        g.active[i] = 1;
        g.rld[i] = 1;
        g.chrgb[i][0] = CHCOL[i][0];
        g.chrgb[i][1] = CHCOL[i][1];
        g.chrgb[i][2] = CHCOL[i][2];
    }
    cfg_load();
    prof_scan();
    filt_reset();
    pthread_mutex_init(&g.mu, NULL);
    pthread_mutex_init(&g.qmu, NULL);
    pthread_mutex_init(&g.csv_mu, NULL);
    pthread_mutex_init(&g.parse_mu, NULL);
    pthread_cond_init(&g.qcv, NULL);
    np_ring_init(&g.ring);
    np_smx_init(&g.smx);
    snprintf(g.cube_ack, sizeof(g.cube_ack), "offer off");
    npl_init(&g.learn);
    {
        char lp[NP_MAX_PATH];
        learn_path(lp, sizeof(lp));
        npl_load(&g.learn, lp);
    }
    cal_load();
    if (pthread_create(&g.cmd_thr, NULL, cmd_thread, NULL) != 0) {
        fprintf(stderr, "cmd thread failed\n");
        return 1;
    }
    for (i = 0; i < NP_NCHAN; i++) {
        if (g.gain[i] < 1) {
            g.gain[i] = 12;
        }
    }
    g.nports = np_list_ports(g.ports, NP_MAX_PORTS);

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--cli")) {
            cli = 1;
        } else if (!strcmp(argv[i], "--imu")) {
            g.board = NP_BOARD_KNIGHT_IMU;
        } else if (!strcmp(argv[i], "--port") && i + 1 < argc) {
            port = argv[++i];
        } else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) {
            seconds = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        }
    }

#ifdef NP_ANDROID_UI
    (void)cli;
    (void)port;
    (void)seconds;
    return 0;
#else
    if (cli) {
        return run_cli(port, seconds);
    }
    return run_gui();
#endif
}
