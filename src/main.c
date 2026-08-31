#define _GNU_SOURCE
#include "np_dsp.h"
#include "np_font.h"
#include "np_knight.h"
#include "np_ring.h"
#include "np_serial.h"
#include "sdl2_min.h"

#include <errno.h>
#include <stdarg.h>
#include <grp.h>
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
#define FFT_H 150
#define WAVE_TOP 8
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
};

static struct np_app g;

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
                np_ring_push(&g.ring, &s);
                if (g.csv) {
                    int c;
                    struct timespec ts;
                    clock_gettime(CLOCK_REALTIME, &ts);
                    fprintf(g.csv, "%ld.%09ld,%u", (long)ts.tv_sec, ts.tv_nsec, s.seq);
                    for (c = 0; c < NP_NCHAN; c++) {
                        fprintf(g.csv, ",%.3f", s.uv[c]);
                    }
                    fputc('\n', g.csv);
                }
            }
        }
    }
    return NULL;
}

/* Official NeuroPawn BrainFlow host: sleep 2s after start_stream, then
 * chon_N_G with ~1s gaps. Commands are processed inside firmware
 * acquire_data(); sending during the Arduino DTR reboot is ignored. */
static void *enable_thread(void *arg)
{
    int c;
    (void)arg;
    set_status(1, "waiting for board (2s)...");
    usleep(2000000);
    if (!g.connected || g.fd < 0) {
        return NULL;
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
        set_status(1, "connected %s", g.nports ? g.ports[g.port_i] : "");
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
    set_status(1, "connected %s — enabling channels", path);
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
        fprintf(g.csv, "time,seq,ch1,ch2,ch3,ch4,ch5,ch6,ch7,ch8\n");
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

static struct hit hits[64];
int nhits;

static void add_hit(int x, int y, int w, int h, int kind, int ch)
{
    if (nhits >= 64) {
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

static void draw_waves(int x, int y, int w, int h)
{
    int c;
    fill(x, y, w, h, 10, 12, 16);
    for (c = 0; c < NP_NCHAN; c++) {
        float buf[NP_RING];
        uint32_t n = np_ring_copy(&g.ring, c, buf, 500);
        int y0 = y + (c * h) / NP_NCHAN;
        int y1b = y + ((c + 1) * h) / NP_NCHAN;
        int row_h = y1b - y0;
        int mid = (y0 + y1b) / 2;
        uint32_t i;
        char lab[28];
        float last = n ? buf[n - 1] : 0.f;
        int nocontact = n && (last > 3000.f || last < -3000.f);
        fill(x, y1b - 1, w, 1, 32, 36, 44);
        snprintf(lab, sizeof(lab), "ch%d", c + 1);
        text(x + 4, y0 + 4, lab, CHCOL[c][0], CHCOL[c][1], CHCOL[c][2], 1);
        snprintf(lab, sizeof(lab), "%+.0f uV", last);
        text(x + 40, y0 + 4, lab, 170, 176, 186, 1);
        if (nocontact) {
            text(x + 120, y0 + 4, "no contact", 200, 90, 80, 1);
        }
        if (!g.active[c] || n < 4) {
            continue;
        }
        np_detrend(buf, (int)n);
        {
            /* Same as the original pyqtgraph view: autoscale the row so you
             * see shape. The uV label is the ground truth. */
            float peak = 8.f;
            for (i = 0; i < n; i++) {
                float a = fabsf(buf[i]);
                if (a > peak) {
                    peak = a;
                }
            }
            SDL_SetRenderDrawColor(R, (Uint8)CHCOL[c][0], (Uint8)CHCOL[c][1],
                                   (Uint8)CHCOL[c][2], 255);
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
}

static void draw_fft(int x, int y, int w, int h)
{
    enum { N = 128 };
    float mag[N / 2];
    float acc[N];
    int c, i, used = 0, rail_n = 0, peak_i = 1;
    char cap[48];
    fill(x, y, w, h, 10, 12, 16);
    memset(mag, 0, sizeof(mag));
    for (c = 0; c < NP_NCHAN; c++) {
        float buf[N];
        uint32_t n;
        float last;
        int rail;
        if (!g.active[c]) {
            continue;
        }
        n = np_ring_copy(&g.ring, c, buf, N);
        if (n < N) {
            continue;
        }
        last = buf[n - 1];
        rail = (last > 5000.f || last < -5000.f);
        if (rail) {
            rail_n++;
            continue;
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
    /* If every enabled channel is open-input, still FFT them — that is what
     * the original host does — but label it so it is not mistaken for EEG. */
    if (used == 0 && rail_n) {
        for (c = 0; c < NP_NCHAN; c++) {
            float buf[N];
            uint32_t n;
            if (!g.active[c]) {
                continue;
            }
            n = np_ring_copy(&g.ring, c, buf, N);
            if (n < N) {
                continue;
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
    }
    snprintf(cap, sizeof(cap), rail_n ? "FFT  (no skin contact)" : "FFT Hz");
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
            snprintf(hz, sizeof(hz), "peak %dHz", (peak_i * NP_DEFAULT_SPS) / N);
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
}

static void draw_status(void)
{
    char st[200];
    uint64_t tot = 0;
    uint32_t good = 0, bad = 0;
    np_ring_stats(&g.ring, &tot, &good, &bad);
    pthread_mutex_lock(&g.mu);
    snprintf(st, sizeof(st), "%s   frames %llu  bad %u  flen %d%s", g.status,
             (unsigned long long)tot, bad, g.parser.frame_len, g.parser.locked ? " lock" : "");
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
    if (g.connected && g.active[ch]) {
        np_parser_set_gain(&g.parser, ch + 1, g.gain[ch]);
        np_cmd_chon(g.fd, ch + 1, g.gain[ch]);
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
        default:
            break;
        }
        return;
    }
}

static void frame(void)
{
    int plot_w = win_w - SIDE_W - 24;
    int wave_h = win_h - WAVE_TOP - FFT_H - 8 - STATUS_H;
    if (wave_h < NP_NCHAN * 36) {
        wave_h = NP_NCHAN * 36;
    }
    nhits = 0;
    fill(0, 0, win_w, win_h, 22, 24, 30);
    fill(win_w - SIDE_W, 0, SIDE_W, win_h, 26, 28, 34);
    draw_waves(12, WAVE_TOP, plot_w, wave_h);
    draw_fft(12, WAVE_TOP + wave_h + 6, plot_w, FFT_H);
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
        set_status(1, "idle — plug board, click Connect");
    }

    while (live) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                live = 0;
            } else if (ev.type == SDL_KEYDOWN) {
                int k = ev.key.keysym.sym;
                if (k == SDLK_ESCAPE || k == SDLK_q) {
                    live = 0;
                } else if (k == SDLK_c) {
                    do_connect();
                } else if (k == SDLK_d) {
                    do_disconnect();
                } else if (k == SDLK_r) {
                    toggle_record();
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
            "  keys: c connect  d disconnect  r record  1-8 channel  Tab port  q quit\n",
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
    g.board = NP_BOARD_KNIGHT_IMU; /* live Knight boards stream the 57-byte IMU frame */
    pthread_mutex_init(&g.mu, NULL);
    pthread_mutex_init(&g.qmu, NULL);
    pthread_cond_init(&g.qcv, NULL);
    np_ring_init(&g.ring);
    if (pthread_create(&g.cmd_thr, NULL, cmd_thread, NULL) != 0) {
        fprintf(stderr, "cmd thread failed\n");
        return 1;
    }
    for (i = 0; i < NP_NCHAN; i++) {
        g.active[i] = 1;
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
