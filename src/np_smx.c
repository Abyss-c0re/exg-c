#include "np_cube.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* One bit per channel per second, an 8³ cube, and 10-10 sites.
 * EEG sits on the shell. Named IMU cells stay interior. */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Zero the record, then claim interior cells for acc, gyr, and mag on X Y Z. A null pointer returns. */
void np_smx_init(struct np_smx *m)
{
    if (!m) {
        return;
    }
    memset(m, 0, sizeof(*m));
    /* Interior IMU slots — not on the EEG shell. */
    np_virt_claim(m, "accX", 3, 3, 3);
    np_virt_claim(m, "accY", 4, 3, 3);
    np_virt_claim(m, "accZ", 5, 3, 3);
    np_virt_claim(m, "gyrX", 3, 4, 3);
    np_virt_claim(m, "gyrY", 4, 4, 3);
    np_virt_claim(m, "gyrZ", 5, 4, 3);
    np_virt_claim(m, "magX", 3, 5, 3);
    np_virt_claim(m, "magY", 4, 5, 3);
    np_virt_claim(m, "magZ", 5, 5, 3);
}

/* Index x + 8*y + 64*z. A coordinate outside 0..7 returns -1. */
int np_cube_idx(int x, int y, int z)
{
    if (x < 0 || x > 7 || y < 0 || y > 7 || z < 0 || z > 7) {
        return -1;
    }
    return x + NP_CUBE3 * y + NP_CUBE3 * NP_CUBE3 * z;
}

/* Split an index into x, y, z. An index outside 0..511 is clamped. Any out pointer may be null. */
void np_cube_unidx(int i, int *x, int *y, int *z)
{
    if (i < 0) {
        i = 0;
    }
    if (i >= NP_CUBE3_N) {
        i = NP_CUBE3_N - 1;
    }
    if (x) {
        *x = i % NP_CUBE3;
    }
    if (y) {
        *y = (i / NP_CUBE3) % NP_CUBE3;
    }
    if (z) {
        *z = i / (NP_CUBE3 * NP_CUBE3);
    }
}

/* 1 when x, y, or z is 0 or 7. */
int np_cube_shell(int x, int y, int z)
{
    return x == 0 || x == 7 || y == 0 || y == 7 || z == 0 || z == 7;
}

/* 1 when that cell is on. A null record or a bad index returns 0. */
int np_cube_get(const struct np_smx *m, int x, int y, int z)
{
    int i = np_cube_idx(x, y, z);
    if (!m || i < 0) {
        return 0;
    }
    return m->cube[i] ? 1 : 0;
}

/* Store on as 0 or 1. kind 0 leaves the old kind tag. A null record or a bad index returns. */
void np_cube_set(struct np_smx *m, int x, int y, int z, int on, int kind)
{
    int i = np_cube_idx(x, y, z);
    if (!m || i < 0) {
        return;
    }
    m->cube[i] = on ? 1 : 0;
    if (kind) {
        m->kind[i] = (uint8_t)kind;
    }
}

/* Turn off every cell whose kind tag matches. The tags themselves stay. A null record returns. */
void np_cube_clear_kind(struct np_smx *m, int kind)
{
    int i;
    if (!m) {
        return;
    }
    for (i = 0; i < NP_CUBE3_N; i++) {
        if (m->kind[i] == (uint8_t)kind) {
            m->cube[i] = 0;
        }
    }
}

/* 512 characters, index order, '1' or '0', NUL terminated. cap below 2, or a null pointer, returns 0. */
int np_cube_pack(const struct np_smx *m, char *out, int cap)
{
    int i, n = 0;
    if (!m || !out || cap < 2) {
        return 0;
    }
    for (i = 0; i < NP_CUBE3_N && n < cap - 1; i++) {
        out[n++] = m->cube[i] ? '1' : '0';
    }
    out[n] = 0;
    return n;
}

/* 64 bytes, bit i in byte i>>3 at bit (i&7). A null pointer returns 0. Success returns 64. */
int np_cube_pack_bin(const struct np_smx *m, uint8_t out[64])
{
    int i;
    if (!m || !out) {
        return 0;
    }
    memset(out, 0, 64);
    for (i = 0; i < NP_CUBE3_N; i++) {
        if (m->cube[i]) {
            out[i >> 3] |= (uint8_t)(1u << (i & 7));
        }
    }
    return 64;
}

/* Bit distance of two 64-byte masks. A null array returns 512. */
int np_cube_hamming(const uint8_t a[64], const uint8_t b[64])
{
    int i, d = 0;
    if (!a || !b) {
        return 512;
    }
    for (i = 0; i < 64; i++) {
        unsigned x = (unsigned)(a[i] ^ b[i]);
        while (x) {
            d += (int)(x & 1u);
            x >>= 1;
        }
    }
    return d;
}

/* Map a cell center into [-1, 1] on each axis. Any out pointer may be null. */
void np_ijk_world(int x, int y, int z, float *wx, float *wy, float *wz)
{
    if (wx) {
        *wx = ((float)x - 3.5f) / 3.5f;
    }
    if (wy) {
        *wy = ((float)y - 3.5f) / 3.5f;
    }
    if (wz) {
        *wz = ((float)z - 3.5f) / 3.5f;
    }
}

/* Map -1..1 onto 0..7 and clamp. +1 lands on 7. */
static int u_to_i(float u)
{
    int i = (int)((u + 1.f) * 4.f);
    if (i < 0) {
        i = 0;
    }
    if (i > 7) {
        i = 7;
    }
    return i;
}

/* Shell cell for a 10-10 site, dominant axis on the face. A bad site writes 3, 7, 3 and returns -1. */
int np_1010_ijk(int site, int *x, int *y, int *z)
{
    struct np_elec e;
    float px, py, pz, ax, ay, az, d;
    int ix = 3, iy = 3, iz = 3;
    if (site < 0 || site >= NP_1010_N) {
        if (x) {
            *x = 3;
        }
        if (y) {
            *y = 7;
        }
        if (z) {
            *z = 3;
        }
        return -1;
    }
    np_1010_elaz(site, &e.az, &e.el);
    np_elec_to_xyz(&e, 1.f, &px, &py, &pz);
    ax = fabsf(px);
    ay = fabsf(py);
    az = fabsf(pz);
    if (ay >= ax && ay >= az) {
        d = ay > 1e-6f ? ay : 1.f;
        ix = u_to_i(px / d);
        iz = u_to_i(pz / d);
        iy = py >= 0.f ? 7 : 0;
    } else if (az >= ax) {
        d = az > 1e-6f ? az : 1.f;
        ix = u_to_i(px / d);
        iy = u_to_i(py / d);
        iz = pz >= 0.f ? 7 : 0;
    } else {
        d = ax > 1e-6f ? ax : 1.f;
        iz = u_to_i(pz / d);
        iy = u_to_i(py / d);
        ix = px >= 0.f ? 7 : 0;
    }
    if (x) {
        *x = ix;
    }
    if (y) {
        *y = iy;
    }
    if (z) {
        *z = iz;
    }
    return 0;
}

/* Count sites that share that cell. out receives at most cap of them. A null out still counts. */
int np_1010_sites_at(int x, int y, int z, int out[], int cap)
{
    int i, n = 0, sx, sy, sz;
    for (i = 0; i < NP_1010_N; i++) {
        if (np_1010_ijk(i, &sx, &sy, &sz) != 0) {
            continue;
        }
        if (sx != x || sy != y || sz != z) {
            continue;
        }
        if (out && n < cap) {
            out[n] = i;
        }
        n++;
    }
    return n;
    return 0;
}

/* Slot of an exact name, or -1. A null record or name returns -1. */
int np_virt_find(const struct np_smx *m, const char *name)
{
    int i;
    if (!m || !name) {
        return -1;
    }
    for (i = 0; i < NP_VIRT_MAX; i++) {
        if (m->virt[i].used && strcmp(m->virt[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Bind a name to an interior cell. A shell cell, an empty name, or a full table returns -1.
 * An existing name is moved. The stored name is cut to 11 characters. */
int np_virt_claim(struct np_smx *m, const char *name, int x, int y, int z)
{
    int i, slot = -1;
    if (!m || !name || !name[0] || np_cube_idx(x, y, z) < 0) {
        return -1;
    }
    if (np_cube_shell(x, y, z)) {
        return -1;
    }
    i = np_virt_find(m, name);
    if (i >= 0) {
        m->virt[i].x = (uint8_t)x;
        m->virt[i].y = (uint8_t)y;
        m->virt[i].z = (uint8_t)z;
        return i;
    }
    for (i = 0; i < NP_VIRT_MAX; i++) {
        if (!m->virt[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return -1;
    }
    snprintf(m->virt[slot].name, NP_VIRT_NAME, "%s", name);
    m->virt[slot].x = (uint8_t)x;
    m->virt[slot].y = (uint8_t)y;
    m->virt[slot].z = (uint8_t)z;
    m->virt[slot].used = 1;
    return slot;
}

/* Turn the named cell on or off and tag it virtual. An unknown name does nothing. */
void np_virt_write(struct np_smx *m, const char *name, int on)
{
    int i;
    if (!m) {
        return;
    }
    i = np_virt_find(m, name);
    if (i < 0) {
        return;
    }
    np_cube_set(m, m->virt[i].x, m->virt[i].y, m->virt[i].z, on, NP_CELL_VIRT);
}

/* 1 when the named cell is on. An unknown name, or a null record, returns 0. */
int np_virt_read(const struct np_smx *m, const char *name)
{
    int i;
    if (!m) {
        return 0;
    }
    i = np_virt_find(m, name);
    if (i < 0) {
        return 0;
    }
    return np_cube_get(m, m->virt[i].x, m->virt[i].y, m->virt[i].z);
}

/* Clear IMU-kind cells, then light acc above 0.25, gyr above 0.15, and mag above 0.20 on the claimed axes.
 * A null axis pointer leaves that trio off. Kind tags of cleared cells stay. */
void np_cube_imu(struct np_smx *m, const float acc[3], const float gyr[3], const float mag[3])
{
    static const char *an[3] = {"accX", "accY", "accZ"};
    static const char *gn[3] = {"gyrX", "gyrY", "gyrZ"};
    static const char *mn[3] = {"magX", "magY", "magZ"};
    int i;
    if (!m) {
        return;
    }
    np_cube_clear_kind(m, NP_CELL_IMU);
    for (i = 0; i < 3; i++) {
        int on = acc && fabsf(acc[i]) > 0.25f;
        int v = np_virt_find(m, an[i]);
        if (v >= 0) {
            np_cube_set(m, m->virt[v].x, m->virt[v].y, m->virt[v].z, on, NP_CELL_IMU);
        }
        on = gyr && fabsf(gyr[i]) > 0.15f;
        v = np_virt_find(m, gn[i]);
        if (v >= 0) {
            np_cube_set(m, m->virt[v].x, m->virt[v].y, m->virt[v].z, on, NP_CELL_IMU);
        }
        on = mag && fabsf(mag[i]) > 0.20f;
        v = np_virt_find(m, mn[i]);
        if (v >= 0) {
            np_cube_set(m, m->virt[v].x, m->virt[v].y, m->virt[v].z, on, NP_CELL_IMU);
        }
    }
}

/* Append one second of 0/1 bits. nch is clamped to 1..8, and columns past nch are cleared. have stops at 32. seq increments.
 * A null record or bits returns. */
void np_smx_push(struct np_smx *m, const uint8_t bits[NP_NCHAN], int nch, uint8_t mask)
{
    int i, w;
    if (!m || !bits) {
        return;
    }
    if (nch < 1) {
        nch = 1;
    }
    if (nch > NP_NCHAN) {
        nch = NP_NCHAN;
    }
    w = (int)(m->wr % NP_SMX_SEC);
    memset(m->bit[w], 0, NP_NCHAN);
    for (i = 0; i < nch; i++) {
        m->bit[w][i] = bits[i] ? 1 : 0;
    }
    m->nch = (uint8_t)nch;
    m->mask = mask;
    m->wr++;
    if (m->have < NP_SMX_SEC) {
        m->have++;
    }
    m->seq++;
}

/* Newest second first, nch characters of '0' or '1' per second, no separator. cap below 2, or a null pointer, returns 0. */
int np_smx_pack(const struct np_smx *m, char *out, int cap)
{
    int t, c, n = 0;
    if (!m || !out || cap < 2) {
        return 0;
    }
    for (t = 0; t < (int)m->have; t++) {
        int row = (int)((m->wr - 1 - (uint32_t)t) % NP_SMX_SEC);
        for (c = 0; c < m->nch && n < cap - 1; c++) {
            out[n++] = m->bit[row][c] ? '1' : '0';
        }
    }
    out[n] = 0;
    return n;
}

/* Mask bits 0..7 become channel ids 1..8, in bit order, until nch ids are filled.
 * When the mask is shorter than nch, the extra ids are the slot index plus 1. */
int np_smx_ch_ids(const struct np_smx *m, int ids[NP_NCHAN])
{
    int c, n = 0;
    if (!m || !ids) {
        return 0;
    }
    for (c = 0; c < NP_NCHAN && n < m->nch; c++) {
        if (m->mask & (uint8_t)(1u << c)) {
            ids[n++] = c + 1;
        }
    }
    while (n < m->nch && n < NP_NCHAN) {
        ids[n] = n + 1;
        n++;
    }
    return n;
}

/* Newest row: column i sets the bit for channel id ids[i]. An empty history returns 0. */
unsigned int np_smx_fold_ch(const struct np_smx *m)
{
    unsigned int f = 0;
    int ids[NP_NCHAN];
    int n, i, row;
    if (!m || m->have < 1) {
        return 0;
    }
    row = (int)((m->wr - 1) % NP_SMX_SEC);
    n = np_smx_ch_ids(m, ids);
    for (i = 0; i < n; i++) {
        int ch = ids[i] - 1;
        if (ch >= 0 && ch < NP_NCHAN && m->bit[row][i]) {
            f |= 1u << ch;
        }
    }
    return f;
}

/* World-space cubes, at most 40. Two core cubes are always attempted, then one cell per channel per second, newest first.
 * z steps back by 0.24 per second. On cells use role 2. A null pointer or cap below 1 returns 0. */
int np_smx_cubes(const struct np_smx *m, struct np_cube *out, int cap)
{
    int n = 0, t, c, nch, nsec;
    if (!m || !out || cap < 1) {
        return 0;
    }
    if (cap > NP_CUBE_BUDGET) {
        cap = NP_CUBE_BUDGET;
    }
    /* Core + gold halo — viz_frame.v1 roles. */
    out[n].x = 0.f;
    out[n].y = 1.35f;
    out[n].z = -2.f;
    out[n].s = 0.55f;
    out[n].r = 255;
    out[n].g = 165;
    out[n].b = 46;
    out[n].a = 120;
    out[n].role = 1;
    n++;
    if (n < cap) {
        out[n].x = 0.f;
        out[n].y = 1.35f;
        out[n].z = -2.f;
        out[n].s = 0.42f;
        out[n].r = NP_CUBE_CR;
        out[n].g = NP_CUBE_CG;
        out[n].b = NP_CUBE_CB;
        out[n].a = 230;
        out[n].role = 1;
        n++;
    }
    nch = m->nch > 0 ? (int)m->nch : 0;
    nsec = (int)m->have;
    if (nch < 1 || nsec < 1) {
        return n;
    }
    if (n + nsec * nch > cap) {
        nsec = (cap - n) / nch;
    }
    if (nsec < 1) {
        return n;
    }
    for (t = 0; t < nsec; t++) {
        int row = (int)((m->wr - 1 - (uint32_t)t) % NP_SMX_SEC);
        for (c = 0; c < nch && n < cap; c++) {
            int on = m->bit[row][c] ? 1 : 0;
            out[n].x = (c - (nch - 1) * 0.5f) * 0.28f;
            out[n].y = on ? 1.12f : 0.86f;
            out[n].z = -1.72f + (float)t * 0.24f;
            out[n].s = 0.16f;
            out[n].r = NP_CUBE_CR;
            out[n].g = NP_CUBE_CG;
            out[n].b = NP_CUBE_CB;
            out[n].a = on ? 230 : 70;
            out[n].role = on ? 2 : 0;
            n++;
        }
    }
    return n;
}

/* Flattened 10-10 (kit headset markings). +x right, +y nose. Lifted to a sphere. */
struct np_1010_def {
    char name[NP_ELEC_NAME];
    int8_t core;
    int8_t hx, hy; /* tenths: x -10..10, y -8..8 */
};

static const struct np_1010_def k1010[NP_1010_N] = {
    {"Fp1", 1, -3, 8},  {"Fpz", 0, 0, 8},   {"Fp2", 1, 3, 8},
    {"AF7", 0, -8, 6},  {"AF3", 0, -4, 6},  {"AFz", 0, 0, 6},  {"AF4", 0, 4, 6},  {"AF8", 0, 8, 6},
    {"F7", 1, -10, 4},  {"F5", 0, -8, 4},   {"F3", 1, -5, 4},  {"F1", 0, -3, 4},  {"Fz", 1, 0, 4},
    {"F2", 0, 3, 4},    {"F4", 1, 5, 4},    {"F6", 0, 8, 4},   {"F8", 1, 10, 4},
    {"FT7", 0, -10, 2}, {"FC5", 0, -8, 2},  {"FC3", 0, -5, 2}, {"FC1", 0, -3, 2}, {"FCz", 0, 0, 2},
    {"FC2", 0, 3, 2},   {"FC4", 0, 5, 2},   {"FC6", 0, 8, 2},  {"FT8", 0, 10, 2},
    {"T7", 1, -10, 0},  {"C5", 0, -8, 0},   {"C3", 1, -5, 0},  {"C1", 0, -3, 0},  {"Cz", 1, 0, 0},
    {"C2", 0, 3, 0},    {"C4", 1, 5, 0},    {"C6", 0, 8, 0},   {"T8", 1, 10, 0},
    {"TP7", 0, -10, -2},{"CP5", 0, -8, -2}, {"CP3", 0, -5, -2},{"CP1", 0, -3, -2},{"CPz", 0, 0, -2},
    {"CP2", 0, 3, -2},  {"CP4", 0, 5, -2},  {"CP6", 0, 8, -2}, {"TP8", 0, 10, -2},
    {"P7", 1, -10, -4}, {"P5", 0, -8, -4},  {"P3", 1, -5, -4}, {"P1", 0, -3, -4}, {"Pz", 1, 0, -4},
    {"P2", 0, 3, -4},   {"P4", 1, 5, -4},   {"P6", 0, 8, -4},  {"P8", 1, 10, -4},
    {"PO7", 0, -8, -6}, {"PO3", 0, -4, -6}, {"POz", 0, 0, -6}, {"PO4", 0, 4, -6}, {"PO8", 0, 8, -6},
    {"O1", 1, -3, -8},  {"Oz", 0, 0, -8},   {"O2", 1, 3, -8},
    {"A1", 0, -10, 0},  {"A2", 0, 10, 0},   {"M1", 0, -10, -2}, {"M2", 0, 10, -2},
};

/* Azimuth is atan2(fx, fy) in degrees, elevation is 90*(1 - radius). A radius above 1 is pulled back to the unit circle.
 * Either out pointer may be null. */
static void flat_to_elaz(float fx, float fy, float *az, float *el)
{
    float rr = sqrtf(fx * fx + fy * fy);
    if (rr > 1.f) {
        fx /= rr;
        fy /= rr;
        rr = 1.f;
    }
    if (az) {
        *az = atan2f(fx, fy) * 180.f / (float)M_PI;
    }
    if (el) {
        *el = 90.f - 90.f * rr;
    }
}

/* Always 65. That is the headset sites plus A1, A2, M1, and M2. */
int np_1010_count(void)
{
    return NP_1010_N;
}

/* Site name. An index outside 0..64 returns "". */
const char *np_1010_name(int i)
{
    if (i < 0 || i >= NP_1010_N) {
        return "";
    }
    return k1010[i].name;
}

/* 1 for a core site. An index outside 0..64 returns 0. */
int np_1010_core(int i)
{
    if (i < 0 || i >= NP_1010_N) {
        return 0;
    }
    return k1010[i].core;
}

/* Azimuth and elevation in degrees from the flat map. A bad index is the origin, which is elevation 90. */
void np_1010_elaz(int i, float *az, float *el)
{
    float fx, fy;
    np_1010_flat(i, &fx, &fy);
    flat_to_elaz(fx, fy, az, el);
}

/* Flat map in headset units, +x right and +y toward the nose, from tenths. A bad index writes 0, 0. */
void np_1010_flat(int i, float *fx, float *fy)
{
    if (i < 0 || i >= NP_1010_N) {
        if (fx) {
            *fx = 0.f;
        }
        if (fy) {
            *fy = 0.f;
        }
        return;
    }
    if (fx) {
        *fx = (float)k1010[i].hx / 10.f;
    }
    if (fy) {
        *fy = (float)k1010[i].hy / 10.f;
    }
}

/* Exact, case-sensitive name. Missing or empty returns -1. */
int np_1010_find(const char *name)
{
    int i;
    if (!name || !name[0]) {
        return -1;
    }
    for (i = 0; i < NP_1010_N; i++) {
        if (strcmp(k1010[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Site with the smallest chord on the unit sphere. Some site is always returned. */
int np_1010_nearest(float az, float el)
{
    int i, best = 0;
    float best_d = 1e9f;
    float ax, ay, azx, bx, by, bzx, dx, dy, dz;
    struct np_elec a;

    a.az = az;
    a.el = el;
    np_elec_to_xyz(&a, 1.f, &ax, &ay, &azx);
    for (i = 0; i < NP_1010_N; i++) {
        struct np_elec b;
        float d;
        np_1010_elaz(i, &b.az, &b.el);
        np_elec_to_xyz(&b, 1.f, &bx, &by, &bzx);
        dx = ax - bx;
        dy = ay - by;
        dz = azx - bzx;
        d = dx * dx + dy * dy + dz * dz;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

/* Copy that site's name and angles. A null electrode returns. A bad site clears the name and stores -1. */
void np_elec_set_site(struct np_elec *e, int site)
{
    if (!e) {
        return;
    }
    if (site < 0 || site >= NP_1010_N) {
        e->site = -1;
        e->name[0] = 0;
        return;
    }
    e->site = site;
    snprintf(e->name, sizeof(e->name), "%s", k1010[site].name);
    np_1010_elaz(site, &e->az, &e->el);
}

/* + end of each hardware pair. Front is +, back is −. */
static const char *k_elec_default[NP_NCHAN] = {"FC3", "FC1", "FCz", "FC2",
                                               "FC4", "PO3", "POz", "PO4"};
static const char *k_neg_default[NP_NCHAN] = {"CP3", "CP1", "CPz", "CP2",
                                              "CP4", "O1", "Oz", "O2"};
/* Referential contrasts among the + electrodes. Right is +, left is −. */
static const char *k_pair[NP_PAIR_N][2] = {
    {"FC4", "FC3"},
    {"FC2", "FC1"},
    {"PO4", "PO3"},
    {"FCz", "POz"},
};
/* One motor laterality pair and one visual laterality pair. */
static const char *k_bipolar[NP_BIPOLAR_N][2] = {
    {"FC4", "FC3"},
    {"PO4", "PO3"},
};

/* Plus sites: FC3 FC1 FCz FC2 FC4 PO3 POz PO4. A null array returns. */
void np_elec_default(struct np_elec e[NP_NCHAN])
{
    /* Five motor pairs across the central sulcus, three into visual cortex. */
    int i;
    if (!e) {
        return;
    }
    for (i = 0; i < NP_NCHAN; i++) {
        np_elec_set_site(&e[i], np_1010_find(k_elec_default[i]));
    }
}

/* Minus sites: CP3 CP1 CPz CP2 CP4 O1 Oz O2. A null array returns. */
void np_neg_default(int site[NP_NCHAN])
{
    int i;
    if (!site) {
        return;
    }
    for (i = 0; i < NP_NCHAN; i++) {
        site[i] = np_1010_find(k_neg_default[i]);
    }
}

/* Write the default plus and minus sites. When rail is set, car becomes 0 and every rld entry is cleared.
 * When rail is clear, car and rld are left as they were. */
void np_montage_restore(struct np_elec e[NP_NCHAN], int neg[NP_NCHAN], int rail, int *car,
                        int rld[NP_NCHAN])
{
    int c;
    np_elec_default(e);
    np_neg_default(neg);
    if (!rail) {
        return;
    }
    if (car) {
        *car = 0;
    }
    if (!rld) {
        return;
    }
    for (c = 0; c < NP_NCHAN; c++) {
        rld[c] = 0;
    }
}

/* Always 4. The pairs are FC4-FC3, FC2-FC1, PO4-PO3, and FCz-POz. */
int np_pair_count(void)
{
    return NP_PAIR_N;
}

/* Plus name of a referential pair (right or front). A bad index returns "". */
const char *np_pair_site_a(int pair)
{
    if (pair < 0 || pair >= NP_PAIR_N) {
        return "";
    }
    return k_pair[pair][0];
}

/* Minus name of a referential pair. A bad index returns "". */
const char *np_pair_site_b(int pair)
{
    if (pair < 0 || pair >= NP_PAIR_N) {
        return "";
    }
    return k_pair[pair][1];
}

/* First channel whose name matches exactly. A null array or an empty name returns -1. */
static int ch_named(const struct np_elec e[NP_NCHAN], const char *name)
{
    int c;
    if (!e || !name || !name[0]) {
        return -1;
    }
    for (c = 0; c < NP_NCHAN; c++) {
        if (strcmp(e[c].name, name) == 0) {
            return c;
        }
    }
    return -1;
}

/* Write the channels for pair A and pair B, or -1 when either name is not on a channel. Both outs are set to -1 before the search. */
int np_pair_chs(const struct np_elec e[NP_NCHAN], int pair, int *cha, int *chb)
{
    int a, b;
    if (cha) {
        *cha = -1;
    }
    if (chb) {
        *chb = -1;
    }
    if (!e || pair < 0 || pair >= NP_PAIR_N) {
        return -1;
    }
    a = ch_named(e, k_pair[pair][0]);
    b = ch_named(e, k_pair[pair][1]);
    if (a < 0 || b < 0) {
        return -1;
    }
    if (cha) {
        *cha = a;
    }
    if (chb) {
        *chb = b;
    }
    return 0;
}

/* Always 2. The pairs are FC4-FC3 and PO4-PO3. */
int np_bipolar_count(void)
{
    return NP_BIPOLAR_N;
}

/* Plus name of a bipolar pair. A bad index returns "". */
const char *np_bipolar_site_a(int pair)
{
    if (pair < 0 || pair >= NP_BIPOLAR_N) {
        return "";
    }
    return k_bipolar[pair][0];
}

/* Minus name of a bipolar pair. A bad index returns "". */
const char *np_bipolar_site_b(int pair)
{
    if (pair < 0 || pair >= NP_BIPOLAR_N) {
        return "";
    }
    return k_bipolar[pair][1];
}

/* 1 when exactly one of the two names starts with "Fp". An empty minus, or the word NONE or none, uses the plus name alone. */
int np_blink_end(const char *plus, const char *minus)
{
    int a, b;
    a = plus && plus[0] == 'F' && plus[1] == 'p';
    if (!minus || !minus[0] || strcmp(minus, "NONE") == 0 || strcmp(minus, "none") == 0) {
        return a;
    }
    b = minus[0] == 'F' && minus[1] == 'p';
    return a != b;
}

/* Write the channels for a bipolar pair. Same -1 rules as the referential lookup. */
int np_bipolar_chs(const struct np_elec e[NP_NCHAN], int pair, int *cha, int *chb)
{
    int a, b;
    if (cha) {
        *cha = -1;
    }
    if (chb) {
        *chb = -1;
    }
    if (!e || pair < 0 || pair >= NP_BIPOLAR_N) {
        return -1;
    }
    a = ch_named(e, k_bipolar[pair][0]);
    b = ch_named(e, k_bipolar[pair][1]);
    if (a < 0 || b < 0) {
        return -1;
    }
    if (cha) {
        *cha = a;
    }
    if (chb) {
        *chb = b;
    }
    return 0;
}

/* World coordinates of that site's shell cell. A bad site follows the 3, 7, 3 fallback. */
void np_1010_cube_xyz(int site, float *x, float *y, float *z)
{
    int ix, iy, iz;
    np_1010_ijk(site, &ix, &iy, &iz);
    np_ijk_world(ix, iy, iz, x, y, z);
}

/* World coordinates of the electrode's site, or of the nearest site when site is negative.
 * A null electrode writes 0, 0.62, 0. */
void np_elec_cube_xyz(const struct np_elec *e, float *x, float *y, float *z)
{
    if (!e) {
        if (x) {
            *x = 0.f;
        }
        if (y) {
            *y = 0.62f;
        }
        if (z) {
            *z = 0.f;
        }
        return;
    }
    if (e->site >= 0) {
        np_1010_cube_xyz(e->site, x, y, z);
        return;
    }
    np_1010_cube_xyz(np_1010_nearest(e->az, e->el), x, y, z);
}

/* Azimuth 0 is +z (nose), positive azimuth is +x (right), elevation 0 is the ear plane, positive elevation is up.
 * Any null pointer returns without writing. */
void np_elec_to_xyz(const struct np_elec *e, float r, float *x, float *y, float *z)
{
    float az, el, ce;
    if (!e || !x || !y || !z) {
        return;
    }
    az = e->az * (float)M_PI / 180.f;
    el = e->el * (float)M_PI / 180.f;
    ce = cosf(el);
    *x = r * ce * sinf(az);
    *y = r * sinf(el);
    *z = r * ce * cosf(az);
}

/* Inverse of the sphere map. Elevation is clamped to -25..85 degrees. A tiny radius sets azimuth and elevation to 0 and leaves name and site alone.
 * A null electrode returns. */
void np_elec_from_xyz(float x, float y, float z, struct np_elec *e)
{
    float r;
    if (!e) {
        return;
    }
    r = sqrtf(x * x + y * y + z * z);
    if (r < 1e-6f) {
        e->az = 0.f;
        e->el = 0.f;
        return;
    }
    e->el = asinf(y / r) * 180.f / (float)M_PI;
    e->az = atan2f(x, z) * 180.f / (float)M_PI;
    if (e->el > 85.f) {
        e->el = 85.f;
    }
    if (e->el < -25.f) {
        e->el = -25.f;
    }
}

/* Yaw, then pitch. Any null out pointer returns without writing. */
void np_view_apply(float yaw, float pitch, float x, float y, float z, float *ox, float *oy,
                   float *oz)
{
    float cy = cosf(yaw), sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    float x1 = cy * x + sy * z;
    float z1 = -sy * x + cy * z;
    if (!ox || !oy || !oz) {
        return;
    }
    *ox = x1;
    *oy = cp * y - sp * z1;
    *oz = sp * y + cp * z1;
}

/* Inverse of yaw-then-pitch. Any null out pointer returns without writing. */
void np_view_undo(float yaw, float pitch, float x, float y, float z, float *ox, float *oy, float *oz)
{
    float cy = cosf(yaw), sy = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    float y1 = cp * y + sp * z;
    float z1 = -sp * y + cp * z;
    if (!ox || !oy || !oz) {
        return;
    }
    *ox = cy * x - sy * z1;
    *oy = y1;
    *oz = sy * x + cy * z1;
}

/* Shell cubes for the eight electrodes, then one cyan cube per claimed virtual cell, at most 40.
 * A channel is on when its shell cell is on, otherwise from the latest second. A null rgb uses the built-in crimson. Role 3 marks a virtual cell. */
int np_smx_head_cubes(const struct np_smx *m, const struct np_elec e[NP_NCHAN],
                      const int rgb[NP_NCHAN][3], struct np_cube *out, int cap)
{
    int n = 0, c, ids[NP_NCHAN], nid, row;
    uint8_t latest[NP_NCHAN];

    if (!m || !e || !out || cap < 1) {
        return 0;
    }
    if (cap > NP_CUBE_BUDGET) {
        cap = NP_CUBE_BUDGET;
    }
    memset(latest, 0, sizeof(latest));
    nid = np_smx_ch_ids(m, ids);
    if (m->have > 0) {
        row = (int)((m->wr - 1) % NP_SMX_SEC);
        for (c = 0; c < m->nch && c < NP_NCHAN; c++) {
            latest[c] = m->bit[row][c];
        }
    }
    /* 8^3 volume: EEG on the shell, IMU/plugins inside. */
    for (c = 0; c < NP_NCHAN && n < cap; c++) {
        float x, y, z;
        int k, on = 0, ix, iy, iz;
        np_elec_cube_xyz(&e[c], &x, &y, &z);
        if (e[c].site >= 0) {
            np_1010_ijk(e[c].site, &ix, &iy, &iz);
            on = np_cube_get(m, ix, iy, iz);
        }
        if (!on) {
            for (k = 0; k < nid; k++) {
                if (ids[k] == c + 1) {
                    on = latest[k] ? 1 : 0;
                    break;
                }
            }
        }
        out[n].x = x;
        out[n].y = y;
        out[n].z = z;
        out[n].s = on ? 0.28f : 0.18f;
        if (rgb) {
            out[n].r = (uint8_t)rgb[c][0];
            out[n].g = (uint8_t)rgb[c][1];
            out[n].b = (uint8_t)rgb[c][2];
        } else {
            out[n].r = NP_CUBE_CR;
            out[n].g = NP_CUBE_CG;
            out[n].b = NP_CUBE_CB;
        }
        out[n].a = on ? 240 : 90;
        out[n].role = on ? 2 : 0;
        n++;
    }
    for (c = 0; c < NP_VIRT_MAX && n < cap; c++) {
        float x, y, z;
        int on;
        if (!m->virt[c].used) {
            continue;
        }
        on = np_cube_get(m, m->virt[c].x, m->virt[c].y, m->virt[c].z);
        np_ijk_world(m->virt[c].x, m->virt[c].y, m->virt[c].z, &x, &y, &z);
        out[n].x = x;
        out[n].y = y;
        out[n].z = z;
        out[n].s = on ? 0.16f : 0.10f;
        out[n].r = 70;
        out[n].g = 170;
        out[n].b = 210;
        out[n].a = on ? 220 : 70;
        out[n].role = 3;
        n++;
    }
    return n;
}
