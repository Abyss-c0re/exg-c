#include "np_local.h"
#include "np_mods.h"

/* Cube view, algos, and made cubes. */

/* Copies the 512 occupancy bytes and lights a cell when cooked µV reaches half
 * that channel's scale, including the minus site while NEG RAIL is on. A null
 * pointer returns, and the live cube is not changed. */
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
        if (g.neg_rail && neg_site_ok(g.neg_site[c])) {
            np_1010_ijk(g.neg_site[c], &ix, &iy, &iz);
            idx = np_cube_idx(ix, iy, iz);
            if (idx >= 0) {
                dst[idx] = 1;
            }
        }
    }
}

/* Library index of the live algo, after seeding. Out of range reads as 0. */
int np_host_algo(void)
{
    alib_seed();
    if (g.algo < 0 || g.algo >= g.alib_n) {
        return 0;
    }
    return g.algo;
}

/* Selects the next library entry and saves. Returns without a change if the
 * library is empty. */
void np_host_cycle_algo(void)
{
    alib_seed();
    if (g.alib_n < 1) {
        return;
    }
    g.algo = (g.algo + 1) % g.alib_n;
    g.alib_sel = g.algo;
    cfg_save();
    set_status(1, "algo %s", g.alib[g.algo].name);
}

/* Selects that library index, makes it the library selection, and saves. An
 * out-of-range id becomes 0. */
void np_host_set_algo(int id)
{
    alib_seed();
    if (id < 0 || id >= g.alib_n) {
        id = 0;
    }
    g.algo = id;
    g.alib_sel = id;
    cfg_save();
    set_status(1, "algo %s", g.alib[g.algo].name);
}

/* Name of the live algo. Falls back to the built-in name if the slot is empty. */
void np_host_algo_name(char *out, int n)
{
    alib_seed();
    if (g.algo >= 0 && g.algo < g.alib_n && g.alib[g.algo].name[0]) {
        snprintf(out, (size_t)n, "%s", g.alib[g.algo].name);
    } else {
        snprintf(out, (size_t)n, "%s", np_algo_name(np_host_algo()));
    }
}

/* One-line rule for the selected cube bit when any cube exists, otherwise the
 * library selection. A stock source uses its built-in sentence; a -1 inherit
 * does not climb to the cube algo. A null out returns. */
void np_host_algo_rule(char *out, int n)
{
    int id;
    if (!out || n < 1) {
        return;
    }
    alib_seed();
    id = g.alib_sel;
    if (g.made_n > 0) {
        id = g.made[g.made_sel].algo[np_host_made_qsel()];
    }
    if (id >= 0 && id < NP_ALGO_N &&
        strcmp(alib_src(id), np_algo_def_src(id)) == 0) {
        snprintf(out, (size_t)n, "%s", np_algo_rule(id));
        return;
    }
    algo_first_line(alib_src(id), out, n);
}

/* Full source of the library selection, not of a made-cube bit. */
void np_host_algo_src(char *out, int n)
{
    alib_seed();
    snprintf(out, (size_t)n, "%s", alib_src(g.alib_sel));
}

/* Compiles and stores source on the library selection, then saves. -1 if that
 * index is outside the library or the source does not compile. */
int np_host_set_algo_src(const char *s, char *err, int n)
{
    return np_host_alib_set_src(g.alib_sel, s, err, n);
}

/* Channel bitmask from the live fold. 0 if the board is not connected. Bit 0 is channel 1. */
unsigned int np_host_algo_fold(void)
{
    uint8_t bits[NP_NCHAN];
    unsigned int fold = 0;
    int c;
    if (!g.connected) {
        return 0;
    }
    learn_fold_byte(bits);
    for (c = 0; c < NP_NCHAN; c++) {
        if (bits[c]) {
            fold |= 1u << c;
        }
    }
    return fold;
}

/* Selected cube bit, 0..7. Out of range reads as 0. */
int np_host_made_qsel(void)
{
    if (g.made_qsel < 0 || g.made_qsel > 7) {
        return 0;
    }
    return g.made_qsel;
}

/* Selects bit q. Outside 0..7 does nothing. Does not save. */
void np_host_made_set_qsel(int q)
{
    if (q < 0 || q > 7) {
        return;
    }
    g.made_qsel = q;
}

/* Source for that bit. A bit of -1 does not inherit the cube algo; it gets the
 * compare default. */
void np_host_made_src(int cube, int q, char *out, int n)
{
    snprintf(out, (size_t)n, "%s", made_src_or_default(cube, q));
}

/* Compiles source onto the bit's own algo index. -1 if the cube or bit is out of
 * range, or the bit inherits (-1) and has no slot. Does not copy the source onto
 * a private algo. */
int np_host_set_made_src(int cube, int q, const char *s, char *err, int n)
{
    int id;
    if (cube < 0 || cube >= g.made_n || q < 0 || q > 7) {
        if (err && n > 0) {
            snprintf(err, (size_t)n, "no cube bit");
        }
        return -1;
    }
    id = g.made[cube].algo[q];
    return np_host_alib_set_src(id, s, err, n);
}

/* Effective algo index: the bit, else the cube algo, else 0. Out of range
 * returns the live algo. */
int np_host_made_algo(int cube, int q)
{
    return made_eff_algo(cube, q);
}

/* The bit's own algo index, which may be -1 for inherit. -1 if the cube or bit
 * is out of range. */
int np_host_made_algo_own(int cube, int q)
{
    if (cube < 0 || cube >= g.made_n || q < 0 || q > 7) {
        return -1;
    }
    return g.made[cube].algo[q];
}

/* Sets the bit's algo and saves. -1 means inherit the cube. Returns -1 if the
 * cube, the bit, or id (other than -1) is out of range. */
int np_host_made_set_algo(int cube, int q, int id)
{
    alib_seed();
    if (cube < 0 || cube >= g.made_n || q < 0 || q > 7) {
        return -1;
    }
    if (id < -1 || id >= g.alib_n) {
        return -1;
    }
    g.made[cube].algo[q] = id;
    g.made_qsel = q;
    cfg_save();
    if (id < 0) {
        set_status(1, "bit %d  same as cube", q + 1);
    } else {
        set_status(1, "bit %d  %s", q + 1, g.alib[id].name);
    }
    return 0;
}

/* The cube algo if every bit inherits it or matches it. -1 if any bit has its
 * own algo, or the cube index is bad. */
int np_host_made_algo_all(int cube)
{
    int q;
    if (cube < 0 || cube >= g.made_n) {
        return -1;
    }
    for (q = 0; q < 8; q++) {
        if (g.made[cube].algo[q] >= 0 &&
            g.made[cube].algo[q] != g.made[cube].cube_algo) {
            return -1;
        }
    }
    return g.made[cube].cube_algo;
}

/* Sets the cube algo, clears every bit to inherit, selects it as the live algo,
 * and saves. id must be a real library index, not -1. */
int np_host_made_set_algo_all(int cube, int id)
{
    int q;
    alib_seed();
    if (cube < 0 || cube >= g.made_n) {
        return -1;
    }
    if (id < 0 || id >= g.alib_n) {
        return -1;
    }
    g.made[cube].cube_algo = id;
    for (q = 0; q < 8; q++) {
        g.made[cube].algo[q] = -1;
    }
    g.algo = id;
    g.alib_sel = id;
    cfg_save();
    set_status(1, "cube  %s", g.alib[id].name);
    return 0;
}

/* How many algos are in the library, after the built-in eight are seeded. */
int np_host_alib_n(void)
{
    alib_seed();
    return g.alib_n;
}

/* Selected library index, after seeding. */
int np_host_alib_sel(void)
{
    alib_seed();
    return g.alib_sel;
}

/* Selects i and also makes it the live algo. Out of range does nothing and does not save. */
void np_host_alib_set_sel(int i)
{
    alib_seed();
    if (i >= 0 && i < g.alib_n) {
        g.alib_sel = i;
        g.algo = i;
    }
}

/* Name at i. A null out returns. Out of range writes an empty string. */
void np_host_alib_name(int i, char *out, int n)
{
    alib_seed();
    if (!out || n < 1) {
        return;
    }
    if (i < 0 || i >= g.alib_n) {
        out[0] = 0;
        return;
    }
    snprintf(out, (size_t)n, "%s", g.alib[i].name);
}

/* Source at i. An index outside the library returns the compare default. */
void np_host_alib_src(int i, char *out, int n)
{
    alib_seed();
    snprintf(out, (size_t)n, "%s", alib_src(i));
}

/* 1 if i is a built-in slot, 0..7. Those cannot be renamed or deleted. */
int np_host_alib_def(int i)
{
    return i >= 0 && i < NP_ALIB_DEF;
}

/* 0 if the source compiles, or -1 and an error string if it does not. Does not
 * save, and a null source is checked as empty. */
int np_host_alib_check(const char *s, char *err, int n)
{
    char e[80];
    if (!s) {
        s = "";
    }
    if (np_algo_compile(s, e, (int)sizeof(e)) != 0) {
        if (err && n > 0) {
            snprintf(err, (size_t)n, "%s", e);
        }
        return -1;
    }
    if (err && n > 0) {
        err[0] = 0;
    }
    return 0;
}

/* Compiles, stores, and writes the ini. -1 if i is outside the library or the
 * source does not compile, and then the slot is unchanged. */
int np_host_alib_set_src(int i, const char *s, char *err, int n)
{
    char e[80];
    alib_seed();
    if (i < 0 || i >= g.alib_n) {
        if (err && n > 0) {
            snprintf(err, (size_t)n, "no algo");
        }
        return -1;
    }
    if (!s) {
        s = "";
    }
    if (np_host_alib_check(s, e, (int)sizeof(e)) != 0) {
        if (err && n > 0) {
            snprintf(err, (size_t)n, "%s", e);
        }
        return -1;
    }
    snprintf(g.alib[i].src, sizeof(g.alib[i].src), "%s", s);
    g.alib_sel = i;
    cfg_save();
    set_status(1, "algo %s", g.alib[i].name);
    if (err && n > 0) {
        err[0] = 0;
    }
    return 0;
}

/* Renames a user algo and saves. A built-in index, an empty name, or a bad index
 * returns -1. Characters other than letters, digits, '_' and '-' become '_'. */
int np_host_alib_set_name(int i, const char *s)
{
    int k;
    char name[NP_ALIB_NAME];
    alib_seed();
    if (i < 0 || i >= g.alib_n || i < NP_ALIB_DEF) {
        return -1;
    }
    if (!s || !s[0]) {
        return -1;
    }
    snprintf(name, sizeof(name), "%s", s);
    for (k = 0; name[k]; k++) {
        unsigned char c = (unsigned char)name[k];
        if (!(isalnum(c) || c == '_' || c == '-')) {
            name[k] = '_';
        }
    }
    snprintf(g.alib[i].name, sizeof(g.alib[i].name), "%s", name);
    cfg_save();
    return 0;
}

/* Appends userN with the default source and saves. Returns the new index, or -1
 * if the list is already full. */
int np_host_alib_add(void)
{
    int i;
    alib_seed();
    if (g.alib_n >= NP_ALIB_N) {
        set_status(0, "algo list full");
        return -1;
    }
    i = g.alib_n;
    memset(&g.alib[i], 0, sizeof(g.alib[i]));
    snprintf(g.alib[i].name, sizeof(g.alib[i].name), "user%d", i + 1);
    snprintf(g.alib[i].src, sizeof(g.alib[i].src), "%s", NP_ALGO_SRC_DEFAULT);
    g.alib_n++;
    g.alib_sel = i;
    cfg_save();
    set_status(1, "algo %s", g.alib[i].name);
    return i;
}

/* Removes a user algo and shifts later indexes down, including cube-bit
 * references, then saves. -1 for a built-in or a bad index, and then nothing moves. */
int np_host_alib_del(int i)
{
    int k, ci, q;
    alib_seed();
    if (i < NP_ALIB_DEF || i >= g.alib_n) {
        set_status(0, "cannot delete a default");
        return -1;
    }
    for (k = i; k < g.alib_n - 1; k++) {
        g.alib[k] = g.alib[k + 1];
    }
    g.alib_n--;
    memset(&g.alib[g.alib_n], 0, sizeof(g.alib[0]));
    if (g.alib_sel >= g.alib_n) {
        g.alib_sel = g.alib_n - 1;
    }
    if (g.algo == i) {
        g.algo = 0;
    } else if (g.algo > i) {
        g.algo--;
    }
    for (ci = 0; ci < g.made_n && ci < 4; ci++) {
        for (q = 0; q < 8; q++) {
            if (g.made[ci].algo[q] == i) {
                g.made[ci].algo[q] = 0;
            } else if (g.made[ci].algo[q] > i) {
                g.made[ci].algo[q]--;
            }
        }
    }
    cfg_save();
    set_status(1, "algo removed");
    return 0;
}

/* Restores a built-in name and source, and saves. -1 if i is not a built-in. */
int np_host_alib_reset(int i)
{
    alib_seed();
    if (i < 0 || i >= NP_ALIB_DEF) {
        return -1;
    }
    snprintf(g.alib[i].name, sizeof(g.alib[i].name), "%s", np_algo_name(i));
    snprintf(g.alib[i].src, sizeof(g.alib[i].src), "%s", np_algo_def_src(i));
    g.alib_sel = i;
    cfg_save();
    set_status(1, "algo %s reset", g.alib[i].name);
    return 0;
}

/* Bitmask of the eight cube bits the algos turned on. 0 if the cube index is bad
 * or the board is not connected. Bit 0 is cube bit 1, not channel 1. */
unsigned int np_host_made_fold(int cube)
{
    unsigned int fold = 0;
    int q, ch;
    if (cube < 0 || cube >= g.made_n) {
        return 0;
    }
    if (g.connected) {
        struct np_algo_bank bank;
        uint8_t chbits[NP_NCHAN];
        algo_now();
        fill_algo_bank(&bank);
        memset(chbits, 0, sizeof(chbits));
        for (q = 0; q < 8; q++) {
            struct np_algo_out o;
            ch = g.made[cube].ch[q];
            if (ch < 1 || ch > 8) {
                continue;
            }
            bank.self = ch - 1;
            np_algo_custom_out(alib_src(made_eff_algo(cube, q)), &bank, &o);
            apply_algo_out(chbits, &o, ch - 1);
        }
        for (q = 0; q < 8; q++) {
            ch = g.made[cube].ch[q];
            if (ch >= 1 && ch <= 8 && chbits[ch - 1]) {
                fold |= 1u << q;
            }
        }
    }
    return fold;
}

/* 1 for the 10-10 assign map, 0 for the crimson viz. */
int np_host_cube_view(void)
{
    return g.cube_view ? 1 : 0;
}

/* Nonzero selects the assign map, 0 the viz. Saves. */
void np_host_set_cube_view(int map)
{
    g.cube_view = map ? 1 : 0;
    cfg_save();
    set_status(1, g.cube_view ? "map  assign 10-10 sites" : "viz  crimson cube");
}

/* 1 if the cube window is floating. */
int np_host_cube_float(void)
{
    return g.cube_float ? 1 : 0;
}

/* Flips the floating cube window and saves. */
void np_host_toggle_cube_float(void)
{
    g.cube_float = !g.cube_float;
    cfg_save();
    set_status(1, g.cube_float ? "float on" : "float off");
}

/* Adds yaw and pitch in radians. Pitch is clamped to -0.35..1.20. Does not save. */
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

/* Steps zoom by 0.2 in the sign of dir, clamps to 0.70..2.80, and saves. */
void np_host_cube_zoom(int dir)
{
    cube_zoom_by(dir);
    cfg_save();
}

/* Zoom factor after clamp, from 0.70 to 2.80. */
float np_host_cube_zoom_get(void)
{
    cube_zoom_clamp();
    return g.cube_zoom;
}

/* Sets zoom, clamps it to 0.70..2.80, and saves. */
void np_host_set_cube_zoom(float z)
{
    g.cube_zoom = z;
    cube_zoom_clamp();
    cfg_save();
}

/* Yaw π, pitch 0.25, zoom 1, so +z and Fp face the camera. Saves. Not the startup pose. */
void np_host_cube_front(void)
{
    /* +z / Fp toward the camera. Not the 0.55/0.40 start pose. */
    g.cube_yaw = (float)M_PI;
    g.cube_pitch = 0.25f;
    g.cube_zoom = 1.0f;
    cfg_save();
    set_status(1, "front");
}

/* How many cubes this board can hold: channel count divided by 8. */
int np_host_made_max(void)
{
    return NP_NCHAN / 8;
}

/* How many cubes exist now, 0..4. */
int np_host_made_n(void)
{
    return g.made_n;
}

/* Selected cube index. May read 0 when none exist. */
int np_host_made_sel(void)
{
    return g.made_sel;
}

/* Selects cube i if it exists. Out of range does nothing. Does not save. */
void np_host_made_set_sel(int i)
{
    if (i >= 0 && i < g.made_n) {
        g.made_sel = i;
    }
}

/* Appends a cube on free channels, bits inheriting the live algo, and saves.
 * Returns the index, or -1 if already at the max, also capped at 4. */
int np_host_made_add(void)
{
    int i;
    if (g.made_n >= np_host_made_max() || g.made_n >= 4) {
        set_status(0, "board is full — %d cube%s max", np_host_made_max(),
                   np_host_made_max() == 1 ? "" : "s");
        return -1;
    }
    i = g.made_n;
    memset(&g.made[i], 0, sizeof(g.made[i]));
    g.made[i].rgb[0] = 255;
    g.made[i].rgb[1] = 20;
    g.made[i].rgb[2] = 40;
    g.made[i].cube_algo = g.algo >= 0 ? g.algo : 0;
    {
        int q, ch;
        for (q = 0; q < 8; q++) {
            for (ch = 1; ch <= NP_NCHAN; ch++) {
                if (!made_ch_used(ch, i)) {
                    g.made[i].ch[q] = ch;
                    break;
                }
            }
            g.made[i].algo[q] = -1;
        }
    }
    g.made_n++;
    g.made_sel = i;
    cfg_save();
    set_status(1, "cube %d — 8 bits", i + 1);
    return i;
}

/* Removes cube i, shifts the rest down, and saves. -1 if i is out of range. */
int np_host_made_del(int i)
{
    int k;
    if (i < 0 || i >= g.made_n) {
        return -1;
    }
    for (k = i; k < g.made_n - 1; k++) {
        g.made[k] = g.made[k + 1];
    }
    g.made_n--;
    if (g.made_sel >= g.made_n) {
        g.made_sel = g.made_n - 1;
    }
    if (g.made_sel < 0) {
        g.made_sel = 0;
    }
    cfg_save();
    set_status(1, g.made_n ? "cube removed" : "no cubes");
    return 0;
}

/* Channel number 1..8 on that bit, or 0 if the bit is empty or the index is out of range. */
int np_host_made_ch(int cube, int q)
{
    if (cube < 0 || cube >= g.made_n || q < 0 || q > 7) {
        return 0;
    }
    return g.made[cube].ch[q];
}

/* Assigns channel ch, where 0 clears the bit, and saves. -1 if out of range or
 * that channel is already on another cube. */
int np_host_made_set_ch(int cube, int q, int ch)
{
    int old;
    if (cube < 0 || cube >= g.made_n || q < 0 || q > 7) {
        return -1;
    }
    if (ch < 0 || ch > NP_NCHAN) {
        return -1;
    }
    old = g.made[cube].ch[q];
    if (ch != 0 && ch != old && made_ch_used(ch, cube)) {
        set_status(0, "ch%d already on a cube", ch);
        return -1;
    }
    g.made[cube].ch[q] = ch;
    cfg_save();
    if (ch == 0) {
        set_status(1, "cube %d  bit %d empty", cube + 1, q + 1);
    } else {
        set_status(1, "cube %d  bit %d = ch%d", cube + 1, q + 1, ch);
    }
    return 0;
}

/* Writes 0..255 color. A bad cube index writes the default crimson 255, 20, 40
 * into any non-null pointer. */
void np_host_made_rgb(int cube, int *r, int *gch, int *b)
{
    if (cube < 0 || cube >= g.made_n) {
        if (r) {
            *r = 255;
        }
        if (gch) {
            *gch = 20;
        }
        if (b) {
            *b = 40;
        }
        return;
    }
    if (r) {
        *r = g.made[cube].rgb[0];
    }
    if (gch) {
        *gch = g.made[cube].rgb[1];
    }
    if (b) {
        *b = g.made[cube].rgb[2];
    }
}

/* Stores each channel clamped to 0..255 and saves. A bad cube index returns
 * without writing. */
void np_host_made_set_rgb(int cube, int r, int gch, int b)
{
    if (cube < 0 || cube >= g.made_n) {
        return;
    }
    g.made[cube].rgb[0] = (unsigned char)(r < 0 ? 0 : (r > 255 ? 255 : r));
    g.made[cube].rgb[1] = (unsigned char)(gch < 0 ? 0 : (gch > 255 ? 255 : gch));
    g.made[cube].rgb[2] = (unsigned char)(b < 0 ? 0 : (b > 255 ? 255 : b));
    cfg_save();
}

/* SMX sequence counter. Not a channel fold. */
unsigned int np_host_smx_seq(void)
{
    return g.smx.seq;
}

/* Latest SMX row as a channel bitmask. 0 if no row has been stored. Bit 0 is channel 1. */
unsigned int np_host_smx_fold(void)
{
    return np_smx_fold_ch(&g.smx);
}
