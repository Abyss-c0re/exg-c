#include "np_algo.h"

#include <math.h>

const char *np_algo_name(int id)
{
    static const char *n[NP_ALGO_N] = {"detect", "sign", "mean", "energy",
                                       "delta",  "fold", "proton"};
    if (id < 0 || id >= NP_ALGO_N) {
        return "detect";
    }
    return n[id];
}

static float mean_abs(const float *x, int n)
{
    int i;
    double s = 0;
    if (n < 1) {
        return 0.f;
    }
    for (i = 0; i < n; i++) {
        s += fabs((double)x[i]);
    }
    return (float)(s / n);
}

int np_algo_bit(int id, const float *x, int n, int detect_bit)
{
    float last, ma, rms, md;
    int i, above;
    double e = 0, ep = 0;

    if (id == NP_ALGO_DETECT) {
        return detect_bit ? 1 : 0;
    }
    if (!x || n < 2) {
        return 0;
    }
    last = x[n - 1];
    ma = mean_abs(x, n);
    if (id == NP_ALGO_SIGN) {
        return last > 0.f ? 1 : 0;
    }
    if (id == NP_ALGO_MEAN) {
        return fabsf(last) > ma * 0.85f ? 1 : 0;
    }
    for (i = 0; i < n; i++) {
        e += (double)x[i] * (double)x[i];
        if (x[i] > 0.f) {
            ep += (double)x[i] * (double)x[i];
        }
    }
    rms = (float)sqrt(e / (double)n);
    if (id == NP_ALGO_ENERGY) {
        return rms > ma * 1.05f ? 1 : 0;
    }
    if (id == NP_ALGO_DELTA) {
        md = 0.f;
        for (i = 1; i < n; i++) {
            md += fabsf(x[i] - x[i - 1]);
        }
        md /= (float)(n - 1);
        return fabsf(last - x[n - 2]) > md * 1.10f ? 1 : 0;
    }
    if (id == NP_ALGO_FOLD) {
        above = 0;
        for (i = 0; i < n; i++) {
            if (x[i] > 0.f) {
                above++;
            }
        }
        return above > n / 2 ? 1 : 0;
    }
    if (id == NP_ALGO_PROTON) {
        return (e > 1e-12 && ep > 0.50 * e) ? 1 : 0;
    }
    return detect_bit ? 1 : 0;
}
