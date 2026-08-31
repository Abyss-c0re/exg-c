#include "nplearn.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct npl_hp {
    float a, x1, y1;
};

struct npl_notch {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
};

struct npl_lp {
    float a, y;
};

static void hp_init(struct npl_hp *f, float hz, float sps)
{
    float rc, dt;
    memset(f, 0, sizeof(*f));
    if (hz <= 0.f || sps <= 0.f) {
        return;
    }
    rc = 1.f / (2.f * (float)M_PI * hz);
    dt = 1.f / sps;
    f->a = rc / (rc + dt);
}

static float hp_step(struct npl_hp *f, float x)
{
    float y;
    if (f->a <= 0.f) {
        return x;
    }
    y = f->a * (f->y1 + x - f->x1);
    f->x1 = x;
    f->y1 = y;
    return y;
}

static void notch_init(struct npl_notch *f, float hz, float sps, float q)
{
    float w, c, r, bw, g, den;
    memset(f, 0, sizeof(*f));
    if (hz <= 0.f || sps <= 0.f) {
        f->b0 = 1.f;
        return;
    }
    if (hz >= sps * 0.5f) {
        hz = sps * 0.48f;
    }
    if (q < 0.5f) {
        q = 0.5f;
    }
    if (hz > sps * 0.42f && q > 4.f) {
        q = 3.f;
    }
    w = 2.f * (float)M_PI * hz / sps;
    c = cosf(w);
    bw = hz / q;
    r = 1.f - (float)M_PI * bw / sps;
    if (r < 0.55f) {
        r = 0.55f;
    }
    if (r > 0.98f) {
        r = 0.98f;
    }
    den = 1.f - 2.f * r * c + r * r;
    if (den < 1e-6f) {
        den = 1e-6f;
    }
    g = (2.f - 2.f * c) / den;
    if (g < 1e-6f) {
        g = 1e-6f;
    }
    f->b0 = 1.f / g;
    f->b1 = -2.f * c / g;
    f->b2 = 1.f / g;
    f->a1 = -2.f * r * c;
    f->a2 = r * r;
}

static float notch_step(struct npl_notch *f, float x)
{
    float y;
    if (f->b0 == 1.f && f->b1 == 0.f) {
        return x;
    }
    y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1;
    f->x1 = x;
    f->y2 = f->y1;
    f->y1 = y;
    return y;
}

static void lp_init(struct npl_lp *f, float hz, float sps)
{
    float rc, dt;
    memset(f, 0, sizeof(*f));
    if (hz <= 0.f || sps <= 0.f) {
        f->a = 1.f;
        return;
    }
    rc = 1.f / (2.f * (float)M_PI * hz);
    dt = 1.f / sps;
    f->a = dt / (rc + dt);
}

static float lp_step(struct npl_lp *f, float x)
{
    f->y += f->a * (x - f->y);
    return f->y;
}

static void detrend(float *x, int n)
{
    int i;
    double m = 0;
    if (n <= 0) {
        return;
    }
    for (i = 0; i < n; i++) {
        m += x[i];
    }
    m /= n;
    for (i = 0; i < n; i++) {
        x[i] = (float)(x[i] - m);
    }
}

void npl_filter(float *x, int n, float sps, float notch_hz)
{
    struct npl_hp hp;
    struct npl_notch nt;
    struct npl_lp lp;
    int i;
    if (notch_hz <= 0.f) {
        notch_hz = 50.f;
    }
    hp_init(&hp, NPL_HP_HZ, sps);
    notch_init(&nt, notch_hz, sps, 30.f);
    lp_init(&lp, NPL_LP_HZ, sps);
    for (i = 0; i < n; i++) {
        float v = hp_step(&hp, x[i]);
        v = notch_step(&nt, v);
        x[i] = lp_step(&lp, v);
    }
    detrend(x, n);
}

void npl_pack(float *dst, const float *src, int n)
{
    int i, j;
    double e = 0;

    memset(dst, 0, (size_t)NPL_LEN * sizeof(float));
    if (n < 4) {
        return;
    }
    for (i = 0; i < NPL_LEN; i++) {
        int a = (int)((long)i * n / NPL_LEN);
        int b = (int)((long)(i + 1) * n / NPL_LEN);
        float s = 0.f;
        int c = 0;
        if (b <= a) {
            b = a + 1;
        }
        if (b > n) {
            b = n;
        }
        for (j = a; j < b; j++) {
            s += src[j];
            c++;
        }
        dst[i] = c ? s / (float)c : 0.f;
    }
    for (i = 0; i < NPL_LEN; i++) {
        e += (double)dst[i] * (double)dst[i];
    }
    if (e < 1e-18) {
        return;
    }
    {
        float inv = 1.f / sqrtf((float)e);
        for (i = 0; i < NPL_LEN; i++) {
            dst[i] *= inv;
        }
    }
}

int npl_prep(float wave[NPL_LEN], float *rms, const float *src, int n, float sps, float notch_hz)
{
    float tmp[2048];
    int i, warm;
    double e = 0;
    const float *use;
    int un;

    if (!src || n < 16) {
        return -1;
    }
    if (n > 2048) {
        src += n - 2048;
        n = 2048;
    }
    memcpy(tmp, src, (size_t)n * sizeof(float));
    if (sps < 1.f) {
        sps = 125.f;
    }
    npl_filter(tmp, n, sps, notch_hz);

    warm = (int)(sps * 0.35f);
    if (warm > n / 3) {
        warm = n / 3;
    }
    if (n - warm < 16) {
        warm = 0;
    }
    use = tmp + warm;
    un = n - warm;
    for (i = 0; i < un; i++) {
        e += (double)use[i] * (double)use[i];
    }
    if (rms) {
        *rms = sqrtf((float)(e / (double)un));
    }
    npl_pack(wave, use, un);
    return 0;
}
