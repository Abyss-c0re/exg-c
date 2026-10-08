#ifndef NP_APP_H
#define NP_APP_H

/* Internal core. Desktop UI may include this. Product API is np_host.h. */

#include "np_algo.h"
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
#define CMD_MODE 5
#define Q_OFF 0
#define Q_ZERO 1
#define Q_LEADOFF 2
#define Q_OPEN 3
#define Q_LIVE 4
enum { FFT_STRIP_N = 128, FFT_STRIP_BINS = 64 };
#define NSCALE 7
#define NWINS 4
#define NWINPREF 4
#define NPAL 12

struct np_app {
    int fd; /* tty on desktop. Android open returns 100, not a kernel fd. */
    int running;
    int connected;
    int want_connect; /* declared only */
    enum np_board board;      /* live frame: EXG, IMU, or still Auto */
    enum np_board board_pref; /* Auto, or a forced length. Saved. */
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
    pthread_t thr; /* USB reader. Android raises this thread to audio priority. */
    pthread_t en_thr; /* channel ladder. Runs only after EXG-FW or a reset this app requested. */
    pthread_t cmd_thr; /* command queue. Not raised to audio priority. */
    int en_running;
    pthread_mutex_t mu;
    pthread_mutex_t qmu;
    pthread_cond_t qcv;
    int qh, qt; /* command queue head and tail */
    struct {
        int op, ch, gain;
    } q[QMAX];
    /* visualization / sampling */
    int window_s;
    int autoscale;
    int og; /* 1 = scale fit, the min and max of the strip */
    int scale_uv;
    int notch_hz; /* 50, 60, 0 off, or -1 so AUTO follows the plate */
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
    uint32_t chip_n; /* seq advance over the same window as sps_n */
    struct timespec sps_t;
    int rate_snap;    /* 125, 200, 250, 500, or 0 while unknown */
    int chip_sps;     /* banner or seq snap: 125, 250, 500 */
    int link_limited; /* USB is dropping frames below chip_sps */
    int banner_sps;   /* 0 none, -1 unset, else 125/250/500 */
    int flashing;
    int fw_seen; /* EXG-FW line, 0 if this connect has not printed one */
    int fw_have; /* last version flashed or read. Saved. */
    int fw_mode; /* EEPROM byte: 0 = 125+IMU, 1 = 250, 2 = 500. Saved. */
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
    } off, on, cal, calm; /* off and on are unused. cal is desk noise. calm is the still wear. */
    int cal_arm;
    int cal_cut; /* 1 turns DC on. DC on is the still-plate mean. */
    int set_gen; /* 1 EXG, 2 API off, 3 pair montage, 4 pair colors, 5 raw default, 6 motor+visual */
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
    int ui_scale; /* tenths: 8..22 → 0.8x..2.2x. 10 / 15 / 20 are the old steps. */
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
    int neg_rail; /* 1 = NEG RAIL: bias off, per-channel − site, sample is V(+)−V(−) */
    int neg_site[NP_NCHAN]; /* 10-10 index of each channel's negative electrode, -1 unset */
    int neg_pick; /* 1 = map Assign writes neg_site[elec_sel] */
    int pair_mode; /* 1 = two bipolar pairs, motor FC and visual PO. 0 = eight channel labels. */
    int made_n;
    int made_sel;
    struct {
        int ch[8]; /* 0 empty, 1..NP_NCHAN — one bit per cell of the 2×2×2 */
        unsigned char rgb[3];
        int cube_algo; /* library index for the whole cube */
        int algo[8];   /* -1 inherit cube_algo, else override */
    } made[4];
    int made_qsel; /* 0..7 bit being edited */
    int algo;      /* default library index for new bits + learn */
    char algo_src[NP_ALGO_SRC]; /* kept for old cfg */
    struct {
        char name[NP_ALIB_NAME];
        char src[NP_ALGO_SRC];
    } alib[NP_ALIB_N];
    int alib_n;
    int alib_sel;
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
    int link; /* 0 USB  1 LAN */
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

/* Status line under g.mu. ok 0 is a fault. fmt is printf-style and may truncate. */
void set_status(int ok, const char *fmt, ...);
/* Turns on-screen text entry on or off. Turning it off also clears a profile-name edit. */
void typing_set(int on);
/* Raw Knight view: filters, CAR, envelope, detrend, and the DC cut off, scale 1000 µV, window 2 s. Does not touch NEG RAIL or the electrode map. */
void apply_readable_defaults(void);
/* Reads exg-c.ini, or ~/.config/exg-c.conf if that is missing, then fills empty algos. Does not recook plates. */
void cfg_load(void);
/* Writes exg-c.ini under the config root, map included. A failed open leaves the previous file. */
void cfg_save(void);
/* USB opens the selected port and starts the reader and enable threads. A LAN follow only starts the link. Returns if already up, or if a flash is running and flash_owner is clear. */
void do_connect(void);
/* Drops the link and joins the reader. Closes an open CSV without fsync. Returns if already down, or if a flash is running and flash_owner is clear. */
void do_disconnect(void);
/* Queues one Knight command. cmd_thread writes it. Drops it if the queue is already full. */
void cmd_push(int op, int ch, int gain);
/* Rebuilds HP, notch, LP, and envelope poles at the design rate. Zeros the live cursor, so the next sync starts from a fresh history. */
void filt_reset(void);
/* Short ID text. A LAN follow name wins and skips the local classifier. A cold stream says it is warming, in measured SPS. */
void id_label(char *out, int n);
/* Path of exg-c.learn under the config root. Creates the root directory if it can. */
void learn_path(char *out, size_t n);
/* Writes the MATCH pose bank to exg-c.learn. Does not write take files. */
void learn_persist(void);
/* Arms a 4 s pose record under the typed name. No name opens the keyboard and returns. No board returns without arming. */
void learn_start_hold(void);
/* While MATCH is on, scores the last second against saved poses about ten times a second. A cold stream, MATCH off, or a failed capture clears the winner and returns. */
void learn_tick(void);
/* files dir if set, else desktop ~/.config or Android HOME. The non-UI Android build tries SDL internal storage first. Does not create it. */
void np_cfg_root(char *out, size_t n);
/* Makes every directory along path, mode 0755. NULL or empty returns. mkdir errors are ignored. */
void np_mkdir_p(const char *path);
/* Samples a plate asks for: window seconds times the design rate, never under NP_PLATE_N and never over the ring. */
uint32_t plate_want(void);
/* Loads the next saved profile. The electrode map stays, and plates and takes are recooked from raw. */
void prof_cycle(void);
/* Loads the named profile but puts the electrode map back, then recooks plates and takes from raw and writes exg-c.ini. An empty name jumps to the first saved profile. */
void prof_load(void);
/* Writes the named profile without the electrode map, then writes exg-c.ini with the map. Returns without a new profile file if the name is illegal or that write fails. */
void prof_save(void);
/* Fills the profile list from *.ini in the profiles directory. A missing directory leaves the count at 0. */
void prof_scan(void);
/* Writes the newest n_samp live samples as planar µV. Returns if n_samp < 16 or malloc fails. Caps at NP_RING. */
void raw_dump_ring(const char *path, uint32_t n_samp);
/* <raw>/<which>.nprw. which is the file name as given, with no sanitize. */
void raw_plate_path(const char *which, char *out, int n);
/* UI tick, at most once a second, and only while connected. Folds live channels into the cube and may store one SMX byte. */
void smx_tick(void);
/* After a stall, at most 3 resets. Returns while disconnected, enabling, or flashing. Android does not pulse DTR, and a link with under 10 frames is not reset. */
void stream_recover(void);
/* UI. Opens a timestamped CSV under the config root, or closes the current one. Refuses when not connected. */
void toggle_record(void);
/* CLEAN STFT at ~12 Hz, not 60×8. Plot uses the last cooked window. */
uint32_t view_copy(int ch, float *dst, uint32_t n);
/* Once a second, while connected and not cold, packs one cooked second into the atom ring. The envelope is forced off for that second and restored. A cold stream returns without a fold. */
void atom_tick(void);
/* Binds or rebinds the share from g. If the bind fails while share was on, turns it off. */
void api_apply(void);
/* Copies the built-in share settings into g and clears the token. Does not bind a socket. */
void api_defaults(void);
/* dc and rms of buf, pk the max absolute sample. Same unit as buf. n of 0 writes zeros. */
void ch_stats(const float *buf, uint32_t n, float *dc, float *rms, float *pk);
/* Button face: "CLN" when Wiener can run, "DC" when only offset subtract is on, else "dc". */
const char *clean_btn(void);
/* Status line for the DC/CLEAN switch. Does not change the switch. */
void clean_set_status(void);
/* Plot suffix: " CLEAN", " DC", or empty. Same switch as the button. */
const char *clean_tag(void);
/* Held snap 125/200/250/500, else a measured 100–160, else 125. 200 is the USB ceiling. Do not build filter poles from a lagged rate. */
float design_sps(void);
/* UI. Rebuilds the strip magnitude at most every 80 ms from cooked windows. Skips a channel that still looks like the noise plate. */
void fft_refresh(void);
/* Pulls the cube zoom into 0.70 .. 2.80. */
void cube_zoom_clamp(void);
/* dir > 0 steps in by 0.20. Any other dir steps out by 0.20, then clamps. */
void cube_zoom_by(int dir);
/* Steps the 10-10 focus by dir, wrapping. Returns if the site list is empty. */
void cube_site_by(int dir);
/* Storage index of the focus-th used virtual cell. -1 if focus is past that set. */
int cube_virt_slot(int focus);
/* Steps virt_focus across used cells, wrapping. Sets 0 when none are used. */
void cube_virt_by(int dir);
/* Writes the focused 10-10 site onto the selected channel and saves. With neg_pick and a channel selected, sets that NEG site and returns. Out of range returns. */
void cube_assign_focus(void);
/* Next ADS gain code for ch. Does nothing if the current code is not in the table. ch is not bounds-checked. */
void next_gain(int ch);
/* Next palette RGB for channel c (0..7). An unknown color jumps to swatch 0. Out of range returns. */
void chcol_cycle(int c);
/* Writes the noise and still plates to exg-c.cal. 0 on success, -1 if it cannot open. */
int cal_save(void);
/* Reads exg-c.cal into the plates. 0 if at least one channel row loaded. -1 if the file is missing or empty. */
int cal_load(void);
/* Noise plate from the live ring, plus a raw dump. Sets the plate even if the file write fails. */
void cal_capture(void);
/* Still plate. Returns without writing if no noise plate exists. Notches a tone above 1 Hz, then stores DC and the residual RMS. */
void calm_capture(void);
/* Desktop only. Re-execs under sg dialout once. Android, NP_EXG_NOSG, or an existing dialout membership returns. A failed sg does not loop. */
void ensure_dialout(int argc, char **argv);
/* 0 off, 1 flat, 2 lead-off, 3 open above 250 mV, 4 live. buf is µV. c is not checked. Bit 0 of lp and ln is channel 1. */
int ch_quality(int c, const float *buf, uint32_t n, uint8_t lp, uint8_t ln);
/* Weak no-op. The UI's own definition replaces it. w and h are ignored here. */
void np_ui_apply_window_size(int w, int h);

#endif
