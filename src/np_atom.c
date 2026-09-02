#include "np_atom.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* CubalC cubalc_eeg.c pack_ch8 — keep the bits the same. */
static uint8_t pack_ch8(const float *w, int n, float scale)
{
    double sum = 0.0, sum2 = 0.0, absmax = 0.0, mean, rms, var;
    int i, zc = 0;
    float prev, rise;
    uint8_t b = 0;

    if (n < 1) {
        return 0;
    }
    if (scale < 1e-6f) {
        scale = NP_ATOM_SCALE;
    }
    prev = w[0];
    for (i = 0; i < n; i++) {
        float v = w[i];
        double a;
        sum += (double)v;
        sum2 += (double)v * (double)v;
        a = fabs((double)v);
        if (a > absmax) {
            absmax = a;
        }
        if (i > 0) {
            if ((prev >= 0.f && v < 0.f) || (prev < 0.f && v >= 0.f)) {
                zc++;
            }
            prev = v;
        }
    }
    mean = sum / (double)n;
    rms = sqrt(sum2 / (double)n);
    rise = w[n - 1] - w[0];
    var = (sum2 / (double)n) - mean * mean;
    if (var < 0.0) {
        var = 0.0;
    }
    if (mean >= 0.0) {
        b |= 1u << 0;
    }
    if (absmax > (double)scale) {
        b |= 1u << 1;
    }
    if (absmax > 2.0 * (double)scale) {
        b |= 1u << 2;
    }
    if (zc * 4 >= n) {
        b |= 1u << 3;
    }
    if (rms > 0.5 * (double)scale) {
        b |= 1u << 4;
    }
    if (rise > 0.f) {
        b |= 1u << 5;
    }
    if (var < 0.05 * (double)scale * (double)scale) {
        b |= 1u << 6;
    }
    if (absmax > 5.0 * (double)scale) {
        b |= 1u << 7;
    }
    return b;
}

uint64_t np_atom_pack(const float *planar, int n_ch, int n_samp, int stride, float scale_uv)
{
    uint64_t a = 0;
    int c;
    if (!planar || n_ch < 1 || n_samp < 1 || stride < n_samp) {
        return 0;
    }
    if (n_ch > 8) {
        n_ch = 8;
    }
    if (scale_uv < 1e-6f) {
        scale_uv = NP_ATOM_SCALE;
    }
    for (c = 0; c < n_ch; c++) {
        uint8_t bits = pack_ch8(planar + c * stride, n_samp, scale_uv);
        a |= (uint64_t)bits << (c * 8);
    }
    return a;
}

int np_atom_popcount(uint64_t a)
{
    int n = 0;
    while (a) {
        n += (int)(a & 1u);
        a >>= 1;
    }
    return n;
}

int np_atom_hamming(uint64_t a, uint64_t b)
{
    return np_atom_popcount(a ^ b);
}

float np_atom_unity(uint64_t a, uint64_t b)
{
    return 1.f - (float)np_atom_hamming(a, b) / (float)NP_ATOM_BITS;
}

float np_atom_ring_unity(const uint64_t *live, int nlive, const uint64_t *ref, int nref)
{
    int k, i;
    double s = 0.0;
    if (!live || !ref || nlive < 1 || nref < 1) {
        return 0.f;
    }
    k = nlive < nref ? nlive : nref;
    for (i = 0; i < k; i++) {
        s += (double)np_atom_unity(live[nlive - k + i], ref[nref - k + i]);
    }
    return (float)(s / (double)k);
}

static void wr_u16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v & 255u);
    p[1] = (unsigned char)((v >> 8) & 255u);
}

static void wr_u32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v & 255u);
    p[1] = (unsigned char)((v >> 8) & 255u);
    p[2] = (unsigned char)((v >> 16) & 255u);
    p[3] = (unsigned char)((v >> 24) & 255u);
}

static void wr_u64(unsigned char *p, uint64_t v)
{
    int i;
    for (i = 0; i < 8; i++) {
        p[i] = (unsigned char)((v >> (8 * i)) & 255u);
    }
}

static unsigned rd_u16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned rd_u32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) |
           ((unsigned)p[3] << 24);
}

static uint64_t rd_u64(const unsigned char *p)
{
    uint64_t v = 0;
    int i;
    for (i = 0; i < 8; i++) {
        v |= (uint64_t)p[i] << (8 * i);
    }
    return v;
}

int np_atom_save(const char *path, const uint64_t *a, int n, int win)
{
    FILE *f;
    unsigned char hdr[12];
    int i;
    if (!path || !a || n < 1) {
        return -1;
    }
    if (win < 1) {
        win = NP_ATOM_WIN;
    }
    f = fopen(path, "wb");
    if (!f) {
        return -1;
    }
    memcpy(hdr, "NPAT", 4);
    hdr[4] = 1;
    hdr[5] = 8;
    wr_u16(hdr + 6, (unsigned)win);
    wr_u32(hdr + 8, (unsigned)n);
    if (fwrite(hdr, 1, 12, f) != 12) {
        fclose(f);
        return -1;
    }
    for (i = 0; i < n; i++) {
        unsigned char b[8];
        wr_u64(b, a[i]);
        if (fwrite(b, 1, 8, f) != 8) {
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return 0;
}

int np_atom_load(const char *path, uint64_t *a, int cap, int *win)
{
    FILE *f;
    unsigned char hdr[12];
    unsigned n, i;
    if (!path || !a || cap < 1) {
        return -1;
    }
    f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "NPAT", 4) != 0 || hdr[4] != 1) {
        fclose(f);
        return -1;
    }
    if (win) {
        *win = (int)rd_u16(hdr + 6);
    }
    n = rd_u32(hdr + 8);
    if (n > (unsigned)cap) {
        n = (unsigned)cap;
    }
    for (i = 0; i < n; i++) {
        unsigned char b[8];
        if (fread(b, 1, 8, f) != 8) {
            fclose(f);
            return -1;
        }
        a[i] = rd_u64(b);
    }
    fclose(f);
    return (int)n;
}
