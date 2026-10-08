#include "np_local.h"
#include "np_mods.h"

/* Scale, time window, and pause. */

/* Window of cooked µV for one channel. 0 if ch is outside 0..7, dst is null, or
 * max is under 8; otherwise the length is window seconds times the design rate,
 * at least 32 and never more than max, and detrend runs when that flag is on. */
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

/* Plot half-scale in µV, as stored. Not clamped here. */
int np_host_scale_uv(void)
{
    return g.scale_uv;
}

/* Next of 50, 100, 200, 500, 1000, 2000, 5000 µV, then wraps, and saves. An
 * unknown stored value becomes 200 and is not saved. */
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

/* A positive value is the half-scale in µV and turns autoscale off. 0 or
 * negative turns autoscale on and does not change the stored scale. Saves either
 * way. */
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

/* Window length in seconds. A stored value under 1 reads as 2. */
int np_host_window_s(void)
{
    return g.window_s < 1 ? 2 : g.window_s;
}

/* Next of 1, 2, 4, 8 seconds, and saves. An unknown stored value becomes 2 and is saved. */
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

/* Clamps the window to 1..8 seconds and saves. */
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

/* 1 while the plot is paused. Does not say whether the board is still streaming. */
int np_host_paused(void)
{
    return g.paused;
}

/* Flips plot pause. Does not save and does not stop USB. */
void np_host_toggle_pause(void)
{
    g.paused = !g.paused;
}

/* UI scale in tenths. Stored values outside 8..22 read as 15. Drawing still
 * treats anything other than 10, 15, or 20 as 15. */
int np_host_ui_scale(void)
{
    if (g.ui_scale < 8 || g.ui_scale > 22) {
        return 15;
    }
    return g.ui_scale;
}

/* Steps through 15, then 20, then 10, and saves. A value under 15 jumps straight to 15. */
void np_host_cycle_ui_scale(void)
{
    if (g.ui_scale < 15) {
        g.ui_scale = 15;
    } else if (g.ui_scale < 20) {
        g.ui_scale = 20;
    } else {
        g.ui_scale = 10;
    }
    cfg_save();
}

/* Stores tenths, saves, and turns anything outside 8..22 into 15. Drawing still
 * treats a value other than 10, 15, or 20 as 15. */
void np_host_set_ui_scale(int tenths)
{
    if (tenths < 8 || tenths > 22) {
        tenths = 15;
    }
    g.ui_scale = tenths;
    cfg_save();
}
