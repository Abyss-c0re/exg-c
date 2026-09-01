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

void np_fft_mag(const float *in, int n, float *mag);
void np_welch_psd(const float *x, int n, float *psd);
void np_plate_destroy(float *x, int n, const float *noise_psd);
void np_detrend(float *x, int n);
void np_hp_init(struct np_hp *f, float hz, float sps);
float np_hp_step(struct np_hp *f, float x);
void np_lp_init(struct np_lp *f, float hz, float sps);
float np_lp_step(struct np_lp *f, float x);
void np_env_init(struct np_lp *f, float tau_s, float sps);
float np_env_step(struct np_lp *f, float x);
int np_sample_clip(float v);
int np_window_clip(const float *x, int n);
void np_car_sample(float *v, const int *use);
void np_notch_init(struct np_notch *f, float hz, float sps, float q);
float np_notch_step(struct np_notch *f, float x);
const char *np_band_name(int id);

/* Dominant tone in [40 Hz, 0.47·fs]. 0 = found. */
int np_tone_hz(const float *x, int n, float sps, float *hz_out);
int np_tone_from_psd(const float *psd, float sps, float *hz_out);
/* Subtract the LS sinusoid at hz (the opposite wave). */
void np_tone_cancel(float *x, int n, float hz, float sps);
void np_sub_dc(float *x, int n, float dc);

/* After plates: 1 noise  2 calm  3 signal (needs CALM)  0 unknown */
#define NP_DET_NONE 0
#define NP_DET_NOISE 1
#define NP_DET_CALM 2
#define NP_DET_SIGNAL 3
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
int np_id_event(const float *rms, const float *calm, const int *fp, uint8_t mask,
                int have_calm, float *ratio);
const char *np_id_name(int id);

#endif
