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

#endif
