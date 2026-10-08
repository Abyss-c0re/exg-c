#ifndef NP_DSP_H
#define NP_DSP_H

#include <stdint.h>

struct np_hp {
    float a, x1, y1;
};

struct np_notch {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
};

struct np_lp {
    float a, y;
};

#define NP_CLIP_UV 4000.f
#define NP_BAND_RAW 0
#define NP_BAND_LINE 1
#define NP_BAND_EEG 2
#define NP_BAND_EMG 3
#define NP_BAND_N 4

#define NP_PLATE_N 1024
#define NP_PSD_BINS (NP_FFT_N / 2)

/* Magnitude of a forward FFT. n above 256 is clipped to 256, and n must be a power of two. mag[i] is |bin i| / n for the first n/2 bins. */
void np_fft_mag(const float *in, int n, float *mag);
/* Hann PSD, hop 128, mean power into NP_PSD_BINS. A null x or n below 256 leaves zeros. */
void np_welch_psd(const float *x, int n, float *psd);
/* Median of bins 2..end — Wiener threshold, not the mean (DC tails lie). */
float np_psd_floor(const float *psd);
/* Destroy live bins that match the noise plate (Wiener gain). Covers the whole window (pads the last hop) and divides by the window sum so a 4 s plot does not keep a raw-amplitude tail. */
void np_plate_destroy(float *x, int n, const float *noise_psd);
/* Subtract the mean in place. n at or below 0 returns. */
void np_detrend(float *x, int n);
/* One-pole high-pass. a is rc/(rc+dt). hz or sps at or below 0 leaves a at 0, so the step copies the input. */
void np_hp_init(struct np_hp *f, float hz, float sps);
/* One high-pass sample. a at or below 0 returns x unchanged. */
float np_hp_step(struct np_hp *f, float x);
/* One-pole low-pass. a is dt/(rc+dt). hz or sps at or below 0 sets a to 1, so the step copies the input. */
void np_lp_init(struct np_lp *f, float hz, float sps);
/* One low-pass sample. y moves toward x by a. */
float np_lp_step(struct np_lp *f, float x);
/* Envelope coefficient from tau_s in seconds. a is 1/(tau_s*sps+1). tau_s or sps at or below 0 sets a to 1. */
void np_env_init(struct np_lp *f, float tau_s, float sps);
/* RMS of x² through the low-pass state. A negative power is clamped to 0 before the square root. */
float np_env_step(struct np_lp *f, float x);
/* 1 when |v| is above 4000 µV. */
int np_sample_clip(float v);
/* 1 when |v| is above 250000 µV. */
int np_sample_rail(float v);
/* 1 when any sample is outside ±4000 µV. A null x or n below 1 returns 0. */
int np_window_clip(const float *x, int n);
/* Subtract the mean of used channels that are inside ±250000 µV. Fewer than two such channels leaves v unchanged. A null v returns. A null use mask selects nothing. */
void np_car_sample(float *v, const int *use);
/* Cookbook notch. hz or sps at or below 0 is an identity, hz at or past half the rate becomes 0.48*sps, Q below 0.5 becomes 0.5, Q above 4 past 0.42*sps is cut to 3, and the pole radius stays in */
void np_notch_init(struct np_notch *f, float hz, float sps, float q);
/* One notch sample. Identity coefficients (b0 = 1 and b1 = 0) return x unchanged. */
float np_notch_step(struct np_notch *f, float x);
/* Name for a band id: raw, line-kill, EEG, or EMG. An id outside 0..3 returns "raw". */
const char *np_band_name(int id);

/* Dominant tone in [40 Hz, 0.47·fs]. 0 = found. */
int np_tone_hz(const float *x, int n, float sps, float *hz_out);
/* Search 40 Hz through 0.47*sps. The peak must be at least 4 times the mean of that band, then a parabolic bin shift. Returns 0 and writes hz, or -1. sps below 1, or a null pointer, returns -1. */
int np_tone_from_psd(const float *psd, float sps, float *hz_out);
/* Subtract the LS sinusoid at hz (the opposite wave). */
void np_tone_cancel(float *x, int n, float hz, float sps);
/* Subtract dc from each sample. A null x or n at or below 0 returns. */
void np_sub_dc(float *x, int n, float dc);

/* After plates: 1 noise  2 calm  3 signal (needs CALM)  0 unknown */
#define NP_DET_NONE 0
#define NP_DET_NOISE 1
#define NP_DET_CALM 2
#define NP_DET_SIGNAL 3
/* ratio is set to 0 first. calm_rms above 1 µV uses resid/calm: 1.50 or more is SIGNAL, raw within 0.70..1.40 of a real noise floor is NOISE, otherwise CALM. */
int np_detect(float raw_rms, float resid_rms, float noise_rms, float calm_rms, float *ratio);

/* Short-window event ID. Needs a worn CALM plate (except RAIL). */
#define NP_ID_NONE 0
#define NP_ID_NEED 1
#define NP_ID_RAIL 2
#define NP_ID_STILL 3
#define NP_ID_BLINK 4
#define NP_ID_CLENCH 5
#define NP_ID_BURST 6
#define NP_ID_CLIP 7
/* Rail when any masked rms exceeds 250000 µV; otherwise NEED with no calm plate, STILL below 1.80, BLINK when frontopolar is hot and the rest is quiet, CLENCH at four hot channels, BURST at 2.50, else */
int np_id_event(const float *rms, const float *calm, const int *fp, uint8_t mask,
                int have_calm, float *ratio);
/* none, need CALM, rail, still, blink, clench, burst, or CLIP. An id outside 0..7 returns "none". */
const char *np_id_name(int id);

#endif
