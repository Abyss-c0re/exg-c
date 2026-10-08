#ifndef NP_HOST_H
#define NP_HOST_H

#include "np_types.h"

/* Copies p over the files dir. NULL or empty leaves the previous root. */
void np_set_files_dir(const char *path);
/* Call once before the UI tick. 0 on success, including a second call. -1 if cmd_thread could not start. files_dir may be NULL. */
int np_host_start(const char *files_dir);
/* Stops cmd_thread, drops the link, and stops the share API. A second call returns. */
void np_host_shutdown(void);
/* UI thread, every frame. Drains share ops, saves a pending mode, and restarts a stalled USB stream. Returns at once if start never finished. */
void np_host_tick(void);

/* Opens the selected port, or the LAN follow. 1 if the link is up afterwards. */
int np_host_connect(void);
/* Closes USB or the LAN follow. Returns if a flash is running and flash_owner is clear. */
void np_host_disconnect(void);
/* 1 while USB or a LAN follow is up. Not the same as frames arriving. */
int np_host_connected(void);
/* Copies the status line. Holds g.mu. Truncates to n-1. */
void np_host_status(char *out, int n);
/* 1 when the last status line is a success. 0 marks a fault. */
int np_host_status_ok(void);
/* Measured frames per second over the last full second. At most 1 reads as 0. */
float np_host_sps(void);
/* Samples seen. Non-zero g.link uses the live cursor; USB uses the ring total. Truncates to unsigned int. */
unsigned int np_host_frames(void);
/* Samples the ring overwrote. Not USB packet loss. */
unsigned int np_host_drops(void);

/* Window of cooked µV for one channel. 0 if ch is outside 0..7, dst is null, or max is under 8; */
int np_host_copy_wave(int ch, float *dst, int max);
/* Plot half-scale in µV, as stored. Not clamped here. */
int np_host_scale_uv(void);
/* Next of 50, 100, 200, 500, 1000, 2000, 5000 µV, then wraps, and saves. An unknown stored value becomes 200 and is not saved. */
void np_host_cycle_scale(void);
/* A positive value is the half-scale in µV and turns autoscale off. 0 or negative turns autoscale on and does not change the stored scale. Saves either way. */
void np_host_set_scale_uv(int uv);
/* Window length in seconds. A stored value under 1 reads as 2. */
int np_host_window_s(void);
/* Next of 1, 2, 4, 8 seconds, and saves. An unknown stored value becomes 2 and is saved. */
void np_host_cycle_window(void);
/* Clamps the window to 1..8 seconds and saves. */
void np_host_set_window_s(int s);
/* 1 while the plot is paused. Does not say whether the board is still streaming. */
int np_host_paused(void);
/* Flips plot pause. Does not save and does not stop USB. */
void np_host_toggle_pause(void);

/* Turns channel ch (0..7) on or off, saves, and sends the command if connected. Refuses to turn off the last channel. A bad index returns. */
void np_host_set_active(int ch, int on);
/* Bias for one channel, saved. NEG RAIL refuses and leaves bias off. The board command waits until connect if the cable is down. */
void np_host_set_rld(int ch, int on);
/* Next legal gain (1, 2, 3, 4, 6, 8, 12), updates the parser, and saves. Sends it only if that channel is connected and on. A bad index returns. */
void np_host_cycle_gain(int ch);
/* Sets the gain only if it is 1, 2, 3, 4, 6, 8, or 12, then saves. Any other value, or a bad channel, returns without a change. */
void np_host_set_gain(int ch, int gain);
/* 1 if channel ch (0..7) is on. 0 if ch is out of range. */
int np_host_active(int ch);
/* 1 if this channel's bias pin is on. NEG RAIL forces the answer to 0. */
int np_host_rld(int ch);
/* NEG RAIL stays: bias off on every channel, each channel keeps its own − site.
 * The sample is then the drop from + to −. CAR and software re-pairs stay off. */
int np_host_neg_rail(void);
/* Turns NEG RAIL on or off and saves. On forces bias off, CAR off, and minus-site picking on. Off does not restore the electrode map. */
void np_host_set_neg_rail(int on);
/* Put back the eight motor/visual pairs. Does not change NEG RAIL. */
void np_host_montage_default(void);
/* 10-10 index of the minus site, or -1 if it is unset or ch is outside 0..7. */
int np_host_neg_site(int ch);
/* Sets the minus site for one channel and saves. -1 clears it, an index outside the 10-10 list returns without a change, and this does not turn NEG RAIL on. */
void np_host_set_neg_site(int ch, int site);
/* Minus-site name, or NONE. A short or null buffer returns without writing. */
void np_host_neg_name(int ch, char *out, int n);
/* 1 if the next site tap assigns a minus site. 0 assigns the plus site. */
int np_host_neg_pick(void);
/* Nonzero picks minus sites, 0 picks plus sites. Does not save. Turning it on moves focus to the selected channel's minus site when that site is set. */
void np_host_set_neg_pick(int on);
/* ADS1299 gain for the channel: 1, 2, 3, 4, 6, 8, or 12. Out of range reads as 12. */
int np_host_gain(int ch);
/* Writes 0..255 RGB for the channel. A bad channel returns without writing the pointers. A null pointer is skipped. */
void np_host_color(int ch, int *r, int *g, int *b);
/* Next palette color for the channel, and saves. */
void np_host_cycle_color(int ch);
/* Clamps each component to 0..255 and saves. A bad channel returns without a change. */
void np_host_set_color(int ch, int r, int g, int b);

/* Same entry as cal start. An older button still lands in the timed plate. */
void np_host_noise_arm(void);
/* During the desk phase, captures the noise plate now. Otherwise starts cal. */
void np_host_noise_ok(void);
/* If a noise plate is in memory, starts the 8 s sit-still timer again. Otherwise the capture asks for noise first. */
void np_host_calm(void);
/* Starts the desk wait, or the sit-still wait if a noise plate already exists. Returns if a timed phase is already running. */
void np_host_cal_start(void);
/* 0 idle, 1 desk, 2 wear prompt, 3 sit still, 4 done, 5 put-down. */
int np_host_cal_phase(void);
/* 0..99 during a timed phase, 100 when done, else 0. Does not finish the phase. */
int np_host_cal_progress(void);
/* Short prompt for the current phase, with seconds left. Returns if out is NULL or n < 4. */
void np_host_cal_line(char *out, int n);
/* Flips DC/CLEAN and saves. Does not capture a plate. */
void np_host_toggle_clean(void);
/* 1 after a noise plate is stored in memory. */
int np_host_cal_have(void);
/* 1 after a still plate is stored in memory. */
int np_host_calm_have(void);
/* 1 when DC subtract / CLEAN is switched on. Not whether the Wiener fit can run. */
int np_host_clean(void);
/* 1 only when Wiener CLEAN actually runs (noise plate + window ≥ FFT). */
int np_host_clean_live(void);

/* Copies the MATCH or take name into the edit buffer. Does not save. A null pointer clears it. */
void np_host_set_name(const char *s);
/* Current MATCH or take name, which may be empty. */
void np_host_get_name(char *out, int n);
/* Starts a 4 s pose record, or cancels one that is already running. Does not write a take file. */
void np_host_record(void);
/* Turns MATCH on or off. Off clears the pose winner and the take winner. On does not record by itself. */
void np_host_toggle_match(void);
/* 1 while MATCH is naming poses and takes. */
int np_host_match(void);
/* How many MATCH poses are stored. Not the take count. */
int np_host_learn_n(void);
/* Index of the winning pose, or -1 when MATCH is off or nothing won. */
int np_host_learn_best(void);
/* Pose name at i. Out of range writes an empty string. */
void np_host_learn_name(int i, char *out, int n);
/* Wave-and-RMS blend for pose i. 0 if i is out of range, or if no pose won by a clear margin. Not a percent. */
float np_host_learn_score(int i);
/* Jaccard of occupied cube cells, 0..1. 0 if i is out of range or that pose has no cube. Does not replace the wave score. */
float np_host_learn_score_cube(int i);
/* Index of the pose selected in the list, or -1 if none. */
int np_host_learn_sel(void);
/* Selects pose i and copies its name into the edit buffer. Out of range does nothing. */
void np_host_learn_select(int i);
/* Deletes pose i, its raw file, and rewrites exg-c.learn. Out of range does nothing. Does not delete a take. */
void np_host_learn_del(int i);

/* Stores a legal profile name only. Does not load or write a file. An illegal name is ignored. */
void np_host_set_profile(const char *s);
/* Current profile name, which may be empty. */
void np_host_get_profile(char *out, int n);
/* Saves the profile without the map, then exg-c.ini. 0 if the name is legal, even when the write failed, and -1 if it is not. */
int np_host_prof_save(void);
/* Loads the profile, keeps the electrode map, and recooks from raw. Always returns 0, including when the file is missing. */
int np_host_prof_load(void);
/* Deletes the current profile. 0 if the name was cleared, -1 if it is still set. */
int np_host_prof_del(void);
/* Renames the profile file. 0 only when the live name is now that name. */
int np_host_prof_rename(const char *to);
/* Rescans the profiles directory and returns how many legal names were kept. */
int np_host_prof_count(void);
/* Rescans, then copies profile i. Out of range writes an empty string. */
void np_host_prof_at(int i, char *out, int n);

/* Rescans and writes one port path per line. out is empty when none exist. */
void np_host_ports(char *out, int n);
/* Next port, wrapping. Does nothing on a LAN follow. */
void np_host_cycle_port(void);
/* Selects a port by index after a rescan. Clamps into range. No-op if the list is empty. */
void np_host_set_port_i(int i);
/* 8³ occupancy (SMX + live EXG at mapped 10-10 sites). Not the 64-bit pack. */
void np_host_copy_cube(unsigned char dst[512]);
/* EXG RMS µV, same cook as the traces. Inactive channels are 0. */
void np_host_cook_uv(float uv[8]);
/* How many software contrasts to draw. 0 while NEG RAIL is on, because each sample is already V(+)−V(−); otherwise the laterality count or the referential count. */
int np_host_pair_n(void);
/* "A-B" names for software pair i: laterality pairs in pair mode, otherwise the referential contrasts. A short or null buffer returns, and NEG RAIL is not refused here. */
void np_host_pair_label(int i, char *out, int n);
/* Writes the two channel indexes. -1, and both pointers set to -1, while NEG RAIL is on. Otherwise the pair table's result. */
int np_host_pair_chs(int i, int *a, int *b);
/* EXG RMS of A−B, same cook as the traces. 0 if a site is off. */
void np_host_pair_uv(float uv[4]);
/* 1 if the two laterality pairs are selected instead of eight channels. Does not say whether NEG RAIL is on. */
int np_host_pair_mode(void);
/* Nonzero selects the two software pairs (motor FC, visual PO) and saves. Does not change NEG RAIL. The subtraction is still refused while NEG RAIL is on. */
void np_host_set_pair_mode(int on);
/* Samples of channel A minus B over the window, in µV. 0 if max is under 8 or the pair is invalid, which includes every pair while NEG RAIL is on. Detrend runs on the difference when that flag is on. */
int np_host_copy_pair(int p, float *dst, int max);
/* 1 if the last copy of laterality pair p was clipped. 0 if p is outside those two slots. Stale until a copy runs. */
int np_host_pair_clip(int p);
/* Notch setting: -1 AUTO, 0 off, 50 or 60. Not the Hz actually applied. */
int np_host_notch(void);
/* Effective notch Hz (AUTO → plate). 0 if idle. */
int np_host_notch_eff(void);
/* High-pass setting in Hz. 0 is off. */
int np_host_hp(void);
/* 50, then 60, off, AUTO, and back to 50. Saves and retunes. Does not reload plates. */
void np_host_cycle_notch(void);
/* Sets 0, 50, 60, or -1. Anything else becomes 50. Saves and recooks. */
void np_host_set_notch(int hz);
/* 0, 1, 2, 5, 20 Hz, then wraps. An unknown current value becomes 1 and does not save. */
void np_host_cycle_hp(void);
/* Sets 0, 1, 2, 5, or 20 Hz. Anything else becomes 1. Saves and recooks. */
void np_host_set_hp(int hz);
/* Low-pass setting in Hz. 0 is off. */
int np_host_lp(void);
/* 0, 20, 40 Hz, then wraps. An unknown current value becomes 0 and does not save. */
void np_host_cycle_lp(void);
/* Sets 0, 20, or 40 Hz. Anything else becomes 0. Saves and recooks. */
void np_host_set_lp(int hz);
/* 1 only when CAR is switched on and NEG RAIL is off. */
int np_host_car(void);
/* Flips CAR and saves. NEG RAIL forces it off, retunes, and does not reload plates. */
void np_host_toggle_car(void);
/* 1 when each plot window is detrended. */
int np_host_detrend(void);
/* Flips detrend and recooks. Does not rebuild the live IIR poles. */
void np_host_toggle_detrend(void);
/* 1 when the live envelope follower is on. */
int np_host_envelope(void);
/* Flips the envelope, rebuilds the poles, and recooks. */
void np_host_toggle_envelope(void);
/* Preset 0 RAW, 1 LINE, 2 EEG, 3 EMG. May disagree with the knobs. */
int np_host_band(void);
/* 1 when the knobs still match the stored preset. 0 for a custom mix. */
int np_host_band_fit(void);
/* Next preset, wrapping through all four. Saves and recooks. */
void np_host_cycle_band(void);
/* Loads that preset. Out of range becomes RAW. */
void np_host_set_band(int band);
/* 1 if the last wave copy of channel ch was clipped. 0 outside 0..7. Stale until a copy runs. */
int np_host_ch_clip(int ch);

/* Library index of the live algo, after seeding. Out of range reads as 0. */
int np_host_algo(void);
/* Selects the next library entry and saves. Returns without a change if the library is empty. */
void np_host_cycle_algo(void);
/* Selects that library index, makes it the library selection, and saves. An out-of-range id becomes 0. */
void np_host_set_algo(int id);
/* Name of the live algo. Falls back to the built-in name if the slot is empty. */
void np_host_algo_name(char *out, int n);
/* One-line rule for the selected cube bit when any cube exists, otherwise the library selection. A stock source uses its built-in sentence; a -1 inherit does not climb to the cube algo. */
void np_host_algo_rule(char *out, int n);
/* Full source of the library selection, not of a made-cube bit. */
void np_host_algo_src(char *out, int n);
/* Compiles and stores source on the library selection, then saves. -1 if that index is outside the library or the source does not compile. */
int np_host_set_algo_src(const char *s, char *err, int n);
/* Channel bitmask from the live fold. 0 if the board is not connected. Bit 0 is channel 1. */
unsigned int np_host_algo_fold(void);
/* How many algos are in the library, after the built-in eight are seeded. */
int np_host_alib_n(void);
/* Selected library index, after seeding. */
int np_host_alib_sel(void);
/* Selects i and also makes it the live algo. Out of range does nothing and does not save. */
void np_host_alib_set_sel(int i);
/* Name at i. A null out returns. Out of range writes an empty string. */
void np_host_alib_name(int i, char *out, int n);
/* Source at i. An index outside the library returns the compare default. */
void np_host_alib_src(int i, char *out, int n);
/* 1 if i is a built-in slot, 0..7. Those cannot be renamed or deleted. */
int np_host_alib_def(int i);
/* Compiles, stores, and writes the ini. -1 if i is outside the library or the source does not compile, and then the slot is unchanged. */
int np_host_alib_set_src(int i, const char *s, char *err, int n);
/* Compile only. 0 ok. Does not save. */
int np_host_alib_check(const char *s, char *err, int n);
/* Renames a user algo and saves. A built-in index, an empty name, or a bad index returns -1. Characters other than letters, digits, '_' and '-' become '_'. */
int np_host_alib_set_name(int i, const char *s);
/* Appends userN with the default source and saves. Returns the new index, or -1 if the list is already full. */
int np_host_alib_add(void);
/* Removes a user algo and shifts later indexes down, including cube-bit references, then saves. -1 for a built-in or a bad index, and then nothing moves. */
int np_host_alib_del(int i);
/* Restores a built-in name and source, and saves. -1 if i is not a built-in. */
int np_host_alib_reset(int i);
/* Selected cube bit, 0..7. Out of range reads as 0. */
int np_host_made_qsel(void);
/* Selects bit q. Outside 0..7 does nothing. Does not save. */
void np_host_made_set_qsel(int q);
/* Source for that bit. A bit of -1 does not inherit the cube algo; it gets the compare default. */
void np_host_made_src(int cube, int q, char *out, int n);
/* Compiles source onto the bit's own algo index. -1 if the cube or bit is out of range, or the bit inherits (-1) and has no slot. Does not copy the source onto a private algo. */
int np_host_set_made_src(int cube, int q, const char *s, char *err, int n);
/* Effective algo index: the bit, else the cube algo, else 0. Out of range returns the live algo. */
int np_host_made_algo(int cube, int q);
int np_host_made_algo_own(int cube, int q); /* -1 = same as cube */
/* Sets the bit's algo and saves. -1 means inherit the cube. Returns -1 if the cube, the bit, or id (other than -1) is out of range. */
int np_host_made_set_algo(int cube, int q, int id);
/* Common algo on all 8 bits, or -1 if mixed. */
int np_host_made_algo_all(int cube);
/* Sets the cube algo, clears every bit to inherit, selects it as the live algo, and saves. id must be a real library index, not -1. */
int np_host_made_set_algo_all(int cube, int id);
/* Bitmask of the eight cube bits the algos turned on. 0 if the cube index is bad or the board is not connected. Bit 0 is cube bit 1, not channel 1. */
unsigned int np_host_made_fold(int cube);

/* 1 for the 10-10 assign map, 0 for the crimson viz. */
int np_host_cube_view(void);
/* Nonzero selects the assign map, 0 the viz. Saves. */
void np_host_set_cube_view(int map);
/* Adds yaw and pitch in radians. Pitch is clamped to -0.35..1.20. Does not save. */
void np_host_cube_spin(float dyaw, float dpitch);
/* Steps zoom by 0.2 in the sign of dir, clamps to 0.70..2.80, and saves. */
void np_host_cube_zoom(int dir);
/* Zoom factor after clamp, from 0.70 to 2.80. */
float np_host_cube_zoom_get(void);
/* Sets zoom, clamps it to 0.70..2.80, and saves. */
void np_host_set_cube_zoom(float z);
/* Yaw π, pitch 0.25, zoom 1, so +z and Fp face the camera. Saves. Not the startup pose. */
void np_host_cube_front(void);
/* 1 if the cube window is floating. */
int np_host_cube_float(void);
/* Flips the floating cube window and saves. */
void np_host_toggle_cube_float(void);
/* How many cubes this board can hold: channel count divided by 8. */
int np_host_made_max(void);
/* How many cubes exist now, 0..4. */
int np_host_made_n(void);
/* Selected cube index. May read 0 when none exist. */
int np_host_made_sel(void);
/* Selects cube i if it exists. Out of range does nothing. Does not save. */
void np_host_made_set_sel(int i);
/* Appends a cube on free channels, bits inheriting the live algo, and saves. Returns the index, or -1 if already at the max, also capped at 4. */
int np_host_made_add(void);
/* Removes cube i, shifts the rest down, and saves. -1 if i is out of range. */
int np_host_made_del(int i);
/* Channel number 1..8 on that bit, or 0 if the bit is empty or the index is out of range. */
int np_host_made_ch(int cube, int q);
/* Assigns channel ch, where 0 clears the bit, and saves. -1 if out of range or that channel is already on another cube. */
int np_host_made_set_ch(int cube, int q, int ch);
/* Writes 0..255 color. A bad cube index writes the default crimson 255, 20, 40 into any non-null pointer. */
void np_host_made_rgb(int cube, int *r, int *g, int *b);
/* Stores each channel clamped to 0..255 and saves. A bad cube index returns without writing. */
void np_host_made_set_rgb(int cube, int r, int g, int b);
/* Channel index the next site tap writes. Not clamped here. */
int np_host_elec_sel(void);
/* Selects channel ch. Out of range returns. Moves focus to the minus site if minus-picking is on and that site is set, else the plus site. */
void np_host_set_elec_sel(int ch);
/* "N name", or on NEG RAIL with a minus site, "N plus-minus". Out of range writes an empty string. */
void np_host_elec_label(int ch, char *out, int n);
/* Plus-site name, or NONE. A short or null buffer returns. Out of range writes an empty string. */
void np_host_elec_name(int ch, char *out, int n);
/* 10-10 index of the plus site, or -1 if none or ch is outside 0..7. Not the minus site. */
int np_host_elec_site(int ch);
/* World position of the plus site's cube cell. A bad channel returns without writing. */
void np_host_elec_xyz(int ch, float *x, float *y, float *z);
/* 10-10 index the map keys move. Not clamped here. */
int np_host_site_focus(void);
/* Moves focus by dir names, wrapping the 10-10 list. Does not assign the site. */
void np_host_site_step(int dir);
/* Assigns site to the selected channel's minus end if minus-picking is on, otherwise the plus end. -1 clears that end and does not change NEG RAIL. */
void np_host_assign_site(int site);
/* How many 10-10 names exist. */
int np_host_site_n(void);
/* 10-10 name at i, or empty if i is outside the list. */
void np_host_site_name(int i, char *out, int n);
/* 1 if this 10-10 name is marked core. 0 if i is outside the list. */
int np_host_site_core(int i);
/* Channel 0..7 whose plus site is i, or -1 if none. Does not look at minus sites. */
int np_host_site_ch(int i);
/* Flat map position, table units divided by 10. A bad index writes 0, 0. */
void np_host_site_flat(int i, float *fx, float *fy);
/* World position of that site's cube cell. */
void np_host_site_xyz(int i, float *x, float *y, float *z);
/* Cube cell indexes. -1 if i is outside the 10-10 list, and the pointers then get 3, 7, 3. */
int np_host_site_ijk(int i, int *x, int *y, int *z);
/* Packed viz cells: xyz[n*3], size[n], rgba[n]. Returns n (≤40). */
int np_host_viz_cells(float *xyz, float *size, int *rgba, int cap);
/* SMX sequence counter. Not a channel fold. */
unsigned int np_host_smx_seq(void);
/* Latest SMX row as a channel bitmask. 0 if no row has been stored. Bit 0 is channel 1. */
unsigned int np_host_smx_fold(void);
/* Writes a profile-shaped ini to path: no electrode map and no API block. 0 on success, -1 if path is empty or the file cannot be opened. */
int np_host_prof_export(const char *path);
/* Reads a profile file, puts the electrode map back, recooks plates and takes from raw, and writes exg-c.ini. -1 if the file cannot be opened, and then nothing else changes. */
int np_host_prof_import(const char *path);
/* Bytes of kit text plus each profile already in the list, or 0 if cap is under 64 or the scratch file cannot be written. Does not rescan the profile directory. */
int np_host_kit_export(char *out, int cap);
/* Applies the kit, including the electrode map, writes each #profile block to its own file, recooks from raw, and writes exg-c.ini. -1 if the text is under 8 bytes or the kit cannot be read. */
int np_host_kit_import(const char *s, int n);

/* ID line for the UI. Before the host is ready, writes "ID —". A short or null buffer returns without writing. */
void np_host_id(char *out, int n);
/* Milliseconds left in the armed 4 s record. 0 if nothing is armed or the window has already elapsed. */
int np_host_rec_ms(void);

/* 1 while a CSV file is open. */
int np_host_csv(void);
/* Opens or closes the timestamped CSV. Same rules as the on-screen record control. */
void np_host_toggle_csv(void);
/* Starts a CSV at path, replacing an open one. -1 if disconnected, path is empty, or the file cannot be created. */
int np_host_csv_begin(const char *path);
/* Starts a CSV on fd. Closes fd on failure, including when not connected. -1 if fd is bad or fdopen fails. */
int np_host_csv_begin_fd(int fd, const char *name);
/* 128-pt strip FFT, 64 bins. Fills dst[0..n), writes peak Hz. */
int np_host_fft(float *dst, int max, int *peak_hz);

/* Last IMU sample, ring units. 0 if this board sent no IMU. */
int np_host_imu(float acc[3], float gyr[3], float mag[3]);
/* 1 when the live board is the IMU frame. The preference alone does not count. */
int np_host_board_imu(void);
/* Preferred frame: auto, EXG, or IMU. Not the detected live board. */
int np_host_board_mode(void);
/* Auto, then IMU, then EXG, then Auto. Refuses while connected, same as set. */
void np_host_cycle_board(void);
/* Preference only: IMU frame if imu is non-zero, else plain EXG. Not auto. Refuses while connected. */
void np_host_set_board_imu(int imu);
/* Stores the frame preference and saves. Refuses while connected. Unknown mode returns. */
void np_host_set_board_mode(int mode);
/* USB exgmode_N. The connected Knight restarts into that rate. No bootloader. */
void np_host_stream_mode(int mode);
/* Short board label, with the snapped SPS when connected. Returns if out is NULL or n < 1. */
void np_host_mode_label(char *out, int n);
/* How many firmware presets exist. */
int np_host_fw_count(void);
/* Preset name for that mode. Truncates to n. */
void np_host_fw_label(int mode, char *out, int n);
/* Next firmware mode, wrapping. Stores and saves it. Does not write the board. */
void np_host_cycle_fw(void);
/* Short label of the saved mode, into out. */
void np_host_fw_button(char *out, int n);
/* Firmware version this build expects. Not what the board last reported. */
int np_host_fw_need(void);
/* Last stored version. 0 means never saved, not firmware 0. */
int np_host_fw_have(void);
/* Version parsed from this boot's banner. 0 if that line has not arrived. */
int np_host_fw_seen(void);
/* 1 when a USB Knight looks older than the version this build expects. 0 on LAN, while disconnected, or before a banner verdict. */
int np_host_fw_behind(void);
/* Saved stream mode, 0..2. An out-of-range value reads as 0. */
int np_host_fw_mode(void);
/* Stores the mode and saves. Does not write the board. Out of range returns. */
void np_host_set_fw_mode(int mode);
/* 1 once per launch after the connected board is behind. Never with no cable. */
int np_host_fw_prompt(void);
/* which 0 = flasher, 1 = serial debug. */
void np_host_log_copy(int which, char *out, int n);
/* "idle|arm|run|ok|err\\nmessage". ok and err stay until the next attempt. */
void np_host_flash_state(char *out, int n);
/* App cache directory. Flash lines are appended to flash.log there and to logcat. */
void np_host_set_temp_dir(const char *dir);
/* Held rate in SPS: snap 125/200/250/500, else a measured 100–160, else 125. */
int np_host_design_sps(void);
/* 1 when frames arrive but stay under 64% of the design rate, and never under 80 SPS. */
int np_host_stream_cold(void);
/* Two taps. The second tap within 8 s uploads the one knight.hex.
 * It does not write the mode byte. confirmed 1 starts immediately. */
void np_host_flash_upload(void);
/* Arms a full hex upload of mode. confirmed non-zero skips the second tap. */
void np_host_flash_preset(int mode, int confirmed);
/* EEPROM byte only. Refuses while the image version is behind. */
void np_host_flash_mode_only(int mode, int confirmed);
/* UI scale in tenths. Stored values outside 8..22 read as 15. Drawing still treats anything other than 10, 15, or 20 as 15. */
int np_host_ui_scale(void);
/* Steps through 15, then 20, then 10, and saves. A value under 15 jumps straight to 15. */
void np_host_cycle_ui_scale(void);
/* Stores tenths, saves, and turns anything outside 8..22 into 15. Drawing still treats a value other than 10, 15, or 20 as 15. */
void np_host_set_ui_scale(int tenths);

/* CubalC atom fold — 8 bytes / second, not CSV. */
/* Starts the take timer, or stops it. Stopping does not save. */
void np_host_toggle_atom(void);
/* Starts counting take seconds from 0. Does not write a file until a later save. */
void np_host_atom_start(void);
/* Stops the take and returns how many seconds were counted. 0 if none. Does not write a file. */
int np_host_atom_stop(void);
/* 1 while a take is being counted. */
int np_host_atom(void);
/* Seconds counted in the running take, or 0 if it is not running. */
int np_host_atom_n(void);
/* Writes name.npat and the raw atom file from the typed name, and stamps the current NEG RAIL bit. -1 if the name is empty or there are no seconds. */
int np_host_atom_save(void);
/* Loads name.npat as the reference chain for unity, without raw or a recook. -1 if the name is empty or the file has no seconds. */
int np_host_atom_load(void);
/* Bit agreement of the live ring with the loaded take, 0..1. 0 if either side is empty. */
float np_host_atom_unity(void);
/* One status sentence, or empty if nothing is recording and no take is picked. A negative pair score reads as a different montage, and a short or null buffer returns. */
void np_host_atom_line(char *out, int n);
/* Name of the loaded take, or empty. A short or null buffer returns. */
void np_host_atom_ref(char *out, int n);
/* Rescans *.npat and returns how many names were kept. */
int np_host_atom_count(void);
/* Name at i from the last rescan. Does not rescan itself. Out of range writes an empty string. */
void np_host_atom_at(int i, char *out, int n);
/* Seconds stored in take i, read from the file. 0 if i is out of range or the path is bad. */
int np_host_atom_secs(int i);
/* Copies that take's name and loads it as the reference. -1 if i is out of range. Otherwise the load result. */
int np_host_atom_select(int i);
/* Deletes the .npat and the raw file. Clears the reference or a compare slot if it was that name. Out of range returns without deleting. */
void np_host_atom_del(int i);
/* Stops a running take and drops the second count. Does not delete a saved file. */
void np_host_atom_discard(void);
/* First tap sets compare slot A and loads it; a second tap sets B and scores the pair, and tapping A again clears both. A montage mismatch scores below 0, and out of range returns. */
void np_host_atom_pick(int i);
/* Compare sentence for the two slots. With nothing picked, says to tap two takes. A short or null buffer returns. */
void np_host_atom_pair(char *out, int n);
/* Name in compare slot A, or empty. A null buffer or n under 2 writes nothing. */
void np_host_atom_slot_a(char *out, int n);
/* Name in compare slot B, or empty. A null buffer or n under 2 writes nothing. */
void np_host_atom_slot_b(char *out, int n);
/* Winning take index while MATCH is on, or -1. Not a pose index. */
int np_host_atom_id_best(void);
/* Closeness of take i to the last second, 0..1, or 0 if i is outside 0..31. Wiped to 0 when no take won. Not a percent. */
float np_host_atom_id_score(int i);
/* "now" plus the winning take name while MATCH is on. Empty if MATCH is off, or "now —" if MATCH is on, the list is non-empty, and nothing won. */
void np_host_atom_id_line(char *out, int n);

/* 1 if the API server is on. It stays 0 until something turns it on. */
int np_host_api_on(void);
/* Turns the server on or off and saves. Refuses to turn on when HTTP, UDP, and TCP are all 0. A failed bind leaves the flag on and sets status. */
void np_host_api_set_on(int on);
/* 1 if the bind is all interfaces, 0 if it is loopback. */
int np_host_api_lan(void);
/* Nonzero binds all interfaces, 0 binds loopback. Saves and rebinds. Does not by itself require a token. */
void np_host_api_set_lan(int lan);
/* Publish rate in Hz. A stored value under 1 reads as 125. */
int np_host_api_hz(void);
/* Clamps the publish rate to 1..500 Hz, saves, and rebinds. */
void np_host_api_set_hz(int hz);
/* HTTP port. 0 means that listener is off. */
int np_host_api_http(void);
/* Sets the HTTP port and saves. Outside 0..65535 becomes 8765, a clash with UDP or TCP keeps the old port, and 0 is allowed. */
void np_host_api_set_http(int port);
/* UDP port. 0 means that listener is off. */
int np_host_api_udp(void);
/* Sets the UDP port and saves. Outside 0..65535 becomes 8766. A clash with HTTP or TCP is refused and the old port stays. */
void np_host_api_set_udp(int port);
/* TCP port for 68-byte EXG1 frames. 0 means that listener is off. */
int np_host_api_tcp(void);
/* Sets the TCP port and saves. Outside 0..65535 becomes 8767. A clash with HTTP or UDP is refused and the old port stays. */
void np_host_api_set_tcp(int port);
/* Shared secret, or empty. Loopback GET does not need one. A null or empty buffer returns. */
void np_host_api_token(char *out, int n);
/* Stores the secret with spaces and newlines removed, saves, and rebinds. Empty is legal. Loopback GET still does not check it. */
void np_host_api_set_token(const char *s);
/* Extra send address, or empty. A null or empty buffer returns. */
void np_host_api_push(char *out, int n);
/* host:port for an extra send, saved. A bad address is cleared and still saved, and empty turns the extra send off. */
void np_host_api_set_push(const char *s);
/* One status line from the API server, into out. */
void np_host_api_line(char *out, int n);

/* 1 if the plot is following LAN, 0 if it is on USB. */
int np_host_link(void);
void np_host_set_link(int path); /* 0 USB  1 LAN */
/* Local peer name. Spaces become underscores, other punctuation is dropped, and an empty name becomes "exg". Does not save. */
void np_host_set_self(const char *s);
/* Flips USB and LAN. A live board is disconnected on the way either direction. */
void np_host_cycle_link(void);
/* Follow address, host or host:port, or empty. A short or null buffer returns. */
void np_host_link_dest(char *out, int n);
/* Sets the follow address and saves. A bt: prefix is stored as empty, a string that is not host or host:port keeps the old address, and a changed address clears the link token. */
void np_host_set_link_dest(const char *s);
/* Token sent when following, or empty. A short or null buffer returns. */
void np_host_link_token(char *out, int n);
/* Stores the follow token and saves. Does not check that the peer will accept it. A null pointer clears it. */
void np_host_set_link_token(const char *s);

/* How many remembered follow peers are stored. */
int np_host_follow_n(void);
/* Follow peer name at i, or empty. A short or null buffer returns. */
void np_host_follow_name(int i, char *out, int n);
/* Follow address at i, or empty. A short or null buffer returns. */
void np_host_follow_dest(int i, char *out, int n);
/* Copies that peer's address and grant into the live follow and forces LAN on. Does not disconnect a board that is still open. A blank or bt: address returns without saving. */
void np_host_follow_use(int i);
/* Removes follow peer i and writes the peer file. A bad index does not write. */
void np_host_follow_del(int i);
/* How many peers this device has allowed to pull EXG. */
int np_host_allow_n(void);
/* Allowed peer name at i, or empty. A short or null buffer returns. */
void np_host_allow_name(int i, char *out, int n);
/* Removes allow entry i and writes the peer file. A bad index does not write. */
void np_host_allow_del(int i);
/* Adds or updates a follow peer, copies dest and grant into the live link, and writes both the peer file and the ini. */
void np_host_follow_remember(const char *name, const char *dest, const char *grant);
/* Grant stored for that follow name, or empty if the name is unknown. A short, null, or null-name buffer returns. */
void np_host_follow_grant(const char *name, char *out, int n);
/* 1 if this grant is on the allow list. 0 if it is empty or unknown. */
int np_host_grant_ok(const char *grant);

/* Incoming EXG request. 0 idle, 1 waiting, 2 allow, 3 no. Name starts a wait. */
int np_host_pair_ask(const char *name, char *grant, int gn);
/* Starts a pair ask. 2 if that name is already allowed and the grant is ready, 1 if the user must answer, and an empty name asks as "exg". */
int np_host_pair_begin(const char *name);
/* 0 idle, 1 waiting, 2 allowed, 3 refused. A wait older than 60 s becomes refused and the grant is cleared. */
int np_host_pair_state(void);
/* Name of the peer in the current ask, or empty. A short or null buffer returns. */
void np_host_pair_name(char *out, int n);
/* If a request is waiting, mints a grant, stores the allow, and writes the peer file. The status line says allowed even when nothing was waiting. */
void np_host_pair_accept(void);
/* If a request is waiting, state becomes refused and the grant is cleared. The status line says refused even when nothing was waiting. */
void np_host_pair_reject(void);
/* Grant from the current ask, or empty. A short or null buffer returns. */
void np_host_pair_grant(char *out, int n);

/* Cooked EXG1 (LAN UDP). */
/* Copies the latest 68-byte EXG1 frame. 0 if there is no sample or cap is under 68. */
int np_host_copy_exg1(unsigned char *dst, int cap);
/* Feeds one 68-byte EXG1 frame into the LAN follow path. -1 if it is not an EXG1 frame, and 0 if it was accepted. */
int np_host_feed_exg1(const unsigned char *raw, int n);
/* Applies view JSON for filters, scale in µV, window seconds, colors, sites, and active flags, and returns on empty text. */
void np_host_apply_cfg_json(const char *js);
/* Writes a JSON object body, without braces, of the view: filters, scale in µV, window in seconds, colors, sites, NEG RAIL, and the ID string. A buffer under 8 bytes returns without writing. */
void np_host_view_json(char *out, int n);
/* Does nothing. The argument is ignored, and no socket is opened or closed. */
void np_host_link_wire(int on);

#endif
