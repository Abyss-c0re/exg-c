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
int np_host_window_s(void);
void np_host_cycle_window(void);
int np_host_paused(void);
void np_host_toggle_pause(void);

void np_host_set_active(int ch, int on);
void np_host_set_rld(int ch, int on);
void np_host_cycle_gain(int ch);
int np_host_active(int ch);
int np_host_rld(int ch);
int np_host_gain(int ch);
void np_host_color(int ch, int *r, int *g, int *b);
void np_host_cycle_color(int ch);

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
int np_host_learn_sel(void);
void np_host_learn_select(int i);
void np_host_learn_del(int i);

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
int np_host_lp(void);
void np_host_cycle_lp(void);
int np_host_car(void);
void np_host_toggle_car(void);
int np_host_detrend(void);
void np_host_toggle_detrend(void);
int np_host_envelope(void);
void np_host_toggle_envelope(void);
int np_host_band(void);
void np_host_cycle_band(void);
int np_host_ch_clip(int ch);

int np_host_algo(void);
void np_host_cycle_algo(void);
void np_host_algo_name(char *out, int n);

int np_host_cube_view(void);
void np_host_set_cube_view(int map);
void np_host_cube_spin(float dyaw, float dpitch);
void np_host_cube_zoom(int dir);
void np_host_cube_front(void);
int np_host_elec_sel(void);
void np_host_set_elec_sel(int ch);
void np_host_elec_label(int ch, char *out, int n);
int np_host_elec_site(int ch);
void np_host_elec_xyz(int ch, float *x, float *y, float *z);
int np_host_site_focus(void);
void np_host_site_step(int dir);
void np_host_assign_site(int site);
int np_host_site_n(void);
void np_host_site_name(int i, char *out, int n);
int np_host_site_core(int i);
int np_host_site_ch(int i);
void np_host_site_flat(int i, float *fx, float *fy);
void np_host_site_xyz(int i, float *x, float *y, float *z);
int np_host_site_ijk(int i, int *x, int *y, int *z);
/* Packed viz cells: xyz[n*3], size[n], rgba[n]. Returns n (≤40). */
int np_host_viz_cells(float *xyz, float *size, int *rgba, int cap);
unsigned int np_host_smx_seq(void);
unsigned int np_host_smx_fold(void);
int np_host_prof_export(const char *path);
int np_host_prof_import(const char *path);

void np_host_id(char *out, int n);
int np_host_rec_ms(void);

int np_host_imu(float acc[3], float gyr[3], float mag[3]);
int np_host_board_imu(void);
void np_host_cycle_board(void);
int np_host_ui_scale(void);
void np_host_cycle_ui_scale(void);

#endif
