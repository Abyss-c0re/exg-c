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

#endif
