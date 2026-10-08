#ifndef NP_RATE_H
#define NP_RATE_H

/* Snap a measured rate to a Knight clock or the 115200 IMU ceiling.
 * 125, 250, and 500 are chip rates. 200 is what the USB link delivers
 * when a 57-byte IMU frame is asked to go faster than 125.
 * Returns 0 when hz is not near one of those. */
int np_rate_snap(float hz);

/* Boot banner written by the Knight before the first 0xA0.
 * 125, 250, or 500 from "EEG 500 SPS". -1 from "EEG rate unset".
 * 0 when the line is not a rate. */
int np_banner_sps(const char *line);

/* "EXG-FW 1" from the sketch. 0 when the line is not a version. */
int np_fw_version_line(const char *line);

/* "EXG-MODE N" or "EXG-SWITCH N". -1 when the line is not a mode. */
int np_fw_mode_line(const char *line);

/* Channel ladder. boot counts reboots (EXG-FW, or a reset this process
 * requested). armed is the generation already configured. fresh stays 1
 * until the first decision. A USB open with boot == armed leaves channels
 * alone, except the first decision when this install has never seen a banner. */
struct np_ladder {
    unsigned boot;
    unsigned armed;
    int fresh;
};

/* Add one to boot. A null pointer does nothing. */
void np_ladder_reboot(struct np_ladder *l);
/* 1 when boot differs from armed, or when fresh is set and fw_have is 0 or less. A null ladder returns 0. fw_have at or below 0 means no banner. */
int np_ladder_due(const struct np_ladder *l, int fw_have);
/* Set armed to the current boot and clear fresh. A null pointer does nothing. */
void np_ladder_mark(struct np_ladder *l);

#endif
