#include "np_local.h"
#include "np_mods.h"

/* exg-c.ini, named profiles, and kit text.
 * Profile load keeps the electrode map and recooks plates and takes from raw.
 * Kit import writes the map. The API stays off until it is turned on.
 */

void typing_set(int on);
void cfg_save(void);

/* Samples a plate asks for: window seconds times the design rate, never under
 * NP_PLATE_N and never over the ring. */
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

/* Writes the exg-c.ini path under the config root. Does not create the file. */
static void cfg_path(char *out, size_t n)
{
    char root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    snprintf(out, n, "%s/exg-c.ini", root);
}

/* DC, RMS, and peak per channel, in µV, into the calm plate when cook is set,
 * else the desk-noise plate. Fewer than 16 samples skips that channel, and cook
 * turns the envelope off for the pass and puts it back. */
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

/* Turns on-screen text entry on or off. Turning it off also clears a profile-name edit. */
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
const char *made_src_or_default(int cube, int q);
static int cfg_write(const char *path);
int cfg_write_ex(const char *path, int with_map);
int cfg_read(const char *path);
void prof_scan(void);
void prof_apply(void);
void prof_autosave(void);
void data_recook(void);

/* Writes the profiles directory path. Does not create the directory. */
static void prof_dir(char *out, size_t n)
{
    char root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    snprintf(out, n, "%s/exg-c/profiles", root);
}

/* 1 if the name is letters, digits, '-' or '_', and shorter than the
 * profile-name limit. Empty, NULL, or anything else is 0. */
int prof_ok_name(const char *s)
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

/* Profile ini path for that name. A null name is written as default, and the
 * name is not checked. */
void prof_file(const char *name, char *out, size_t n)
{
    char dir[192];
    prof_dir(dir, sizeof(dir));
    snprintf(out, n, "%s/%.23s.ini", dir, name ? name : "default");
}

/* Writes the full ini, electrode map included. A failed open leaves the previous
 * file; a successful open truncates it first. */
static int cfg_write(const char *path)
{
    return cfg_write_ex(path, 1);
}

/* Writes view, channels, and NEG RAIL. with_map also writes the API block and
 * the plus-site names. A failed open leaves the previous file; a successful open
 * has already truncated it. */
int cfg_write_ex(const char *path, int with_map)
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
    fprintf(f, "board=%d\n", (int)g.board_pref);
    fprintf(f, "fw_have=%d\n", g.fw_have);
    fprintf(f, "fw_mode=%d\n", g.fw_mode);
    fprintf(f, "algo=%d\n", g.algo);
    {
        const char *s = g.algo_src[0] ? g.algo_src : NP_ALGO_SRC_DEFAULT;
        fprintf(f, "algo_src=");
        for (; *s; s++) {
            if (*s == '\n') {
                fputs("\\n", f);
            } else if (*s == '\\') {
                fputs("\\\\", f);
            } else if (*s != '\r') {
                fputc(*s, f);
            }
        }
        fputc('\n', f);
    }
    fprintf(f, "alib_n=%d\n", g.alib_n);
    fprintf(f, "alib_sel=%d\n", g.alib_sel);
    {
        int ai;
        for (ai = 0; ai < g.alib_n && ai < NP_ALIB_N; ai++) {
            const char *s = g.alib[ai].src;
            fprintf(f, "alib%dname=%s\n", ai + 1, g.alib[ai].name);
            fprintf(f, "alib%dsrc=", ai + 1);
            for (; *s; s++) {
                if (*s == '\n') {
                    fputs("\\n", f);
                } else if (*s == '\\') {
                    fputs("\\\\", f);
                } else if (*s != '\r') {
                    fputc(*s, f);
                }
            }
            fputc('\n', f);
        }
    }
    fprintf(f, "\n[cube]\n");
    fprintf(f, "yaw=%.4f\n", (double)g.cube_yaw);
    fprintf(f, "pitch=%.4f\n", (double)g.cube_pitch);
    fprintf(f, "zoom=%.2f\n", (double)g.cube_zoom);
    fprintf(f, "view=%d\n", g.cube_view ? 1 : 0);
    fprintf(f, "float=%d\n", g.cube_float ? 1 : 0);
    fprintf(f, "made_n=%d\n", g.made_n);
    fprintf(f, "made_sel=%d\n", g.made_sel);
    {
        int ci, q;
        for (ci = 0; ci < g.made_n && ci < 4; ci++) {
            fprintf(f, "made%drgb=%d,%d,%d\n", ci + 1, g.made[ci].rgb[0],
                    g.made[ci].rgb[1], g.made[ci].rgb[2]);
            fprintf(f, "made%dalgo=%d\n", ci + 1, g.made[ci].cube_algo);
            for (q = 0; q < 8; q++) {
                fprintf(f, "made%dq%d=%d\n", ci + 1, q + 1, g.made[ci].ch[q]);
                fprintf(f, "made%dq%dalgo=%d\n", ci + 1, q + 1, g.made[ci].algo[q]);
            }
        }
    }
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
            if (g.elec[i].site < 0) {
                fprintf(f, "elec%d=NONE\n", i + 1);
            } else if (g.elec[i].name[0]) {
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
        if (neg_site_ok(g.neg_site[i])) {
            fprintf(f, "neg%d=%s\n", i + 1, np_1010_name(g.neg_site[i]));
        } else {
            fprintf(f, "neg%d=NONE\n", i + 1);
        }
    }
    fprintf(f, "neg_rail=%d\n", g.neg_rail ? 1 : 0);
    fclose(f);
    return 0;
}

/* Writes a kit: filters, algo source, plus-site names, channels, and NEG RAIL,
 * with no API block. A failed open leaves any previous kit file. */
int cfg_write_kit(const char *path)
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
    fprintf(f, "board=%d\n", (int)g.board_pref);
    fprintf(f, "fw_have=%d\n", g.fw_have);
    fprintf(f, "fw_mode=%d\n", g.fw_mode);
    fprintf(f, "algo=%d\n", g.algo);
    {
        const char *s = g.algo_src[0] ? g.algo_src : NP_ALGO_SRC_DEFAULT;
        fprintf(f, "algo_src=");
        for (; *s; s++) {
            if (*s == '\n') {
                fputs("\\n", f);
            } else if (*s == '\\') {
                fputs("\\\\", f);
            } else if (*s != '\r') {
                fputc(*s, f);
            }
        }
        fputc('\n', f);
    }
    fprintf(f, "\n[cube]\n");
    fprintf(f, "float=%d\n", g.cube_float ? 1 : 0);
    for (i = 0; i < NP_NCHAN; i++) {
        if (g.elec[i].site < 0) {
            fprintf(f, "elec%d=NONE\n", i + 1);
        } else if (g.elec[i].name[0]) {
            fprintf(f, "elec%d=%s\n", i + 1, g.elec[i].name);
        }
    }
    fprintf(f, "\n[channels]\n");
    for (i = 0; i < NP_NCHAN; i++) {
        fprintf(f, "gain%d=%d\n", i + 1, g.gain[i]);
        fprintf(f, "color%d=%d,%d,%d\n", i + 1, g.chrgb[i][0], g.chrgb[i][1], g.chrgb[i][2]);
        fprintf(f, "active%d=%d\n", i + 1, g.active[i] ? 1 : 0);
        fprintf(f, "rld%d=%d\n", i + 1, g.rld[i] ? 1 : 0);
        if (neg_site_ok(g.neg_site[i])) {
            fprintf(f, "neg%d=%s\n", i + 1, np_1010_name(g.neg_site[i]));
        } else {
            fprintf(f, "neg%d=NONE\n", i + 1);
        }
    }
    fprintf(f, "neg_rail=%d\n", g.neg_rail ? 1 : 0);
    fclose(f);
    return 0;
}

/* Loads an ini into live settings, including plus sites when elec lines are
 * present, and ignores pair_mode. NEG RAIL forces bias and CAR off after the
 * read, and a missing file returns -1. */
int cfg_read(const char *path)
{
    FILE *f;
    char line[640];
    if (!path || !path[0]) {
        return -1;
    }
    f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    while (fgets(line, sizeof(line), f)) {
        int v, ch, i, r, gc, b;
        float fa, fb;
        char ename[24];
        char longv[64];
        if (!strncmp(line, "alib", 4) && strstr(line, "name=")) {
            int ai = 0;
            char nm[NP_ALIB_NAME];
            if (sscanf(line, "alib%dname=%15s", &ai, nm) == 2 && ai >= 1 &&
                ai <= NP_ALIB_N) {
                snprintf(g.alib[ai - 1].name, sizeof(g.alib[ai - 1].name), "%s",
                         nm);
                if (ai > g.alib_n) {
                    g.alib_n = ai;
                }
            }
        } else if (!strncmp(line, "alib", 4) && strstr(line, "src=")) {
            int ai = 0;
            const char *eq;
            if (sscanf(line, "alib%dsrc=", &ai) == 1 && ai >= 1 &&
                ai <= NP_ALIB_N) {
                int o = 0;
                eq = strchr(line, '=');
                if (eq) {
                    const char *in = eq + 1;
                    while (*in && *in != '\n' && *in != '\r' &&
                           o < NP_ALGO_SRC - 1) {
                        if (in[0] == '\\' && in[1] == 'n') {
                            g.alib[ai - 1].src[o++] = '\n';
                            in += 2;
                        } else if (in[0] == '\\' && in[1] == '\\') {
                            g.alib[ai - 1].src[o++] = '\\';
                            in += 2;
                        } else {
                            g.alib[ai - 1].src[o++] = *in++;
                        }
                    }
                    g.alib[ai - 1].src[o] = 0;
                }
                if (ai > g.alib_n) {
                    g.alib_n = ai;
                }
            }
        } else if (sscanf(line, "alib_n=%d", &v) == 1 && v >= 0 &&
                   v <= NP_ALIB_N) {
            g.alib_n = v;
        } else if (sscanf(line, "alib_sel=%d", &v) == 1 && v >= 0 &&
                   v < NP_ALIB_N) {
            g.alib_sel = v;
        } else if (sscanf(line, "made%dq%dalgo=%d", &ch, &i, &v) == 3 &&
                   ch >= 1 && ch <= 4 && i >= 1 && i <= 8 && v >= -1 &&
                   v < NP_ALIB_N) {
            g.made[ch - 1].algo[i - 1] = v;
        } else if (sscanf(line, "made%dalgo=%d", &ch, &v) == 2 && ch >= 1 &&
                   ch <= 4 && v >= 0 && v < NP_ALIB_N) {
            g.made[ch - 1].cube_algo = v;
        } else if (!strncmp(line, "made", 4) && strstr(line, "src=")) {
            /* 2.70 per-bit source — Algos tab library is SoT now */
        } else if (!strncmp(line, "algo_src=", 9)) {
            const char *in = line + 9;
            int o = 0;
            while (*in && *in != '\n' && *in != '\r' && o < NP_ALGO_SRC - 1) {
                if (in[0] == '\\' && in[1] == 'n') {
                    g.algo_src[o++] = '\n';
                    in += 2;
                } else if (in[0] == '\\' && in[1] == '\\') {
                    g.algo_src[o++] = '\\';
                    in += 2;
                } else {
                    g.algo_src[o++] = *in++;
                }
            }
            g.algo_src[o] = 0;
        } else if (sscanf(line, "window_s=%d", &v) == 1) {
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
        } else if (sscanf(line, "pair_mode=%d", &v) == 1) {
            (void)v; /* 2.66 toggle — ignored */
        } else if (sscanf(line, "made_n=%d", &v) == 1 && v >= 0 && v <= 4) {
            g.made_n = v;
        } else if (sscanf(line, "made_sel=%d", &v) == 1 && v >= 0 && v < 4) {
            g.made_sel = v;
        } else if (sscanf(line, "made%dq%d=%d", &ch, &i, &v) == 3 && ch >= 1 &&
                   ch <= 4 && i >= 1 && i <= 8 && v >= 0 && v <= NP_NCHAN) {
            g.made[ch - 1].ch[i - 1] = v;
        } else if (sscanf(line, "made%drgb=%d,%d,%d", &ch, &r, &gc, &b) == 4 &&
                   ch >= 1 && ch <= 4) {
            g.made[ch - 1].rgb[0] = (unsigned char)(r < 0 ? 0 : (r > 255 ? 255 : r));
            g.made[ch - 1].rgb[1] = (unsigned char)(gc < 0 ? 0 : (gc > 255 ? 255 : gc));
            g.made[ch - 1].rgb[2] = (unsigned char)(b < 0 ? 0 : (b > 255 ? 255 : b));
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
        } else if (sscanf(line, "fw_have=%d", &v) == 1 && v >= 0 && v < 10000) {
            g.fw_have = v;
        } else if (sscanf(line, "fw_mode=%d", &v) == 1 && v >= 0 && v <= 2) {
            g.fw_mode = v;
        } else if (sscanf(line, "board=%d", &v) == 1) {
            if (v == (int)NP_BOARD_KNIGHT) {
                g.board = g.board_pref = NP_BOARD_KNIGHT;
            } else if (v == (int)NP_BOARD_AUTO) {
                g.board = g.board_pref = NP_BOARD_AUTO;
            } else {
                g.board = g.board_pref = NP_BOARD_KNIGHT_IMU;
            }
        } else if (sscanf(line, "algo=%d", &v) == 1 && v >= 0 && v < NP_ALGO_N) {
            g.algo = v;
        } else if (sscanf(line, "scale=%d", &v) == 1 && v >= 1) {
            if (v == 1) {
                g.ui_scale = 10;
            } else if (v == 2) {
                g.ui_scale = 15;
            } else if (v == 3) {
                g.ui_scale = 20;
            } else if (v >= 8 && v <= 22) {
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
            g.link = v;
        } else if (sscanf(line, "pitch=%f", &fa) == 1) {
            g.cube_pitch = fa;
        } else if (sscanf(line, "elec%d=%f,%f", &v, &fa, &fb) == 3 && v >= 1 && v <= NP_NCHAN) {
            g.elec[v - 1].az = fa;
            g.elec[v - 1].el = fb;
            np_elec_set_site(&g.elec[v - 1], np_1010_nearest(fa, fb));
        } else if (sscanf(line, "elec%d=%7s", &v, ename) == 2 && v >= 1 && v <= NP_NCHAN) {
            if (!strcmp(ename, "NONE") || !strcmp(ename, "none")) {
                np_elec_set_site(&g.elec[v - 1], -1);
            } else {
                int s = np_1010_find(ename);
                if (s >= 0) {
                    np_elec_set_site(&g.elec[v - 1], s);
                }
            }
        } else {
            int ch, gn, r, gc, b;
            if (sscanf(line, "gain%d=%d", &ch, &gn) == 2 && ch >= 1 && ch <= NP_NCHAN &&
                np_gain_ok(gn)) {
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
            } else if (sscanf(line, "neg_rail=%d", &v) == 1) {
                g.neg_rail = v ? 1 : 0;
            } else if (sscanf(line, "neg%d=%7s", &ch, ename) == 2 && ch >= 1 &&
                       ch <= NP_NCHAN) {
                if (!strcmp(ename, "NONE") || !strcmp(ename, "none")) {
                    g.neg_site[ch - 1] = -1;
                } else {
                    int s = np_1010_find(ename);
                    if (s >= 0) {
                        g.neg_site[ch - 1] = s;
                    }
                }
            } else if (sscanf(line, "neg_site=%7s", ename) == 1) {
                /* 2.83 one shared site — copy onto any still-empty channel. */
                int s = np_1010_find(ename);
                if (s >= 0) {
                    int c;
                    for (c = 0; c < NP_NCHAN; c++) {
                        if (!neg_site_ok(g.neg_site[c])) {
                            g.neg_site[c] = s;
                        }
                    }
                }
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
    if (g.neg_rail) {
        int c;
        for (c = 0; c < NP_NCHAN; c++) {
            g.rld[c] = 0;
        }
        g.car = 0;
    }
    if (g.window_s < 1) {
        g.window_s = 2;
    }
    if (g.window_s > 8) {
        g.window_s = 8;
    }
    if (g.scale_uv < 20) {
        g.scale_uv = 200;
    }
    if (g.ui_scale < 8 || g.ui_scale > 22) {
        g.ui_scale = 15;
    }
    if (g.pref_w < 800) {
        g.pref_w = WIN_W;
    }
    if (g.pref_h < 560) {
        g.pref_h = WIN_H;
    }
    link_sanitize();
    if (g.scale_uv > 20000) {
        g.scale_uv = 5000;
    }
    if (g.notch_hz != 0 && g.notch_hz != 50 && g.notch_hz != 60 && g.notch_hz != -1) {
        g.notch_hz = 0;
    }
    if (g.hp_hz != 0 && g.hp_hz != 1 && g.hp_hz != 2 && g.hp_hz != 5 && g.hp_hz != 20) {
        g.hp_hz = 0;
    }
    if (g.lp_hz != 0 && g.lp_hz != 20 && g.lp_hz != 40) {
        g.lp_hz = 0;
    }
    if (g.api_hz < 1 || g.api_hz > 500) {
        g.api_hz = 125;
    }
    if (g.api_http < 0 || g.api_http > 65535) {
        g.api_http = 8765;
    }
    if (g.api_udp < 0 || g.api_udp > 65535) {
        g.api_udp = 8766;
    }
    if (g.api_tcp < 0 || g.api_tcp > 65535) {
        g.api_tcp = 8767;
    }
    {
        int c, i, ok;
        for (c = 0; c < NP_NCHAN; c++) {
            ok = 0;
            for (i = 0; i < NP_NGAINS; i++) {
                if (NP_GAINS[i] == g.gain[c]) {
                    ok = 1;
                    break;
                }
            }
            if (!ok) {
                g.gain[c] = 12;
            }
        }
    }
    if (g.made_n < 0 || g.made_n > 4) {
        g.made_n = 0;
    }
    if (g.made_n > np_host_made_max()) {
        g.made_n = np_host_made_max();
    }
    if (g.made_sel < 0 || g.made_sel >= g.made_n) {
        g.made_sel = g.made_n > 0 ? 0 : 0;
    }
    {
        int ci, q, inherit;
        for (ci = 0; ci < 4; ci++) {
            inherit = 0;
            for (q = 0; q < 8; q++) {
                if (g.made[ci].algo[q] < 0) {
                    inherit = 1;
                }
            }
            if (!inherit) {
                g.made[ci].cube_algo = g.made[ci].algo[0] >= 0 ? g.made[ci].algo[0]
                                                              : 0;
                for (q = 0; q < 8; q++) {
                    if (g.made[ci].algo[q] == g.made[ci].cube_algo) {
                        g.made[ci].algo[q] = -1;
                    }
                }
            }
        }
    }
    if (g.made_qsel < 0 || g.made_qsel > 7) {
        g.made_qsel = 0;
    }
    if (!g.algo_src[0]) {
        snprintf(g.algo_src, sizeof(g.algo_src), "%s", NP_ALGO_SRC_DEFAULT);
    }
    {
        int ci;
        for (ci = 0; ci < 4; ci++) {
            if (g.made[ci].rgb[0] == 0 && g.made[ci].rgb[1] == 0 && g.made[ci].rgb[2] == 0) {
                g.made[ci].rgb[0] = 255;
                g.made[ci].rgb[1] = 20;
                g.made[ci].rgb[2] = 40;
            }
        }
    }
    return 0;
}

/* Writes exg-c.ini under the config root, map included. A failed open leaves the
 * previous file. */
void cfg_save(void)
{
    char path[NP_MAX_PATH], root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    mkdir(root, 0755);
    cfg_path(path, sizeof(path));
    cfg_write(path);
}

/* Raw Knight view: filters, CAR, envelope, detrend, and the DC cut off, scale
 * 1000 µV, window 2 s. Does not touch NEG RAIL or the electrode map. */
void apply_readable_defaults(void)
{
    /* Raw, same as the official Knight plot. Off-head rails. Line-kill is a band. */
    g.band = 0;
    g.notch_hz = 0;
    g.hp_hz = 0;
    g.lp_hz = 0;
    g.car = 0;
    g.envelope = 0;
    g.detrend = 0;
    g.cal_cut = 0;
    g.scale_uv = 1000;
    g.window_s = 2;
}

/* Reads exg-c.ini, or ~/.config/exg-c.conf if that is missing, then fills empty
 * algos. Does not recook plates. */
void cfg_load(void)
{
    char path[NP_MAX_PATH];
    cfg_path(path, sizeof(path));
    if (cfg_read(path) != 0) {
        const char *h = getenv("HOME");
        if (h && h[0]) {
            snprintf(path, sizeof(path), "%s/.config/exg-c.conf", h);
            cfg_read(path);
        }
    }
    alib_seed();
}

/* Fills the profile list from *.ini in the profiles directory. A missing
 * directory leaves the count at 0. */
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

/* Pushes gains into the parser and, if the board is connected, channel and bias
 * commands. Returns before those commands when nothing is connected. NEG RAIL
 * still sends bias off. */
void prof_apply(void)
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
        cmd_push(rld_want(c) ? CMD_RLDADD : CMD_RLDRM, c + 1, 0);
    }
}

/* Writes the named profile without the electrode map, then writes exg-c.ini with
 * the map. Returns without a new profile file if the name is illegal or that
 * write fails. */
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

/* Loads the named profile but puts the electrode map back, then recooks plates
 * and takes from raw and writes exg-c.ini. An empty name jumps to the first
 * saved profile. A missing file leaves settings alone. */
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

/* Rewrites the current profile without the electrode map. Returns immediately if
 * the name is not legal, and does not write exg-c.ini. */
void prof_autosave(void)
{
    char path[NP_MAX_PATH];
    if (!prof_ok_name(g.prof)) {
        return;
    }
    prof_file(g.prof, path, sizeof(path));
    cfg_write_ex(path, 0);
}

/* Deletes the current profile file and clears the name. A bad name or a failed
 * unlink leaves the name set. */
void prof_del(void)
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

/* Renames the profile file. The same name, or a name that is not legal, does not
 * touch the file. */
void prof_rename(const char *to)
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

/* Loads the next saved profile. The electrode map stays, and plates and takes
 * are recooked from raw. */
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

/* 1 and writes the integer if "key" appears as a JSON number. 0 if the key or
 * the number is missing. First match only. */
int cfg_jint(const char *js, const char *key, int *out)
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

/* Copies the first line that is not blank and not a # or // comment, and cuts at
 * the newline. A null source or a null out leaves an empty string, or returns
 * without writing. */
void algo_first_line(const char *s, char *out, int n)
{
    if (!out || n < 1) {
        return;
    }
    if (!s) {
        s = "";
    }
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        s++;
    }
    while (*s == '#' || (s[0] == '/' && s[1] == '/')) {
        while (*s && *s != '\n') {
            s++;
        }
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
            s++;
        }
    }
    snprintf(out, (size_t)n, "%s", s);
    {
        char *nl = strchr(out, '\n');
        if (nl) {
            *nl = 0;
        }
    }
}

/* Source text for that cube bit. Out of range uses the live algo. A bit set to
 * inherit (-1) does not climb to the cube algo; it gets the compare default. */
const char *made_src_or_default(int cube, int q)
{
    if (cube < 0 || cube >= g.made_n || q < 0 || q > 7) {
        return alib_src(g.algo);
    }
    return alib_src(g.made[cube].algo[q]);
}

/* 1 if channel ch (1..8) is already on another made cube. skip is the cube index
 * to ignore, and the answer is 0 if ch is outside 1..8. */
int made_ch_used(int ch, int skip)
{
    int i, q;
    if (ch < 1 || ch > NP_NCHAN) {
        return 0;
    }
    for (i = 0; i < g.made_n; i++) {
        if (i == skip) {
            continue;
        }
        for (q = 0; q < 8; q++) {
            if (g.made[i].ch[q] == ch) {
                return 1;
            }
        }
    }
    return 0;
}

/* Path of the scratch kit file exg-c.kit. Does not create it. */
void kit_tmp(char *out, int n)
{
    char root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    snprintf(out, (size_t)n, "%s/exg-c.kit", root);
}

/* Rebuilds the desk-noise spectrum CLEAN uses and the calm plate from raw, turns
 * the still-plate DC cut on if calm loaded, and rewrites takes and MATCH poses
 * from raw without moving the electrode map. A take with no montage flag is
 * stamped with the current NEG RAIL, and a replaced pose loses its cube. */
void data_recook(void)
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
            int pair = np_atom_montage(path);
            if (pair < 0) {
                pair = g.neg_rail ? 1 : 0;
            }
            np_atom_save_m(path, bits, rms, nsec, NP_ATOM_WIN, pair);
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

/* Profiles and kits. */

/* Stores a legal profile name only. Does not load or write a file. An illegal
 * name is ignored. */
void np_host_set_profile(const char *s)
{
    if (s && prof_ok_name(s)) {
        snprintf(g.prof, sizeof(g.prof), "%s", s);
    }
}

/* Current profile name, which may be empty. */
void np_host_get_profile(char *out, int n)
{
    snprintf(out, (size_t)n, "%s", g.prof);
}

/* Saves the profile without the map, then exg-c.ini. 0 if the name is legal,
 * even when the write failed, and -1 if it is not. */
int np_host_prof_save(void)
{
    prof_save();
    return prof_ok_name(g.prof) ? 0 : -1;
}

/* Loads the profile, keeps the electrode map, and recooks from raw. Always
 * returns 0, including when the file is missing. */
int np_host_prof_load(void)
{
    prof_load();
    return 0;
}

/* Deletes the current profile. 0 if the name was cleared, -1 if it is still set. */
int np_host_prof_del(void)
{
    prof_del();
    return g.prof[0] ? -1 : 0;
}

/* Renames the profile file. 0 only when the live name is now that name. */
int np_host_prof_rename(const char *to)
{
    prof_rename(to);
    return prof_ok_name(g.prof) && to && strcmp(g.prof, to) == 0 ? 0 : -1;
}

/* Rescans the profiles directory and returns how many legal names were kept. */
int np_host_prof_count(void)
{
    prof_scan();
    return g.nprof;
}

/* Rescans, then copies profile i. Out of range writes an empty string. */
void np_host_prof_at(int i, char *out, int n)
{
    prof_scan();
    if (i < 0 || i >= g.nprof) {
        out[0] = 0;
        return;
    }
    snprintf(out, (size_t)n, "%s", g.profiles[i]);
}

/* Writes a profile-shaped ini to path: no electrode map and no API block. 0 on
 * success, -1 if path is empty or the file cannot be opened. */
int np_host_prof_export(const char *path)
{
    return cfg_write_ex(path, 0);
}

/* Reads a profile file, puts the electrode map back, recooks plates and takes
 * from raw, and writes exg-c.ini. -1 if the file cannot be opened, and then
 * nothing else changes. */
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

/* Bytes of kit text plus each profile already in the list, or 0 if cap is under
 * 64 or the scratch file cannot be written. Does not rescan the profile
 * directory. */
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

/* Applies the kit, including the electrode map, writes each #profile block to
 * its own file, recooks from raw, and writes exg-c.ini. -1 if the text is under
 * 8 bytes or the kit cannot be read. */
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
