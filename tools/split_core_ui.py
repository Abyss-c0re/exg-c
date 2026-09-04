#!/usr/bin/env python3
"""Split src/main.c into np_core.c (framework) and np_ui.c (SDL desktop)."""
import re
from pathlib import Path

ROOT = Path("/home/atlas/src/exg-c")
lines = (ROOT / "src/main.c").read_text().splitlines(keepends=True)


def sl(a, b):
    return "".join(lines[a - 1 : b])


EXPORT = {
    "set_status",
    "typing_set",
    "apply_readable_defaults",
    "cfg_load",
    "cfg_save",
    "do_connect",
    "do_disconnect",
    "cmd_push",
    "filt_reset",
    "id_label",
    "learn_path",
    "learn_persist",
    "learn_start_hold",
    "learn_tick",
    "np_cfg_root",
    "np_mkdir_p",
    "plate_want",
    "prof_cycle",
    "prof_load",
    "prof_save",
    "prof_scan",
    "raw_dump_ring",
    "raw_plate_path",
    "smx_tick",
    "stream_recover",
    "toggle_record",
    "view_copy",
    "atom_tick",
    "api_apply",
    "api_defaults",
    "ch_stats",
    "clean_btn",
    "clean_set_status",
    "clean_tag",
    "design_sps",
    "fft_refresh",
    "cube_zoom_by",
    "cube_site_by",
    "cube_virt_by",
    "cube_assign_focus",
    "next_gain",
    "chcol_cycle",
    "cal_save",
    "cal_load",
    "cal_capture",
    "calm_capture",
    "ensure_dialout",
    "ch_quality",
}

struct = sl(79, 202)
header = """#ifndef NP_APP_H
#define NP_APP_H

/* Internal core. Desktop UI may include this. Product API is np_host.h. */

#include "np_api.h"
#include "np_atom.h"
#include "np_dsp.h"
#include "np_knight.h"
#include "np_ring.h"
#include "np_serial.h"
#include "np_smx.h"
#include "np_types.h"
#include "nplearn.h"

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
#define Q_OFF 0
#define Q_ZERO 1
#define Q_LEADOFF 2
#define Q_OPEN 3
#define Q_LIVE 4
enum { FFT_STRIP_N = 128, FFT_STRIP_BINS = 64 };
#define NSCALE 6
#define NWINS 4
#define NWINPREF 4
#define NPAL 12

""" + struct + """

extern struct np_app g;
extern const int CHCOL[NP_NCHAN][3];
extern const int PALETTE[NPAL][3];
extern const int SCALE_UV[NSCALE];
extern const int WIN_S[NWINS];
extern const int WINPREF[NWINPREF][2];
extern float fft_hold[FFT_STRIP_BINS];
extern int fft_used, fft_open, fft_peak_hz;

void set_status(int ok, const char *fmt, ...);
void typing_set(int on);
void apply_readable_defaults(void);
void cfg_load(void);
void cfg_save(void);
void do_connect(void);
void do_disconnect(void);
void cmd_push(int op, int ch, int gain);
void filt_reset(void);
void id_label(char *out, int n);
void learn_path(char *out, int n);
void learn_persist(void);
void learn_start_hold(void);
void learn_tick(void);
void np_cfg_root(char *out, int n);
void np_mkdir_p(const char *path);
uint32_t plate_want(void);
void prof_cycle(int dir);
int prof_load(void);
int prof_save(void);
void prof_scan(void);
void raw_dump_ring(const char *path);
void raw_plate_path(char *out, int n, const char *which);
void smx_tick(void);
void stream_recover(void);
void toggle_record(void);
uint32_t view_copy(int ch, float *dst, uint32_t n);
void atom_tick(void);
void api_apply(void);
void api_defaults(void);
void ch_stats(const float *buf, uint32_t n, float *dc, float *rms, float *pk);
const char *clean_btn(void);
void clean_set_status(void);
const char *clean_tag(void);
float design_sps(void);
void fft_refresh(void);
void cube_zoom_by(int dir);
void cube_site_by(int dir);
void cube_virt_by(int dir);
void cube_assign_focus(void);
void next_gain(int ch);
void chcol_cycle(int c);
int cal_save(void);
int cal_load(void);
void cal_capture(void);
void calm_capture(void);
void ensure_dialout(int argc, char **argv);
int ch_quality(int c, const float *buf, uint32_t n, uint8_t lp, uint8_t ln);
void np_ui_apply_window_size(int w, int h);

#endif
"""

core_head = sl(266, 2995)
extracted = sl(3143, 3160) + sl(3162, 3451) + sl(3715, 3780) + sl(3913, 4010) + sl(5176, 5185)
host = sl(6008, 8048)

core_c = r'''#define _GNU_SOURCE
#include "np_app.h"
#include "np_algo.h"
#include "np_cube.h"
#include "np_font.h"
#include "np_host.h"
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
const int SCALE_UV[NSCALE] = {50, 100, 200, 500, 1000, 5000};
const int WIN_S[NWINS] = {1, 2, 4, 8};
const int WINPREF[NWINPREF][2] = {{1280, 800}, {1440, 900}, {1600, 1000}, {1920, 1080}};
const int CHCOL[NP_NCHAN][3] = {
    {80, 200, 255}, {255, 180, 70}, {120, 220, 140}, {240, 110, 140},
    {180, 150, 255}, {255, 230, 90}, {90, 230, 210}, {230, 140, 255},
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

''' + core_head + extracted + host

exp_re = re.compile(
    r"^(\s*)static\s+(.+\b(?:" + "|".join(sorted(EXPORT)) + r")\s*\(.*)$"
)
fixed = []
for ln in core_c.splitlines(keepends=True):
    m = exp_re.match(ln.rstrip("\n"))
    if m:
        ln = m.group(1) + m.group(2) + "\n"
    ln = ln.replace("if (Win)", "if (g.pref_w > 0)")
    ln = ln.replace(
        "SDL_SetWindowSize(Win, g.pref_w, g.pref_h)",
        "np_ui_apply_window_size(g.pref_w, g.pref_h)",
    )
    # drop unused static CHCOL/PALETTE/SCALE if they appear in core_head
    if re.match(r"^static const int (CHCOL|PALETTE|SCALE_UV|WIN_S|WINPREF)", ln):
        continue
    if ln.strip() in ("#define NSCALE 6", "#define NWINS 4", "#define NWINPREF 4", "#define NPAL 12"):
        continue
    if ln.startswith("static void present_cube"):
        continue
    fixed.append(ln)
core_c = "".join(fixed)

ui_c = r'''#define _GNU_SOURCE
#include "np_app.h"
#include "np_cube.h"
#include "np_font.h"
#include "np_host.h"
#ifdef NP_ANDROID_UI
#include "sdl2_min.h"
#else
#include "sdl2_min.h"
#endif
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef NP_ANDROID_UI

static int win_w = WIN_W, win_h = WIN_H;
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

void np_ui_apply_window_size(int w, int h)
{
    if (Win) {
        SDL_SetWindowSize(Win, w, h);
    }
}

''' + sl(2997, 3131) + sl(3452, 3708) + sl(3781, 3912) + sl(4011, 5175) + sl(5186, 5962) + sl(5964, 6006) + sl(8050, 8177) + """
#endif /* !NP_ANDROID_UI */
"""

# strip CHCOL/PALETTE/Q macros duplicated into UI
ui_fixed = []
skip_block = 0
for ln in ui_c.splitlines(keepends=True):
    if re.match(r"^static const int (CHCOL|PALETTE)", ln):
        skip_block = 1
        continue
    if skip_block:
        if ln.strip().endswith("};"):
            skip_block = 0
        continue
    if ln.startswith("#define NPAL"):
        continue
    if ln.startswith("#define Q_"):
        continue
    if "enum { FFT_STRIP_N" in ln:
        continue
    if re.match(r"^static float fft_hold", ln) or re.match(r"^static uint32_t fft_t", ln):
        continue
    if re.match(r"^static int fft_used", ln):
        continue
    ui_fixed.append(ln)
ui_c = "".join(ui_fixed)

(ROOT / "include/np_app.h").write_text(header)
(ROOT / "src/np_core.c").write_text(core_c)
(ROOT / "src/np_ui.c").write_text(ui_c)
print("np_app.h", header.count("\n") + 1)
print("np_core.c", core_c.count("\n") + 1)
print("np_ui.c", ui_c.count("\n") + 1)
