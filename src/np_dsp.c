#include "np_dsp.h"
#include "np_types.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void np_detrend(float *x, int n)
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

static void bitrev(float *re, float *im, int n)
{
    int i, j, k;
    j = 0;
    for (i = 1; i < n; i++) {
        k = n >> 1;
        while (j & k) {
            j ^= k;
            k >>= 1;
        }
        j ^= k;
        if (i < j) {
            float tr = re[i], ti = im[i];
            re[i] = re[j];
            im[i] = im[j];
            re[j] = tr;
            im[j] = ti;
        }
    }
}

void np_fft_mag(const float *in, int n, float *mag)
{
    float re[NP_FFT_N], im[NP_FFT_N];
    int i, len, step;
    if (n > NP_FFT_N) {
        n = NP_FFT_N;
    }
    /* pad / truncate to power of two already assumed */
    memset(re, 0, sizeof(re));
    memset(im, 0, sizeof(im));
    memcpy(re, in, (size_t)n * sizeof(float));
    bitrev(re, im, n);
    for (len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / len;
        double wr = 1, wi = 0;
        double cr = cos(ang), ci = sin(ang);
        int half = len >> 1;
        for (step = 0; step < half; step++) {
            for (i = step; i < n; i += len) {
                int j = i + half;
                float tr = (float)(wr * re[j] - wi * im[j]);
                float ti = (float)(wr * im[j] + wi * re[j]);
                re[j] = re[i] - tr;
                im[j] = im[i] - ti;
                re[i] += tr;
                im[i] += ti;
            }
            {
                double nwr = wr * cr - wi * ci;
                wi = wr * ci + wi * cr;
                wr = nwr;
            }
        }
    }
    for (i = 0; i < n / 2; i++) {
        mag[i] = sqrtf(re[i] * re[i] + im[i] * im[i]) / (float)n;
    }
}

void np_hp_init(struct np_hp *f, float hz, float sps)
{
    float rc, dt;
    memset(f, 0, sizeof(*f));
    if (hz <= 0.f || sps <= 0.f) {
        f->a = 0.f;
        return;
    }
    rc = 1.f / (2.f * (float)M_PI * hz);
    dt = 1.f / sps;
    f->a = rc / (rc + dt);
}

float np_hp_step(struct np_hp *f, float x)
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

void np_notch_init(struct np_notch *f, float hz, float sps, float q)
{
    float w, c, a;
    memset(f, 0, sizeof(*f));
    if (hz <= 0.f || sps <= 0.f) {
        f->b0 = 1.f;
        return;
    }
    /* Knight is 125 SPS. Nyquist 62.5 Hz. A 60 Hz biquad is two
     * samples per cycle — Q=30 becomes a sliver and looks like off. */
    if (hz >= sps * 0.47f) {
        f->b0 = 1.f;
        return;
    }
    if (q < 0.5f) {
        q = 0.5f;
    }
    w = 2.f * (float)M_PI * hz / sps;
    c = cosf(w);
    a = sinf(w) / (2.f * q);
    f->b0 = 1.f / (1.f + a);
    f->b1 = -2.f * c * f->b0;
    f->b2 = f->b0;
    f->a1 = f->b1;
    f->a2 = (1.f - a) * f->b0;
}

float np_notch_step(struct np_notch *f, float x)
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
