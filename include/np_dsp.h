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

void np_fft_mag(const float *in, int n, float *mag);
void np_detrend(float *x, int n);
void np_hp_init(struct np_hp *f, float hz, float sps);
float np_hp_step(struct np_hp *f, float x);
void np_notch_init(struct np_notch *f, float hz, float sps, float q);
float np_notch_step(struct np_notch *f, float x);

/* Dominant tone in [40 Hz, 0.47·fs]. 0 = found. */
int np_tone_hz(const float *x, int n, float sps, float *hz_out);
/* Subtract the LS sinusoid at hz (the opposite wave). */
void np_tone_cancel(float *x, int n, float hz, float sps);
void np_sub_dc(float *x, int n, float dc);

/* After noise-tone + calm-DC: 1 noise  2 calm  3 signal  0 unknown */
#define NP_DET_NONE 0
#define NP_DET_NOISE 1
#define NP_DET_CALM 2
#define NP_DET_SIGNAL 3
int np_detect(float raw_rms, float resid_rms, float noise_rms, float calm_rms, float *ratio);

#endif
