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

static float rel_scale(float base)
{
    if (base < 25.f) {
        return 25.f;
    }
    return base;
}

uint64_t np_atom_pack_rel(const float *planar, int n_ch, int n_samp, int stride,
                          const float base_uv[8])
{
    uint64_t a = 0;
    int c;
    if (!planar || n_ch < 1 || n_samp < 1 || stride < n_samp) {
        return 0;
    }
    if (n_ch > 8) {
        n_ch = 8;
    }
    for (c = 0; c < n_ch; c++) {
        float sc = base_uv ? rel_scale(base_uv[c]) : NP_ATOM_SCALE;
        uint8_t bits = pack_ch8(planar + c * stride, n_samp, sc);
        a |= (uint64_t)bits << (c * 8);
    }
    return a;
}

uint64_t np_atom_from_uv8(const float uv[8], const float base_uv[8])
{
    float planar[8];
    int c;
    if (!uv) {
        return 0;
    }
    for (c = 0; c < 8; c++) {
        planar[c] = uv[c];
    }
    return np_atom_pack_rel(planar, 8, 1, 1, base_uv);
}

void np_atom_faces8(uint64_t atom, uint8_t cube[512])
{
    int c, b;
    if (!cube) {
        return;
    }
    memset(cube, 0, 512);
    for (c = 0; c < 8; c++) {
        uint8_t bits = (uint8_t)((atom >> (8 * c)) & 0xffu);
        for (b = 0; b < 8; b++) {
            if (bits & (uint8_t)(1u << b)) {
                /* face z=0: x=channel, y=feature. 64 cells. Rest stay 0. */
                cube[c + b * 8] = 1;
            }
        }
    }
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

void np_atom_rms8(const float *planar, int n_ch, int n_samp, int stride, float rms[8])
{
    int c, i;
    if (!rms) {
        return;
    }
    memset(rms, 0, 8 * sizeof(float));
    if (!planar || n_ch < 1 || n_samp < 1 || stride < n_samp) {
        return;
    }
    if (n_ch > 8) {
        n_ch = 8;
    }
    for (c = 0; c < n_ch; c++) {
        double e = 0.0;
        const float *w = planar + c * stride;
        for (i = 0; i < n_samp; i++) {
            e += (double)w[i] * (double)w[i];
        }
        rms[c] = (float)sqrt(e / (double)n_samp);
    }
}

float np_atom_rms_cos(const float *live, int nlive, const float *ref, int nref)
{
    int k, t, c;
    double acc = 0.0;
    if (!live || !ref || nlive < 1 || nref < 1) {
        return 0.f;
    }
    k = nlive < nref ? nlive : nref;
    for (t = 0; t < k; t++) {
        const float *a = live + (nlive - k + t) * 8;
        const float *b = ref + (nref - k + t) * 8;
        double dot = 0.0, na = 0.0, nb = 0.0;
        for (c = 0; c < 8; c++) {
            dot += (double)a[c] * (double)b[c];
            na += (double)a[c] * (double)a[c];
            nb += (double)b[c] * (double)b[c];
        }
        if (na < 1e-12 || nb < 1e-12) {
            continue;
        }
        acc += dot / (sqrt(na) * sqrt(nb));
    }
    return (float)(acc / (double)k);
}

float np_atom_rms_close(const float *live, int nlive, const float *ref, int nref)
{
    int k, t, c, n = 0;
    double acc = 0.0;
    if (!live || !ref || nlive < 1 || nref < 1) {
        return 0.f;
    }
    k = nlive < nref ? nlive : nref;
    for (t = 0; t < k; t++) {
        const float *a = live + (nlive - k + t) * 8;
        const float *b = ref + (nref - k + t) * 8;
        double d = 0.0;
        for (c = 0; c < 8; c++) {
            double la = log((double)a[c] + 1.0);
            double lb = log((double)b[c] + 1.0);
            d += fabs(la - lb);
        }
        acc += d / 8.0;
        n++;
    }
    if (n < 1) {
        return 0.f;
    }
    /* e^{-mean |ln a − ln b|}: identical → 1, 2× → ~0.50, 10× → ~0.10 */
    return (float)exp(-acc / (double)n);
}

float np_atom_rms_close_to_mean(const float *live, int nlive, const float *ref, int nref)
{
    float mean[8], last[8];
    int t, c;
    if (!live || !ref || nlive < 1 || nref < 1) {
        return 0.f;
    }
    memset(mean, 0, sizeof(mean));
    for (t = 0; t < nref; t++) {
        for (c = 0; c < 8; c++) {
            mean[c] += ref[t * 8 + c];
        }
    }
    for (c = 0; c < 8; c++) {
        mean[c] /= (float)nref;
        last[c] = live[(nlive - 1) * 8 + c];
    }
    return np_atom_rms_close(last, 1, mean, 1);
}

#define NP_PAT_FAR 0.85f
#define NP_PAT_SAME 0.90f

int np_atom_rms_pattern(const float *ref, int nref, const float *base, int nbase, float out[8])
{
    float tmean[8], bmean[8];
    int t, c, nfar = 0;

    if (!out) {
        return 0;
    }
    memset(out, 0, 8 * sizeof(float));
    if (!ref || nref < 1) {
        return 0;
    }
    memset(tmean, 0, sizeof(tmean));
    for (t = 0; t < nref; t++) {
        for (c = 0; c < 8; c++) {
            tmean[c] += ref[t * 8 + c];
        }
    }
    for (c = 0; c < 8; c++) {
        tmean[c] /= (float)nref;
    }
    if (!base || nbase < 1) {
        int best = 0;
        float beste = -1.f;
        for (t = 0; t < nref; t++) {
            float e = 0.f;
            for (c = 0; c < 8; c++) {
                e += ref[t * 8 + c];
            }
            if (e > beste) {
                beste = e;
                best = t;
            }
        }
        memcpy(out, ref + best * 8, 8 * sizeof(float));
        return 1;
    }
    memset(bmean, 0, sizeof(bmean));
    for (t = 0; t < nbase; t++) {
        for (c = 0; c < 8; c++) {
            bmean[c] += base[t * 8 + c];
        }
    }
    for (c = 0; c < 8; c++) {
        bmean[c] /= (float)nbase;
    }
    /* Whole take looks like rest — the pattern is rest, not a phantom burst. */
    if (np_atom_rms_close(tmean, 1, bmean, 1) >= NP_PAT_SAME) {
        memcpy(out, tmean, 8 * sizeof(float));
        return nref;
    }
    for (t = 0; t < nref; t++) {
        if (np_atom_rms_close(ref + t * 8, 1, bmean, 1) < NP_PAT_FAR) {
            for (c = 0; c < 8; c++) {
                out[c] += ref[t * 8 + c];
            }
            nfar++;
        }
    }
    if (nfar < 1) {
        return 0;
    }
    for (c = 0; c < 8; c++) {
        out[c] /= (float)nfar;
    }
    return nfar;
}

float np_atom_rms_close_to_pattern(const float *live, int nlive, const float *ref, int nref,
                                   const float *base, int nbase)
{
    float pat[8], last[8];
    int c, npat;

    if (!live || nlive < 1) {
        return 0.f;
    }
    npat = np_atom_rms_pattern(ref, nref, base, nbase, pat);
    if (npat < 1) {
        return 0.f;
    }
    for (c = 0; c < 8; c++) {
        last[c] = live[(nlive - 1) * 8 + c];
    }
    return np_atom_rms_close(last, 1, pat, 1);
}

float np_atom_file_close(const char *pa, const char *pb)
{
    uint64_t aa[NP_ATOM_RING], bb[NP_ATOM_RING];
    float ra[NP_ATOM_RING * 8], rb[NP_ATOM_RING * 8];
    int na, nb, wa = 0, wb = 0, ha = 0, hb = 0;

    if (!pa || !pb) {
        return 0.f;
    }
    na = np_atom_load2(pa, aa, ra, NP_ATOM_RING, &wa, &ha);
    nb = np_atom_load2(pb, bb, rb, NP_ATOM_RING, &wb, &hb);
    if (na < 1 || nb < 1) {
        return 0.f;
    }
    if (ha && hb) {
        return np_atom_rms_close(ra, na, rb, nb);
    }
    /* Bit Hamming is not a score. v1 files have no RMS. */
    (void)aa;
    (void)bb;
    return 0.f;
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
    return np_atom_save2(path, a, NULL, n, win);
}

int np_atom_save2(const char *path, const uint64_t *a, const float *rms, int n, int win)
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
    hdr[4] = rms ? 2 : 1;
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
        if (rms) {
            if (fwrite(rms + i * 8, sizeof(float), 8, f) != 8) {
                fclose(f);
                return -1;
            }
        }
    }
    fclose(f);
    return 0;
}

int np_atom_load(const char *path, uint64_t *a, int cap, int *win)
{
    return np_atom_load2(path, a, NULL, cap, win, NULL);
}

int np_atom_load2(const char *path, uint64_t *a, float *rms, int cap, int *win, int *have_rms)
{
    FILE *f;
    unsigned char hdr[12];
    unsigned n, i;
    int ver, hr = 0;
    if (!path || !a || cap < 1) {
        return -1;
    }
    if (have_rms) {
        *have_rms = 0;
    }
    f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "NPAT", 4) != 0) {
        fclose(f);
        return -1;
    }
    ver = hdr[4];
    if (ver != 1 && ver != 2) {
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
    hr = (ver == 2);
    for (i = 0; i < n; i++) {
        unsigned char b[8];
        if (fread(b, 1, 8, f) != 8) {
            fclose(f);
            return -1;
        }
        a[i] = rd_u64(b);
        if (ver == 2) {
            float r[8];
            if (fread(r, sizeof(float), 8, f) != 8) {
                fclose(f);
                return -1;
            }
            if (rms) {
                memcpy(rms + i * 8, r, sizeof(r));
            }
        } else if (rms) {
            memset(rms + i * 8, 0, 8 * sizeof(float));
        }
    }
    fclose(f);
    if (have_rms) {
        *have_rms = hr;
    }
    return (int)n;
}

int np_raw_save(const char *path, const float *planar, int n_ch, int n_samp, float sps)
{
    FILE *f;
    unsigned char hdr[16];
    size_t n;
    if (!path || !planar || n_ch < 1 || n_samp < 1) {
        return -1;
    }
    if (n_ch > 8) {
        n_ch = 8;
    }
    f = fopen(path, "wb");
    if (!f) {
        return -1;
    }
    memcpy(hdr, "NPRW", 4);
    hdr[4] = 1;
    hdr[5] = (unsigned char)n_ch;
    hdr[6] = 0;
    hdr[7] = 0;
    wr_u32(hdr + 8, (unsigned)n_samp);
    memcpy(hdr + 12, &sps, 4);
    if (fwrite(hdr, 1, 16, f) != 16) {
        fclose(f);
        return -1;
    }
    n = (size_t)n_ch * (size_t)n_samp;
    if (fwrite(planar, sizeof(float), n, f) != n) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

int np_raw_load(const char *path, float *planar, int cap, int *n_ch, int *n_samp, float *sps)
{
    FILE *f;
    unsigned char hdr[16];
    int ch, ns;
    size_t n, want;
    if (!path || !planar || cap < 1) {
        return -1;
    }
    f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    if (fread(hdr, 1, 16, f) != 16 || memcmp(hdr, "NPRW", 4) != 0 || hdr[4] != 1) {
        fclose(f);
        return -1;
    }
    ch = hdr[5];
    ns = (int)rd_u32(hdr + 8);
    if (ch < 1 || ch > 8 || ns < 1) {
        fclose(f);
        return -1;
    }
    n = (size_t)ch * (size_t)ns;
    want = n > (size_t)cap ? (size_t)cap : n;
    if (fread(planar, sizeof(float), want, f) != want) {
        fclose(f);
        return -1;
    }
    fclose(f);
    if (n_ch) {
        *n_ch = ch;
    }
    if (n_samp) {
        *n_samp = ns;
    }
    if (sps) {
        memcpy(sps, hdr + 12, 4);
    }
    return (int)want;
}
