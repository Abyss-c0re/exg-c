#define _GNU_SOURCE
#include "np_app.h"
#include "np_algo.h"
#include "np_cube.h"
#include "np_font.h"
#include "np_host.h"
#ifdef NP_ANDROID_UI
#include "sdl2_min.h"
#include <android/log.h>
#define NP_ALOG(...) __android_log_print(ANDROID_LOG_INFO, "exg-c", __VA_ARGS__)
#else
#include "sdl2_min.h"
#define NP_ALOG(...) ((void)0)
#endif
#include <ctype.h>
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
    if (g.cube_float && s_cube_drag != 1) {
        viz_auto_yaw += 0.148f * dt;
    }
}

static void cam_pt(float x, float y, float z, int *sx, int *sy, float *depth)
{
    float vx, vy, vz, yaw = g.cube_yaw, yy = y;
    if (g.cube_view == 0 && g.cube_float) {
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
        {
            float uv[8], puv[4];
            float sc = (float)g.scale_uv;
            int p;
            if (sc < 25.f) {
                sc = 25.f;
            }
            np_host_leftover_uv(uv);
            np_host_pair_uv(puv);
            for (p = 0; p < NP_PAIR_N; p++) {
                int ca, cb, ax, ay, bx, by;
                float rel, cax, cay, caz, cbx, cby, cbz;
                if (np_host_pair_chs(p, &ca, &cb) != 0 || !g.active[ca] || !g.active[cb]) {
                    continue;
                }
                rel = puv[p] / sc;
                if (rel > 2.f) {
                    rel = 2.f;
                }
                if (rel < 0.03f) {
                    continue;
                }
                np_elec_cube_xyz(&g.elec[ca], &cax, &cay, &caz);
                np_elec_cube_xyz(&g.elec[cb], &cbx, &cby, &cbz);
                cam_pt(cax, cay, caz, &ax, &ay, NULL);
                cam_pt(cbx, cby, cbz, &bx, &by, NULL);
                SDL_SetRenderDrawColor(R, 255, 20, 26, (Uint8)(50 + 180 * (rel > 1.f ? 1.f : rel)));
                SDL_RenderDrawLine(R, ax, ay, bx, by);
            }
            for (c = 0; c < NP_NCHAN; c++) {
                float cx, cy, cz, rel;
                int sx, sy, rad;
                char nlab[12];
                if (!g.active[c]) {
                    s_elec_sx[c] = -20000;
                    s_elec_sy[c] = -20000;
                    continue;
                }
                np_elec_cube_xyz(&g.elec[c], &cx, &cy, &cz);
                cam_pt(cx, cy, cz, &sx, &sy, NULL);
                s_elec_sx[c] = sx;
                s_elec_sy[c] = sy;
                rel = uv[c] / sc;
                if (rel > 2.f) {
                    rel = 2.f;
                }
                rad = 4 + (int)(rel * 14.f * g.cube_zoom);
                fill_disk(sx, sy, rad, g.chrgb[c][0], g.chrgb[c][1], g.chrgb[c][2]);
                snprintf(nlab, sizeof(nlab), "%d %s", c + 1,
                         g.elec[c].name[0] ? g.elec[c].name : "?");
                text(sx - (int)strlen(nlab) * 3, sy - 18, nlab, g.chrgb[c][0], g.chrgb[c][1],
                     g.chrgb[c][2], 1);
            }
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
    const char *port = g.link ? (g.link_dest[0] ? g.link_dest : "other phone…") : port_short();
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
    btn(x + 12, y, 80, bh, g.link ? "other" : "board", 1, 70, 0, g.link ? 30 : 32, g.link ? 80 : 36,
        g.link ? 100 : 44);
    btn(x + 96, y, 84, bh, port, 1, 4, 0, 32, 36, 44);
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
        text(x + 12, y, "filters + band. Electrode map stays.", 100, 108, 116, 1);
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
        {
            char cl[40];
            np_host_cal_line(cl, sizeof(cl));
            btn(x + 6, y + 46, 160, 32, cl, g.cal_phase > 0 || g.cal.have, 25, 0,
                g.cal_phase == 4 ? 28 : 36, g.cal_phase >= 1 ? 90 : 38,
                g.cal_phase == 3 ? 40 : 46);
        }
        btn(x + 170, y + 46, 48, 32, clean_btn(), g.cal_cut, 27, 0,
            g.cal_cut ? 28 : 36, g.cal_cut ? 80 : 38, g.cal_cut ? 70 : 46);
        if (g.cal_arm) {
            text(x + 222, y + 56, "desk / off, then OK", 230, 190, 90, 1);
        } else if (g.cal.have && g.calm.have) {
            snprintf(lab, sizeof(lab), "noise %.0fHz  calm %.0f uV%s", g.cal_hz, g.calm.rms[0],
                     clean_tag());
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
                bx = x + 6 + i * bw;
                fill(bx, y + 84, bw - 5, 40, on ? 42 : 22, hit ? 50 : (on ? 50 : 26),
                     hit ? 42 : 32);
                text(bx + 4, y + 88, g.learn.s[i].name, 230, 232, 236, 1);
                if (hit) {
                    text(bx + 4, y + 100, "now", 80, 230, 140, 1);
                }
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
        snprintf(lab, sizeof(lab), "now: %s", g.learn.s[g.learn.best].name);
        text(x + 480, y + 8, lab, 80, 230, 120, 1);
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
            bx = x + 6 + i * bw;
            fill(bx, y + 28, bw - 5, 36, on ? 42 : 22, hit ? 50 : (on ? 50 : 26),
                 hit ? 42 : 32);
            text(bx + 4, y + 31, g.learn.s[i].name, 230, 232, 236, 1);
            if (hit) {
                text(bx + 4, y + 42, "now", 80, 230, 140, 1);
            }
            add_hit(bx, y + 28, bw - 5, 36, 20, i);
        }
    }

    {
        char cl[24];
        np_host_cal_line(cl, sizeof(cl));
        btn(x + 6, y + h - 22, 128, 18, cl, g.cal_phase > 0 || g.cal.have, 25, 0,
            g.cal_phase == 4 ? 28 : 36, g.cal_phase >= 1 ? 90 : 38, 46);
    }
    btn(x + 138, y + h - 22, 40, 18, clean_btn(), g.cal_cut, 27, 0,
        g.cal_cut ? 28 : 36, g.cal_cut ? 80 : 38, g.cal_cut ? 70 : 46);
    if (g.cal_arm) {
        text(x + 186, y + h - 18, "desk / off, then OK", 230, 190, 90, 1);
    } else if (g.cal.have && g.calm.have) {
        snprintf(lab, sizeof(lab), "noise %.0fHz  calm %.0f uV%s", g.cal_hz,
                 g.calm.rms[0], clean_tag());
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
        case 70:
            np_host_set_link(!np_host_link());
            break;
        case 3:
            toggle_record();
            break;
        case 4:
            if (g.link) {
                set_status(1, "other phone is name:settings-port, not USB");
                break;
            }
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
            np_host_cal_start();
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
            clean_set_status();
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
            set_status(1, "default  FCz-CPz CP4-FC3 FC4-CP3 C3-C4");
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
static void usage(const char *a0)
{
    fprintf(stderr,
            "usage: %s [--cli] [--port PATH] [--imu] [--seconds N]\n"
            "  GUI: click Connect, toggle channels / RLD / gain, Record CSV\n"
            "  keys: c connect  d disconnect  r record  space pause  1-8 channel  Tab port  q quit\n"
            "  learn: click name, type, Enter/Save. MATCH names only a unique winner.\n",
            a0);
}

int main(int argc, char **argv)
{
    int i, cli = 0, seconds = 8;
    const char *port = NULL;

    ensure_dialout(argc, argv);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--cli")) {
            cli = 1;
        } else if (!strcmp(argv[i], "--imu")) {
            /* default board */
        } else if (!strcmp(argv[i], "--port") && i + 1 < argc) {
            port = argv[++i];
        } else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) {
            seconds = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        }
    }
    if (np_host_start(NULL) != 0) {
        fprintf(stderr, "host start failed\n");
        return 1;
    }
    if (cli) {
        return run_cli(port, seconds);
    }
    return run_gui();
}

#endif /* !NP_ANDROID_UI */
