#include "np_local.h"
#include "np_mods.h"

/* MATCH poses, live ID, and the one-second atom fold.
 * A pose name is a Record in exg-c.learn. A take is a different file.
 * NEG RAIL samples stay V(+)−V(−). A shared floor does not turn bias on.
 */

float atom_raw[NP_ATOM_RING][NP_NCHAN * NP_ATOM_WIN];
static void atom_identify(void);
float id_base[NP_NCHAN];
int id_base_ok;

/* Path of exg-c.learn under the config root. Creates the root directory if it can. */
void learn_path(char *out, size_t n)
{
    char root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    mkdir(root, 0755);
    snprintf(out, n, "%s/exg-c.learn", root);
}

/* Writes the MATCH pose bank to exg-c.learn. Does not write take files. */
void learn_persist(void)
{
    char path[NP_MAX_PATH];
    learn_path(path, sizeof(path));
    npl_save(&g.learn, path);
}

/* Copies only letters, digits, '-' and '_' into dst. A null or short dst returns
 * without writing, and a null source clears dst. */
void atom_sanitize(char *dst, int n, const char *src)
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

/* Path of the atoms directory. Creates it, and the config root, if they are missing. */
static void atom_dir(char *out, int n)
{
    char root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    mkdir(root, 0755);
    snprintf(out, (size_t)n, "%s/exg-c/atoms", root);
    np_mkdir_p(out);
}

/* Path of the .npat file after the letter filter. -1 if the name is empty once filtered. */
int atom_path(char *out, int n, const char *name)
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

/* Copies the newest k one-second folds, oldest first. k under 1 returns without
 * writing, k is clamped to the folds on hand, bits or rms may be NULL, and each
 * rms second is 8 floats in µV. */
void atom_last(uint64_t *bits, float *rms, int k)
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

/* Newest recording seconds, or the live ring if nothing was counted, into dst.
 * *n is how many were copied. */
static void atom_flatten(uint64_t *dst, int *n)
{
    int k = g.atom_rec_n > 0 ? g.atom_rec_n : g.atom_n;
    if (k > g.atom_n) {
        k = g.atom_n;
    }
    *n = k;
    atom_last(dst, NULL, k);
}

/* Bit agreement of the live folds with the loaded take, 0..1. Sets 0 and returns
 * if either side has no seconds. */
void atom_score(void)
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

/* Median calm RMS in µV over active channels above 50 µV. 50 µV if there is no
 * calm plate or no such channel. */
float atom_scale(void)
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

/* Once a second, while connected and not cold, packs one cooked second into the
 * atom ring. The envelope is forced off for that second and restored. A cold
 * stream returns without a fold. */
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

    if (!g.connected || (stream_cold())) {
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

uint8_t rec_smx[NPL_SMX_SEC];
int rec_smx_n;

/* Fills missing built-in algos up to the default eight and clamps the selection
 * into range. Does not write the ini. */
void alib_seed(void)
{
    int i;
    if (g.alib_n < 0) {
        g.alib_n = 0;
    }
    if (g.alib_n > NP_ALIB_N) {
        g.alib_n = NP_ALIB_N;
    }
    if (g.alib_n < NP_ALIB_DEF) {
        for (i = g.alib_n; i < NP_ALIB_DEF; i++) {
            snprintf(g.alib[i].name, sizeof(g.alib[i].name), "%s", np_algo_name(i));
            snprintf(g.alib[i].src, sizeof(g.alib[i].src), "%s", np_algo_def_src(i));
        }
        g.alib_n = NP_ALIB_DEF;
    }
    for (i = 0; i < NP_ALIB_DEF && i < g.alib_n; i++) {
        if (!g.alib[i].name[0]) {
            snprintf(g.alib[i].name, sizeof(g.alib[i].name), "%s", np_algo_name(i));
        }
        if (!g.alib[i].src[0]) {
            snprintf(g.alib[i].src, sizeof(g.alib[i].src), "%s", np_algo_def_src(i));
        }
    }
    if (g.alib_sel < 0 || g.alib_sel >= g.alib_n) {
        g.alib_sel = (g.algo >= 0 && g.algo < g.alib_n) ? g.algo : 0;
    }
    if (g.algo < 0 || g.algo >= g.alib_n) {
        g.algo = g.alib_sel;
    }
}

/* Algo index for that cube bit. A negative bit inherits the cube algo, then 0 if
 * that is outside the library. Out of range returns the live algo index. */
int made_eff_algo(int cube, int q)
{
    int id;
    if (cube < 0 || cube >= g.made_n || q < 0 || q > 7) {
        return g.algo;
    }
    id = g.made[cube].algo[q];
    if (id < 0) {
        id = g.made[cube].cube_algo;
    }
    if (id < 0 || id >= g.alib_n) {
        return 0;
    }
    return id;
}

/* Source text for library index i, or the built-in text if that slot is empty.
 * An index outside the library, including -1, returns the compare default. */
const char *alib_src(int i)
{
    if (i >= 0 && i < g.alib_n && g.alib[i].src[0]) {
        return g.alib[i].src;
    }
    if (i >= 0 && i < NP_ALGO_N) {
        return np_algo_def_src(i);
    }
    return np_algo_def_src(NP_ALGO_COMPARE);
}

/* Last 2 seconds of each active channel, filtered, into the algo bank. A channel
 * with no samples is left empty. */
void fill_algo_bank(struct np_algo_bank *b)
{
    int c;
    uint32_t want = (uint32_t)(2.f * design_sps());
    np_algo_bank_clear(b);
    if (want < 32) {
        want = 32;
    }
    if (want > NP_RING) {
        want = NP_RING;
    }
    for (c = 0; c < NP_NCHAN; c++) {
        float buf[NP_RING], dc = 0, rms = 0, pk = 0, raw = 0, rr = 0;
        uint32_t n;
        int det = 0;
        if (!g.active[c]) {
            continue;
        }
        n = np_ring_copy(&g.ring, c, buf, want);
        if (n < 1) {
            continue;
        }
        ch_stats(buf, n, &dc, &rms, &pk);
        raw = rms;
        apply_filt(c, buf, n);
        ch_stats(buf, n, &dc, &rms, &pk);
        det = np_detect(raw > 1.f ? raw : rms, rms, g.cal.have ? g.cal.rms[c] : 0.f,
                        g.calm.have ? g.calm.rms[c] : 0.f, &rr);
        np_algo_bank_set_ex(b, c, buf, (int)n, det == NP_DET_SIGNAL);
    }
}

/* Copies bits the algo wrote. If it did not write self but asked for self_bit,
 * that channel is forced on. A null result returns immediately. */
void apply_algo_out(uint8_t bits[NP_NCHAN], const struct np_algo_out *o,
                           int self)
{
    int c;
    if (!o) {
        return;
    }
    for (c = 0; c < NP_NCHAN; c++) {
        if (o->wrote[c]) {
            bits[c] = o->bit[c] ? 1 : 0;
        }
    }
    if (self >= 0 && self < NP_NCHAN && !o->wrote[self] && o->self_bit) {
        bits[self] = 1;
    }
}

/* Stamps the algo clock from the monotonic clock, in milliseconds. A failed
 * clock read leaves the stamp alone. */
void algo_now(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        np_algo_set_now((uint64_t)ts.tv_sec * 1000ull +
                        (uint64_t)ts.tv_nsec / 1000000ull);
    }
}

/* Runs the live algo, or each made-cube bit, and returns the eight channel bits
 * in one byte. bits is cleared first. Bit 0 is channel 1. */
uint8_t learn_fold_byte(uint8_t bits[NP_NCHAN])
{
    struct np_algo_bank bank;
    struct np_algo_out o;
    int c, ci, q, ch;
    uint8_t fold = 0;
    memset(bits, 0, NP_NCHAN);
    algo_now();
    fill_algo_bank(&bank);
    if (g.made_n < 1) {
        for (c = 0; c < NP_NCHAN; c++) {
            if (!g.active[c]) {
                continue;
            }
            bank.self = c;
            np_algo_custom_out(alib_src(g.algo), &bank, &o);
            apply_algo_out(bits, &o, c);
        }
    }
    for (ci = 0; ci < g.made_n && ci < 4; ci++) {
        for (q = 0; q < 8; q++) {
            ch = g.made[ci].ch[q];
            if (ch < 1 || ch > NP_NCHAN) {
                continue;
            }
            bank.self = ch - 1;
            np_algo_custom_out(alib_src(made_eff_algo(ci, q)), &bank, &o);
            apply_algo_out(bits, &o, ch - 1);
        }
    }
    fold = 0;
    for (c = 0; c < NP_NCHAN; c++) {
        if (bits[c]) {
            fold |= (uint8_t)(1u << c);
        }
    }
    return fold;
}

/* Paints EEG cells on the live cube from channel bits, and packs 64 bytes. On
 * NEG RAIL a hot channel also lights its minus site. Clears other EEG cells
 * first. */
static void learn_write_cube(const uint8_t bits[NP_NCHAN], uint8_t cube[64])
{
    int c, ix, iy, iz;
    np_cube_clear_kind(&g.smx, NP_CELL_EEG);
    for (c = 0; c < NP_NCHAN; c++) {
        if (g.elec[c].site >= 0) {
            np_1010_ijk(g.elec[c].site, &ix, &iy, &iz);
            np_cube_set(&g.smx, ix, iy, iz, bits[c] ? 1 : 0, NP_CELL_EEG);
        }
        if (bits[c] && g.neg_rail && neg_site_ok(g.neg_site[c])) {
            np_1010_ijk(g.neg_site[c], &ix, &iy, &iz);
            np_cube_set(&g.smx, ix, iy, iz, 1, NP_CELL_EEG);
        }
    }
    np_cube_pack_bin(&g.smx, cube);
}

/* 1 if this channel's plus or minus name is a blink end. With no site assigned,
 * only channels 0 and 1 count. */
static int site_is_fp(int ch)
{
    const char *plus, *minus;
    if (ch < 0 || ch >= NP_NCHAN) {
        return 0;
    }
    plus = (g.elec[ch].site >= 0) ? np_1010_name(g.elec[ch].site) : "";
    if (g.neg_rail && neg_site_ok(g.neg_site[ch])) {
        minus = np_1010_name(g.neg_site[ch]);
        return np_blink_end(plus, minus);
    }
    if (g.elec[ch].site < 0) {
        return ch == 0 || ch == 1;
    }
    return np_blink_end(plus, NULL);
}

/* Last ~0.5 s vs a rolling quiet floor. A shared floor above about 0.8 mV,
 * with the channels nearly equal, is not a pose.
 * NEG RAIL off: that floor turns CAR on, sets a 2 Hz high-pass if none is set,
 * turns detrend on, and saves the ini. NEG RAIL only notes that bias is off.
 * ratio may be NULL. A cold stream returns NP_ID_NONE. */
int stream_id(float *ratio)
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
    if (g.connected && stream_cold()) {
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
    /* Lockstep millivolt floor. Referential: turn CAR on.
     * NEG RAIL already subtracted in the amp — a shared floor means no bias. */
    if (g.neg_rail && nlive >= 4 && mx > 800.f && med > 400.f && mx < med * 1.25f) {
        static int noted;
        if (!noted) {
            noted = 1;
            set_status(0, "shared floor — NEG RAIL, bias is off");
        }
    } else if (!g.neg_rail && !g.car && nlive >= 4 && mx > 800.f && med > 400.f &&
               mx < med * 1.25f) {
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

/* Short ID text. A LAN follow name wins and skips the local classifier. A cold
 * stream says it is warming, in measured SPS. */
void id_label(char *out, int n)
{
    float r = 0.f;
    int id;
    if (g.link && g.link_id[0]) {
        snprintf(out, (size_t)n, "%s", g.link_id);
        return;
    }
    id = stream_id(&r);
    if (g.connected && stream_cold()) {
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

/* One second around the gesture, not the plot window. 0 when a channel
 * was prepared. -3 if any active channel clipped, and then no file is
 * written. -1 when no channel is on, -2 when none prepared. Writes the
 * raw file when g.namebuf is set. */
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

/* While MATCH is on, scores the last second against saved poses about ten times
 * a second. A cold stream, MATCH off, or a failed capture clears the winner and
 * returns. */
void learn_tick(void)
{
    static uint32_t last;
    float wave[NPL_NCHAN][NPL_LEN], rms[NPL_NCHAN];
    uint8_t mask;
    uint32_t now = SDL_GetTicks();
    learn_hold_tick();
    if (g.connected && stream_cold()) {
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

/* Saves the typed name as a MATCH pose from the last second, plus cube and SMX
 * rows, and writes exg-c.learn. Refuses a clip, an empty name, or no samples,
 * and also writes the raw file when the name is set. */
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

/* Arms a 4 s pose record under the typed name. No name opens the keyboard and
 * returns. No board returns without arming. */
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

/* Ends the armed record on a blink, clench, or burst after 280 ms, or when 4 s
 * elapse. A cold stream or a clip at the deadline cancels without saving. */
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
    if (g.connected && stream_cold()) {
        if (dt >= (int)REC_MS) {
            g.rec_t0 = 0;
            set_status(0, "still enabling — wait for the stream, then Record");
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

/* Rereads *.npat names into the take list, sorted. A missing directory returns 0. */
int atom_rescan(void)
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

/* Closeness of the two picked take files. Empty slots or a bad path leave it at
 * 0. A montage mismatch is negative; identical RMS is 1. */
void atom_pair_score(void)
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

/* While MATCH is on, scores the last second against each take and keeps one
 * winner at 0.70 with a 0.08 gap. A take whose montage flag differs from NEG
 * RAIL is skipped. No clear winner clears every score. */
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
                int mont = np_atom_montage(path);
                if (mont < 0 || mont == (g.neg_rail ? 1 : 0)) {
                    nbase = tn;
                }
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
            {
                int mont = np_atom_montage(path);
                if (mont >= 0 && mont != (g.neg_rail ? 1 : 0)) {
                    continue;
                }
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

/* Record names and the ID line. */

/* Copies the MATCH or take name into the edit buffer. Does not save. A null
 * pointer clears it. */
void np_host_set_name(const char *s)
{
    snprintf(g.namebuf, sizeof(g.namebuf), "%s", s ? s : "");
}

/* Current MATCH or take name, which may be empty. */
void np_host_get_name(char *out, int n)
{
    snprintf(out, (size_t)n, "%s", g.namebuf);
}

/* Starts a 4 s pose record, or cancels one that is already running. Does not
 * write a take file. */
void np_host_record(void)
{
    if (g.rec_t0) {
        g.rec_t0 = 0;
        set_status(1, "record cancelled");
    } else {
        learn_start_hold();
    }
}

/* Turns MATCH on or off. Off clears the pose winner and the take winner. On does
 * not record by itself. */
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

/* 1 while MATCH is naming poses and takes. */
int np_host_match(void)
{
    return g.learn.match;
}

/* How many MATCH poses are stored. Not the take count. */
int np_host_learn_n(void)
{
    return g.learn.n;
}

/* Index of the winning pose, or -1 when MATCH is off or nothing won. */
int np_host_learn_best(void)
{
    return g.learn.best;
}

/* Pose name at i. Out of range writes an empty string. */
void np_host_learn_name(int i, char *out, int n)
{
    if (i < 0 || i >= g.learn.n) {
        out[0] = 0;
        return;
    }
    snprintf(out, (size_t)n, "%s", g.learn.s[i].name);
}

/* Wave-and-RMS blend for pose i. 0 if i is out of range, or if no pose won by a
 * clear margin. Not a percent. */
float np_host_learn_score(int i)
{
    if (i < 0 || i >= g.learn.n) {
        return 0.f;
    }
    return g.learn.score[i];
}

/* Jaccard of occupied cube cells, 0..1. 0 if i is out of range or that pose has
 * no cube. Does not replace the wave score. */
float np_host_learn_score_cube(int i)
{
    if (i < 0 || i >= g.learn.n) {
        return 0.f;
    }
    return g.learn.score_cube[i];
}

/* Index of the pose selected in the list, or -1 if none. */
int np_host_learn_sel(void)
{
    return g.learn.sel;
}

/* Selects pose i and copies its name into the edit buffer. Out of range does nothing. */
void np_host_learn_select(int i)
{
    if (i < 0 || i >= g.learn.n) {
        return;
    }
    g.learn.sel = i;
    snprintf(g.namebuf, sizeof(g.namebuf), "%s", g.learn.s[i].name);
}

/* Deletes pose i, its raw file, and rewrites exg-c.learn. Out of range does
 * nothing. Does not delete a take. */
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

/* ID line for the UI. Before the host is ready, writes "ID —". A short or null
 * buffer returns without writing. */
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

/* Milliseconds left in the armed 4 s record. 0 if nothing is armed or the window
 * has already elapsed. */
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
