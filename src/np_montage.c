#include "np_local.h"
#include "np_mods.h"

/* Channels, electrode sites, and the bipolar traces. */

static int last_pair_clip[NP_BIPOLAR_N];

/* Restores the eight motor and visual plus/minus pairs and saves the ini.
 * Whether NEG RAIL is on does not change; on the rail, bias and CAR stay off and
 * a connected board is told to drop bias. */
void np_host_montage_default(void)
{
    int c;
    int rail = g.neg_rail ? 1 : 0;
    /* Sites only. NEG RAIL stays whatever the user already chose.
     * On rail, bias and CAR stay off. Off rail, bias and CAR stay as they are. */
    np_montage_restore(g.elec, g.neg_site, rail, &g.car, g.rld);
    if (rail && g.connected) {
        for (c = 0; c < NP_NCHAN; c++) {
            cmd_push(CMD_RLDRM, c + 1, 0);
        }
    }
    filt_reset();
    cfg_save();
    set_status(1, rail ? "defaults restored — NEG RAIL stays on"
                       : "defaults restored — bias RLD stays");
}

/* Turns channel ch (0..7) on or off, saves, and sends the command if connected.
 * Refuses to turn off the last channel. A bad index returns. */
void np_host_set_active(int ch, int on)
{
    if (ch < 0 || ch >= NP_NCHAN) {
        return;
    }
    if (!on) {
        int i, n = 0;
        for (i = 0; i < NP_NCHAN; i++) {
            if (i != ch && g.active[i]) {
                n++;
            }
        }
        if (n < 1) {
            set_status(0, "leave at least one channel on");
            return;
        }
    }
    g.active[ch] = on ? 1 : 0;
    cfg_save();
    if (g.connected) {
        cmd_push(g.active[ch] ? CMD_CHON : CMD_CHOFF, ch + 1, g.gain[ch]);
    }
}

/* Bias for one channel, saved. NEG RAIL refuses and leaves bias off. The board
 * command waits until connect if the cable is down. */
void np_host_set_rld(int ch, int on)
{
    if (ch < 0 || ch >= NP_NCHAN) {
        return;
    }
    if (g.neg_rail) {
        set_status(0, "neg rail — bias locked off");
        return;
    }
    g.rld[ch] = on ? 1 : 0;
    cfg_save();
    if (g.connected) {
        cmd_push(rld_want(ch) ? CMD_RLDADD : CMD_RLDRM, ch + 1, 0);
        set_status(1, "ch%d bias %s", ch + 1, g.rld[ch] ? "on" : "off");
    } else {
        set_status(1, "ch%d bias %s (applies on Connect)", ch + 1, g.rld[ch] ? "on" : "off");
    }
}

/* Next legal gain (1, 2, 3, 4, 6, 8, 12), updates the parser, and saves. Sends
 * it only if that channel is connected and on. A bad index returns. */
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

/* Sets the gain only if it is 1, 2, 3, 4, 6, 8, or 12, then saves. Any other
 * value, or a bad channel, returns without a change. */
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

/* 1 if channel ch (0..7) is on. 0 if ch is out of range. */
int np_host_active(int ch)
{
    return (ch >= 0 && ch < NP_NCHAN) ? g.active[ch] : 0;
}

/* 1 if this channel's bias pin is on. NEG RAIL forces the answer to 0. */
int np_host_rld(int ch)
{
    return rld_want(ch);
}

/* 1 if NEG RAIL is on. Samples are then already V(+)−V(−), and bias stays off. */
int np_host_neg_rail(void)
{
    return g.neg_rail ? 1 : 0;
}

/* Turns NEG RAIL on or off and saves. On forces bias off, CAR off, and
 * minus-site picking on. Off does not restore the electrode map. */
void np_host_set_neg_rail(int on)
{
    int c;
    g.neg_rail = on ? 1 : 0;
    if (g.neg_rail) {
        g.neg_pick = 1;
        g.car = 0;
        for (c = 0; c < NP_NCHAN; c++) {
            g.rld[c] = 0;
            if (g.connected) {
                cmd_push(CMD_RLDRM, c + 1, 0);
            }
        }
        set_status(1, "NEG RAIL — bias off, each channel is + to −");
    } else {
        g.neg_pick = 0;
        set_status(1, "bias RLD — per channel");
    }
    filt_reset();
    cfg_save();
}

/* 10-10 index of the minus site, or -1 if it is unset or ch is outside 0..7. */
int np_host_neg_site(int ch)
{
    if (ch < 0 || ch >= NP_NCHAN) {
        return -1;
    }
    return neg_site_ok(g.neg_site[ch]) ? g.neg_site[ch] : -1;
}

/* Sets the minus site for one channel and saves. -1 clears it, an index outside
 * the 10-10 list returns without a change, and this does not turn NEG RAIL on. */
void np_host_set_neg_site(int ch, int site)
{
    if (ch < 0 || ch >= NP_NCHAN) {
        return;
    }
    if (site == -1) {
        g.neg_site[ch] = -1;
        g.elec_sel = ch;
        g.neg_pick = 1;
        cfg_save();
        set_status(1, "ch%d − NONE", ch + 1);
        return;
    }
    if (!neg_site_ok(site)) {
        return;
    }
    g.neg_site[ch] = site;
    g.elec_sel = ch;
    g.neg_pick = 1;
    g.site_focus = site;
    cfg_save();
    set_status(1, "ch%d − @ %s", ch + 1, np_1010_name(site));
}

/* Minus-site name, or NONE. A short or null buffer returns without writing. */
void np_host_neg_name(int ch, char *out, int n)
{
    const char *s;
    if (!out || n < 2) {
        return;
    }
    s = np_1010_name(np_host_neg_site(ch));
    snprintf(out, (size_t)n, "%s", s[0] ? s : "NONE");
}

/* 1 if the next site tap assigns a minus site. 0 assigns the plus site. */
int np_host_neg_pick(void)
{
    return g.neg_pick ? 1 : 0;
}

/* Nonzero picks minus sites, 0 picks plus sites. Does not save. Turning it on
 * moves focus to the selected channel's minus site when that site is set. */
void np_host_set_neg_pick(int on)
{
    g.neg_pick = on ? 1 : 0;
    if (g.neg_pick && g.elec_sel >= 0 && g.elec_sel < NP_NCHAN &&
        neg_site_ok(g.neg_site[g.elec_sel])) {
        g.site_focus = g.neg_site[g.elec_sel];
    }
}

/* ADS1299 gain for the channel: 1, 2, 3, 4, 6, 8, or 12. Out of range reads as 12. */
int np_host_gain(int ch)
{
    return (ch >= 0 && ch < NP_NCHAN) ? g.gain[ch] : 12;
}

/* Writes 0..255 RGB for the channel. A bad channel returns without writing the
 * pointers. A null pointer is skipped. */
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

/* Next palette color for the channel, and saves. */
void np_host_cycle_color(int ch)
{
    chcol_cycle(ch);
    cfg_save();
}

/* Clamps each component to 0..255 and saves. A bad channel returns without a change. */
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

/* How many software contrasts to draw. 0 while NEG RAIL is on, because each
 * sample is already V(+)−V(−); otherwise the laterality count or the referential
 * count. */
int np_host_pair_n(void)
{
    /* Hardware pairs are the eight channels. Do not subtract them again. */
    if (g.neg_rail) {
        return 0;
    }
    return g.pair_mode ? np_bipolar_count() : np_pair_count();
}

/* "A-B" names for software pair i: laterality pairs in pair mode, otherwise the
 * referential contrasts. A short or null buffer returns, and NEG RAIL is not
 * refused here. */
void np_host_pair_label(int i, char *out, int n)
{
    if (!out || n < 4) {
        return;
    }
    if (g.pair_mode) {
        snprintf(out, (size_t)n, "%s-%s", np_bipolar_site_a(i), np_bipolar_site_b(i));
        return;
    }
    snprintf(out, (size_t)n, "%s-%s", np_pair_site_a(i), np_pair_site_b(i));
}

/* Writes the two channel indexes. -1, and both pointers set to -1, while NEG
 * RAIL is on. Otherwise the pair table's result. */
int np_host_pair_chs(int i, int *a, int *b)
{
    if (g.neg_rail) {
        if (a) {
            *a = -1;
        }
        if (b) {
            *b = -1;
        }
        return -1;
    }
    if (g.pair_mode) {
        return np_bipolar_chs(g.elec, i, a, b);
    }
    return np_pair_chs(g.elec, i, a, b);
}

/* 1 if the two laterality pairs are selected instead of eight channels. Does not
 * say whether NEG RAIL is on. */
int np_host_pair_mode(void)
{
    return g.pair_mode ? 1 : 0;
}

/* Nonzero selects the two software pairs (motor FC, visual PO) and saves. Does
 * not change NEG RAIL. The subtraction is still refused while NEG RAIL is on. */
void np_host_set_pair_mode(int on)
{
    g.pair_mode = on ? 1 : 0;
    cfg_save();
    set_status(1, g.pair_mode ? "2 pairs — motor FC and visual PO" : "8 channels");
}

/* Samples of channel A minus B over the window, in µV. 0 if max is under 8 or
 * the pair is invalid, which includes every pair while NEG RAIL is on. Detrend
 * runs on the difference when that flag is on. */
int np_host_copy_pair(int p, float *dst, int max)
{
    float a[NP_RING], b[NP_RING];
    int ca, cb;
    uint32_t na, nb, n, i, want;
    if (!dst || max < 8 || np_host_pair_chs(p, &ca, &cb) != 0) {
        return 0;
    }
    want = (uint32_t)(g.window_s * design_sps());
    if (want < 32) {
        want = 32;
    }
    if (want > (uint32_t)max) {
        want = (uint32_t)max;
    }
    na = view_copy(ca, a, want);
    nb = view_copy(cb, b, want);
    n = na < nb ? na : nb;
    if (n > want) {
        n = want;
    }
    for (i = 0; i < n; i++) {
        dst[i] = a[i] - b[i];
    }
    if (g.detrend && n > 4) {
        np_detrend(dst, (int)n);
    }
    if (p >= 0 && p < NP_BIPOLAR_N) {
        last_pair_clip[p] = np_window_clip(dst, (int)n);
    }
    return (int)n;
}

/* 1 if the last copy of laterality pair p was clipped. 0 if p is outside those
 * two slots. Stale until a copy runs. */
int np_host_pair_clip(int p)
{
    if (p < 0 || p >= NP_BIPOLAR_N) {
        return 0;
    }
    return last_pair_clip[p];
}

/* RMS in µV of each software pair over the last 32 samples. The array is cleared
 * first, a null pointer returns, and NEG RAIL leaves all zeros. */
void np_host_pair_uv(float uv[4])
{
    float a[NP_RING], b[NP_RING], d[NP_RING];
    int p;
    if (!uv) {
        return;
    }
    memset(uv, 0, 4 * sizeof(float));
    for (p = 0; p < np_host_pair_n(); p++) {
        int ca, cb;
        uint32_t na, nb, n, i;
        float dc = 0, rms = 0, pk = 0;
        if (np_host_pair_chs(p, &ca, &cb) != 0) {
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

/* Channel index the next site tap writes. Not clamped here. */
int np_host_elec_sel(void)
{
    return g.elec_sel;
}

/* Selects channel ch. Out of range returns. Moves focus to the minus site if
 * minus-picking is on and that site is set, else the plus site. */
void np_host_set_elec_sel(int ch)
{
    if (ch < 0 || ch >= NP_NCHAN) {
        return;
    }
    g.elec_sel = ch;
    if (g.neg_pick && neg_site_ok(g.neg_site[ch])) {
        g.site_focus = g.neg_site[ch];
    } else if (g.elec[ch].site >= 0) {
        g.site_focus = g.elec[ch].site;
    }
    set_status(1, "ch%d %s", ch + 1, g.elec[ch].name[0] ? g.elec[ch].name : "?");
}

/* "N name", or on NEG RAIL with a minus site, "N plus-minus". Out of range
 * writes an empty string. */
void np_host_elec_label(int ch, char *out, int n)
{
    if (ch < 0 || ch >= NP_NCHAN) {
        out[0] = 0;
        return;
    }
    if (g.neg_rail && neg_site_ok(g.neg_site[ch]) && g.elec[ch].name[0]) {
        snprintf(out, (size_t)n, "%d %s-%s", ch + 1, g.elec[ch].name,
                 np_1010_name(g.neg_site[ch]));
        return;
    }
    snprintf(out, (size_t)n, "%d %s", ch + 1, g.elec[ch].name[0] ? g.elec[ch].name : "NONE");
}

/* Plus-site name, or NONE. A short or null buffer returns. Out of range writes
 * an empty string. */
void np_host_elec_name(int ch, char *out, int n)
{
    if (!out || n < 2) {
        return;
    }
    if (ch < 0 || ch >= NP_NCHAN) {
        out[0] = 0;
        return;
    }
    snprintf(out, (size_t)n, "%s", g.elec[ch].name[0] ? g.elec[ch].name : "NONE");
}

/* 10-10 index of the plus site, or -1 if none or ch is outside 0..7. Not the minus site. */
int np_host_elec_site(int ch)
{
    return (ch >= 0 && ch < NP_NCHAN) ? g.elec[ch].site : -1;
}

/* World position of the plus site's cube cell. A bad channel returns without writing. */
void np_host_elec_xyz(int ch, float *x, float *y, float *z)
{
    if (ch < 0 || ch >= NP_NCHAN) {
        return;
    }
    np_elec_cube_xyz(&g.elec[ch], x, y, z);
}

/* 10-10 index the map keys move. Not clamped here. */
int np_host_site_focus(void)
{
    return g.site_focus;
}

/* Moves focus by dir names, wrapping the 10-10 list. Does not assign the site. */
void np_host_site_step(int dir)
{
    cube_site_by(dir);
}

/* Assigns site to the selected channel's minus end if minus-picking is on,
 * otherwise the plus end. -1 clears that end and does not change NEG RAIL. An
 * index outside the list does not move focus, then the current focus is
 * assigned. */
void np_host_assign_site(int site)
{
    if (site == -1) {
        if (g.neg_pick && g.elec_sel >= 0 && g.elec_sel < NP_NCHAN) {
            np_host_set_neg_site(g.elec_sel, -1);
        } else if (g.elec_sel >= 0 && g.elec_sel < NP_NCHAN) {
            np_elec_set_site(&g.elec[g.elec_sel], -1);
            cfg_save();
            set_status(1, "ch%d + NONE", g.elec_sel + 1);
        }
        return;
    }
    if (site >= 0 && site < np_1010_count()) {
        g.site_focus = site;
    }
    cube_assign_focus();
}

/* How many 10-10 names exist. */
int np_host_site_n(void)
{
    return np_1010_count();
}

/* 10-10 name at i, or empty if i is outside the list. */
void np_host_site_name(int i, char *out, int n)
{
    const char *s = np_1010_name(i);
    snprintf(out, (size_t)n, "%s", s ? s : "");
}

/* 1 if this 10-10 name is marked core. 0 if i is outside the list. */
int np_host_site_core(int i)
{
    return np_1010_core(i);
}

/* Channel 0..7 whose plus site is i, or -1 if none. Does not look at minus sites. */
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

/* Flat map position, table units divided by 10. A bad index writes 0, 0. */
void np_host_site_flat(int i, float *fx, float *fy)
{
    np_1010_flat(i, fx, fy);
}

/* World position of that site's cube cell. */
void np_host_site_xyz(int i, float *x, float *y, float *z)
{
    np_1010_cube_xyz(i, x, y, z);
}

/* Cube cell indexes. -1 if i is outside the 10-10 list, and the pointers then get 3, 7, 3. */
int np_host_site_ijk(int i, int *x, int *y, int *z)
{
    return np_1010_ijk(i, x, y, z);
}

/* Fills up to cap head cells: xyz, size, and rgba packed as A,R,G,B bytes.
 * Returns how many, or 0 if a pointer is null or cap is under 1. */
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
