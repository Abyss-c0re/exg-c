#define _GNU_SOURCE
#include "np_dsp.h"
#include "np_font.h"
#include "np_knight.h"
#include "nplearn.h"
#include "np_ring.h"
#include "np_serial.h"
#include "sdl2_min.h"

#include <errno.h>
#include <stdarg.h>
#include <grp.h>
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
#include <unistd.h>

#define WIN_W 1280
#define WIN_H 800
#define SIDE_W 300
#define STATUS_H 28
#define FFT_H 118
#define LEARN_H 72
#define WAVE_TOP 8
#define OPEN_UV 3000.f
#define QMAX 48
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
    int scale_uv;
    int notch_hz;
    int hp_hz;
    int grid;
    int paused;
    int show_uv;
    int detrend;
    float sps;
    uint32_t sps_n;
    struct timespec sps_t;
    struct np_hp hp[NP_NCHAN];
    struct np_notch notch[NP_NCHAN];
    struct npl learn;
    int typing;
    char namebuf[NPL_NAME];
    struct {
        int have;
        uint32_t n;
        float dc[NP_NCHAN], rms[NP_NCHAN], pk[NP_NCHAN];
    } off, on;
};

static struct np_app g;
static void set_status(int ok, const char *fmt, ...);
static const int SCALE_UV[] = {50, 100, 200, 500, 1000, 5000};
#define NSCALE 6
static const int WIN_S[] = {1, 2, 4, 8};
#define NWINS 4

static void filt_reset(void)
{
    int i;
    float sps = g.sps > 1.f ? g.sps : (float)NP_DEFAULT_SPS;
    for (i = 0; i < NP_NCHAN; i++) {
        np_hp_init(&g.hp[i], (float)g.hp_hz, sps);
        np_notch_init(&g.notch[i], (float)g.notch_hz, sps, 30.f);
    }
}

static void cfg_path(char *out, size_t n)
{
    const char *h = getenv("HOME");
    if (h && h[0]) {
        snprintf(out, n, "%s/.config/exg-c.conf", h);
    } else {
        snprintf(out, n, "exg-c.conf");
    }
}

static void learn_path(char *out, size_t n)
{
    const char *h = getenv("HOME");
    if (h && h[0]) {
        char dir[NP_MAX_PATH];
        snprintf(dir, sizeof(dir), "%s/.config", h);
        mkdir(dir, 0755);
        snprintf(out, n, "%s/.config/exg-c.learn", h);
    } else {
        snprintf(out, n, "exg-c.learn");
    }
}

static void learn_persist(void)
{
    char path[NP_MAX_PATH];
    learn_path(path, sizeof(path));
    npl_save(&g.learn, path);
}

/* Pull the live window through nplearn. Does not reject rail — that was
 * why Save said "nothing to learn" on a perfectly live stream. */
static int learn_capture(float wave[NPL_NCHAN][NPL_LEN], float rms[NPL_NCHAN], uint8_t *mask)
{
    int c, have = 0;
    uint32_t want = (uint32_t)(g.window_s * (g.sps > 1.f ? g.sps : NP_DEFAULT_SPS));
    float sps = g.sps > 1.f ? g.sps : (float)NP_DEFAULT_SPS;
    float notch = g.notch_hz > 0 ? (float)g.notch_hz : 50.f;
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
        float buf[NP_RING];
        uint32_t n;
        if (!g.active[c]) {
            continue;
        }
        have = 1;
        n = np_ring_copy(&g.ring, c, buf, want);
        if (n < 16) {
            continue;
        }
        if (npl_prep(wave[c], &rms[c], buf, (int)n, sps, notch) == 0) {
            *mask |= (uint8_t)(1u << c);
        }
    }
    if (*mask) {
        return 0;
    }
    return have ? -2 : -1;
}

static void learn_tick(void)
{
    float wave[NPL_NCHAN][NPL_LEN], rms[NPL_NCHAN];
    uint8_t mask;
    if (!g.learn.match || g.learn.n <= 0) {
        g.learn.best = -1;
        return;
    }
    if (learn_capture(wave, rms, &mask) != 0) {
        g.learn.best = -1;
        return;
    }
    npl_score(&g.learn, wave, rms, mask);
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
    if (err != 0) {
        set_status(0, "turn a channel ON and wait one window");
        return;
    }
    r = npl_add(&g.learn, g.namebuf, wave, rms, mask);
    if (r == -2) {
        set_status(0, "learn full (%d)", NPL_MAX);
        return;
    }
    if (r < 0) {
        set_status(0, "learn add failed");
        return;
    }
    learn_persist();
    {
        int nc = 0, b;
        for (b = 0; b < NPL_NCHAN; b++) {
            if (mask & (uint8_t)(1u << b)) {
                nc++;
            }
        }
        set_status(1, "saved '%s'  %d ch  filt hp%.0f/lp%.0f/notch%.0f", g.namebuf, nc, NPL_HP_HZ,
                   NPL_LP_HZ, g.notch_hz > 0 ? (float)g.notch_hz : 50.f);
    }
}

static void typing_set(int on)
{
    g.typing = on;
    if (on) {
        SDL_StartTextInput();
    } else {
        SDL_StopTextInput();
    }
}

static void cfg_save(void)
{
    char path[NP_MAX_PATH];
    FILE *f;
    cfg_path(path, sizeof(path));
    f = fopen(path, "w");
    if (!f) {
        return;
    }
    fprintf(f, "window_s=%d\n", g.window_s);
    fprintf(f, "autoscale=%d\n", g.autoscale);
    fprintf(f, "scale_uv=%d\n", g.scale_uv);
    fprintf(f, "notch_hz=%d\n", g.notch_hz);
    fprintf(f, "hp_hz=%d\n", g.hp_hz);
    fprintf(f, "grid=%d\n", g.grid);
    fprintf(f, "show_uv=%d\n", g.show_uv);
    fprintf(f, "detrend=%d\n", g.detrend);
    fprintf(f, "board=%d\n", (int)g.board);
    fclose(f);
}

static void cfg_load(void)
{
    char path[NP_MAX_PATH], line[80];
    FILE *f;
    cfg_path(path, sizeof(path));
    f = fopen(path, "r");
    if (!f) {
        return;
    }
    while (fgets(line, sizeof(line), f)) {
        int v;
        if (sscanf(line, "window_s=%d", &v) == 1) {
            g.window_s = v;
        } else if (sscanf(line, "autoscale=%d", &v) == 1) {
            g.autoscale = v;
        } else if (sscanf(line, "scale_uv=%d", &v) == 1) {
            g.scale_uv = v;
        } else if (sscanf(line, "notch_hz=%d", &v) == 1) {
            g.notch_hz = v;
        } else if (sscanf(line, "hp_hz=%d", &v) == 1) {
            g.hp_hz = v;
        } else if (sscanf(line, "grid=%d", &v) == 1) {
            g.grid = v;
        } else if (sscanf(line, "show_uv=%d", &v) == 1) {
            g.show_uv = v;
        } else if (sscanf(line, "detrend=%d", &v) == 1) {
            g.detrend = v;
        } else if (sscanf(line, "board=%d", &v) == 1) {
            g.board = v ? NP_BOARD_KNIGHT_IMU : NP_BOARD_KNIGHT;
        }
    }
    fclose(f);
    if (g.window_s < 1) {
        g.window_s = 2;
    }
    if (g.scale_uv < 20) {
        g.scale_uv = 200;
    }
}

static void apply_filt(int ch, float *buf, uint32_t n)
{
    uint32_t i;
    if (g.hp_hz <= 0 && g.notch_hz <= 0) {
        return;
    }
    for (i = 0; i < n; i++) {
        float v = buf[i];
        if (g.hp_hz > 0) {
            v = np_hp_step(&g.hp[ch], v);
        }
        if (g.notch_hz > 0) {
            v = np_notch_step(&g.notch[ch], v);
        }
        buf[i] = v;
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
    struct group *gr = getgrnam("dialout");
    if (!gr || in_group(gr->gr_gid) || getenv("NP_EXG_NOSG")) {
        return;
    }
    {
        char **na = calloc((size_t)argc + 3, sizeof(*na));
        int i;
        if (!na) {
            return;
        }
        na[0] = "sg";
        na[1] = "dialout";
        na[2] = argv[0];
        for (i = 1; i < argc; i++) {
            na[i + 2] = argv[i];
        }
        setenv("NP_EXG_NOSG", "1", 1);
        execvp("sg", na);
        free(na);
    }
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
            np_parser_set_gain(&g.parser, ch, gain);
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
        int n = np_serial_read(g.fd, buf, (int)sizeof(buf));
        int i;
        if (n < 0) {
            set_status(0, "serial read failed");
            break;
        }
        if (n == 0) {
            usleep(2000);
            continue;
        }
        for (i = 0; i < n; i++) {
            struct np_sample s;
            int r = np_parser_feed(&g.parser, buf[i], &s);
            if (r < 0) {
                if (g.parser.locked) {
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
            }
        }
    }
    return NULL;
}

/* Official host: sleep 2s after start_stream, then chon_1..8 with ~1s gaps.
 * After a DTR reset the board prints "Scanning for IMU..." and emits no
 * A0 frames until that finishes. Wait for a locked frame, not a wall clock.
 * Do not send chon_0: official never does, and it is not a documented cmd. */
static void *enable_thread(void *arg)
{
    int c, waits;
    (void)arg;
    set_status(1, "board reset, waiting for frames...");
    for (waits = 0; waits < 80 && g.connected; waits++) {
        if (g.parser.locked) {
            break;
        }
        usleep(100000);
    }
    if (!g.connected || g.fd < 0) {
        return NULL;
    }
    if (!g.parser.locked) {
        set_status(0, "no frames after reset (IMU scan stuck?)");
    }
    for (c = 0; c < NP_NCHAN && g.connected; c++) {
        if (!g.active[c]) {
            continue;
        }
        set_status(1, "enable ch%d gain %d", c + 1, g.gain[c]);
        cmd_push(CMD_CHON, c + 1, g.gain[c]);
        if (g.rld[c]) {
            cmd_push(CMD_RLDADD, c + 1, 0);
        }
    }
    if (g.connected) {
        set_status(1, "connected %s - USB stream != analog switches",
                   g.nports ? g.ports[g.port_i] : "");
    }
    g.en_running = 0;
    return NULL;
}

static void do_connect(void)
{
    const char *path;
    if (g.connected) {
        return;
    }
    g.nports = np_list_ports(g.ports, NP_MAX_PORTS);
    if (g.nports <= 0) {
        set_status(0, "no /dev/ttyUSB* or /dev/ttyACM*");
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
    filt_reset();
    np_serial_pulse_dtr(g.fd);
    np_serial_flush(g.fd);
    g.connected = 1;
    if (pthread_create(&g.thr, NULL, reader_thread, NULL) != 0) {
        np_serial_close(g.fd);
        g.fd = -1;
        g.connected = 0;
        set_status(0, "thread create failed");
        return;
    }
    g.en_running = 1;
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
    if (g.csv) {
        fclose(g.csv);
        g.csv = NULL;
        g.recording = 0;
    }
    set_status(1, "disconnected");
}

static void toggle_record(void)
{
    if (!g.connected) {
        set_status(0, "connect before record");
        return;
    }
    if (g.recording) {
        fclose(g.csv);
        g.csv = NULL;
        g.recording = 0;
        set_status(1, "stopped %s", g.csv_path);
        return;
    }
    {
        time_t t = time(NULL);
        struct tm tm;
        localtime_r(&t, &tm);
        strftime(g.csv_path, sizeof(g.csv_path), "knight-%Y%m%d-%H%M%S.csv", &tm);
        g.csv = fopen(g.csv_path, "w");
        if (!g.csv) {
            set_status(0, "cannot write %s", g.csv_path);
            return;
        }
        fprintf(g.csv, "time,seq,ch1,ch2,ch3,ch4,ch5,ch6,ch7,ch8,loff_p,loff_n\n");
        g.recording = 1;
        set_status(1, "recording %s", g.csv_path);
    }
}

/* ---------- drawing ---------- */

static SDL_Renderer *R;

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

static struct hit hits[80];
int nhits;

static void add_hit(int x, int y, int w, int h, int kind, int ch)
{
    if (nhits >= 80) {
        return;
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

static void ab_grab(int onhead)
{
    uint32_t want = (uint32_t)(g.window_s * (g.sps > 1.f ? g.sps : NP_DEFAULT_SPS));
    int c;
    if (want < 32) {
        want = 32;
    }
    if (want > NP_RING) {
        want = NP_RING;
    }
    if (onhead) {
        memset(&g.on, 0, sizeof(g.on));
    } else {
        memset(&g.off, 0, sizeof(g.off));
    }
    for (c = 0; c < NP_NCHAN; c++) {
        float buf[NP_RING], dc, rms, pk;
        uint32_t n = np_ring_copy(&g.ring, c, buf, want);
        ch_stats(buf, n, &dc, &rms, &pk);
        if (onhead) {
            g.on.dc[c] = dc;
            g.on.rms[c] = rms;
            g.on.pk[c] = pk;
            if (n > g.on.n) {
                g.on.n = n;
            }
        } else {
            g.off.dc[c] = dc;
            g.off.rms[c] = rms;
            g.off.pk[c] = pk;
            if (n > g.off.n) {
                g.off.n = n;
            }
        }
    }
    if (onhead) {
        g.on.have = 1;
    } else {
        g.off.have = 1;
    }
}

static void ab_write(void)
{
    char path[NP_MAX_PATH];
    const char *h = getenv("HOME");
    FILE *f;
    int c;
    if (h && h[0]) {
        snprintf(path, sizeof(path), "%s/exg-c-ab.txt", h);
    } else {
        snprintf(path, sizeof(path), "exg-c-ab.txt");
    }
    f = fopen(path, "w");
    if (!f) {
        set_status(0, "cannot write %s", path);
        return;
    }
    fprintf(f, "ch,off_dc_uV,off_rms_uV,off_pk_uV,on_dc_uV,on_rms_uV,on_pk_uV,ratio\n");
    for (c = 0; c < NP_NCHAN; c++) {
        float ratio = 0.f;
        if (g.on.have && g.on.rms[c] > 1.f) {
            ratio = g.off.rms[c] / g.on.rms[c];
        }
        fprintf(f, "%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", c + 1,
                g.off.have ? g.off.dc[c] : 0, g.off.have ? g.off.rms[c] : 0,
                g.off.have ? g.off.pk[c] : 0, g.on.have ? g.on.dc[c] : 0,
                g.on.have ? g.on.rms[c] : 0, g.on.have ? g.on.pk[c] : 0, ratio);
    }
    fclose(f);
    set_status(1, "wrote %s", path);
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
        uint32_t want = (uint32_t)(g.window_s * (g.sps > 1.f ? g.sps : NP_DEFAULT_SPS));
        uint32_t n;
        int y0, y1b, row_h, mid, q;
        uint32_t i;
        char lab[36];
        float last, peak;
        int cr, cg, cb, dim;

        if (want < 32) {
            want = 32;
        }
        if (want > NP_RING) {
            want = NP_RING;
        }
        if (!g.paused) {
            n = np_ring_copy(&g.ring, c, buf, want);
            snapn[c] = n;
            memcpy(snap[c], buf, n * sizeof(float));
        } else {
            n = snapn[c];
            memcpy(buf, snap[c], n * sizeof(float));
        }
        y0 = y + (c * h) / NP_NCHAN;
        y1b = y + ((c + 1) * h) / NP_NCHAN;
        row_h = y1b - y0;
        mid = (y0 + y1b) / 2;
        last = n ? buf[n - 1] : 0.f;
        q = ch_quality(c, buf, n, lp, ln);
        {
            float dc = 0, rms = 0, pk = 0;
            ch_stats(buf, n, &dc, &rms, &pk);
            fill(x, y1b - 1, w, 1, 32, 36, 44);
            snprintf(lab, sizeof(lab), "ch%d", c + 1);
            text(x + 4, y0 + 4, lab, CHCOL[c][0], CHCOL[c][1], CHCOL[c][2], 1);
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
            }
        }
        if (q == Q_OFF || n < 4) {
            continue;
        }
        apply_filt(c, buf, n);
        if (g.detrend) {
            np_detrend(buf, (int)n);
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
        /* EEG-range scale. Autoscale only when the window is actually small.
         * Fitting a 0.13 V rail into the row is what made on/off look identical. */
        peak = (float)g.scale_uv;
        if (peak < 20.f) {
            peak = 200.f;
        }
        if (g.autoscale && q == Q_LIVE) {
            peak = 8.f;
            for (i = 0; i < n; i++) {
                float a = fabsf(buf[i]);
                if (a > peak) {
                    peak = a;
                }
            }
            if (peak > 2000.f) {
                peak = 2000.f;
            }
        }
        dim = (q == Q_OPEN || q == Q_LEADOFF);
        cr = dim ? CHCOL[c][0] / 3 : CHCOL[c][0];
        cg = dim ? CHCOL[c][1] / 3 : CHCOL[c][1];
        cb = dim ? CHCOL[c][2] / 3 : CHCOL[c][2];
        SDL_SetRenderDrawColor(R, (Uint8)cr, (Uint8)cg, (Uint8)cb, 255);
        for (i = 1; i < n; i++) {
            int x1 = x + (int)((i - 1) * (w - 1) / (n - 1));
            int x2 = x + (int)(i * (w - 1) / (n - 1));
            int py1 = mid - (int)(buf[i - 1] / peak * (row_h * 0.40f));
            int py2 = mid - (int)(buf[i] / peak * (row_h * 0.40f));
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

static void draw_fft(int x, int y, int w, int h)
{
    enum { N = 128 };
    float mag[N / 2];
    float acc[N];
    int c, i, used = 0, open_n = 0, peak_i = 1;
    uint8_t lp = 0, ln = 0;
    char cap[56];
    fill(x, y, w, h, 10, 12, 16);
    memset(mag, 0, sizeof(mag));
    np_ring_loff(&g.ring, &lp, &ln);
    for (c = 0; c < NP_NCHAN; c++) {
        float buf[N];
        uint32_t n;
        int q;
        if (!g.active[c]) {
            continue;
        }
        n = np_ring_copy(&g.ring, c, buf, N);
        if (n < N) {
            continue;
        }
        q = ch_quality(c, buf, n, lp, ln);
        if (q != Q_LIVE) {
            open_n++;
        }
        apply_filt(c, buf, n);
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
    if (open_n && used == open_n) {
        snprintf(cap, sizeof(cap), "FFT (open/rail inputs)");
    } else {
        snprintf(cap, sizeof(cap), "FFT Hz");
    }
    text(x + 6, y + 4, cap, 160, 168, 180, 1);
    if (used) {
        float peak = 1e-12f;
        int bins = N / 2;
        int plot_y = y + 16;
        int plot_h = h - 22;
        int bar_w;
        for (i = 1; i < bins; i++) {
            if (mag[i] > peak) {
                peak = mag[i];
                peak_i = i;
            }
        }
        bar_w = w / (bins - 1);
        if (bar_w < 1) {
            bar_w = 1;
        }
        for (i = 1; i < bins; i++) {
            int bh = (int)(mag[i] / peak * (plot_h - 2));
            int bx = x + (i - 1) * (w - 1) / (bins - 1);
            if (bh < 1) {
                bh = 1;
            }
            fill(bx, plot_y + plot_h - bh, bar_w, bh, 70, 170, 230);
        }
        {
            char hz[24];
            /* 125 SPS, N-point FFT → bin * sps / N */
            int sps = g.sps > 1.f ? (int)(g.sps + 0.5f) : NP_DEFAULT_SPS;
            snprintf(hz, sizeof(hz), "peak %dHz", (peak_i * sps) / N);
            text(x + w - 70, y + 4, hz, 180, 200, 210, 1);
        }
    }
}

static void draw_side(int x)
{
    int y = 16;
    int c;
    char line[96];
    const char *port = g.nports ? g.ports[g.port_i] : "(no port)";
    const char *bname = g.board == NP_BOARD_KNIGHT_IMU ? "8-ch + IMU" : "8-ch EEG";

    text(x + 12, y, "exg-c", 240, 242, 248, 2);
    y += 22;
    text(x + 12, y, "pure C host", 120, 128, 140, 1);
    y += 14;
    {
        float ax = 0, ay = 0, az = 0;
        char imu[40];
        pthread_mutex_lock(&g.ring.mu);
        if (g.ring.wr) {
            uint32_t w = (g.ring.wr - 1) % NP_RING;
            ax = g.ring.acc[0][w];
            ay = g.ring.acc[1][w];
            az = g.ring.acc[2][w];
        }
        pthread_mutex_unlock(&g.ring.mu);
        snprintf(imu, sizeof(imu), "IMU %.2f %.2f %.2f", ax, ay, az);
        text(x + 12, y, imu, 180, 200, 120, 1);
    }
    y += 12;
    text(x + 12, y, "USB stream != switches", 120, 128, 140, 1);
    y += 14;

    text(x + 12, y, "Port", 140, 148, 160, 1);
    y += 11;
    btn(x + 12, y, SIDE_W - 24, 24, port, 1, 4, 0, 32, 36, 44);
    y += 30;

    text(x + 12, y, "Board", 140, 148, 160, 1);
    y += 11;
    btn(x + 12, y, SIDE_W - 24, 24, bname, 1, 5, 0, 32, 36, 44);
    y += 30;

    if (!g.connected) {
        btn(x + 12, y, SIDE_W - 24, 28, "Connect", 1, 1, 0, 30, 110, 80);
    } else {
        btn(x + 12, y, SIDE_W - 24, 28, "Disconnect", 1, 2, 0, 120, 40, 48);
    }
    y += 34;
    btn(x + 12, y, SIDE_W - 24, 24, g.recording ? "Stop record" : "Record CSV",
        g.recording, 3, 0, g.recording ? 110 : 36, g.recording ? 50 : 40,
        g.recording ? 40 : 52);
    y += 32;

    text(x + 12, y, "Channels 1-8", 140, 148, 160, 1);
    y += 14;
    for (c = 0; c < NP_NCHAN; c++) {
        int col = c / 4;
        int row = c % 4;
        int bx = x + 10 + col * 145;
        int by = y + row * 28;
        char gn[8];
        snprintf(line, sizeof(line), "%d", c + 1);
        text(bx, by + 6, line, CHCOL[c][0], CHCOL[c][1], CHCOL[c][2], 1);
        btn(bx + 14, by, 36, 22, g.active[c] ? "ON" : "off", g.active[c], 6, c,
            g.active[c] ? 28 : 40, g.active[c] ? 90 : 42, g.active[c] ? 60 : 50);
        btn(bx + 52, by, 36, 22, g.rld[c] ? "RLD" : "rld", g.rld[c], 7, c,
            g.rld[c] ? 50 : 40, g.rld[c] ? 70 : 42, g.rld[c] ? 110 : 50);
        snprintf(gn, sizeof(gn), "g%d", g.gain[c]);
        btn(bx + 90, by, 40, 22, gn, 1, 8, c, 40, 42, 52);
    }
    y += 4 * 28 + 12;
    {
        char b[40];
        text(x + 12, y, "View", 140, 148, 160, 1);
        y += 14;
        snprintf(b, sizeof(b), "win %ds", g.window_s);
        btn(x + 12, y, 136, 22, b, 1, 9, 0, 36, 40, 48);
        if (g.autoscale) {
            snprintf(b, sizeof(b), "scale AUTO");
        } else {
            snprintf(b, sizeof(b), "scale +-%duV", g.scale_uv);
        }
        btn(x + 152, y, 136, 22, b, 1, 10, 0, 36, 40, 48);
        y += 26;
        if (g.notch_hz) {
            snprintf(b, sizeof(b), "notch %dHz", g.notch_hz);
        } else {
            snprintf(b, sizeof(b), "notch off");
        }
        btn(x + 12, y, 136, 22, b, g.notch_hz != 0, 11, 0, 36, 40, 48);
        if (g.hp_hz) {
            snprintf(b, sizeof(b), "hp %dHz", g.hp_hz);
        } else {
            snprintf(b, sizeof(b), "hp off");
        }
        btn(x + 152, y, 136, 22, b, g.hp_hz != 0, 12, 0, 36, 40, 48);
        y += 26;
        btn(x + 12, y, 136, 22, g.grid ? "grid on" : "grid off", g.grid, 13, 0, 36, 40, 48);
        btn(x + 152, y, 136, 22, g.paused ? "PAUSED" : "live", !g.paused, 14, 0,
            g.paused ? 110 : 36, g.paused ? 50 : 40, g.paused ? 40 : 48);
        y += 26;
        btn(x + 12, y, 136, 22, g.show_uv ? "uV on" : "uV off", g.show_uv, 15, 0, 36, 40, 48);
        btn(x + 152, y, 136, 22, g.detrend ? "detrend" : "raw DC", g.detrend, 21, 0, 36, 40, 48);
    }
}

static void draw_learn(int x, int y, int w, int h)
{
    char lab[48];
    int i, bx, bw;
    int notch = g.notch_hz > 0 ? g.notch_hz : 50;
    fill(x, y, w, h, 10, 12, 16);
    text(x + 4, y + 4, "Learn", 160, 168, 180, 1);
    snprintf(lab, sizeof(lab), "hp%.0f lp%.0f n%d", NPL_HP_HZ, NPL_LP_HZ, notch);
    text(x + 46, y + 4, lab, 100, 108, 118, 1);

    fill(x + 160, y + 2, 150, 16, g.typing ? 40 : 28, g.typing ? 48 : 32, g.typing ? 58 : 40);
    {
        char shown[NPL_NAME + 2];
        snprintf(shown, sizeof(shown), "%s%s", g.namebuf[0] ? g.namebuf : "name",
                 g.typing ? "_" : "");
        text(x + 164, y + 5, shown, g.namebuf[0] ? 230 : 120, 230, 236, 1);
    }
    add_hit(x + 160, y + 2, 150, 16, 16, 0);
    btn(x + 314, y + 1, 50, 18, "Save", 1, 17, 0, 30, 90, 70);
    btn(x + 368, y + 1, 58, 18, g.learn.match ? "MATCH" : "match", g.learn.match, 18, 0,
        g.learn.match ? 30 : 40, g.learn.match ? 80 : 42, g.learn.match ? 70 : 50);
    btn(x + 430, y + 1, 36, 18, "del", 0, 19, 0, 90, 40, 44);

    if (g.learn.best >= 0 && g.learn.match && g.learn.n) {
        snprintf(lab, sizeof(lab), "now %s %.0f%%", g.learn.s[g.learn.best].name,
                 g.learn.score[g.learn.best] * 100.f);
        text(x + 474, y + 5, lab,
             g.learn.score[g.learn.best] > 0.55f ? 80 : 200,
             g.learn.score[g.learn.best] > 0.55f ? 220 : 160,
             g.learn.score[g.learn.best] > 0.55f ? 120 : 80, 1);
    }

    btn(x + 4, y + 20, 40, 16, "OFF", g.off.have, 22, 0, g.off.have ? 90 : 36, 40, 44);
    btn(x + 48, y + 20, 36, 16, "ON", g.on.have, 23, 0, 36, g.on.have ? 90 : 40, 50);
    btn(x + 88, y + 20, 48, 16, "write", 0, 24, 0, 36, 40, 48);
    {
        char ab[96];
        if (g.off.have && g.on.have) {
            int c, diff = 0;
            float rmax = 0.f;
            for (c = 0; c < NP_NCHAN; c++) {
                if (!g.active[c] || g.on.rms[c] < 1.f) {
                    continue;
                }
                {
                    float r = g.off.rms[c] / g.on.rms[c];
                    if (r > rmax) {
                        rmax = r;
                    }
                    if (r > 3.f || r < 0.33f) {
                        diff = 1;
                    }
                }
            }
            snprintf(ab, sizeof(ab), "off %.0f uV  on %.0f uV  ratio %.1fx  %s",
                     g.off.rms[0], g.on.rms[0], rmax, diff ? "DIFFERENT" : "SAME amp");
            text(x + 142, y + 24, ab, diff ? 80 : 220, diff ? 210 : 140, 90, 1);
        } else if (g.off.have) {
            snprintf(ab, sizeof(ab), "off ch1 rms %.0f uV - put headset on, click ON",
                     g.off.rms[0]);
            text(x + 142, y + 24, ab, 180, 180, 160, 1);
        } else if (g.on.have) {
            snprintf(ab, sizeof(ab), "on ch1 rms %.0f uV - remove headset, click OFF",
                     g.on.rms[0]);
            text(x + 142, y + 24, ab, 180, 180, 160, 1);
        } else {
            text(x + 142, y + 24, "A/B: click OFF (no headset), then ON (on skin)", 110, 116,
                 124, 1);
        }
    }
    if (g.learn.n <= 0) {
        return;
    }
    bw = (w - 8) / (g.learn.n > 8 ? 8 : g.learn.n);
    if (bw < 70) {
        bw = 70;
    }
    for (i = 0; i < g.learn.n && i < 8; i++) {
        int on = (i == g.learn.sel);
        int hit = (g.learn.match && i == g.learn.best && g.learn.score[i] > 0.55f);
        int bar;
        bx = x + 4 + i * bw;
        fill(bx, y + 38, bw - 4, 28, on ? 40 : 24, on ? 48 : 28, hit ? 50 : 34);
        text(bx + 3, y + 40, g.learn.s[i].name, 220, 222, 228, 1);
        snprintf(lab, sizeof(lab), "%3.0f", g.learn.score[i] * 100.f);
        text(bx + 3, y + 50, lab, 140, 180, 160, 1);
        bar = (int)(g.learn.score[i] * (bw - 10));
        if (bar < 1) {
            bar = 1;
        }
        fill(bx + 3, y + 60, bar, 3, hit ? 70 : 50, hit ? 180 : 90, 90);
        add_hit(bx, y + 38, bw - 4, 28, 20, i);
    }
}

static void draw_status(void)
{
    char st[320];
    uint64_t tot = 0;
    uint32_t good = 0, bad = 0;
    uint8_t lp = 0, ln = 0;
    np_ring_stats(&g.ring, &tot, &good, &bad);
    np_ring_loff(&g.ring, &lp, &ln);
    pthread_mutex_lock(&g.mu);
    snprintf(st, sizeof(st),
             "%s   %.0f sps  frames %llu  bad %u  flen %d%s  loff %02X/%02X%s",
             g.status, g.sps > 1.f ? g.sps : (float)NP_DEFAULT_SPS, (unsigned long long)tot, bad,
             g.parser.frame_len, g.parser.locked ? " lock" : "", lp, ln,
             g.paused ? " PAUSE" : "");
    pthread_mutex_unlock(&g.mu);
    fill(0, win_h - STATUS_H, win_w, STATUS_H, 16, 18, 22);
    text(12, win_h - STATUS_H + 8, st, g.status_ok ? 80 : 240, g.status_ok ? 210 : 90,
         g.status_ok ? 120 : 90, 1);
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
            if (g.connected && g.active[hits[i].ch]) {
                cmd_push(CMD_CHON, hits[i].ch + 1, g.gain[hits[i].ch]);
            }
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
            if (g.autoscale) {
                g.autoscale = 0;
                g.scale_uv = 200;
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
            g.notch_hz = g.notch_hz == 0 ? 50 : (g.notch_hz == 50 ? 60 : 0);
            filt_reset();
            cfg_save();
            break;
        case 12:
            g.hp_hz = g.hp_hz == 0 ? 1 : (g.hp_hz == 1 ? 2 : 0);
            filt_reset();
            cfg_save();
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
            typing_set(1);
            return;
        case 17:
            typing_set(0);
            learn_save_named();
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
        case 22:
            ab_grab(0);
            set_status(1, "OFF-head snap  ch1 rms %.0f uV  n=%u", g.off.rms[0], g.off.n);
            break;
        case 23:
            ab_grab(1);
            set_status(1, "ON-head snap  ch1 rms %.0f uV  n=%u", g.on.rms[0], g.on.n);
            break;
        case 24:
            if (!g.off.have && !g.on.have) {
                set_status(0, "snap OFF and/or ON first");
            } else {
                ab_write();
            }
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
    int plot_w = win_w - SIDE_W - 24;
    int wave_h = win_h - WAVE_TOP - LEARN_H - FFT_H - 10 - STATUS_H;
    if (wave_h < NP_NCHAN * 36) {
        wave_h = NP_NCHAN * 36;
    }
    nhits = 0;
    fill(0, 0, win_w, win_h, 22, 24, 30);
    fill(win_w - SIDE_W, 0, SIDE_W, win_h, 26, 28, 34);
    draw_waves(12, WAVE_TOP, plot_w, wave_h);
    draw_learn(12, WAVE_TOP + wave_h + 4, plot_w, LEARN_H);
    draw_fft(12, WAVE_TOP + wave_h + LEARN_H + 8, plot_w, FFT_H);
    draw_side(win_w - SIDE_W);
    draw_status();
}

static int run_gui(void)
{
    SDL_Window *win;
    SDL_Event ev;
    int live = 1;

    setenv("SDL_VIDEODRIVER", "x11", 0);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    win = SDL_CreateWindow("exg-c", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           WIN_W, WIN_H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    R = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!R) {
        R = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!R) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
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
            } else if (ev.type == SDL_TEXTINPUT && g.typing) {
                const char *s = ev.text.text;
                int n = (int)strlen(g.namebuf);
                while (*s && n < NPL_NAME - 1) {
                    unsigned char c = (unsigned char)*s++;
                    if (c >= 32 && c < 127 && c != '/' && c != '\\') {
                        g.namebuf[n++] = (char)c;
                    }
                }
                g.namebuf[n] = 0;
            } else if (ev.type == SDL_KEYDOWN) {
                int k = ev.key.keysym.sym;
                if (g.typing) {
                    if (k == SDLK_ESCAPE) {
                        typing_set(0);
                    } else if (k == SDLK_RETURN) {
                        typing_set(0);
                        learn_save_named();
                    } else if (k == SDLK_BACKSPACE) {
                        int n = (int)strlen(g.namebuf);
                        if (n > 0) {
                            g.namebuf[n - 1] = 0;
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
                } else if (k >= SDLK_1 && k <= SDLK_8) {
                    int ch = k - SDLK_1;
                    g.active[ch] = !g.active[ch];
                    if (g.connected) {
                        cmd_push(g.active[ch] ? CMD_CHON : CMD_CHOFF, ch + 1,
                                 g.gain[ch]);
                    }
                }
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                click(ev.button.x, ev.button.y);
            }
        }
        learn_tick();
        SDL_GetWindowSize(win, &win_w, &win_h);
        if (win_w < 800) {
            win_w = 800;
        }
        if (win_h < 560) {
            win_h = 560;
        }
        frame();
        SDL_RenderPresent(R);
    }
    g.running = 0;
    pthread_cond_signal(&g.qcv);
    pthread_join(g.cmd_thr, NULL);
    do_disconnect();
    SDL_DestroyRenderer(R);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

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
    g.scale_uv = 200;
    g.notch_hz = 50;
    g.hp_hz = 1;
    g.detrend = 0;
    g.grid = 1;
    g.show_uv = 1;
    cfg_load();
    filt_reset();
    pthread_mutex_init(&g.mu, NULL);
    pthread_mutex_init(&g.qmu, NULL);
    pthread_cond_init(&g.qcv, NULL);
    np_ring_init(&g.ring);
    npl_init(&g.learn);
    {
        char lp[NP_MAX_PATH];
        learn_path(lp, sizeof(lp));
        npl_load(&g.learn, lp);
    }
    if (pthread_create(&g.cmd_thr, NULL, cmd_thread, NULL) != 0) {
        fprintf(stderr, "cmd thread failed\n");
        return 1;
    }
    for (i = 0; i < NP_NCHAN; i++) {
        g.active[i] = 1;
        g.rld[i] = 1;
        g.gain[i] = 12;
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

    if (cli) {
        return run_cli(port, seconds);
    }
    return run_gui();
}
