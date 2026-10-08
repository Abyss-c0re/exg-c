#ifndef NP_COOK_H
#define NP_COOK_H

#include "np_app.h"

/* Internal. Owner: src/np_cook.c. Product API stays in np_host.h. */

/* 1 if channel ch belongs in the bias net. Always 0 in NEG RAIL. Out of 0..7 is 0. */
int rld_want(int ch);
/* NEG RAIL samples are already V(+)−V(−). Their mean is not a reference. */
int car_on(void);
/* 1 if s is a real 10-10 index. -1 and anything past the table are 0. */
int neg_site_ok(int s);
/* Below ~64% of the design rate. At 125 SPS that is still 80. */
int stream_cold(void);
/* Hz to notch. AUTO (setting -1) uses the noise-plate tone, which may be 0. A fixed setting is that many Hz. */
float notch_hz_eff(void);
/* Wiener needs NP_FFT_N samples. Default 2 s × 125 SPS is 250 — too short. */
int clean_wiener_ready(void);
extern uint64_t live_seen;
extern pthread_mutex_t live_mu;
/* Monotonic milliseconds, truncated to 32 bits. Not wall clock. */
uint32_t pair_now_ms(void);
/* Forces link to local or follow. A stored 2 becomes follow. A bt: dest and its token are cleared. */
void link_sanitize(void);
/* <config root>/exg-c.peers. Does not create the file. */
void peers_path(char *out, int n);
/* Writes the peer table to exg-c.peers. */
void peers_flush(void);
/* 1 if grant matches a saved peer. NULL or empty is 0. */
int host_grant_ok(const char *grant);
/* Snapshot path (learn / CAL). Own poles — must not smash the live IIR. */
void apply_filt(int ch, float *buf, uint32_t n);
/* In-place cook of a copied window: HP, notch, CAR, LP, detrend, envelope. want is unused. Skips a channel under 16 samples. Does not touch the live IIR. */
void cook_all(float buf[NP_NCHAN][NP_RING], uint32_t nn[NP_NCHAN], uint32_t want);
/* ID cook: EXG after shared floor. Not the display envelope. */
void cook_id(float buf[NP_NCHAN][NP_RING], uint32_t nn[NP_NCHAN]);
/* RAW, LINE, EEG, or EMG when the knobs match, else -1. EMG matches a fixed 50 Hz notch only. NEG RAIL expects CAR off. */
int band_from_filters(void);
/* If the knobs match a preset, stores that preset. A custom mix leaves the preset alone. */
void band_resync(void);
/* Loads one preset, 0..3, else RAW. Retunes, saves, and recooks. LINE and EEG turn CLEAN on only when a noise plate exists. */
void band_apply(int band);
/* JSON fields for the follower: filters, colors, sites, NEG names. Not a full object. Returns if out is NULL or n < 8. */
void api_view_json(char *out, int n);
/* Writes knobs, sites, and colors from a follower blob. Does not save. Unknown keys are ignored. NEG RAIL on clears bias and CAR. */
void apply_link_cfg(const char *js);
/* Copies remote µV into the live window without the local IIR. A set mask bit turns that channel on and does not turn the others off. NULL returns. */
void link_on_sample(const struct np_api_sample *s);
/* UI tick. Applies queued share commands. A port or on/off change saves and rebinds. */
void api_drain(void);
/* One IIR step per new sample. Display copies this. Never re-filter the window. */
void live_sync_u(void);
/* UI tick. Takes live_mu and steps new samples. Do not call it while already holding live_mu; the reader steps under that lock. */
void live_sync(void);
/* µV scale for channel c, at least 25. Uses the id baseline, else the still-plate RMS, when that is larger. */
float cook_scale_ch(int c);
/* RMS µV of the last 32 cooked samples. Inactive channels and windows under 4 stay 0. base, if not NULL, receives the scale. uv may be NULL. */
void cook_now(float uv[8], float base[8]);

#endif /* NP_COOK_H */
