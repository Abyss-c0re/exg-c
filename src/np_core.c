#define _GNU_SOURCE
#include "np_app.h"
#include "np_algo.h"
#include "np_cube.h"
#include "np_font.h"
#include "np_host.h"
#include "np_link.h"
#include "np_peer.h"
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
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct np_app g;
static float atom_raw[NP_ATOM_RING][NP_NCHAN * NP_ATOM_WIN];
const int SCALE_UV[NSCALE] = {50, 100, 200, 500, 1000, 5000};
const int WIN_S[NWINS] = {1, 2, 4, 8};
const int WINPREF[NWINPREF][2] = {{1280, 800}, {1440, 900}, {1600, 1000}, {1920, 1080}};
const int CHCOL[NP_NCHAN][3] = {
    {255, 255, 255}, {255, 255, 255}, {255, 230, 90}, {255, 230, 90},
    {80, 200, 255}, {80, 200, 255}, {255, 90, 90}, {255, 90, 90},
};
const int PALETTE[NPAL][3] = {
    {80, 200, 255}, {255, 180, 70}, {120, 220, 140}, {240, 110, 140},
    {180, 150, 255}, {255, 230, 90}, {90, 230, 210}, {230, 140, 255},
    {255, 90, 90}, {90, 255, 140}, {255, 255, 255}, {255, 140, 40},
};
float fft_hold[FFT_STRIP_BINS];
uint32_t fft_t;
int fft_used, fft_open, fft_peak_hz;

#if defined(__GNUC__)
void np_ui_apply_window_size(int w, int h) __attribute__((weak));
#endif
void np_ui_apply_window_size(int w, int h)
{
    (void)w;
    (void)h;
}

void set_status(int ok, const char *fmt, ...);
void typing_set(int on);
static void apply_filt(int ch, float *buf, uint32_t n);
static void cook_all(float buf[NP_NCHAN][NP_RING], uint32_t nn[NP_NCHAN], uint32_t want);
static void cook_id(float buf[NP_NCHAN][NP_RING], uint32_t nn[NP_NCHAN]);
static void band_apply(int band);
void ch_stats(const float *buf, uint32_t n, float *dc, float *rms, float *pk);
void cmd_push(int op, int ch, int gain);
void cfg_save(void);

/* Do not cook filter poles from a lagged measured rate (46 SPS makes
 * a 50 Hz notch sit past Nyquist and a 60 Hz notch is already there). */
uint32_t view_copy(int ch, float *dst, uint32_t n);
static float cook_scale_ch(int c);
static void cook_now(float uv[8], float base[8]);
static void atom_identify(void);
float design_sps(void)
{
    if (g.sps >= 100.f && g.sps <= 160.f) {
        return g.sps;
    }
    return (float)NP_DEFAULT_SPS;
}

uint32_t plate_want(void)
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

/* Wiener needs NP_FFT_N samples. Default 2 s × 125 SPS is 250 — too short. */
static int clean_wiener_ready(void)
{
    return g.cal_cut && (g.noise_psd_ok || g.noise_psd_ch_ok) &&
           (int)(g.window_s * design_sps()) >= NP_FFT_N;
}

const char *clean_btn(void)
{
    if (clean_wiener_ready()) {
        return "CLN";
    }
    return g.cal_cut ? "DC" : "dc";
}

const char *clean_tag(void)
{
    if (clean_wiener_ready()) {
        return "  CLEAN";
    }
    if (g.cal_cut) {
        return "  DC";
    }
    return "";
}

void clean_set_status(void)
{
    if (!g.cal_cut) {
        set_status(1, "DC off");
    } else if (clean_wiener_ready()) {
        set_status(1, "CLEAN on — noise plate");
    } else if (g.calm.have) {
        set_status(1, "DC on — still-plate offset");
    } else {
        set_status(1, "DC on — take a still plate to cut offset");
    }
}

static uint64_t live_seen;
static int live_sig = -1;
static uint32_t live_wr;
static float live_ch[NP_NCHAN][NP_RING];
static int last_clip[NP_NCHAN];
static pthread_mutex_t live_mu = PTHREAD_MUTEX_INITIALIZER;
static struct np_peers peers;
static char pair_name[NP_PEER_NAME];
static char pair_grant[NP_PEER_GRANT];
static int pair_dec; /* 0 idle 1 wait 2 allow 3 no */
static pthread_mutex_t pair_mu = PTHREAD_MUTEX_INITIALIZER;

static void peers_path(char *out, int n)
{
    char root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    snprintf(out, (size_t)n, "%s/exg-c.peers", root);
}

static void peers_flush(void)
{
    char path[NP_MAX_PATH];
    peers_path(path, (int)sizeof(path));
    np_peers_save(&peers, path);
}

static int host_grant_ok(const char *grant)
{
    return np_peers_grant_ok(&peers, grant);
}
static float id_base[NP_NCHAN];
static int id_base_ok;
static uint32_t clean_t;
static uint64_t clean_seen;
static uint32_t clean_n[NP_NCHAN];
static float clean_ch[NP_NCHAN][NP_RING];

static int filt_sig(void)
{
    return g.hp_hz + (g.notch_hz + 3) * 97 + (int)(notch_hz_eff() * 10.f) + g.cal_cut * 10007 +
           g.lp_hz * 13 + g.car * 17 + g.envelope * 19 + g.band * 23;
}

void filt_reset(void)
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

void np_mkdir_p(const char *path)
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

void np_cfg_root(char *out, size_t n)
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

void learn_path(char *out, size_t n)
{
    char root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    mkdir(root, 0755);
    snprintf(out, n, "%s/exg-c.learn", root);
}

void learn_persist(void)
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

static void raw_root(char *out, int n)
{
    char root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    mkdir(root, 0755);
    snprintf(out, (size_t)n, "%s/exg-c/raw", root);
    mkdir(out, 0755);
}

void raw_plate_path(const char *which, char *out, int n)
{
    char dir[NP_MAX_PATH];
    raw_root(dir, (int)sizeof(dir));
    snprintf(out, (size_t)n, "%s/%s.nprw", dir, which);
}

static void raw_named_path(const char *kind, const char *name, char *out, int n)
{
    char dir[NP_MAX_PATH], sub[NP_MAX_PATH], safe[NP_ATOM_NAME];
    raw_root(dir, (int)sizeof(dir));
    snprintf(sub, sizeof(sub), "%s/%s", dir, kind);
    mkdir(sub, 0755);
    atom_sanitize(safe, (int)sizeof(safe), name);
    snprintf(out, (size_t)n, "%s/%s.nprw", sub, safe);
}

static void plate_stats_from_buf(float buf[NP_NCHAN][NP_RING], uint32_t nn[NP_NCHAN],
                                 int cook)
{
    int c;
    if (cook) {
        int env = g.envelope;
        g.envelope = 0;
        cook_all(buf, nn, NP_RING);
        g.envelope = env;
    }
    for (c = 0; c < NP_NCHAN; c++) {
        float dc = 0.f, rms = 0.f, pk = 0.f;
        if (nn[c] < 16) {
            continue;
        }
        ch_stats(buf[c], nn[c], &dc, &rms, &pk);
        if (cook) {
            g.calm.dc[c] = dc;
            g.calm.rms[c] = rms;
            g.calm.pk[c] = pk;
            if (nn[c] > g.calm.n) {
                g.calm.n = nn[c];
            }
        } else {
            g.cal.dc[c] = dc;
            g.cal.rms[c] = rms;
            g.cal.pk[c] = pk;
            if (nn[c] > g.cal.n) {
                g.cal.n = nn[c];
            }
        }
    }
}

void raw_dump_ring(const char *path, uint32_t n_samp)
{
    float *planar;
    int c;
    if (!path || n_samp < 16) {
        return;
    }
    if (n_samp > NP_RING) {
        n_samp = NP_RING;
    }
    planar = (float *)malloc((size_t)NP_NCHAN * n_samp * sizeof(float));
    if (!planar) {
        return;
    }
    memset(planar, 0, (size_t)NP_NCHAN * n_samp * sizeof(float));
    for (c = 0; c < NP_NCHAN; c++) {
        float tmp[NP_RING];
        uint32_t n = np_ring_copy(&g.ring, c, tmp, n_samp);
        if (n > n_samp) {
            n = n_samp;
        }
        if (n > 0) {
            memcpy(planar + c * n_samp, tmp, (size_t)n * sizeof(float));
        }
    }
    np_raw_save(path, planar, NP_NCHAN, (int)n_samp, design_sps());
    free(planar);
}

static int raw_load_plate(const char *which, float buf[NP_NCHAN][NP_RING], uint32_t nn[NP_NCHAN])
{
    char path[NP_MAX_PATH];
    float *planar;
    int ch = 0, ns = 0, c;
    float sps = 0.f;
    raw_plate_path(which, path, (int)sizeof(path));
    planar = (float *)malloc((size_t)NP_NCHAN * NP_RING * sizeof(float));
    if (!planar) {
        return -1;
    }
    if (np_raw_load(path, planar, NP_NCHAN * NP_RING, &ch, &ns, &sps) < 1 || ns < 16) {
        free(planar);
        return -1;
    }
    if (ns > NP_RING) {
        ns = NP_RING;
    }
    memset(nn, 0, NP_NCHAN * sizeof(nn[0]));
    memset(buf, 0, sizeof(float) * (size_t)NP_NCHAN * NP_RING);
    for (c = 0; c < ch && c < NP_NCHAN; c++) {
        memcpy(buf[c], planar + c * ns, (size_t)ns * sizeof(float));
        nn[c] = (uint32_t)ns;
    }
    free(planar);
    return 0;
}

static void atom_last(uint64_t *bits, float *rms, int k)
{
    int i;
    if (k < 1) {
        return;
    }
    if (k > g.atom_n) {
        k = g.atom_n;
    }
    for (i = 0; i < k; i++) {
        int idx = (g.atom_wr - k + i + NP_ATOM_RING) % NP_ATOM_RING;
        if (bits) {
            bits[i] = g.atom_live[idx];
        }
        if (rms) {
            memcpy(rms + i * 8, g.atom_live_rms + idx * 8, 8 * sizeof(float));
        }
    }
}

static void atom_flatten(uint64_t *dst, int *n)
{
    int k = g.atom_rec_n > 0 ? g.atom_rec_n : g.atom_n;
    if (k > g.atom_n) {
        k = g.atom_n;
    }
    *n = k;
    atom_last(dst, NULL, k);
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

static float atom_scale(void)
{
    float v[NP_NCHAN];
    int n = 0, c, i, j;
    if (!g.calm.have) {
        return NP_ATOM_SCALE;
    }
    for (c = 0; c < NP_NCHAN; c++) {
        if (g.active[c] && g.calm.rms[c] > 50.f) {
            v[n++] = g.calm.rms[c];
        }
    }
    if (n < 1) {
        return NP_ATOM_SCALE;
    }
    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            if (v[j] < v[i]) {
                float t = v[i];
                v[i] = v[j];
                v[j] = t;
            }
        }
    }
    return v[n / 2];
}

void atom_tick(void)
{
    static uint64_t last;
    struct timespec ts;
    uint64_t now;
    float planar[NP_NCHAN * NP_ATOM_WIN];
    float buf[NP_NCHAN][NP_RING];
    uint32_t nn[NP_NCHAN];
    float scale;
    int c, got = 0, env;
    uint32_t want;

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
    memset(nn, 0, sizeof(nn));
    memset(buf, 0, sizeof(buf));
    for (c = 0; c < NP_NCHAN; c++) {
        if (!g.active[c]) {
            continue;
        }
        /* Bipolar wave, not the envelope plot. Envelope has no ZC/rise. */
        nn[c] = np_ring_copy(&g.ring, c, buf[c], want);
        if (nn[c] >= 32) {
            got++;
        }
    }
    if (got < 1) {
        return;
    }
    memset(atom_raw[g.atom_wr], 0, sizeof(atom_raw[0]));
    for (c = 0; c < NP_NCHAN; c++) {
        uint32_t n = nn[c] > want ? want : nn[c];
        if (n > 0) {
            memcpy(atom_raw[g.atom_wr] + c * NP_ATOM_WIN, buf[c], (size_t)n * sizeof(float));
        }
    }
    env = g.envelope;
    g.envelope = 0;
    cook_all(buf, nn, want);
    g.envelope = env;
    memset(planar, 0, sizeof(planar));
    for (c = 0; c < NP_NCHAN; c++) {
        uint32_t n = nn[c];
        if (n < 32) {
            continue;
        }
        if (n > want) {
            n = want;
        }
        memcpy(planar + c * NP_ATOM_WIN, buf[c], (size_t)n * sizeof(float));
    }
    /* CubalC default 50 µV saturates this head. Scale from worn CALM. */
    scale = atom_scale();
    {
        uint64_t bits = np_atom_pack(planar, NP_NCHAN, NP_ATOM_WIN, NP_ATOM_WIN, scale);
        float rms[8];
        np_atom_rms8(planar, NP_NCHAN, NP_ATOM_WIN, NP_ATOM_WIN, rms);
        g.atom_live[g.atom_wr] = bits;
        memcpy(g.atom_live_rms + g.atom_wr * 8, rms, sizeof(rms));
        g.atom_wr = (g.atom_wr + 1) % NP_ATOM_RING;
        if (g.atom_n < NP_ATOM_RING) {
            g.atom_n++;
        }
        g.atom_seq++;
        if (g.atom_on) {
            g.atom_rec_n++;
            if (g.atom_rec_n > NP_ATOM_RING) {
                g.atom_rec_n = NP_ATOM_RING;
            }
        }
    }
    atom_score();
    atom_identify();
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

/* Last ~0.5 s EXG vs a rolling quiet floor. Shared 2 mV is not a pose. */
static int stream_id(float *ratio)
{
    float rms[NP_NCHAN], base[NP_NCHAN];
    float buf[NP_NCHAN][NP_RING];
    uint32_t nn[NP_NCHAN];
    int fp[NP_NCHAN];
    uint8_t mask = 0;
    int c, nclip = 0, nlive = 0, id;
    uint32_t want = (uint32_t)(0.50f * design_sps());
    float med = 0.f, mx = 0.f;

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
    memset(base, 0, sizeof(base));
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
    if (nclip >= 6) {
        if (ratio) {
            *ratio = (float)nclip;
        }
        return NP_ID_CLIP;
    }
    cook_id(buf, nn);
    for (c = 0; c < NP_NCHAN; c++) {
        float dc = 0, pk = 0;
        if (!g.active[c] || nn[c] < 16) {
            continue;
        }
        ch_stats(buf[c], nn[c], &dc, &rms[c], &pk);
        fp[c] = site_is_fp(c);
        mask |= (uint8_t)(1u << c);
        nlive++;
        if (rms[c] > mx) {
            mx = rms[c];
        }
    }
    if (nlive < 1) {
        return NP_ID_NONE;
    }
    {
        float tmp[NP_NCHAN];
        int n = 0;
        for (c = 0; c < NP_NCHAN; c++) {
            if (mask & (uint8_t)(1u << c)) {
                tmp[n++] = rms[c];
            }
        }
        if (n > 0) {
            int i, j;
            for (i = 0; i < n; i++) {
                for (j = i + 1; j < n; j++) {
                    if (tmp[j] < tmp[i]) {
                        float s = tmp[i];
                        tmp[i] = tmp[j];
                        tmp[j] = s;
                    }
                }
            }
            med = tmp[n / 2];
        }
    }
    /* Lockstep millivolt floor: turn display CAR on so the plot is EXG. */
    if (!g.car && nlive >= 4 && mx > 800.f && med > 400.f && mx < med * 1.25f) {
        g.car = 1;
        if (g.hp_hz < 1) {
            g.hp_hz = 2;
        }
        g.envelope = 0;
        g.detrend = 1;
        filt_reset();
        cfg_save();
        set_status(1, "shared floor — CAR on, ID on EXG");
    }
    if (!id_base_ok) {
        for (c = 0; c < NP_NCHAN; c++) {
            id_base[c] = rms[c] > 8.f ? rms[c] : 25.f;
        }
        id_base_ok = 1;
    }
    for (c = 0; c < NP_NCHAN; c++) {
        base[c] = id_base[c] > 8.f ? id_base[c] : 25.f;
    }
    id = np_id_event(rms, base, fp, mask, 1, ratio);
    if (id == NP_ID_STILL) {
        for (c = 0; c < NP_NCHAN; c++) {
            if (mask & (uint8_t)(1u << c)) {
                id_base[c] = 0.95f * id_base[c] + 0.05f * rms[c];
            }
        }
    }
    return id;
}

void id_label(char *out, int n)
{
    float r = 0.f;
    int id;
    if (g.link && g.link_id[0]) {
        snprintf(out, (size_t)n, "%s", g.link_id);
        return;
    }
    id = stream_id(&r);
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
 * mixed still+noise is why MATCH could not tell a blink from EXG. */
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
    {
        char rpath[NP_MAX_PATH];
        float *planar = (float *)malloc((size_t)NP_NCHAN * want * sizeof(float));
        if (planar && g.namebuf[0]) {
            int c2;
            memset(planar, 0, (size_t)NP_NCHAN * want * sizeof(float));
            for (c2 = 0; c2 < NPL_NCHAN; c2++) {
                if (nn[c2] > 0) {
                    memcpy(planar + c2 * want, buf[c2], (size_t)nn[c2] * sizeof(float));
                }
            }
            raw_named_path("learn", g.namebuf, rpath, (int)sizeof(rpath));
            np_raw_save(rpath, planar, NP_NCHAN, (int)want, sps);
        }
        free(planar);
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

void learn_tick(void)
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

void learn_start_hold(void)
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
            set_status(0, "saved — no burst. Blink hard or clench.");
        }
    }
}

void typing_set(int on)
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

void cfg_save(void);
static void cal_tick(void);
static int cfg_write(const char *path);
static int cfg_write_ex(const char *path, int with_map);
static int cfg_read(const char *path);
void prof_scan(void);
static void prof_apply(void);
static void prof_autosave(void);
static void data_recook(void);

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
    return cfg_write_ex(path, 1);
}

static int cfg_write_ex(const char *path, int with_map)
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
    fprintf(f, "set_gen=%d\n", g.set_gen);
    fprintf(f, "board=%d\n", (int)g.board);
    fprintf(f, "algo=%d\n", g.algo);
    fprintf(f, "\n[cube]\n");
    fprintf(f, "yaw=%.4f\n", (double)g.cube_yaw);
    fprintf(f, "pitch=%.4f\n", (double)g.cube_pitch);
    fprintf(f, "zoom=%.2f\n", (double)g.cube_zoom);
    fprintf(f, "view=%d\n", g.cube_view ? 1 : 0);
    fprintf(f, "float=%d\n", g.cube_float ? 1 : 0);
    if (with_map) {
        fprintf(f, "\n[api]\n");
        fprintf(f, "on=%d\n", g.api_on ? 1 : 0);
        fprintf(f, "bind=%s\n", g.api_lan ? "lan" : "local");
        fprintf(f, "http=%d\n", g.api_http);
        fprintf(f, "udp=%d\n", g.api_udp);
        fprintf(f, "tcp=%d\n", g.api_tcp);
        fprintf(f, "hz=%d\n", g.api_hz);
        if (g.api_token[0]) {
            fprintf(f, "token=%s\n", g.api_token);
        }
        if (g.api_push[0]) {
            fprintf(f, "push=%s\n", g.api_push);
        }
        fprintf(f, "link=%d\n", g.link);
        if (g.link_dest[0]) {
            fprintf(f, "link_dest=%s\n", g.link_dest);
        }
        if (g.link_token[0]) {
            fprintf(f, "link_token=%s\n", g.link_token);
        }
    }
    if (with_map) {
        for (i = 0; i < NP_NCHAN; i++) {
            if (g.elec[i].name[0]) {
                fprintf(f, "elec%d=%s\n", i + 1, g.elec[i].name);
            } else {
                fprintf(f, "elec%d=%.2f,%.2f\n", i + 1, (double)g.elec[i].az,
                        (double)g.elec[i].el);
            }
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

static int cfg_write_kit(const char *path)
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
    fprintf(f, "[view]\n");
    fprintf(f, "window_s=%d\n", g.window_s);
    fprintf(f, "scale_uv=%d\n", g.scale_uv);
    fprintf(f, "notch_hz=%d\n", g.notch_hz);
    fprintf(f, "hp_hz=%d\n", g.hp_hz);
    fprintf(f, "lp_hz=%d\n", g.lp_hz);
    fprintf(f, "car=%d\n", g.car ? 1 : 0);
    fprintf(f, "envelope=%d\n", g.envelope ? 1 : 0);
    fprintf(f, "band=%d\n", g.band);
    fprintf(f, "detrend=%d\n", g.detrend);
    fprintf(f, "cal_cut=%d\n", g.cal_cut);
    fprintf(f, "board=%d\n", (int)g.board);
    fprintf(f, "algo=%d\n", g.algo);
    fprintf(f, "\n[cube]\n");
    fprintf(f, "float=%d\n", g.cube_float ? 1 : 0);
    for (i = 0; i < NP_NCHAN; i++) {
        if (g.elec[i].name[0]) {
            fprintf(f, "elec%d=%s\n", i + 1, g.elec[i].name);
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
        char longv[64];
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
        } else if (sscanf(line, "set_gen=%d", &v) == 1) {
            g.set_gen = v;
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
        } else if (sscanf(line, "float=%d", &v) == 1) {
            g.cube_float = v ? 1 : 0;
        } else if (sscanf(line, "link_dest=%63s", longv) == 1) {
            snprintf(g.link_dest, sizeof(g.link_dest), "%s", longv);
        } else if (sscanf(line, "link_token=%31s", longv) == 1) {
            snprintf(g.link_token, sizeof(g.link_token), "%s", longv);
        } else if (sscanf(line, "link=%d", &v) == 1) {
            if (v < 0) {
                v = 0;
            }
            if (v > 2) {
                v = 2;
            }
            g.link = v;
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
            } else if (sscanf(line, "on=%d", &v) == 1 && strstr(line, "token") == NULL) {
                /* last on= wins; api section uses on= after channels */
                g.api_on = v ? 1 : 0;
            } else if (sscanf(line, "bind=%23s", ename) == 1) {
                g.api_lan = strcmp(ename, "local") != 0;
            } else if (sscanf(line, "http=%d", &v) == 1) {
                g.api_http = v;
            } else if (sscanf(line, "udp=%d", &v) == 1) {
                g.api_udp = v;
            } else if (sscanf(line, "tcp=%d", &v) == 1) {
                g.api_tcp = v;
            } else if (sscanf(line, "hz=%d", &v) == 1 && v >= 1 && v <= 125) {
                g.api_hz = v;
            } else if (sscanf(line, "token=%31s", longv) == 1) {
                snprintf(g.api_token, sizeof(g.api_token), "%s", longv);
            } else if (sscanf(line, "push=%63s", longv) == 1) {
                snprintf(g.api_push, sizeof(g.api_push), "%s", longv);
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

void cfg_save(void)
{
    char path[NP_MAX_PATH], root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    mkdir(root, 0755);
    cfg_path(path, sizeof(path));
    cfg_write(path);
}

void apply_readable_defaults(void)
{
    /* Worn head is 200–300 µV raw; off-head ~1 mV; lockstep floor hides
     * actions. Line-kill EXG is what ID can name. */
    g.band = 1;
    g.notch_hz = -1;
    g.hp_hz = 2;
    g.lp_hz = 0;
    g.car = 1;
    g.envelope = 0;
    g.detrend = 1;
    g.cal_cut = 1;
    g.scale_uv = 1000;
    g.window_s = 2;
}

void cfg_load(void)
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

void prof_scan(void)
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
    if (g.pref_w > 0) {
        np_ui_apply_window_size(g.pref_w, g.pref_h);
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

void prof_save(void)
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
    if (cfg_write_ex(path, 0) != 0) {
        set_status(0, "cannot write profile %s", g.prof);
        return;
    }
    cfg_save();
    prof_scan();
    set_status(1, "saved profile '%s'", g.prof);
}

void prof_load(void)
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
    {
        struct np_elec keep[NP_NCHAN];
        memcpy(keep, g.elec, sizeof(keep));
        if (cfg_read(path) != 0) {
            set_status(0, "no profile '%s'", g.prof);
            return;
        }
        memcpy(g.elec, keep, sizeof(keep));
    }
    prof_apply();
    data_recook();
    cfg_save();
    set_status(1, "profile '%s' — map kept, plates recooked", g.prof);
}

static void prof_autosave(void)
{
    char path[NP_MAX_PATH];
    if (!prof_ok_name(g.prof)) {
        return;
    }
    prof_file(g.prof, path, sizeof(path));
    cfg_write_ex(path, 0);
}

static void prof_del(void)
{
    char path[NP_MAX_PATH];
    if (!prof_ok_name(g.prof)) {
        set_status(0, "no profile to delete");
        return;
    }
    prof_file(g.prof, path, sizeof(path));
    if (unlink(path) != 0) {
        set_status(0, "cannot delete '%s'", g.prof);
        return;
    }
    set_status(1, "deleted profile '%s'", g.prof);
    g.prof[0] = 0;
    prof_scan();
    cfg_save();
}

static void prof_rename(const char *to)
{
    char from[NP_MAX_PATH], dest[NP_MAX_PATH];
    if (!prof_ok_name(g.prof) || !prof_ok_name(to)) {
        set_status(0, "need a valid name");
        return;
    }
    if (strcmp(g.prof, to) == 0) {
        return;
    }
    prof_file(g.prof, from, sizeof(from));
    prof_file(to, dest, sizeof(dest));
    if (rename(from, dest) != 0) {
        set_status(0, "cannot rename to '%s'", to);
        return;
    }
    snprintf(g.prof, sizeof(g.prof), "%s", to);
    prof_scan();
    cfg_save();
    set_status(1, "profile is now '%s'", g.prof);
}

void prof_cycle(void)
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
    prof_load();
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
    if (g.cal_cut && n >= (uint32_t)NP_FFT_N) {
        const float *psd = NULL;
        if ((g.noise_psd_ch_ok & (1u << ch)) != 0) {
            psd = g.noise_psd_ch[ch];
        } else if (g.noise_psd_ok) {
            psd = g.noise_psd;
        }
        if (psd) {
            np_plate_destroy(buf, (int)n, psd);
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

/* ID cook: EXG after shared floor. Not the display envelope. */
static void cook_id(float buf[NP_NCHAN][NP_RING], uint32_t nn[NP_NCHAN])
{
    int c, t;
    uint32_t nmax = 0;
    float sps = design_sps();
    float nh = notch_hz_eff();
    struct np_hp hp[NP_NCHAN];
    struct np_notch nt[NP_NCHAN];

    if (nh < 1.f) {
        nh = 50.f;
    }
    for (c = 0; c < NP_NCHAN; c++) {
        if (!g.active[c] || nn[c] < 16) {
            continue;
        }
        np_hp_init(&hp[c], 2.f, sps);
        np_notch_init(&nt[c], nh, sps, 30.f);
        for (t = 0; t < (int)nn[c]; t++) {
            float v = np_hp_step(&hp[c], buf[c][t]);
            buf[c][t] = np_notch_step(&nt[c], v);
        }
        if (nn[c] > nmax) {
            nmax = nn[c];
        }
    }
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
    for (c = 0; c < NP_NCHAN; c++) {
        if (nn[c] >= 16) {
            np_detrend(buf[c], (int)nn[c]);
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
        g.hp_hz = 2;
        g.lp_hz = 0;
        g.car = 1;
        g.envelope = 0;
        g.detrend = 1;
        g.scale_uv = 1000;
        if (g.cal.have) {
            g.cal_cut = 1;
        }
    } else if (band == NP_BAND_EEG) {
        g.notch_hz = g.cal_hz > 1.f ? -1 : 50;
        g.hp_hz = 2;
        g.lp_hz = 40;
        g.car = 1;
        g.envelope = 0;
        g.detrend = 1;
        g.scale_uv = 200;
        if (g.cal.have) {
            g.cal_cut = 1;
        }
    } else {
        g.notch_hz = 50;
        g.hp_hz = 20;
        g.lp_hz = 0;
        g.car = 1;
        g.envelope = 1;
        g.detrend = 1;
        g.scale_uv = 2000;
    }
    filt_reset();
    cfg_save();
    prof_autosave();
    data_recook();
}

void api_defaults(void)
{
    struct np_api_cfg c;
    np_api_cfg_default(&c);
    g.api_on = c.on;
    g.api_lan = c.lan;
    g.api_http = c.http;
    g.api_udp = c.udp;
    g.api_tcp = c.tcp;
    g.api_hz = c.hz;
    g.api_token[0] = 0;
    snprintf(g.api_push, sizeof(g.api_push), "%s", c.push);
}

static void api_json_esc(const char *in, char *out, int n)
{
    int o = 0;
    if (!out || n < 2) {
        return;
    }
    out[0] = 0;
    if (!in) {
        return;
    }
    for (; *in && o < n - 2; in++) {
        unsigned char c = (unsigned char)*in;
        if (c == '"' || c == '\\') {
            if (o + 2 >= n) {
                break;
            }
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c < 32) {
            out[o++] = ' ';
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = 0;
}

static void api_status_json(char *out, int n)
{
    char st[160], id[80], ste[180], ide[96], line[96];
    unsigned mask = 0;
    int c;
    np_host_status(st, sizeof(st));
    np_host_id(id, sizeof(id));
    np_api_line(line, sizeof(line));
    api_json_esc(st, ste, sizeof(ste));
    api_json_esc(id, ide, sizeof(ide));
    for (c = 0; c < NP_NCHAN; c++) {
        if (g.active[c]) {
            mask |= 1u << c;
        }
    }
    snprintf(out, (size_t)n,
             "{\"ok\":true,\"v\":\"2.55\",\"connected\":%s,\"paused\":%s,\"sps\":%.1f,"
             "\"frames\":%u,\"status\":\"%s\",\"id\":\"%s\",\"id_best\":%d,"
             "\"notch\":%d,\"hp\":%d,\"lp\":%d,\"car\":%d,\"band\":%d,\"mask\":%u,"
             "\"api\":\"%s\"}",
             g.connected ? "true" : "false", g.paused ? "true" : "false",
             g.sps > 1.f ? (double)g.sps : 0.0, np_host_frames(), ste, ide, g.atom_id_best,
             g.notch_hz, g.hp_hz, g.lp_hz, g.car ? 1 : 0, g.band, mask, line);
}

static void api_view_json(char *out, int n)
{
    char id[80], ide[96];
    int c;
    if (!out || n < 8) {
        return;
    }
    np_host_id(id, sizeof(id));
    api_json_esc(id, ide, sizeof(ide));
    snprintf(out, (size_t)n,
             "\"notch\":%d,\"hp\":%d,\"lp\":%d,\"car\":%d,\"detrend\":%d,\"env\":%d,"
             "\"band\":%d,\"scale_uv\":%d,\"window_s\":%d,"
             "\"color\":[[%d,%d,%d],[%d,%d,%d],[%d,%d,%d],[%d,%d,%d],"
             "[%d,%d,%d],[%d,%d,%d],[%d,%d,%d],[%d,%d,%d]],"
             "\"elec\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"],"
             "\"active\":[%d,%d,%d,%d,%d,%d,%d,%d],\"id\":\"%s\"",
             g.notch_hz, g.hp_hz, g.lp_hz, g.car ? 1 : 0, g.detrend ? 1 : 0,
             g.envelope ? 1 : 0, g.band, g.scale_uv, g.window_s, g.chrgb[0][0],
             g.chrgb[0][1], g.chrgb[0][2], g.chrgb[1][0], g.chrgb[1][1], g.chrgb[1][2],
             g.chrgb[2][0], g.chrgb[2][1], g.chrgb[2][2], g.chrgb[3][0], g.chrgb[3][1],
             g.chrgb[3][2], g.chrgb[4][0], g.chrgb[4][1], g.chrgb[4][2], g.chrgb[5][0],
             g.chrgb[5][1], g.chrgb[5][2], g.chrgb[6][0], g.chrgb[6][1], g.chrgb[6][2],
             g.chrgb[7][0], g.chrgb[7][1], g.chrgb[7][2], g.elec[0].name, g.elec[1].name,
             g.elec[2].name, g.elec[3].name, g.elec[4].name, g.elec[5].name, g.elec[6].name,
             g.elec[7].name, g.active[0] ? 1 : 0, g.active[1] ? 1 : 0, g.active[2] ? 1 : 0,
             g.active[3] ? 1 : 0, g.active[4] ? 1 : 0, g.active[5] ? 1 : 0,
             g.active[6] ? 1 : 0, g.active[7] ? 1 : 0, ide);
    (void)c;
}

static int cfg_jint(const char *js, const char *key, int *out)
{
    char pat[40];
    const char *p;
    if (!js || !key || !out) {
        return 0;
    }
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(js, pat);
    if (!p) {
        return 0;
    }
    p = strchr(p, ':');
    if (!p) {
        return 0;
    }
    p++;
    while (*p == ' ') {
        p++;
    }
    if (!(*p == '-' || (*p >= '0' && *p <= '9'))) {
        return 0;
    }
    *out = atoi(p);
    return 1;
}

static void apply_link_cfg(const char *js)
{
    int v, c;
    const char *p;
    if (!js || !js[0]) {
        return;
    }
    if (cfg_jint(js, "notch", &v)) {
        g.notch_hz = v;
    }
    if (cfg_jint(js, "hp", &v)) {
        g.hp_hz = v;
    }
    if (cfg_jint(js, "lp", &v)) {
        g.lp_hz = v;
    }
    if (cfg_jint(js, "car", &v)) {
        g.car = v ? 1 : 0;
    }
    if (cfg_jint(js, "detrend", &v)) {
        g.detrend = v ? 1 : 0;
    }
    if (cfg_jint(js, "env", &v)) {
        g.envelope = v ? 1 : 0;
    }
    if (cfg_jint(js, "band", &v) && v >= 0 && v < NP_BAND_N) {
        g.band = v;
    }
    if (cfg_jint(js, "scale_uv", &v) && v >= 20) {
        g.scale_uv = v;
    }
    if (cfg_jint(js, "window_s", &v) && v >= 1 && v <= 8) {
        g.window_s = v;
    }
    p = strstr(js, "\"color\":");
    if (p) {
        p = strchr(p, '[');
        if (p) {
            p++;
            for (c = 0; c < NP_NCHAN; c++) {
                int r = 0, gc = 0, b = 0;
                const char *q = strchr(p, '[');
                if (!q) {
                    break;
                }
                if (sscanf(q, "[%d,%d,%d]", &r, &gc, &b) == 3) {
                    g.chrgb[c][0] = r;
                    g.chrgb[c][1] = gc;
                    g.chrgb[c][2] = b;
                }
                p = q + 1;
            }
        }
    }
    p = strstr(js, "\"elec\":");
    if (p) {
        p = strchr(p, '[');
        if (p) {
            p++;
            for (c = 0; c < NP_NCHAN; c++) {
                char name[8];
                const char *q = strchr(p, '"');
                int i = 0;
                if (!q) {
                    break;
                }
                q++;
                while (*q && *q != '"' && i < 7) {
                    name[i++] = *q++;
                }
                name[i] = 0;
                if (name[0]) {
                    int s = np_1010_find(name);
                    if (s >= 0) {
                        np_elec_set_site(&g.elec[c], s);
                    }
                }
                p = q + 1;
            }
        }
    }
    p = strstr(js, "\"active\":[");
    if (p) {
        p = strchr(p, '[');
        if (p) {
            p++;
            for (c = 0; c < NP_NCHAN; c++) {
                while (*p == ' ' || *p == ',') {
                    p++;
                }
                if (*p == '0' || *p == '1') {
                    g.active[c] = *p == '1';
                    p++;
                }
            }
        }
    }
    p = strstr(js, "\"id\":\"");
    if (p) {
        p += 6;
        snprintf(g.link_id, sizeof(g.link_id), "%s", p);
        for (c = 0; g.link_id[c]; c++) {
            if (g.link_id[c] == '"') {
                g.link_id[c] = 0;
                break;
            }
        }
    }
}

static void link_on_sample(const struct np_api_sample *s)
{
    int c;
    if (!s) {
        return;
    }
    pthread_mutex_lock(&live_mu);
    for (c = 0; c < NP_NCHAN; c++) {
        live_ch[c][live_wr % NP_RING] = s->uv[c];
        if (s->mask & (uint8_t)(1u << c)) {
            g.active[c] = 1;
        }
    }
    live_wr++;
    live_seen++;
    pthread_mutex_unlock(&live_mu);
    g.connected = 1;
    g.sps = s->sps;
    g.paused = (s->flags & 2) ? 1 : 0;
    if (!g.status_ok) {
        set_status(1, "following EXG");
    }
}

void api_apply(void)
{
    struct np_api_cfg c;
    memset(&c, 0, sizeof(c));
    c.on = g.api_on;
    c.lan = g.api_lan;
    c.http = g.api_http;
    c.udp = g.api_udp;
    c.tcp = g.api_tcp;
    c.hz = g.api_hz;
    snprintf(c.token, sizeof(c.token), "%s", g.api_token);
    snprintf(c.push, sizeof(c.push), "%s", g.api_push);
    np_api_set_status_fn(api_status_json);
    np_api_set_view_fn(api_view_json);
    np_api_set_grant_fn(host_grant_ok);
    np_api_set_kit_fn(np_host_kit_export, np_host_kit_import);
    np_api_apply(&c);
}

static void api_emit(const float *v, uint32_t frames)
{
    static uint32_t hold;
    struct np_api_sample s;
    struct timespec ts;
    int c, hz, sps;
    if (!np_api_on() || !v) {
        return;
    }
    hz = np_api_hz();
    sps = (int)design_sps();
    if (sps < 1) {
        sps = 125;
    }
    if (hz < 1) {
        hz = sps;
    }
    hold += (uint32_t)hz;
    if (hold < (uint32_t)sps) {
        return;
    }
    hold -= (uint32_t)sps;
    memset(&s, 0, sizeof(s));
    clock_gettime(CLOCK_REALTIME, &ts);
    s.t_us = (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
    s.frames = frames;
    s.nch = NP_NCHAN;
    for (c = 0; c < NP_NCHAN; c++) {
        s.uv[c] = v[c];
        if (g.active[c]) {
            s.mask = (uint8_t)(s.mask | (1u << c));
        }
        if (np_sample_rail(v[c]) || last_clip[c]) {
            s.clip = (uint8_t)(s.clip | (1u << c));
        }
    }
    s.flags = (uint8_t)((g.connected ? 1 : 0) | (g.paused ? 2 : 0) |
                        (g.learn.match ? 4 : 0));
    s.sps = g.sps;
    s.id_best = (int8_t)g.atom_id_best;
    s.id_score = (g.atom_id_best >= 0 && g.atom_id_best < 32) ? g.atom_id[g.atom_id_best] : 0.f;
    np_api_push(&s);
}

static void api_drain(void)
{
    int op, arg;
    int need = 0;
    while (np_api_take_op(&op, &arg)) {
        switch (op) {
        case NP_API_OP_CONNECT:
            np_host_connect();
            break;
        case NP_API_OP_DISC:
            np_host_disconnect();
            break;
        case NP_API_OP_PAUSE:
            np_host_toggle_pause();
            break;
        case NP_API_OP_NOTCH:
            np_host_set_notch(arg);
            break;
        case NP_API_OP_HP:
            np_host_set_hp(arg);
            break;
        case NP_API_OP_LP:
            np_host_set_lp(arg);
            break;
        case NP_API_OP_CAR:
            if ((arg ? 1 : 0) != (g.car ? 1 : 0)) {
                np_host_toggle_car();
            }
            break;
        case NP_API_OP_BAND:
            np_host_set_band(arg);
            break;
        case NP_API_OP_HZ:
            g.api_hz = arg < 1 ? 1 : (arg > 125 ? 125 : arg);
            need = 1;
            break;
        case NP_API_OP_LAN:
            g.api_lan = arg ? 1 : 0;
            need = 1;
            break;
        case NP_API_OP_ON:
            g.api_on = arg ? 1 : 0;
            need = 1;
            break;
        case NP_API_OP_HTTP:
            g.api_http = arg;
            need = 1;
            break;
        case NP_API_OP_UDP:
            g.api_udp = arg;
            need = 1;
            break;
        case NP_API_OP_TCP:
            g.api_tcp = arg;
            need = 1;
            break;
        default:
            break;
        }
    }
    if (need) {
        cfg_save();
        api_apply();
    }
}

/* One IIR step per new sample. Display copies this. Never re-filter the window. */
static void live_sync_u(void)
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
        api_emit(v, (uint32_t)(live_seen + i + 1));
    }
    live_wr += need;
    live_seen = tot;
}

static void live_sync(void)
{
    pthread_mutex_lock(&live_mu);
    live_sync_u();
    pthread_mutex_unlock(&live_mu);
}

static uint32_t live_copy(int ch, float *dst, uint32_t n)
{
    uint32_t have, i, start;
    pthread_mutex_lock(&live_mu);
    live_sync_u();
    have = live_seen < NP_RING ? (uint32_t)live_seen : NP_RING;
    if (n > have) {
        n = have;
    }
    start = (live_wr + NP_RING - n) % NP_RING;
    for (i = 0; i < n; i++) {
        dst[i] = live_ch[ch][(start + i) % NP_RING];
    }
    pthread_mutex_unlock(&live_mu);
    return n;
}

static float cook_scale_ch(int c)
{
    float sc = 25.f;
    if (c < 0 || c >= NP_NCHAN) {
        return sc;
    }
    if (id_base_ok && id_base[c] > sc) {
        sc = id_base[c];
    } else if (g.calm.have && g.calm.rms[c] > sc) {
        sc = g.calm.rms[c];
    }
    return sc;
}

static void cook_now(float uv[8], float base[8])
{
    float buf[NP_RING];
    int c;
    if (uv) {
        memset(uv, 0, 8 * sizeof(float));
    }
    if (base) {
        memset(base, 0, 8 * sizeof(float));
    }
    for (c = 0; c < NP_NCHAN; c++) {
        float dc = 0, rms = 0, pk = 0, sc;
        uint32_t n;
        sc = cook_scale_ch(c);
        if (base) {
            base[c] = sc;
        }
        if (!g.active[c]) {
            continue;
        }
        n = view_copy(c, buf, 32);
        if (n < 4) {
            continue;
        }
        ch_stats(buf, n, &dc, &rms, &pk);
        if (uv) {
            uv[c] = rms;
        }
    }
}

/* CLEAN STFT at ~12 Hz, not 60×8. Plot uses the last cooked window. */
uint32_t view_copy(int ch, float *dst, uint32_t n)
{
    uint32_t got, c;
    uint64_t tot = 0;
    uint32_t now;

    got = live_copy(ch, dst, n);
    if (!(g.cal_cut && (g.noise_psd_ok || g.noise_psd_ch_ok) &&
          got >= (uint32_t)NP_FFT_N)) {
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
                const float *psd = NULL;
                if ((g.noise_psd_ch_ok & (1u << c)) != 0) {
                    psd = g.noise_psd_ch[c];
                } else if (g.noise_psd_ok) {
                    psd = g.noise_psd;
                }
                if (psd) {
                    np_plate_destroy(clean_ch[c], (int)m, psd);
                }
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

void smx_tick(void)
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
    {
        uint64_t atom = 0;
        for (c = 0; c < NP_NCHAN; c++) {
            uint32_t n;
            float dc = 0, rms = 0, pk = 0, sc;
            uint8_t row = 0;
            if (!g.active[c]) {
                continue;
            }
            mask |= (uint8_t)(1u << c);
            n = view_copy(c, buf, want);
            if (n > 32) {
                memmove(buf, buf + (n - 32), 32 * sizeof(float));
                n = 32;
            }
            ch_stats(buf, n, &dc, &rms, &pk);
            sc = cook_scale_ch(c);
            if (n >= 8) {
                uint64_t one = np_atom_pack_rel(buf, 1, (int)n, (int)n, &sc);
                row = (uint8_t)(one & 0xffu);
            }
            atom |= (uint64_t)row << (8 * c);
            bits[nch] = (row & 0x16u) ? 1 : 0;
            nch++;
        }
        np_atom_faces8(atom, g.cube_bits);
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

void ensure_dialout(int argc, char **argv)
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

void set_status(int ok, const char *fmt, ...)
{
    va_list ap;
    pthread_mutex_lock(&g.mu);
    g.status_ok = ok;
    va_start(ap, fmt);
    vsnprintf(g.status, sizeof(g.status), fmt, ap);
    va_end(ap);
    pthread_mutex_unlock(&g.mu);
}

void cmd_push(int op, int ch, int gain)
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
            usleep(400);
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
                live_sync();
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
        if (g.active[c]) {
            cmd_push(g.rld[c] ? CMD_RLDADD : CMD_RLDRM, c + 1, 0);
        }
    }
    cmd_drain(25000);
    if (g.connected) {
        set_status(1, "connected %s", g.nports ? g.ports[g.port_i] : "");
        if (!g.cal.have) {
            g.cal_phase = 5;
            g.cal_t0 = 0;
            set_status(1, "Put it on the desk… 5s");
        }
    }
    g.en_running = 0;
    return NULL;
}

void stream_recover(void)
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

void do_connect(void)
{
    const char *path;
    if (g.connected) {
        return;
    }
    if (g.link == 2) {
        set_status(0, "pick EXG nearby");
        return;
    }
    if (g.link == 1) {
        if (!g.link_dest[0] || !strncmp(g.link_dest, "bt:", 3)) {
            set_status(0, "no EXG on LAN — pair first or pick a saved share");
            return;
        }
        np_link_set_hooks(link_on_sample, apply_link_cfg);
        if (np_link_start(g.link_dest, g.link_token) != 0) {
            set_status(0, "could not reach EXG on LAN");
            return;
        }
        g.connected = 1;
        g.stall_t = SDL_GetTicks();
        set_status(1, "following EXG on LAN — waiting");
        return;
    }
    g.nports = np_list_ports(g.ports, NP_MAX_PORTS);
    if (g.nports <= 0) {
        set_status(0, NP_TOUCH ? "no Knight on USB — try LAN or Bluetooth"
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

void do_disconnect(void)
{
    if (!g.connected) {
        return;
    }
    g.connected = 0;
    id_base_ok = 0;
    if (g.link) {
        np_link_stop();
        g.link_id[0] = 0;
        set_status(1, "disconnected");
        return;
    }
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

static void csv_close(void)
{
    g.recording = 0;
    pthread_mutex_lock(&g.csv_mu);
    if (g.csv) {
        fclose(g.csv);
        g.csv = NULL;
    }
    pthread_mutex_unlock(&g.csv_mu);
}

static int csv_open(FILE *f, const char *label)
{
    if (!f) {
        return -1;
    }
    setvbuf(f, NULL, _IOFBF, 8192);
    fprintf(f, "time,seq,ch1,ch2,ch3,ch4,ch5,ch6,ch7,ch8,loff_p,loff_n\n");
    pthread_mutex_lock(&g.csv_mu);
    if (g.csv) {
        fclose(g.csv);
    }
    g.csv = f;
    pthread_mutex_unlock(&g.csv_mu);
    g.recording = 1;
    snprintf(g.csv_path, sizeof(g.csv_path), "%s", label && label[0] ? label : "csv");
    set_status(1, "recording %s", g.csv_path);
    return 0;
}

int np_host_csv_begin(const char *path)
{
    FILE *f;
    if (!g.connected) {
        set_status(0, "connect before record");
        return -1;
    }
    if (!path || !path[0]) {
        return -1;
    }
    if (g.recording) {
        csv_close();
    }
    f = fopen(path, "w");
    if (!f) {
        set_status(0, "cannot write %s", path);
        return -1;
    }
    return csv_open(f, path);
}

int np_host_csv_begin_fd(int fd, const char *name)
{
    FILE *f;
    if (!g.connected) {
        if (fd >= 0) {
            close(fd);
        }
        set_status(0, "connect before record");
        return -1;
    }
    if (fd < 0) {
        return -1;
    }
    if (g.recording) {
        csv_close();
    }
    f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        set_status(0, "cannot write CSV");
        return -1;
    }
    return csv_open(f, name && name[0] ? name : "exg.csv");
}

void toggle_record(void)
{
    if (!g.connected) {
        set_status(0, "connect before record");
        return;
    }
    if (g.recording) {
        csv_close();
        set_status(1, "saved %s", g.csv_path);
        return;
    }
    {
        time_t t = time(NULL);
        struct tm tm;
        FILE *f;
        char stamp[40], root[NP_MAX_PATH];
        localtime_r(&t, &tm);
        strftime(stamp, sizeof(stamp), "knight-%Y%m%d-%H%M%S.csv", &tm);
        np_cfg_root(root, sizeof(root));
        mkdir(root, 0755);
        snprintf(g.csv_path, sizeof(g.csv_path), "%s/%s", root, stamp);
        f = fopen(g.csv_path, "w");
        if (!f) {
            set_status(0, "cannot write %s", g.csv_path);
            return;
        }
        csv_open(f, g.csv_path);
    }
}

void chcol_cycle(int c)
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

int ch_quality(int c, const float *buf, uint32_t n, uint8_t lp, uint8_t ln)
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

void ch_stats(const float *buf, uint32_t n, float *dc, float *rms, float *pk)
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

int cal_save(void)
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

int cal_load(void)
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
        g.cal_phase = g.calm.have ? 4 : 2;
    }
    return got > 0 ? 0 : -1;
}

void cal_capture(void)
{
    int c;
    uint32_t want = plate_want();
    {
        char rp[NP_MAX_PATH];
        raw_plate_path("noise", rp, (int)sizeof(rp));
        raw_dump_ring(rp, want);
    }
    memset(&g.cal, 0, sizeof(g.cal));
    g.cal_hz = 0.f;
    g.noise_psd_ok = 0;
    g.noise_psd_ch_ok = 0;
    memset(g.noise_psd, 0, sizeof(g.noise_psd));
    memset(g.noise_psd_ch, 0, sizeof(g.noise_psd_ch));
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
            if (take >= (uint32_t)NP_FFT_N) {
                np_welch_psd(buf, (int)take, g.noise_psd_ch[c]);
                g.noise_psd_ch_ok |= 1u << c;
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

void calm_capture(void)
{
    int c;
    uint32_t want = plate_want();
    if (!g.cal.have) {
        set_status(0, "NOISE first (desk / headset off, then OK)");
        return;
    }
    {
        char rp[NP_MAX_PATH];
        raw_plate_path("calm", rp, (int)sizeof(rp));
        raw_dump_ring(rp, want);
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
    set_status(1, "Still plate  ch1 resid %.0f uV", g.calm.rms[0]);
}
void fft_refresh(void)
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
void cube_zoom_clamp(void)
{
    if (g.cube_zoom < 0.70f) {
        g.cube_zoom = 0.70f;
    }
    if (g.cube_zoom > 2.80f) {
        g.cube_zoom = 2.80f;
    }
}

void cube_zoom_by(int dir)
{
    g.cube_zoom += dir > 0 ? 0.20f : -0.20f;
    cube_zoom_clamp();
}

void cube_site_by(int dir)
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

int cube_virt_slot(int focus)
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

void cube_virt_by(int dir)
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

void cube_assign_focus(void)
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
void next_gain(int ch)
{
    int i;
    for (i = 0; i < NP_NGAINS; i++) {
        if (NP_GAINS[i] == g.gain[ch]) {
            g.gain[ch] = NP_GAINS[(i + 1) % NP_NGAINS];
            break;
        }
    }
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
    apply_readable_defaults();
    g.grid = 1;
    g.show_uv = 1;
    g.ui_scale = 15;
    g.pref_w = 1280;
    g.pref_h = 720;
    g.cube_yaw = 0.55f;
    g.cube_pitch = 0.40f;
    g.cube_zoom = 1.0f;
    g.cube_float = 1;
    api_defaults();
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
    {
        char pp[NP_MAX_PATH];
        peers_path(pp, (int)sizeof(pp));
        np_peers_load(&peers, pp);
    }
    if (g.set_gen < 1) {
        apply_readable_defaults();
        g.set_gen = 1;
        filt_reset();
        cfg_save();
    }
    if (g.set_gen < 2) {
        g.api_on = 0;
        g.set_gen = 2;
        cfg_save();
    }
    if (g.set_gen < 3) {
        np_elec_default(g.elec);
        g.set_gen = 3;
        cfg_save();
    }
    if (g.set_gen < 4) {
        int c;
        for (c = 0; c < NP_NCHAN; c++) {
            g.chrgb[c][0] = CHCOL[c][0];
            g.chrgb[c][1] = CHCOL[c][1];
            g.chrgb[c][2] = CHCOL[c][2];
        }
        g.set_gen = 4;
        cfg_save();
    }
    if (g.api_http == 8788) {
        g.api_http = 8765;
    }
    if (!strncmp(g.api_push, "192.", 4)) {
        g.api_push[0] = 0;
    }
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
    api_apply();
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
    np_api_stop();
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
    {
        uint32_t nn[NP_NCHAN];
        float left[NP_NCHAN][NP_RING];
        int k;
        fprintf(f, "after_car\n");
        fprintf(f, "ch,resid_rms,resid_pk\n");
        for (k = 0; k < NP_NCHAN; k++) {
            nn[k] = n0;
            memcpy(left[k], raw[k], (size_t)n0 * sizeof(float));
        }
        cook_id(left, nn);
        for (k = 0; k < NP_NCHAN; k++) {
            float dc = 0, rms = 0, pk = 0;
            ch_stats(left[k], nn[k], &dc, &rms, &pk);
            fprintf(f, "%d,%.3f,%.3f\n", k + 1, rms, pk);
        }
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
    api_drain();
    if (g.link) {
        np_link_poll();
    } else {
        live_sync();
    }
    smx_tick();
    atom_tick();
    learn_tick();
    live_snap();
    cal_tick();
    if (g.connected && !g.en_running && !g.link) {
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
    if (g.link) {
        return (unsigned int)(live_seen > 0 ? live_seen : 0);
    }
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
    cfg_save();
    if (g.connected) {
        cmd_push(g.rld[ch] ? CMD_RLDADD : CMD_RLDRM, ch + 1, 0);
        set_status(1, "ch%d bias %s", ch + 1, g.rld[ch] ? "on" : "off");
    } else {
        set_status(1, "ch%d bias %s (applies on Connect)", ch + 1, g.rld[ch] ? "on" : "off");
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
#define CAL_DESK_MS 8000u
#define CAL_WEAR_MS 8000u
#define CAL_PLACE_MS 5000u

static void cal_tick(void)
{
    uint32_t now, dt;
    if (g.cal_phase != 1 && g.cal_phase != 3 && g.cal_phase != 5) {
        return;
    }
    if (!g.connected || (g.sps > 0.f && g.sps < 80.f)) {
        return;
    }
    now = SDL_GetTicks();
    if (!g.cal_t0) {
        g.cal_t0 = now ? now : 1;
    }
    dt = now - g.cal_t0;
    if (g.cal_phase == 5 && dt >= CAL_PLACE_MS) {
        g.cal_phase = 1;
        g.cal_t0 = now ? now : 1;
        set_status(1, "Desk plate — leave it down");
        return;
    }
    if (g.cal_phase == 1 && dt >= CAL_DESK_MS) {
        cal_capture();
        g.cal_cut = 1;
        g.cal_phase = 2;
        cfg_save();
        set_status(1, "Wear the headset, sit still, tap Calibrate");
    } else if (g.cal_phase == 3 && dt >= CAL_WEAR_MS) {
        calm_capture();
        g.cal_phase = 4;
        cfg_save();
        set_status(1, "Calibrated");
        clean_set_status();
    }
}

void np_host_cal_start(void)
{
    uint32_t now = SDL_GetTicks();
    if (!g.connected) {
        set_status(0, "connect first");
        return;
    }
    if (g.sps > 0.f && g.sps < 80.f) {
        set_status(0, "wait for 125 sps");
        return;
    }
    if (g.cal_phase == 1 || g.cal_phase == 3 || g.cal_phase == 5) {
        return;
    }
    if (g.cal_phase == 2 || (g.cal.have && !g.calm.have)) {
        g.cal_phase = 3;
        g.cal_t0 = now ? now : 1;
        set_status(1, "Sit still…");
        return;
    }
    g.cal_phase = 5;
    g.cal_t0 = now ? now : 1;
    set_status(1, "Put it on the desk… 5s");
}

int np_host_cal_phase(void)
{
    return g.cal_phase;
}

int np_host_cal_progress(void)
{
    uint32_t now, dt, need;
    if (g.cal_phase != 1 && g.cal_phase != 3 && g.cal_phase != 5) {
        return g.cal_phase == 4 ? 100 : 0;
    }
    now = SDL_GetTicks();
    dt = now - g.cal_t0;
    need = g.cal_phase == 5 ? CAL_PLACE_MS : (g.cal_phase == 1 ? CAL_DESK_MS : CAL_WEAR_MS);
    if (dt >= need) {
        return 99;
    }
    return (int)(dt * 100u / need);
}

void np_host_cal_line(char *out, int n)
{
    int left;
    if (!out || n < 4) {
        return;
    }
    if (g.cal_phase == 5) {
        left = (int)(CAL_PLACE_MS / 1000u) - (int)((SDL_GetTicks() - g.cal_t0) / 1000u);
        if (left < 1) {
            left = 1;
        }
        snprintf(out, (size_t)n, "Put it down… %ds", left);
    } else if (g.cal_phase == 1) {
        left = (int)(CAL_DESK_MS / 1000u) - (int)((SDL_GetTicks() - g.cal_t0) / 1000u);
        if (left < 0) {
            left = 0;
        }
        snprintf(out, (size_t)n, "Desk… %ds  leave it down", left);
    } else if (g.cal_phase == 2) {
        snprintf(out, (size_t)n, "Wear it — tap");
    } else if (g.cal_phase == 3) {
        left = (int)(CAL_WEAR_MS / 1000u) - (int)((SDL_GetTicks() - g.cal_t0) / 1000u);
        if (left < 0) {
            left = 0;
        }
        snprintf(out, (size_t)n, "Sit still… %ds", left);
    } else if (g.cal.have && g.calm.have) {
        snprintf(out, (size_t)n, "Calibrated");
    } else {
        snprintf(out, (size_t)n, "Calibrate");
    }
}

void np_host_noise_arm(void)
{
    np_host_cal_start();
}
void np_host_noise_ok(void)
{
    if (g.cal_phase == 1) {
        cal_capture();
        g.cal_cut = 1;
        g.cal_phase = 2;
        cfg_save();
        set_status(1, "Wear the headset, sit still, tap Calibrate");
        return;
    }
    np_host_cal_start();
}
void np_host_calm(void)
{
    if (g.cal_phase == 2 || g.cal.have) {
        g.cal_phase = 3;
        g.cal_t0 = SDL_GetTicks();
        if (!g.cal_t0) {
            g.cal_t0 = 1;
        }
        set_status(1, "Sit still…");
        return;
    }
    calm_capture();
}
void np_host_toggle_clean(void)
{
    g.cal_cut = !g.cal_cut;
    cfg_save();
    clean_set_status();
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
int np_host_clean_live(void)
{
    return clean_wiener_ready();
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
        g.atom_id_best = -1;
        set_status(1, np_host_atom_count() > 0 ? "ID off" : "MATCH off");
    } else if (np_host_atom_count() > 0) {
        set_status(1, "ID on — unique winner only");
    } else if (g.learn.n < 1) {
        set_status(0, "MATCH on — Record a pose first");
    } else {
        set_status(1, "MATCH on — names a unique pose, no percent");
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
    {
        char rp[NP_MAX_PATH];
        raw_named_path("learn", g.learn.s[i].name, rp, (int)sizeof(rp));
        unlink(rp);
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
int np_host_prof_del(void)
{
    prof_del();
    return g.prof[0] ? -1 : 0;
}
int np_host_prof_rename(const char *to)
{
    prof_rename(to);
    return prof_ok_name(g.prof) && to && strcmp(g.prof, to) == 0 ? 0 : -1;
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
    if (g.link) {
        return;
    }
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
    float uv[8];
    int c;
    if (!dst) {
        return;
    }
    memcpy(dst, g.smx.cube, 512);
    cook_now(uv, NULL);
    for (c = 0; c < NP_NCHAN; c++) {
        int ix, iy, iz, idx;
        if (!g.active[c] || g.elec[c].site < 0) {
            continue;
        }
        if (uv[c] < 0.5f * cook_scale_ch(c)) {
            continue;
        }
        np_1010_ijk(g.elec[c].site, &ix, &iy, &iz);
        idx = np_cube_idx(ix, iy, iz);
        if (idx >= 0) {
            dst[idx] = 1;
        }
    }
}

void np_host_cook_uv(float uv[8])
{
    cook_now(uv, NULL);
}

int np_host_pair_n(void)
{
    return np_pair_count();
}

void np_host_pair_label(int i, char *out, int n)
{
    if (!out || n < 4) {
        return;
    }
    snprintf(out, (size_t)n, "%s-%s", np_pair_site_a(i), np_pair_site_b(i));
}

int np_host_pair_chs(int i, int *a, int *b)
{
    return np_pair_chs(g.elec, i, a, b);
}

void np_host_pair_uv(float uv[4])
{
    float a[NP_RING], b[NP_RING], d[NP_RING];
    int p;
    if (!uv) {
        return;
    }
    memset(uv, 0, 4 * sizeof(float));
    for (p = 0; p < NP_PAIR_N; p++) {
        int ca, cb;
        uint32_t na, nb, n, i;
        float dc = 0, rms = 0, pk = 0;
        if (np_pair_chs(g.elec, p, &ca, &cb) != 0) {
            continue;
        }
        if (!g.active[ca] || !g.active[cb]) {
            continue;
        }
        na = view_copy(ca, a, 32);
        nb = view_copy(cb, b, 32);
        n = na < nb ? na : nb;
        if (n < 4) {
            continue;
        }
        for (i = 0; i < n; i++) {
            d[i] = a[i] - b[i];
        }
        ch_stats(d, n, &dc, &rms, &pk);
        uv[p] = rms;
    }
}
int np_host_notch(void)
{
    return g.notch_hz;
}
int np_host_notch_eff(void)
{
    float hz = notch_hz_eff();
    if (hz < 1.f) {
        return 0;
    }
    return (int)(hz + 0.5f);
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
    prof_autosave();
    data_recook();
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
    prof_autosave();
    data_recook();
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
    prof_autosave();
    data_recook();
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
    prof_autosave();
    data_recook();
}
int np_host_detrend(void)
{
    return g.detrend;
}
void np_host_toggle_detrend(void)
{
    g.detrend = !g.detrend;
    cfg_save();
    prof_autosave();
    data_recook();
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
    prof_autosave();
    data_recook();
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
    set_status(1, g.cube_view ? "map  assign 10-10 sites" : "viz  crimson cube");
}
int np_host_cube_float(void)
{
    return g.cube_float ? 1 : 0;
}
void np_host_toggle_cube_float(void)
{
    g.cube_float = !g.cube_float;
    cfg_save();
    set_status(1, g.cube_float ? "float on" : "float off");
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
    return n;
}

unsigned int np_host_smx_seq(void)
{
    return g.smx.seq;
}

unsigned int np_host_smx_fold(void)
{
    return np_smx_fold_ch(&g.smx);
}

int np_host_prof_export(const char *path)
{
    return cfg_write_ex(path, 0);
}

int np_host_prof_import(const char *path)
{
    struct np_elec keep[NP_NCHAN];
    memcpy(keep, g.elec, sizeof(keep));
    if (cfg_read(path) != 0) {
        set_status(0, "cannot read profile file");
        return -1;
    }
    memcpy(g.elec, keep, sizeof(keep));
    prof_apply();
    data_recook();
    cfg_save();
    set_status(1, "opened profile — map kept, plates recooked");
    return 0;
}

static void kit_tmp(char *out, int n)
{
    char root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    snprintf(out, (size_t)n, "%s/exg-c.kit", root);
}

int np_host_kit_export(char *out, int cap)
{
    char tmp[NP_MAX_PATH], pfile[NP_MAX_PATH];
    FILE *f;
    int n = 0, i;
    if (!out || cap < 64) {
        return 0;
    }
    kit_tmp(tmp, (int)sizeof(tmp));
    if (cfg_write_kit(tmp) != 0) {
        return 0;
    }
    f = fopen(tmp, "r");
    if (!f) {
        return 0;
    }
    n = (int)fread(out, 1, (size_t)(cap - 1), f);
    fclose(f);
    if (n < 0) {
        n = 0;
    }
    for (i = 0; i < g.nprof && n < cap - 80; i++) {
        FILE *pf;
        int r;
        n += snprintf(out + n, (size_t)(cap - n), "\n#profile %s\n", g.profiles[i]);
        prof_file(g.profiles[i], pfile, sizeof(pfile));
        pf = fopen(pfile, "r");
        if (!pf) {
            continue;
        }
        r = (int)fread(out + n, 1, (size_t)(cap - 1 - n), pf);
        fclose(pf);
        if (r > 0) {
            n += r;
        }
    }
    out[n] = 0;
    return n;
}

int np_host_kit_import(const char *s, int n)
{
    char tmp[NP_MAX_PATH], pfile[NP_MAX_PATH], name[24];
    const char *p, *next, *body;
    FILE *f;
    int mainn;
    if (!s || n < 8) {
        return -1;
    }
    kit_tmp(tmp, (int)sizeof(tmp));
    p = strstr(s, "\n#profile ");
    mainn = p ? (int)(p - s) : n;
    f = fopen(tmp, "w");
    if (!f) {
        return -1;
    }
    fwrite(s, 1, (size_t)mainn, f);
    fclose(f);
    if (cfg_read(tmp) != 0) {
        return -1;
    }
    while (p) {
        p += 10;
        name[0] = 0;
        sscanf(p, "%23s", name);
        next = strstr(p, "\n#profile ");
        body = strchr(p, '\n');
        if (body) {
            body++;
        } else {
            body = p;
        }
        if (prof_ok_name(name)) {
            int ln = next ? (int)(next - body) : (int)((s + n) - body);
            if (ln < 0) {
                ln = 0;
            }
            prof_file(name, pfile, sizeof(pfile));
            f = fopen(pfile, "w");
            if (f) {
                fwrite(body, 1, (size_t)ln, f);
                fclose(f);
            }
        }
        p = next;
    }
    prof_scan();
    prof_apply();
    data_recook();
    cfg_save();
    set_status(1, "map & settings saved here");
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
    g.atom_rec_n = 0;
    set_status(1, "take discarded");
}

void np_host_atom_start(void)
{
    g.atom_rec_n = 0;
    g.atom_on = 1;
    set_status(1, "take running — watch the plot, then Stop");
}

int np_host_atom_stop(void)
{
    int n = g.atom_rec_n;
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
    return g.atom_on ? g.atom_rec_n : 0;
}

int np_host_atom_save(void)
{
    char path[NP_MAX_PATH];
    uint64_t live[NP_ATOM_RING];
    float rms[NP_ATOM_RING * 8];
    int n = 0;
    if (atom_path(path, (int)sizeof(path), g.namebuf) != 0) {
        set_status(0, "ATOM need a name");
        return -1;
    }
    n = g.atom_rec_n > 0 ? g.atom_rec_n : 0;
    if (n > g.atom_n) {
        n = g.atom_n;
    }
    atom_last(live, rms, n);
    if (n < 1) {
        set_status(0, "ATOM empty — wait for 1 s folds");
        return -1;
    }
    {
        int i, c, hot = 0;
        for (i = 0; i < n; i++) {
            for (c = 0; c < 8; c++) {
                if (rms[i * 8 + c] >= 4000.f) {
                    hot++;
                }
            }
        }
        if (hot >= n * 4) {
            set_status(0, "take is loud (%.0f uV) — saved anyway", (double)rms[0]);
        }
    }
    if (np_atom_save2(path, live, rms, n, NP_ATOM_WIN) != 0) {
        set_status(0, "ATOM cannot write %s", path);
        return -1;
    }
    {
        char rpath[NP_MAX_PATH];
        float *planar = (float *)malloc((size_t)NP_NCHAN * n * NP_ATOM_WIN * sizeof(float));
        int i, c;
        if (planar) {
            memset(planar, 0, (size_t)NP_NCHAN * n * NP_ATOM_WIN * sizeof(float));
            for (i = 0; i < n; i++) {
                int idx = (g.atom_wr - n + i + NP_ATOM_RING) % NP_ATOM_RING;
                for (c = 0; c < NP_NCHAN; c++) {
                    memcpy(planar + c * (n * NP_ATOM_WIN) + i * NP_ATOM_WIN,
                           atom_raw[idx] + c * NP_ATOM_WIN,
                           (size_t)NP_ATOM_WIN * sizeof(float));
                }
            }
            raw_named_path("atoms", g.namebuf, rpath, (int)sizeof(rpath));
            np_raw_save(rpath, planar, NP_NCHAN, n * NP_ATOM_WIN, design_sps());
            free(planar);
        }
    }
    memcpy(g.atom_ref, live, (size_t)n * sizeof(uint64_t));
    g.atom_ref_n = n;
    atom_sanitize(g.atom_ref_name, (int)sizeof(g.atom_ref_name), g.namebuf);
    snprintf(g.atom_a, sizeof(g.atom_a), "%s", g.atom_ref_name);
    g.atom_b[0] = 0;
    g.atom_ab = 0.f;
    atom_score();
    g.atom_rec_n = 0;
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
    set_status(1, "ATOM loaded %s  %d s", g.atom_ref_name, n);
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
        if (g.atom_ab >= 0.90f) {
            snprintf(out, (size_t)n, "%s vs %s  same head — not distinct",
                     g.atom_a, g.atom_b);
        } else if (g.atom_ab <= 0.f) {
            snprintf(out, (size_t)n, "%s vs %s  no RMS — cannot compare",
                     g.atom_a, g.atom_b);
        } else {
            snprintf(out, (size_t)n, "%s vs %s  distinct", g.atom_a, g.atom_b);
        }
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

static void data_recook(void)
{
    float buf[NP_NCHAN][NP_RING];
    uint32_t nn[NP_NCHAN];
    int ntake = 0, nlearn = 0, i;
    char path[NP_MAX_PATH];

    if (raw_load_plate("noise", buf, nn) == 0) {
        int c;
        plate_stats_from_buf(buf, nn, 0);
        g.cal.have = 1;
        if (g.cal.n < 1) {
            g.cal.n = 1;
        }
        g.noise_psd_ch_ok = 0;
        memset(g.noise_psd_ch, 0, sizeof(g.noise_psd_ch));
        for (c = 0; c < NP_NCHAN; c++) {
            if (nn[c] >= (uint32_t)NP_FFT_N) {
                np_welch_psd(buf[c], (int)nn[c], g.noise_psd_ch[c]);
                g.noise_psd_ch_ok |= 1u << c;
            }
        }
    }
    if (raw_load_plate("calm", buf, nn) == 0) {
        g.calm.n = 0;
        plate_stats_from_buf(buf, nn, 1);
        g.calm.have = 1;
        g.cal_cut = 1;
    }
    if (g.cal.have || g.calm.have) {
        cal_save();
    }
    ntake = atom_rescan();
    for (i = 0; i < ntake; i++) {
        float *planar;
        int ch = 0, ns = 0, sec, nsec, c;
        float sps = 0.f;
        uint64_t bits[NP_ATOM_RING];
        float rms[NP_ATOM_RING * 8];
        raw_named_path("atoms", atom_listed[i], path, (int)sizeof(path));
        planar = (float *)malloc((size_t)NP_NCHAN * NP_ATOM_RING * NP_ATOM_WIN * sizeof(float));
        if (!planar) {
            continue;
        }
        if (np_raw_load(path, planar, NP_NCHAN * NP_ATOM_RING * NP_ATOM_WIN, &ch, &ns, &sps) <
            1) {
            free(planar);
            continue;
        }
        nsec = ns / NP_ATOM_WIN;
        if (nsec < 1) {
            free(planar);
            continue;
        }
        if (nsec > NP_ATOM_RING) {
            nsec = NP_ATOM_RING;
        }
        for (sec = 0; sec < nsec; sec++) {
            float win[NP_NCHAN * NP_ATOM_WIN];
            float cookb[NP_NCHAN][NP_RING];
            uint32_t cnn[NP_NCHAN];
            int env;
            memset(win, 0, sizeof(win));
            memset(cookb, 0, sizeof(cookb));
            memset(cnn, 0, sizeof(cnn));
            for (c = 0; c < ch && c < NP_NCHAN; c++) {
                memcpy(cookb[c], planar + c * ns + sec * NP_ATOM_WIN,
                       (size_t)NP_ATOM_WIN * sizeof(float));
                cnn[c] = NP_ATOM_WIN;
            }
            env = g.envelope;
            g.envelope = 0;
            cook_all(cookb, cnn, NP_ATOM_WIN);
            g.envelope = env;
            for (c = 0; c < NP_NCHAN; c++) {
                memcpy(win + c * NP_ATOM_WIN, cookb[c], (size_t)NP_ATOM_WIN * sizeof(float));
            }
            bits[sec] = np_atom_pack(win, NP_NCHAN, NP_ATOM_WIN, NP_ATOM_WIN, atom_scale());
            np_atom_rms8(win, NP_NCHAN, NP_ATOM_WIN, NP_ATOM_WIN, rms + sec * 8);
        }
        if (atom_path(path, (int)sizeof(path), atom_listed[i]) == 0) {
            np_atom_save2(path, bits, rms, nsec, NP_ATOM_WIN);
        }
        free(planar);
    }
    for (i = 0; i < g.learn.n; i++) {
        float *planar;
        int ch = 0, ns = 0, c;
        float sps = 0.f;
        float cookb[NP_NCHAN][NP_RING];
        uint32_t cnn[NP_NCHAN];
        uint8_t mask = 0;
        float wave[NPL_NCHAN][NPL_LEN], lrms[NPL_NCHAN];
        raw_named_path("learn", g.learn.s[i].name, path, (int)sizeof(path));
        planar = (float *)malloc((size_t)NP_NCHAN * NP_RING * sizeof(float));
        if (!planar) {
            continue;
        }
        if (np_raw_load(path, planar, NP_NCHAN * NP_RING, &ch, &ns, &sps) < 1 || ns < 16) {
            free(planar);
            continue;
        }
        memset(cookb, 0, sizeof(cookb));
        memset(cnn, 0, sizeof(cnn));
        for (c = 0; c < ch && c < NP_NCHAN; c++) {
            int take = ns > NP_RING ? NP_RING : ns;
            memcpy(cookb[c], planar + c * ns, (size_t)take * sizeof(float));
            cnn[c] = (uint32_t)take;
        }
        cook_all(cookb, cnn, (uint32_t)ns);
        memset(wave, 0, sizeof(wave));
        memset(lrms, 0, sizeof(lrms));
        for (c = 0; c < NP_NCHAN; c++) {
            if (cnn[c] >= 16 &&
                npl_prep(wave[c], &lrms[c], cookb[c], (int)cnn[c], design_sps(),
                         notch_hz_eff()) == 0) {
                mask |= (uint8_t)(1u << c);
            }
        }
        if (mask) {
            npl_add(&g.learn, g.learn.s[i].name, wave, lrms, mask);
            nlearn++;
        }
        free(planar);
    }
    if (nlearn) {
        learn_persist();
    }
    filt_reset();
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
    {
        char rp[NP_MAX_PATH];
        raw_named_path("atoms", atom_listed[i], rp, (int)sizeof(rp));
        unlink(rp);
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
    char pa[NP_MAX_PATH], pb[NP_MAX_PATH];
    g.atom_ab = 0.f;
    if (!g.atom_a[0] || !g.atom_b[0]) {
        return;
    }
    if (atom_path(pa, (int)sizeof(pa), g.atom_a) != 0 ||
        atom_path(pb, (int)sizeof(pb), g.atom_b) != 0) {
        return;
    }
    g.atom_ab = np_atom_file_close(pa, pb);
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
    if (g.atom_ab >= 0.90f) {
        set_status(0, "%s vs %s  same head — not distinct", g.atom_a, g.atom_b);
    } else if (g.atom_ab <= 0.f) {
        set_status(0, "%s vs %s  no RMS — cannot compare", g.atom_a, g.atom_b);
    } else {
        set_status(1, "%s vs %s  distinct", g.atom_a, g.atom_b);
    }
}

void np_host_atom_pair(char *out, int n)
{
    if (!out || n < 4) {
        return;
    }
    if (g.atom_a[0] && g.atom_b[0]) {
        if (g.atom_ab >= 0.90f) {
            snprintf(out, (size_t)n, "%s vs %s  same head — not distinct",
                     g.atom_a, g.atom_b);
        } else if (g.atom_ab <= 0.f) {
            snprintf(out, (size_t)n, "%s vs %s  no RMS — cannot compare",
                     g.atom_a, g.atom_b);
        } else {
            snprintf(out, (size_t)n, "%s vs %s  distinct", g.atom_a, g.atom_b);
        }
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

static void atom_identify(void)
{
    uint64_t liveb[NP_ATOM_RING];
    float liver[NP_ATOM_RING * 8];
    int i, nlist, k, best = -1, second = -1;
    float bests = -1.f, secs = -1.f;

    nlist = atom_rescan();
    memset(g.atom_id, 0, sizeof(g.atom_id));
    g.atom_id_best = -1;
    g.atom_clip = 0;
    if (!g.learn.match || nlist < 1 || g.atom_n < 1) {
        return;
    }
    /* Last 1 s vs each take's pattern vs rest/CALM. Never the whole file mean. */
    k = 1;
    atom_last(liveb, liver, k);
    {
        float base[NP_ATOM_RING * 8];
        int nbase = 0, bi;
        memset(base, 0, sizeof(base));
        for (bi = 0; bi < nlist; bi++) {
            const char *nm = atom_listed[bi];
            char path[NP_MAX_PATH];
            int tn, win = 0, have = 0;
            if (!nm[0]) {
                continue;
            }
            if ((nm[0] != 'r' && nm[0] != 'R') || (nm[1] != 'e' && nm[1] != 'E') ||
                (nm[2] != 's' && nm[2] != 'S') || (nm[3] != 't' && nm[3] != 'T') || nm[4]) {
                continue;
            }
            if (atom_path(path, (int)sizeof(path), nm) != 0) {
                continue;
            }
            tn = np_atom_load2(path, liveb, base, NP_ATOM_RING, &win, &have);
            if (tn > 0 && have) {
                nbase = tn;
            }
            break;
        }
        if (nbase < 1 && g.calm.have) {
            memcpy(base, g.calm.rms, 8 * sizeof(float));
            nbase = 1;
        }
        for (i = 0; i < nlist; i++) {
            uint64_t tb[NP_ATOM_RING];
            float tr[NP_ATOM_RING * 8];
            char path[NP_MAX_PATH];
            int tn, win = 0, have_rms = 0;
            float r, s;
            if (atom_path(path, (int)sizeof(path), atom_listed[i]) != 0) {
                continue;
            }
            tn = np_atom_load2(path, tb, tr, NP_ATOM_RING, &win, &have_rms);
            if (tn < 1) {
                continue;
            }
            r = have_rms ? np_atom_rms_close_to_pattern(liver, k, tr, tn, base, nbase) : 0.f;
            s = r;
            g.atom_id[i] = s;
            if (s > bests) {
                secs = bests;
                second = best;
                bests = s;
                best = i;
            } else if (s > secs) {
                secs = s;
                second = i;
            }
        }
    }
    (void)second;
    /* Fail closed. No winner → no percents. A split is not a score. */
    if (best >= 0 && bests >= 0.70f && (second < 0 || bests - secs >= 0.08f)) {
        g.atom_id_best = best;
    } else {
        memset(g.atom_id, 0, sizeof(g.atom_id));
    }
}

int np_host_atom_id_best(void)
{
    return g.atom_id_best;
}

float np_host_atom_id_score(int i)
{
    if (i < 0 || i >= 32) {
        return 0.f;
    }
    return g.atom_id[i];
}

void np_host_atom_id_line(char *out, int n)
{
    if (!out || n < 4) {
        return;
    }
    if (!g.learn.match || g.atom_id_best < 0 || g.atom_id_best >= atom_listed_n) {
        if (g.learn.match && atom_listed_n > 0) {
            snprintf(out, (size_t)n, "now —");
        } else {
            out[0] = 0;
        }
        return;
    }
    snprintf(out, (size_t)n, "now %s  %.0f%%", atom_listed[g.atom_id_best],
             (double)(g.atom_id[g.atom_id_best] * 100.f));
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

int np_host_api_on(void)
{
    return g.api_on ? 1 : 0;
}
void np_host_api_set_on(int on)
{
    g.api_on = on ? 1 : 0;
    cfg_save();
    api_apply();
}
int np_host_api_lan(void)
{
    return g.api_lan ? 1 : 0;
}
void np_host_api_set_lan(int lan)
{
    g.api_lan = lan ? 1 : 0;
    cfg_save();
    api_apply();
}
int np_host_api_hz(void)
{
    return g.api_hz < 1 ? 125 : g.api_hz;
}
void np_host_api_set_hz(int hz)
{
    if (hz < 1) {
        hz = 1;
    }
    if (hz > 125) {
        hz = 125;
    }
    g.api_hz = hz;
    cfg_save();
    api_apply();
}
int np_host_api_http(void)
{
    return g.api_http;
}
void np_host_api_set_http(int port)
{
    if (port < 0 || port > 65535) {
        port = 8765;
    }
    g.api_http = port;
    cfg_save();
    api_apply();
}
int np_host_api_udp(void)
{
    return g.api_udp;
}
void np_host_api_set_udp(int port)
{
    if (port < 0 || port > 65535) {
        port = 8766;
    }
    g.api_udp = port;
    cfg_save();
    api_apply();
}
int np_host_api_tcp(void)
{
    return g.api_tcp;
}
void np_host_api_set_tcp(int port)
{
    if (port < 0 || port > 65535) {
        port = 8767;
    }
    g.api_tcp = port;
    cfg_save();
    api_apply();
}
void np_host_api_token(char *out, int n)
{
    if (!out || n < 1) {
        return;
    }
    snprintf(out, (size_t)n, "%s", g.api_token);
}
void np_host_api_set_token(const char *s)
{
    snprintf(g.api_token, sizeof(g.api_token), "%s", s ? s : "");
    cfg_save();
    api_apply();
}
void np_host_api_push(char *out, int n)
{
    if (!out || n < 1) {
        return;
    }
    snprintf(out, (size_t)n, "%s", g.api_push);
}
void np_host_api_set_push(const char *s)
{
    snprintf(g.api_push, sizeof(g.api_push), "%s", s ? s : "");
    cfg_save();
    api_apply();
}
void np_host_api_line(char *out, int n)
{
    np_api_line(out, n);
}

int np_host_link(void)
{
    return g.link;
}

void np_host_set_link(int path)
{
    int want = path;
    if (want < 0) {
        want = 0;
    }
    if (want > 2) {
        want = 2;
    }
    if (g.link == want) {
        return;
    }
    if (g.connected) {
        do_disconnect();
    }
    g.link = want;
    cfg_save();
    if (g.link == 0) {
        set_status(1, "USB — Knight on this device");
    } else if (g.link == 1) {
        set_status(1, "LAN — EXG on wifi");
    } else {
        set_status(1, "Bluetooth — EXG nearby");
    }
}

void np_host_cycle_link(void)
{
    np_host_set_link((g.link + 1) % 3);
}

void np_host_link_dest(char *out, int n)
{
    if (!out || n < 2) {
        return;
    }
    snprintf(out, (size_t)n, "%s", g.link_dest);
}

void np_host_set_link_dest(const char *s)
{
    snprintf(g.link_dest, sizeof(g.link_dest), "%s", s ? s : "");
    cfg_save();
}

void np_host_link_token(char *out, int n)
{
    if (!out || n < 2) {
        return;
    }
    snprintf(out, (size_t)n, "%s", g.link_token);
}

void np_host_set_link_token(const char *s)
{
    snprintf(g.link_token, sizeof(g.link_token), "%s", s ? s : "");
    cfg_save();
}

int np_host_follow_n(void)
{
    return peers.nfollow;
}

void np_host_follow_name(int i, char *out, int n)
{
    if (!out || n < 2) {
        return;
    }
    out[0] = 0;
    if (i >= 0 && i < peers.nfollow) {
        snprintf(out, (size_t)n, "%s", peers.follow[i].name);
    }
}

void np_host_follow_dest(int i, char *out, int n)
{
    if (!out || n < 2) {
        return;
    }
    out[0] = 0;
    if (i >= 0 && i < peers.nfollow) {
        snprintf(out, (size_t)n, "%s", peers.follow[i].dest);
    }
}

void np_host_follow_use(int i)
{
    if (i < 0 || i >= peers.nfollow) {
        return;
    }
    snprintf(g.link_dest, sizeof(g.link_dest), "%s", peers.follow[i].dest);
    snprintf(g.link_token, sizeof(g.link_token), "%s", peers.follow[i].grant);
    g.link = strncmp(peers.follow[i].dest, "bt:", 3) == 0 ? 2 : 1;
    cfg_save();
}

void np_host_follow_del(int i)
{
    np_peers_follow_del(&peers, i);
    peers_flush();
}

int np_host_allow_n(void)
{
    return peers.nallow;
}

void np_host_allow_name(int i, char *out, int n)
{
    if (!out || n < 2) {
        return;
    }
    out[0] = 0;
    if (i >= 0 && i < peers.nallow) {
        snprintf(out, (size_t)n, "%s", peers.allow[i].name);
    }
}

void np_host_allow_del(int i)
{
    np_peers_allow_del(&peers, i);
    peers_flush();
}

void np_host_follow_grant(const char *name, char *out, int n)
{
    int i;
    if (!out || n < 2) {
        return;
    }
    out[0] = 0;
    if (!name) {
        return;
    }
    for (i = 0; i < peers.nfollow; i++) {
        if (strcmp(peers.follow[i].name, name) == 0) {
            snprintf(out, (size_t)n, "%s", peers.follow[i].grant);
            return;
        }
    }
}

int np_host_grant_ok(const char *grant)
{
    return host_grant_ok(grant);
}

void np_host_follow_remember(const char *name, const char *dest, const char *grant)
{
    np_peers_follow_add(&peers, name, dest, grant);
    if (dest && dest[0]) {
        snprintf(g.link_dest, sizeof(g.link_dest), "%s", dest);
    }
    if (grant && grant[0]) {
        snprintf(g.link_token, sizeof(g.link_token), "%s", grant);
    }
    peers_flush();
    cfg_save();
}

int np_host_pair_begin(const char *name)
{
    int i;
    pthread_mutex_lock(&pair_mu);
    if (name && name[0]) {
        for (i = 0; i < peers.nallow; i++) {
            if (strcmp(peers.allow[i].name, name) == 0) {
                snprintf(pair_name, sizeof(pair_name), "%s", name);
                snprintf(pair_grant, sizeof(pair_grant), "%s", peers.allow[i].grant);
                pair_dec = 2;
                pthread_mutex_unlock(&pair_mu);
                return 2;
            }
        }
    }
    snprintf(pair_name, sizeof(pair_name), "%s", name ? name : "exg");
    pair_grant[0] = 0;
    pair_dec = 1;
    pthread_mutex_unlock(&pair_mu);
    set_status(1, "%s wants EXG", pair_name);
    return 1;
}

int np_host_pair_state(void)
{
    int d;
    pthread_mutex_lock(&pair_mu);
    d = pair_dec;
    pthread_mutex_unlock(&pair_mu);
    return d;
}

void np_host_pair_name(char *out, int n)
{
    if (!out || n < 2) {
        return;
    }
    pthread_mutex_lock(&pair_mu);
    snprintf(out, (size_t)n, "%s", pair_name);
    pthread_mutex_unlock(&pair_mu);
}

void np_host_pair_accept(void)
{
    pthread_mutex_lock(&pair_mu);
    if (pair_dec == 1) {
        np_peers_mkgrant(pair_grant, (int)sizeof(pair_grant));
        np_peers_allow_add(&peers, pair_name, pair_grant);
        peers_flush();
        pair_dec = 2;
    }
    pthread_mutex_unlock(&pair_mu);
    set_status(1, "EXG allowed");
}

void np_host_pair_reject(void)
{
    pthread_mutex_lock(&pair_mu);
    if (pair_dec == 1) {
        pair_dec = 3;
        pair_grant[0] = 0;
    }
    pthread_mutex_unlock(&pair_mu);
    set_status(0, "EXG refused");
}

void np_host_pair_grant(char *out, int n)
{
    if (!out || n < 2) {
        return;
    }
    pthread_mutex_lock(&pair_mu);
    snprintf(out, (size_t)n, "%s", pair_grant);
    pthread_mutex_unlock(&pair_mu);
}

int np_host_copy_exg1(unsigned char *dst, int cap)
{
    struct np_api_sample s;
    if (!np_api_latest(&s)) {
        return 0;
    }
    return np_api_pack(dst, cap, &s);
}

int np_host_feed_exg1(const unsigned char *raw, int n)
{
    struct np_api_sample s;
    if (np_api_unpack(raw, n, &s) != NP_API_FRAME) {
        return -1;
    }
    link_on_sample(&s);
    return 0;
}

void np_host_apply_cfg_json(const char *js)
{
    apply_link_cfg(js);
}

void np_host_view_json(char *out, int n)
{
    api_view_json(out, n);
}

void np_host_link_wire(int on)
{
    if (on) {
        g.link = 2;
        g.connected = 1;
        np_link_set_hooks(link_on_sample, apply_link_cfg);
        set_status(1, "following EXG on bluetooth");
    } else if (g.link) {
        g.connected = 0;
        set_status(1, "disconnected");
    }
}
