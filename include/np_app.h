#ifndef NP_APP_H
#define NP_APP_H

/* Internal core. Desktop UI may include this. Product API is np_host.h. */

#include "np_api.h"
#include "np_atom.h"
#include "np_dsp.h"
#include "np_knight.h"
#include "np_ring.h"
#include "np_serial.h"
#include "np_smx.h"
#include "np_types.h"
#include "nplearn.h"

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>

#define WIN_W 1280
#define WIN_H 800
#define SIDE_W 300
#define STATUS_H 28
#define FFT_H 96
#define LEARN_H 108
#ifdef __ANDROID__
#define NP_TOUCH 1
#else
#define NP_TOUCH 0
#endif
#define REC_MS 4000
#define LEARN_S 1.0f
#define WAVE_TOP 8
#define OPEN_UV 3000.f
#define QMAX 48
#define NP_PROF_NAME 24
#define NP_MAX_PROF 16
#define CMD_CHON 1
#define CMD_CHOFF 2
#define CMD_RLDADD 3
#define CMD_RLDRM 4
#define Q_OFF 0
#define Q_ZERO 1
#define Q_LEADOFF 2
#define Q_OPEN 3
#define Q_LIVE 4
enum { FFT_STRIP_N = 128, FFT_STRIP_BINS = 64 };
#define NSCALE 6
#define NWINS 4
#define NWINPREF 4
#define NPAL 12

struct np_app {
    int fd;
    int running;
    int connected;
    int want_connect;
    enum np_board board;
    int port_i;
    int nports;
    char ports[NP_MAX_PORTS][NP_MAX_PATH];
    int active[NP_NCHAN];
    int rld[NP_NCHAN];
    int gain[NP_NCHAN];
    int recording;
    FILE *csv;
    char csv_path[NP_MAX_PATH];
    char status[160];
    int status_ok;
    struct np_parser parser;
    struct np_ring ring;
    pthread_t thr;
    pthread_t en_thr;
    pthread_t cmd_thr;
    int en_running;
    pthread_mutex_t mu;
    pthread_mutex_t qmu;
    pthread_cond_t qcv;
    int qh, qt;
    struct {
        int op, ch, gain;
    } q[QMAX];
    /* visualization / sampling */
    int window_s;
    int autoscale;
    int og;
    int scale_uv;
    int notch_hz;
    int hp_hz;
    int lp_hz;
    int car;
    int envelope;
    int band;
    int grid;
    int paused;
    int show_uv;
    int detrend;
    float sps;
    uint32_t sps_n;
    struct timespec sps_t;
    struct np_hp hp[NP_NCHAN];
    struct np_notch notch[NP_NCHAN];
    struct np_lp lp[NP_NCHAN];
    struct np_lp env[NP_NCHAN];
    struct npl learn;
    int typing;
    char namebuf[NPL_NAME];
    struct {
        int have;
        uint32_t n;
        float dc[NP_NCHAN], rms[NP_NCHAN], pk[NP_NCHAN];
    } off, on, cal, calm;
    int cal_arm;
    int cal_cut;
    int set_gen; /* 1 EXG, 2 API off, 3 pair montage, 4 pair colors */
    float cal_hz; /* line tone from noise plate; 0 = none */
    float noise_psd[NP_PSD_BINS];
    float noise_psd_ch[NP_NCHAN][NP_PSD_BINS];
    int noise_psd_ok;
    unsigned noise_psd_ch_ok;
    int cal_phase;
    uint32_t cal_t0;
    uint32_t rec_t0;
    uint32_t saved_t0;
    uint64_t stall_tot;
    int stall_n;
    uint32_t stall_t;
    int recover_n;
    int tab;
    int ui_scale; /* tenths: 10, 15, 20 → 1.0x 1.5x 2.0x */
    int pref_w, pref_h;
    int chrgb[NP_NCHAN][3];
    pthread_mutex_t csv_mu;
    pthread_mutex_t parse_mu;
    struct np_smx smx;
    char cube_ack[48];
    int cube_ok;
    float cube_yaw, cube_pitch;
    float cube_zoom;
    int cube_view; /* 0 viz (crimson 8³)  1 map (10-10 assign) */
    int cube_float; /* 1 = levitate */
    uint8_t cube_bits[64];
    int site_focus; /* 10-10 index */
    int virt_focus; /* IMU / plugin slot */
    struct np_elec elec[NP_NCHAN];
    int elec_sel; /* 0..7 or -1 */
    int algo;     /* NP_ALGO_* 0/1 fold for cube + learn */
    char prof[NP_PROF_NAME];
    char profiles[NP_MAX_PROF][NP_PROF_NAME];
    int nprof;
    int typing_prof;
    int side_scroll;
    int atom_on;
    uint64_t atom_live[NP_ATOM_RING];
    int atom_n;
    int atom_wr;
    uint64_t atom_ref[NP_ATOM_RING];
    int atom_ref_n;
    char atom_ref_name[NP_ATOM_NAME];
    float atom_unity;
    unsigned int atom_seq;
    char atom_a[NP_ATOM_NAME];
    char atom_b[NP_ATOM_NAME];
    float atom_ab;
    float atom_live_rms[NP_ATOM_RING * 8];
    int atom_rec_n;
    float atom_id[32];
    int atom_id_best;
    int atom_clip;
    int api_on;
    int api_lan;
    int api_http;
    int api_udp;
    int api_tcp;
    int api_hz;
    char api_token[NP_API_TOKEN];
    char api_push[NP_API_PUSH];
    int link; /* 0 USB  1 LAN EXG  2 Bluetooth EXG */
    char link_dest[NP_API_PUSH];
    char link_token[NP_API_TOKEN];
    char link_id[80];
};


extern struct np_app g;
extern const int CHCOL[NP_NCHAN][3];
extern const int PALETTE[NPAL][3];
extern const int SCALE_UV[NSCALE];
extern const int WIN_S[NWINS];
extern const int WINPREF[NWINPREF][2];
extern float fft_hold[FFT_STRIP_BINS];
extern int fft_used, fft_open, fft_peak_hz;

void set_status(int ok, const char *fmt, ...);
void typing_set(int on);
void apply_readable_defaults(void);
void cfg_load(void);
void cfg_save(void);
void do_connect(void);
void do_disconnect(void);
void cmd_push(int op, int ch, int gain);
void filt_reset(void);
void id_label(char *out, int n);
void learn_path(char *out, size_t n);
void learn_persist(void);
void learn_start_hold(void);
void learn_tick(void);
void np_cfg_root(char *out, size_t n);
void np_mkdir_p(const char *path);
uint32_t plate_want(void);
void prof_cycle(void);
void prof_load(void);
void prof_save(void);
void prof_scan(void);
void raw_dump_ring(const char *path, uint32_t n_samp);
void raw_plate_path(const char *which, char *out, int n);
void smx_tick(void);
void stream_recover(void);
void toggle_record(void);
uint32_t view_copy(int ch, float *dst, uint32_t n);
void atom_tick(void);
void api_apply(void);
void api_defaults(void);
void ch_stats(const float *buf, uint32_t n, float *dc, float *rms, float *pk);
const char *clean_btn(void);
void clean_set_status(void);
const char *clean_tag(void);
float design_sps(void);
void fft_refresh(void);
void cube_zoom_clamp(void);
void cube_zoom_by(int dir);
void cube_site_by(int dir);
int cube_virt_slot(int focus);
void cube_virt_by(int dir);
void cube_assign_focus(void);
void next_gain(int ch);
void chcol_cycle(int c);
int cal_save(void);
int cal_load(void);
void cal_capture(void);
void calm_capture(void);
void ensure_dialout(int argc, char **argv);
int ch_quality(int c, const float *buf, uint32_t n, uint8_t lp, uint8_t ln);
void np_ui_apply_window_size(int w, int h);

#endif
