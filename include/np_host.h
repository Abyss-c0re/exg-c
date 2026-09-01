#ifndef NP_HOST_H
#define NP_HOST_H

#include "np_types.h"

void np_set_files_dir(const char *path);
int np_host_start(const char *files_dir);
void np_host_shutdown(void);
void np_host_tick(void);

int np_host_connect(void);
void np_host_disconnect(void);
int np_host_connected(void);
void np_host_status(char *out, int n);
int np_host_status_ok(void);
float np_host_sps(void);
unsigned int np_host_frames(void);

int np_host_copy_wave(int ch, float *dst, int max);
int np_host_scale_uv(void);
void np_host_cycle_scale(void);
int np_host_paused(void);
void np_host_toggle_pause(void);

void np_host_set_active(int ch, int on);
void np_host_set_rld(int ch, int on);
void np_host_cycle_gain(int ch);
int np_host_active(int ch);
int np_host_rld(int ch);
int np_host_gain(int ch);
void np_host_color(int ch, int *r, int *g, int *b);

void np_host_noise_arm(void);
void np_host_noise_ok(void);
void np_host_calm(void);
void np_host_toggle_clean(void);
int np_host_cal_have(void);
int np_host_calm_have(void);
int np_host_clean(void);

void np_host_set_name(const char *s);
void np_host_get_name(char *out, int n);
void np_host_record(void);
void np_host_toggle_match(void);
int np_host_match(void);
int np_host_learn_n(void);
int np_host_learn_best(void);
void np_host_learn_name(int i, char *out, int n);
float np_host_learn_score(int i);

void np_host_set_profile(const char *s);
void np_host_get_profile(char *out, int n);
int np_host_prof_save(void);
int np_host_prof_load(void);
int np_host_prof_count(void);
void np_host_prof_at(int i, char *out, int n);

void np_host_ports(char *out, int n);
void np_host_cycle_port(void);
void np_host_copy_cube(unsigned char dst[512]);
int np_host_notch(void);
int np_host_hp(void);
void np_host_cycle_notch(void);
void np_host_cycle_hp(void);

#endif
