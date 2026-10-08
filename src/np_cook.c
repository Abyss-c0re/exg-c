#include "np_local.h"
#include "np_mods.h"

/* Display cook and the live IIR. The reader holds live_mu and steps
 * one sample at a time. The UI tick copies that window and drains
 * share commands. Do not rebuild poles on the plot path. */

void apply_filt(int ch, float *buf, uint32_t n);
void cook_all(float buf[NP_NCHAN][NP_RING], uint32_t nn[NP_NCHAN], uint32_t want);
void cook_id(float buf[NP_NCHAN][NP_RING], uint32_t nn[NP_NCHAN]);
void band_apply(int band);

/* 1 if channel ch belongs in the bias net. Always 0 in NEG RAIL. Out of 0..7 is 0. */
int rld_want(int ch)
{
    if (g.neg_rail) {
        return 0;
    }
    return (ch >= 0 && ch < NP_NCHAN && g.rld[ch]) ? 1 : 0;
}

/* NEG RAIL samples are already V(+)−V(−). Their mean is not a reference. */
int car_on(void)
{
    return g.car && !g.neg_rail;
}

/* 1 if s is a real 10-10 index. -1 and anything past the table are 0. */
int neg_site_ok(int s)
{
    return s >= 0 && s < np_1010_count();
}

uint32_t view_copy(int ch, float *dst, uint32_t n);
float cook_scale_ch(int c);
void cook_now(float uv[8], float base[8]);

/* Do not cook filter poles from a lagged measured rate (46 SPS makes
 * a 50 Hz notch sit past Nyquist and a 60 Hz notch is already there).
 * rate_snap is a held 125 / 200 / 250 / 500. 200 is the USB ceiling.
 * With no snap, a measured 100–160 SPS is used as-is. Anything else is 125. */
float design_sps(void)
{
    if (g.rate_snap == 125 || g.rate_snap == 200 || g.rate_snap == 250 || g.rate_snap == 500) {
        return (float)g.rate_snap;
    }
    if (g.sps >= 100.f && g.sps <= 160.f) {
        return g.sps;
    }
    return (float)NP_DEFAULT_SPS;
}

/* Below ~64% of the design rate. At 125 SPS that is still 80. */
int stream_cold(void)
{
    float floor = 0.64f * design_sps();
    if (floor < 80.f) {
        floor = 80.f;
    }
    return g.sps > 0.f && g.sps < floor;
}

/* Hz to notch. AUTO (setting -1) uses the noise-plate tone, which may be 0.
 * A fixed setting is that many Hz. */
float notch_hz_eff(void)
{
    if (g.notch_hz < 0) {
        return g.cal_hz;
    }
    return (float)g.notch_hz;
}

/* Wiener needs NP_FFT_N samples. Default 2 s × 125 SPS is 250 — too short. */
int clean_wiener_ready(void)
{
    return g.cal_cut && (g.noise_psd_ok || g.noise_psd_ch_ok) &&
           (int)(g.window_s * design_sps()) >= NP_FFT_N;
}

/* Button face: "CLN" when Wiener can run, "DC" when only offset subtract is on, else "dc". */
const char *clean_btn(void)
{
    if (clean_wiener_ready()) {
        return "CLN";
    }
    return g.cal_cut ? "DC" : "dc";
}

/* Plot suffix: "  CLEAN", "  DC", or empty. Same switch as the button. */
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

/* Status line for the DC/CLEAN switch. Does not change the switch. */
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

/* Samples already stepped through the live IIR. A filter reset zeros it. Not the ring total. */
uint64_t live_seen;
/* Last filter-knob mix. -1 forces the next sync to rebuild the poles. */
static int live_sig = -1;
static uint32_t live_wr;
static float live_ch[NP_NCHAN][NP_RING];
pthread_mutex_t live_mu = PTHREAD_MUTEX_INITIALIZER;

/* Monotonic milliseconds, truncated to 32 bits. Not wall clock. */
uint32_t pair_now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000u + (uint32_t)(t.tv_nsec / 1000000u));
}

/* Forces link to local or follow. A stored 2 becomes follow.
 * A bt: dest and its token are cleared. */
void link_sanitize(void)
{
    if (g.link == 2) {
        g.link = 1;
    }
    if (g.link != 0 && g.link != 1) {
        g.link = 0;
    }
    if (!strncmp(g.link_dest, "bt:", 3)) {
        g.link_dest[0] = 0;
        g.link_token[0] = 0;
    }
}

/* <config root>/exg-c.peers. Does not create the file. */
void peers_path(char *out, int n)
{
    char root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    snprintf(out, (size_t)n, "%s/exg-c.peers", root);
}

/* Writes the peer table to exg-c.peers. */
void peers_flush(void)
{
    char path[NP_MAX_PATH];
    peers_path(path, (int)sizeof(path));
    np_peers_save(&peers, path);
}

/* 1 if grant matches a saved peer. NULL or empty is 0. */
int host_grant_ok(const char *grant)
{
    return np_peers_grant_ok(&peers, grant);
}
/* SDL ticks of the last Wiener pass. Inside 80 ms the plot reuses that window. */
static uint32_t clean_t;
static uint64_t clean_seen;
static uint32_t clean_n[NP_NCHAN];
static float clean_ch[NP_NCHAN][NP_RING];

/* Mix of the live filter knobs. A change means the IIR poles are stale. */
static int filt_sig(void)
{
    return g.hp_hz + (g.notch_hz + 3) * 97 + (int)(notch_hz_eff() * 10.f) + g.cal_cut * 10007 +
           g.lp_hz * 13 + car_on() * 17 + g.envelope * 19 + g.band * 23 + g.neg_rail * 29;
}

/* Rebuilds HP, notch, LP, and envelope poles at the design rate. Zeros the live
 * cursor, so the next sync starts from a fresh history. */
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

/* Snapshot path (learn / CAL). Own poles — must not smash the live IIR. */
void apply_filt(int ch, float *buf, uint32_t n)
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

/* In-place cook of a copied window: HP, notch, CAR, LP, detrend, envelope.
 * want is unused. Skips a channel under 16 samples. Does not touch the live IIR. */
void cook_all(float buf[NP_NCHAN][NP_RING], uint32_t nn[NP_NCHAN], uint32_t want)
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
    if (car_on() && nmax > 0) {
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
void cook_id(float buf[NP_NCHAN][NP_RING], uint32_t nn[NP_NCHAN])
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
    if (!g.neg_rail) {
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
        if (nn[c] >= 16) {
            np_detrend(buf[c], (int)nn[c]);
        }
    }
}

/* RAW, LINE, EEG, or EMG when the knobs match, else -1. EMG matches a fixed 50 Hz
 * notch only. NEG RAIL expects CAR off. */
int band_from_filters(void)
{
    int notch_on = g.notch_hz != 0;
    int want_car = g.neg_rail ? 0 : 1;
    if (g.hp_hz == 0 && g.lp_hz == 0 && !g.car && !g.envelope && !g.detrend && !notch_on) {
        return NP_BAND_RAW;
    }
    if (g.hp_hz == 2 && g.lp_hz == 0 && g.car == want_car && !g.envelope && g.detrend && notch_on) {
        return NP_BAND_LINE;
    }
    if (g.hp_hz == 2 && g.lp_hz == 40 && g.car == want_car && !g.envelope && g.detrend &&
        notch_on) {
        return NP_BAND_EEG;
    }
    if (g.hp_hz == 20 && g.lp_hz == 0 && g.car == want_car && g.envelope && g.detrend &&
        g.notch_hz == 50) {
        return NP_BAND_EMG;
    }
    return -1;
}

/* If the knobs match a preset, stores that preset. A custom mix leaves the preset alone. */
void band_resync(void)
{
    int b = band_from_filters();
    if (b >= 0) {
        g.band = b;
    }
}

/* Loads one preset, 0..3, else RAW. Retunes, saves, and recooks. LINE and EEG turn
 * CLEAN on only when a noise plate exists. */
void band_apply(int band)
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
        g.car = g.neg_rail ? 0 : 1;
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
        g.car = g.neg_rail ? 0 : 1;
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
        g.car = g.neg_rail ? 0 : 1;
        g.envelope = 1;
        g.detrend = 1;
        g.scale_uv = 2000;
    }
    filt_reset();
    cfg_save();
    prof_autosave();
    data_recook();
}

/* Copies the built-in share settings into g and clears the token. Does not bind a socket. */
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

/* Copies in into out. Quotes and backslashes get a backslash. Bytes under 32 become
 * spaces. Truncates to n. NULL in writes an empty string. */
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

/* One JSON object of link, rate, firmware, and filter knobs. No trailing newline. */
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
             "{\"ok\":true,\"v\":\"" NP_APP_VER "\",\"connected\":%s,\"paused\":%s,\"sps\":%.1f,"
             "\"rate\":%d,\"chip\":%d,\"link_limited\":%s,"
             "\"fw\":%d,\"fw_need\":%d,\"fw_mode\":%d,"
             "\"frames\":%u,\"status\":\"%s\",\"id\":\"%s\",\"id_best\":%d,"
             "\"notch\":%d,\"hp\":%d,\"lp\":%d,\"car\":%d,\"band\":%d,\"mask\":%u,"
             "\"api\":\"%s\"}",
             g.connected ? "true" : "false", g.paused ? "true" : "false",
             g.sps > 1.f ? (double)g.sps : 0.0, (int)design_sps(), g.chip_sps,
             g.link_limited ? "true" : "false", g.fw_seen > 0 ? g.fw_seen : g.fw_have,
             EXG_FW_NEED, g.fw_mode, np_host_frames(), ste, ide, g.atom_id_best,
             g.notch_hz, g.hp_hz, g.lp_hz, g.car ? 1 : 0, g.band, mask, line);
}

/* JSON fields for the follower: filters, colors, sites, NEG names. Not a full object.
 * Returns if out is NULL or n < 8. */
void api_view_json(char *out, int n)
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
             "\"active\":[%d,%d,%d,%d,%d,%d,%d,%d],\"neg_rail\":%d,"
             "\"neg\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"],"
             "\"id\":\"%s\"",
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
             g.active[6] ? 1 : 0, g.active[7] ? 1 : 0, g.neg_rail ? 1 : 0,
             neg_site_ok(g.neg_site[0]) ? np_1010_name(g.neg_site[0]) : "",
             neg_site_ok(g.neg_site[1]) ? np_1010_name(g.neg_site[1]) : "",
             neg_site_ok(g.neg_site[2]) ? np_1010_name(g.neg_site[2]) : "",
             neg_site_ok(g.neg_site[3]) ? np_1010_name(g.neg_site[3]) : "",
             neg_site_ok(g.neg_site[4]) ? np_1010_name(g.neg_site[4]) : "",
             neg_site_ok(g.neg_site[5]) ? np_1010_name(g.neg_site[5]) : "",
             neg_site_ok(g.neg_site[6]) ? np_1010_name(g.neg_site[6]) : "",
             neg_site_ok(g.neg_site[7]) ? np_1010_name(g.neg_site[7]) : "",
             ide);
    (void)c;
}

/* Writes knobs, sites, and colors from a follower blob. Does not save. Unknown keys
 * are ignored. NEG RAIL on clears bias and CAR. */
void apply_link_cfg(const char *js)
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
    if (cfg_jint(js, "neg_rail", &v)) {
        g.neg_rail = v ? 1 : 0;
        if (g.neg_rail) {
            for (c = 0; c < NP_NCHAN; c++) {
                g.rld[c] = 0;
            }
            g.car = 0;
        }
    }
    p = strstr(js, "\"neg\":[");
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
                        g.neg_site[c] = s;
                    }
                }
                p = q + 1;
            }
        }
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

/* Copies remote µV into the live window without the local IIR. A set mask bit turns
 * that channel on and does not turn the others off. NULL returns. */
void link_on_sample(const struct np_api_sample *s)
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
    {
        int snap = np_rate_snap(s->sps);
        if (snap) {
            g.rate_snap = snap;
            g.chip_sps = snap == 200 ? g.chip_sps : snap;
            g.link_limited = snap == 200;
        }
    }
    g.paused = (s->flags & 2) ? 1 : 0;
    if (!g.status_ok) {
        set_status(1, "following EXG");
    }
}

/* Binds or rebinds the share from g. If the bind fails while share was on, turns it off. */
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
    np_api_set_pair_ask_fn(np_host_pair_ask);
    if (np_api_apply(&c) != 0 && g.api_on) {
        g.api_on = 0;
        set_status(0, "share did not bind — pick a free port");
    }
}

/* Pushes one cooked 8-channel µV sample when the share is on. Decimates toward the
 * share Hz instead of pushing every design-rate call. */
static void api_emit(const float *v, uint32_t frames, uint64_t t_us)
{
    static uint32_t hold;
    struct np_api_sample s;
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
    s.t_us = t_us;
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
    s.sps = g.rate_snap > 0 ? design_sps() : g.sps;
    s.id_best = (int8_t)g.atom_id_best;
    s.id_score = (g.atom_id_best >= 0 && g.atom_id_best < 32) ? g.atom_id[g.atom_id_best] : 0.f;
    np_api_push(&s);
}

/* UI tick. Applies queued share commands. A port or on/off change saves and rebinds. */
void api_drain(void)
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
            g.api_hz = arg < 1 ? 1 : (arg > 500 ? 500 : arg);
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
void live_sync_u(void)
{
    uint64_t tot = 0;
    uint32_t need, c, i;
    int sig = filt_sig();
    float nh = 0.f;
    float tmp[NP_NCHAN][NP_RING];
    uint32_t got[NP_NCHAN];
    struct np_api_grid grid;
    int have_grid = 0;

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
    if (np_api_on()) {
        int sps_i = (int)(design_sps() + 0.5f);
        struct timespec ts;
        uint64_t now;
        if (sps_i != 125 && sps_i != 200 && sps_i != 250 && sps_i != 500) {
            sps_i = NP_DEFAULT_SPS;
        }
        clock_gettime(CLOCK_REALTIME, &ts);
        now = (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
        np_api_stamp_burst((uint32_t)(live_seen + 1u), (int)need, sps_i, now, &grid);
        have_grid = 1;
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
        if (car_on()) {
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
        api_emit(v, (uint32_t)(live_seen + i + 1),
                 have_grid ? grid.start_us + (uint64_t)i * grid.step_us : 0);
    }
    live_wr += need;
    live_seen = tot;
}

/* UI tick. Takes live_mu and steps new samples. Do not call it while already
 * holding live_mu; the reader steps under that lock. */
void live_sync(void)
{
    pthread_mutex_lock(&live_mu);
    live_sync_u();
    pthread_mutex_unlock(&live_mu);
}

/* Newest n cooked µV for ch, oldest first. Holds live_mu. Returns the count copied,
 * which may be less than n. ch is not bounds-checked. */
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

/* µV scale for channel c, at least 25. Uses the id baseline, else the still-plate RMS,
 * when that is larger. */
float cook_scale_ch(int c)
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

/* RMS µV of the last 32 cooked samples. Inactive channels and windows under 4 stay 0.
 * base, if not NULL, receives the scale. uv may be NULL. */
void cook_now(float uv[8], float base[8])
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
        if (g.detrend && got > 1) {
            np_detrend(dst, (int)got);
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
    if (g.detrend && got > 1) {
        np_detrend(dst, (int)got);
    }
    return got;
}

/* Filter controls. */

/* Eight RMS values in µV, same cook as the traces. Inactive channels are 0. */
void np_host_cook_uv(float uv[8])
{
    cook_now(uv, NULL);
}
/* Notch setting: -1 AUTO, 0 off, 50 or 60. Not the Hz actually applied. */
int np_host_notch(void)
{
    return g.notch_hz;
}
/* Hz actually notched, rounded. 0 when the tone is idle. */
int np_host_notch_eff(void)
{
    float hz = notch_hz_eff();
    if (hz < 1.f) {
        return 0;
    }
    return (int)(hz + 0.5f);
}
/* High-pass setting in Hz. 0 is off. */
int np_host_hp(void)
{
    return g.hp_hz;
}
/* 50, then 60, off, AUTO, and back to 50. Saves and retunes. Does not reload plates. */
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
    band_resync();
    filt_reset();
    cfg_save();
}
/* Sets 0, 50, 60, or -1. Anything else becomes 50. Saves and recooks. */
void np_host_set_notch(int hz)
{
    if (hz != 0 && hz != 50 && hz != 60 && hz != -1) {
        hz = 50;
    }
    g.notch_hz = hz;
    band_resync();
    filt_reset();
    cfg_save();
    prof_autosave();
    data_recook();
}
/* 0, 1, 2, 5, 20 Hz, then wraps. An unknown current value becomes 1 and does not save. */
void np_host_cycle_hp(void)
{
    static const int hp[] = {0, 1, 2, 5, 20};
    int k;
    for (k = 0; k < 5; k++) {
        if (hp[k] == g.hp_hz) {
            g.hp_hz = hp[(k + 1) % 5];
            band_resync();
            filt_reset();
            cfg_save();
            return;
        }
    }
    g.hp_hz = 1;
}
/* Sets 0, 1, 2, 5, or 20 Hz. Anything else becomes 1. Saves and recooks. */
void np_host_set_hp(int hz)
{
    if (hz != 0 && hz != 1 && hz != 2 && hz != 5 && hz != 20) {
        hz = 1;
    }
    g.hp_hz = hz;
    band_resync();
    filt_reset();
    cfg_save();
    prof_autosave();
    data_recook();
}
/* Low-pass setting in Hz. 0 is off. */
int np_host_lp(void)
{
    return g.lp_hz;
}
/* 0, 20, 40 Hz, then wraps. An unknown current value becomes 0 and does not save. */
void np_host_cycle_lp(void)
{
    static const int lp[] = {0, 20, 40};
    int k;
    for (k = 0; k < 3; k++) {
        if (lp[k] == g.lp_hz) {
            g.lp_hz = lp[(k + 1) % 3];
            band_resync();
            filt_reset();
            cfg_save();
            return;
        }
    }
    g.lp_hz = 0;
}
/* Sets 0, 20, or 40 Hz. Anything else becomes 0. Saves and recooks. */
void np_host_set_lp(int hz)
{
    if (hz != 0 && hz != 20 && hz != 40) {
        hz = 0;
    }
    g.lp_hz = hz;
    band_resync();
    filt_reset();
    cfg_save();
    prof_autosave();
    data_recook();
}
/* 1 only when CAR is switched on and NEG RAIL is off. */
int np_host_car(void)
{
    return car_on();
}
/* Flips CAR and saves. NEG RAIL forces it off, retunes, and does not reload plates. */
void np_host_toggle_car(void)
{
    if (g.neg_rail) {
        g.car = 0;
        set_status(0, "NEG RAIL — CAR stays off");
        band_resync();
        filt_reset();
        cfg_save();
        prof_autosave();
        return;
    }
    g.car = !g.car;
    band_resync();
    filt_reset();
    cfg_save();
    prof_autosave();
    data_recook();
}
/* 1 when each plot window is detrended. */
int np_host_detrend(void)
{
    return g.detrend;
}
/* Flips detrend and recooks. Does not rebuild the live IIR poles. */
void np_host_toggle_detrend(void)
{
    g.detrend = !g.detrend;
    band_resync();
    cfg_save();
    prof_autosave();
    data_recook();
}
/* 1 when the live envelope follower is on. */
int np_host_envelope(void)
{
    return g.envelope;
}
/* Flips the envelope, rebuilds the poles, and recooks. */
void np_host_toggle_envelope(void)
{
    g.envelope = !g.envelope;
    band_resync();
    filt_reset();
    cfg_save();
    prof_autosave();
    data_recook();
}
/* Preset 0 RAW, 1 LINE, 2 EEG, 3 EMG. May disagree with the knobs. */
int np_host_band(void)
{
    return g.band;
}
/* Next preset, wrapping through all four. Saves and recooks. */
void np_host_cycle_band(void)
{
    band_apply((g.band + 1) % NP_BAND_N);
}
/* Loads that preset. Out of range becomes RAW. */
void np_host_set_band(int band)
{
    band_apply(band);
}

/* 1 when the knobs still match the stored preset. 0 for a custom mix. */
int np_host_band_fit(void)
{
    return band_from_filters() == g.band;
}
/* 1 if the last wave copy of channel ch was clipped. 0 outside 0..7.
 * Stale until a copy runs. */
int np_host_ch_clip(int ch)
{
    if (ch < 0 || ch >= NP_NCHAN) {
        return 0;
    }
    return last_clip[ch];
}
