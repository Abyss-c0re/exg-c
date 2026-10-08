#include "np_dsp.h"
#include "np_types.h"

#include <math.h>
#include <string.h>

/* Filters, spectra, and short-window labels. Samples are µV.
 * Clip is ±4000. Rail is ±250000. SIGNAL needs a worn CALM plate. */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Flush denormals — otherwise IIR dies into subnormals and the hot path
 * gets ~100× slower after a few minutes of near-zero residual. */
static float np_flush(float y)
{
    if (y < 1e-18f && y > -1e-18f) {
        return 0.f;
    }
    return y;
}

/* Subtract the mean in place. n at or below 0 returns. */
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

/* Bit-reverse re and im in place. n must be a power of two. */
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

/* Radix-2 FFT in place. A non-zero inv also divides by n. n must be a power of two. */
static void fft_inplace(float *re, float *im, int n, int inv)
{
    int i, len, step;
    bitrev(re, im, n);
    for (len = 2; len <= n; len <<= 1) {
        double ang = (inv ? 1.0 : -1.0) * 2.0 * M_PI / len;
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
    if (inv) {
        float s = 1.f / (float)n;
        for (i = 0; i < n; i++) {
            re[i] *= s;
            im[i] *= s;
        }
    }
}

/* Magnitude of a forward FFT. n above 256 is clipped to 256, and n must be a power of two.
 * mag[i] is |bin i| / n for the first n/2 bins. */
void np_fft_mag(const float *in, int n, float *mag)
{
    float re[NP_FFT_N], im[NP_FFT_N];
    int i;
    if (n > NP_FFT_N) {
        n = NP_FFT_N;
    }
    memset(re, 0, sizeof(re));
    memset(im, 0, sizeof(im));
    memcpy(re, in, (size_t)n * sizeof(float));
    fft_inplace(re, im, n, 0);
    for (i = 0; i < n / 2; i++) {
        mag[i] = sqrtf(re[i] * re[i] + im[i] * im[i]) / (float)n;
    }
}

/* Hann PSD, hop 128, mean power into NP_PSD_BINS. A null x or n below 256 leaves zeros. */
void np_welch_psd(const float *x, int n, float *psd)
{
    float re[NP_FFT_N], im[NP_FFT_N];
    int hop = NP_FFT_N / 2, start, k, frames = 0;
    memset(psd, 0, (size_t)NP_PSD_BINS * sizeof(float));
    if (!x || n < NP_FFT_N) {
        return;
    }
    for (start = 0; start + NP_FFT_N <= n; start += hop) {
        int i;
        for (i = 0; i < NP_FFT_N; i++) {
            float win = 0.5f - 0.5f * cosf(2.f * (float)M_PI * (float)i / (float)(NP_FFT_N - 1));
            re[i] = x[start + i] * win;
            im[i] = 0.f;
        }
        fft_inplace(re, im, NP_FFT_N, 0);
        for (k = 0; k < NP_PSD_BINS; k++) {
            psd[k] += re[k] * re[k] + im[k] * im[k];
        }
        frames++;
    }
    if (frames > 0) {
        for (k = 0; k < NP_PSD_BINS; k++) {
            psd[k] /= (float)frames;
        }
    }
}

/* Median of bins from index 2 through the end. A null psd returns 0. */
float np_psd_floor(const float *psd)
{
    float v[NP_PSD_BINS];
    int n = 0, i, j;
    if (!psd) {
        return 0.f;
    }
    for (i = 2; i < NP_PSD_BINS; i++) {
        v[n++] = psd[i];
    }
    if (n < 1) {
        return 0.f;
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

/* Destroy live bins that match the noise plate (Wiener gain).
 * Covers the whole window (pads the last hop) and divides by the
 * window sum so a 4 s plot does not keep a raw-amplitude tail. */
void np_plate_destroy(float *x, int n, const float *noise_psd)
{
    float ola[NP_RING];
    float wsum[NP_RING];
    float re[NP_FFT_N], im[NP_FFT_N];
    float win[NP_FFT_N];
    int hop = NP_FFT_N / 2, start, i, k;
    float mean = 0.f;
    if (!x || !noise_psd || n < NP_FFT_N || n > NP_RING) {
        return;
    }
    memset(ola, 0, (size_t)n * sizeof(float));
    memset(wsum, 0, (size_t)n * sizeof(float));
    for (i = 0; i < NP_FFT_N; i++) {
        win[i] = 0.5f - 0.5f * cosf(2.f * (float)M_PI * (float)i / (float)(NP_FFT_N - 1));
    }
    mean = np_psd_floor(noise_psd);
    if (mean < 1e-18f) {
        for (k = 2; k < NP_PSD_BINS; k++) {
            mean += noise_psd[k];
        }
        mean /= (float)(NP_PSD_BINS - 2);
    }
    for (start = 0; start < n; start += hop) {
        for (i = 0; i < NP_FFT_N; i++) {
            int t = start + i;
            re[i] = (t < n) ? x[t] * win[i] : 0.f;
            im[i] = 0.f;
        }
        fft_inplace(re, im, NP_FFT_N, 0);
        for (k = 0; k < NP_PSD_BINS; k++) {
            float p = re[k] * re[k] + im[k] * im[k];
            float npw = noise_psd[k];
            float g = 1.f;
            if (k >= 2 && npw > 1.5f * mean) {
                g = p / (p + 2.f * npw + 1e-12f);
                if (g < 0.05f) {
                    g = 0.05f;
                }
            }
            re[k] *= g;
            im[k] *= g;
            if (k > 0 && k < NP_FFT_N - k) {
                re[NP_FFT_N - k] *= g;
                im[NP_FFT_N - k] *= g;
            }
        }
        fft_inplace(re, im, NP_FFT_N, 1);
        for (i = 0; i < NP_FFT_N; i++) {
            int t = start + i;
            if (t >= n) {
                break;
            }
            ola[t] += re[i];
            wsum[t] += win[i];
        }
        if (start + hop >= n) {
            break;
        }
    }
    for (i = 0; i < n; i++) {
        float d = wsum[i];
        if (d < 0.30f) {
            d = 0.30f;
        }
        x[i] = ola[i] / d;
    }
}

/* One-pole high-pass. a is rc/(rc+dt). hz or sps at or below 0 leaves a at 0, so the step copies the input. */
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

/* One high-pass sample. a at or below 0 returns x unchanged. */
float np_hp_step(struct np_hp *f, float x)
{
    float y;
    if (f->a <= 0.f) {
        return x;
    }
    y = np_flush(f->a * (f->y1 + x - f->x1));
    f->x1 = x;
    f->y1 = y;
    return y;
}

/* One-pole low-pass. a is dt/(rc+dt). hz or sps at or below 0 sets a to 1, so the step copies the input. */
void np_lp_init(struct np_lp *f, float hz, float sps)
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

/* One low-pass sample. y moves toward x by a. */
float np_lp_step(struct np_lp *f, float x)
{
    f->y += f->a * (x - f->y);
    return f->y;
}

/* Envelope coefficient from tau_s in seconds. a is 1/(tau_s*sps+1). tau_s or sps at or below 0 sets a to 1. */
void np_env_init(struct np_lp *f, float tau_s, float sps)
{
    memset(f, 0, sizeof(*f));
    if (tau_s <= 0.f || sps <= 0.f) {
        f->a = 1.f;
        return;
    }
    f->a = 1.f / (tau_s * sps + 1.f);
}

/* RMS of x² through the low-pass state. A negative power is clamped to 0 before the square root. */
float np_env_step(struct np_lp *f, float x)
{
    float p = x * x;
    f->y += f->a * (p - f->y);
    if (f->y < 0.f) {
        f->y = 0.f;
    }
    return sqrtf(f->y);
}

/* 1 when |v| is above 4000 µV. */
int np_sample_clip(float v)
{
    return v > NP_CLIP_UV || v < -NP_CLIP_UV;
}

/* 1 when |v| is above 250000 µV. */
int np_sample_rail(float v)
{
    return v > 250000.f || v < -250000.f;
}

/* 1 when any sample is outside ±4000 µV. A null x or n below 1 returns 0. */
int np_window_clip(const float *x, int n)
{
    int i;
    if (!x || n < 1) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (np_sample_clip(x[i])) {
            return 1;
        }
    }
    return 0;
}

/* Subtract the mean of used channels that are inside ±250000 µV. Fewer than two such channels leaves v unchanged.
 * A null v returns. A null use mask selects nothing. */
void np_car_sample(float *v, const int *use)
{
    int c, n = 0;
    double s = 0;
    float m;
    if (!v) {
        return;
    }
    /* 4 mV is a plot mark, not a rail. This head lives at 2–6 mV.
     * Skipping those samples left common-mode spikes on the plot. */
    for (c = 0; c < 8; c++) {
        if (use && use[c] && !np_sample_rail(v[c])) {
            s += v[c];
            n++;
        }
    }
    if (n < 2) {
        return;
    }
    m = (float)(s / (double)n);
    for (c = 0; c < 8; c++) {
        if (use && use[c] && !np_sample_rail(v[c])) {
            v[c] -= m;
        }
    }
}

/* Name for a band id: raw, line-kill, EEG, or EMG. An id outside 0..3 returns "raw". */
const char *np_band_name(int id)
{
    static const char *n[NP_BAND_N] = {"raw", "line-kill", "EEG", "EMG"};
    if (id < 0 || id >= NP_BAND_N) {
        return "raw";
    }
    return n[id];
}

/* Cookbook notch. hz or sps at or below 0 is an identity, hz at or past half the rate becomes 0.48*sps, Q below 0.5 becomes 0.5, Q above 4 past 0.42*sps is cut to 3, and the pole radius stays in 0.55..0.98. */
void np_notch_init(struct np_notch *f, float hz, float sps, float q)
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
    /* 60 Hz at 125 SPS is 0.48·fs. Cookbook Q=30 → a≈0.002, a sliver.
     * Zeros on the unit circle at ±ω, poles at r·e^{±jω}, r from BW. */
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

/* One notch sample. Identity coefficients (b0 = 1 and b1 = 0) return x unchanged. */
float np_notch_step(struct np_notch *f, float x)
{
    float y;
    if (f->b0 == 1.f && f->b1 == 0.f) {
        return x;
    }
    y = np_flush(f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2);
    f->x2 = f->x1;
    f->x1 = x;
    f->y2 = f->y1;
    f->y1 = y;
    return y;
}

/* Search 40 Hz through 0.47*sps. The peak must be at least 4 times the mean of that band, then a parabolic bin shift.
 * Returns 0 and writes hz, or -1. sps below 1, or a null pointer, returns -1. */
int np_tone_from_psd(const float *psd, float sps, float *hz_out)
{
    int N = NP_FFT_N, i, lo, hi, peak_i = 0, cnt = 0;
    float best = 0.f, acc = 0.f, shift, d, hz;
    if (!psd || sps < 1.f || !hz_out) {
        return -1;
    }
    lo = (int)(40.f * (float)N / sps);
    hi = (int)(0.47f * (float)N);
    if (lo < 2) {
        lo = 2;
    }
    if (hi > N / 2 - 2) {
        hi = N / 2 - 2;
    }
    for (i = lo; i <= hi; i++) {
        acc += psd[i];
        cnt++;
        if (psd[i] > best) {
            best = psd[i];
            peak_i = i;
        }
    }
    if (cnt < 3 || best < 4.f * (acc / (float)cnt)) {
        return -1;
    }
    d = psd[peak_i - 1] - 2.f * psd[peak_i] + psd[peak_i + 1];
    shift = 0.f;
    if (d < -1e-12f || d > 1e-12f) {
        shift = 0.5f * (psd[peak_i - 1] - psd[peak_i + 1]) / d;
    }
    hz = ((float)peak_i + shift) * sps / (float)N;
    if (hz < 40.f || hz >= sps * 0.47f) {
        return -1;
    }
    *hz_out = hz;
    return 0;
}

/* Windowed FFT of at most 256 samples after detrend, same 40 Hz .. 0.47*sps peak test.
 * n below 32, sps below 1, or a null pointer returns -1. */
int np_tone_hz(const float *x, int n, float sps, float *hz_out)
{
    float tmp[NP_FFT_N], mag[NP_FFT_N / 2];
    int N = NP_FFT_N, i, lo, hi, peak_i = 0, cnt = 0;
    float best = 0.f, acc = 0.f, shift, d, hz;
    if (!x || n < 32 || sps < 1.f || !hz_out) {
        return -1;
    }
    memset(tmp, 0, sizeof(tmp));
    if (n > N) {
        n = N;
    }
    memcpy(tmp, x, (size_t)n * sizeof(float));
    np_detrend(tmp, n);
    for (i = 0; i < n; i++) {
        float win = 0.5f - 0.5f * cosf(2.f * (float)M_PI * (float)i / (float)(n - 1));
        tmp[i] *= win;
    }
    np_fft_mag(tmp, N, mag);
    lo = (int)(40.f * (float)N / sps);
    hi = (int)(0.47f * (float)N);
    if (lo < 2) {
        lo = 2;
    }
    if (hi > N / 2 - 2) {
        hi = N / 2 - 2;
    }
    if (hi <= lo) {
        return -1;
    }
    for (i = lo; i <= hi; i++) {
        acc += mag[i];
        cnt++;
        if (mag[i] > best) {
            best = mag[i];
            peak_i = i;
        }
    }
    if (cnt < 3 || best < 4.f * (acc / (float)cnt)) {
        return -1;
    }
    d = mag[peak_i - 1] - 2.f * mag[peak_i] + mag[peak_i + 1];
    shift = 0.f;
    if (d < -1e-12f || d > 1e-12f) {
        shift = 0.5f * (mag[peak_i - 1] - mag[peak_i + 1]) / d;
    }
    hz = ((float)peak_i + shift) * sps / (float)N;
    if (hz < 40.f || hz >= sps * 0.47f) {
        return -1;
    }
    *hz_out = hz;
    return 0;
}

/* Subtract a 2/n least-squares sinusoid at hz. n below 8, or hz outside (0, 0.47*sps), returns with x unchanged. */
void np_tone_cancel(float *x, int n, float hz, float sps)
{
    int i;
    float w, re = 0.f, im = 0.f, scale;
    if (!x || n < 8 || hz <= 0.f || sps < 1.f || hz >= sps * 0.47f) {
        return;
    }
    w = 2.f * (float)M_PI * hz / sps;
    for (i = 0; i < n; i++) {
        float t = w * (float)i;
        re += x[i] * cosf(t);
        im += x[i] * sinf(t);
    }
    scale = 2.f / (float)n;
    re *= scale;
    im *= scale;
    for (i = 0; i < n; i++) {
        float t = w * (float)i;
        x[i] -= re * cosf(t) + im * sinf(t);
    }
}

/* Subtract dc from each sample. A null x or n at or below 0 returns. */
void np_sub_dc(float *x, int n, float dc)
{
    int i;
    if (!x || n <= 0) {
        return;
    }
    for (i = 0; i < n; i++) {
        x[i] -= dc;
    }
}

/* ratio is set to 0 first. calm_rms above 1 µV uses resid/calm: 1.50 or more is SIGNAL, raw within 0.70..1.40 of a real noise floor is NOISE, otherwise CALM.
 * With no calm plate the same noise test can return NOISE, and every other case returns NONE. */
int np_detect(float raw_rms, float resid_rms, float noise_rms, float calm_rms, float *ratio)
{
    const float floor_uv = 1.f;
    if (ratio) {
        *ratio = 0.f;
    }
    /* Residual vs calm first — a new tone on a loud mains plate must
     * still register. Raw-vs-noise is only the desk label. */
    if (calm_rms > floor_uv) {
        float r = resid_rms / calm_rms;
        if (ratio) {
            *ratio = r;
        }
        if (r >= 1.50f) {
            return NP_DET_SIGNAL;
        }
        if (noise_rms > floor_uv && raw_rms > 0.70f * noise_rms &&
            raw_rms < 1.40f * noise_rms) {
            return NP_DET_NOISE;
        }
        return NP_DET_CALM;
    }
    if (noise_rms > floor_uv && raw_rms > 0.70f * noise_rms && raw_rms < 1.40f * noise_rms) {
        return NP_DET_NOISE;
    }
    /* No worn CALM plate → do not invent SIGNAL from EXG rail. */
    return NP_DET_NONE;
}

/* none, need CALM, rail, still, blink, clench, burst, or CLIP. An id outside 0..7 returns "none". */
const char *np_id_name(int id)
{
    static const char *n[] = {"none", "need CALM", "rail", "still", "blink", "clench",
                              "burst", "CLIP"};
    if (id < 0 || id > NP_ID_CLIP) {
        return "none";
    }
    return n[id];
}

/* Rail when any masked rms exceeds 250000 µV; otherwise NEED with no calm plate, STILL below 1.80, BLINK when frontopolar is hot and the rest is quiet, CLENCH at four hot channels, BURST at 2.50, else STILL.
 * A missing calm base is 25 µV. CLIP is never returned. A null rms returns NONE and ratio stays 0. */
int np_id_event(const float *rms, const float *calm, const int *fp, uint8_t mask,
                int have_calm, float *ratio)
{
    int c, n = 0, nfp = 0, nfp_hot = 0, nhot = 0;
    float maxr = 0.f, fpr = 0.f, restmax = 0.f;

    if (ratio) {
        *ratio = 0.f;
    }
    if (!rms) {
        return NP_ID_NONE;
    }
    for (c = 0; c < 8; c++) {
        float base, r;
        int isfp;
        if (!(mask & (uint8_t)(1u << c))) {
            continue;
        }
        n++;
        if (rms[c] > 250000.f) {
            if (ratio) {
                *ratio = rms[c];
            }
            return NP_ID_RAIL;
        }
        if (!have_calm) {
            continue;
        }
        base = (calm && calm[c] > 1.f) ? calm[c] : 25.f;
        r = rms[c] / base;
        if (r > maxr) {
            maxr = r;
        }
        isfp = fp && fp[c];
        if (isfp) {
            nfp++;
            if (r > fpr) {
                fpr = r;
            }
            if (r >= 2.50f) {
                nfp_hot++;
            }
        } else {
            if (r > restmax) {
                restmax = r;
            }
            /* 2.5× is EMG. EEG+lp40 jaw clench is ~2× on this montage. */
            if (r >= 1.80f) {
                nhot++;
            }
        }
    }
    if (ratio) {
        *ratio = maxr;
    }
    if (n < 1) {
        return NP_ID_NONE;
    }
    if (!have_calm) {
        return NP_ID_NEED;
    }
    if (maxr < 1.80f) {
        return NP_ID_STILL;
    }
    if (nfp >= 1 && nfp_hot >= (nfp >= 2 ? 2 : 1) && fpr >= 2.50f && restmax < 2.20f) {
        return NP_ID_BLINK;
    }
    if (nfp >= 2 && nfp_hot >= 2 && fpr >= 2.20f && restmax < fpr * 0.70f) {
        return NP_ID_BLINK;
    }
    if (nhot + nfp_hot >= 4 && maxr >= 1.80f) {
        return NP_ID_CLENCH;
    }
    if (maxr >= 2.50f) {
        return NP_ID_BURST;
    }
    return NP_ID_STILL;
}
