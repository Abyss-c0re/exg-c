#include "np_rate.h"

#include <stdio.h>
#include <string.h>

/* Knight rate snap and boot-banner parsers.
 * Ladder counts reboots so a quiet reopen does not retune channels. */

/* Snap a measured rate to 125, 200, 250, or 500. 200 means the USB link is full.
 * Returns 0 below 100 Hz, from 340 Hz through 419 Hz, and above 560 Hz. */
int np_rate_snap(float hz)
{
    if (hz >= 100.f && hz < 170.f) {
        return 125;
    }
    if (hz >= 170.f && hz < 225.f) {
        return 200;
    }
    if (hz >= 225.f && hz < 340.f) {
        return 250;
    }
    if (hz >= 420.f && hz <= 560.f) {
        return 500;
    }
    return 0;
}

/* Read 125, 250, or 500 from an "EEG N" banner. The substring unset returns -1.
 * Empty, no EEG token, or any other number, including 200, returns 0. */
int np_banner_sps(const char *line)
{
    const char *p;
    int n = 0;

    if (!line || !line[0]) {
        return 0;
    }
    if (strstr(line, "unset")) {
        return -1;
    }
    p = strstr(line, "EEG");
    if (!p) {
        return 0;
    }
    if (sscanf(p, "EEG %d", &n) != 1) {
        return 0;
    }
    if (n == 125 || n == 250 || n == 500) {
        return n;
    }
    return 0;
}

/* Parse "EXG-FW N" and return N when it is 1..9999. Any other line returns 0. */
int np_fw_version_line(const char *line)
{
    int v = 0;

    if (!line || sscanf(line, "EXG-FW %d", &v) != 1) {
        return 0;
    }
    if (v < 1 || v > 9999) {
        return 0;
    }
    return v;
}

/* Parse "EXG-MODE N" or "EXG-SWITCH N". N must be 0, 1, or 2.
 * A null line, a bad scan, or any other mode returns -1. */
int np_fw_mode_line(const char *line)
{
    int m = -1;

    if (!line) {
        return -1;
    }
    if (sscanf(line, "EXG-MODE %d", &m) != 1 && sscanf(line, "EXG-SWITCH %d", &m) != 1) {
        return -1;
    }
    if (m < 0 || m > 2) {
        return -1;
    }
    return m;
}

/* Add one to boot. A null pointer does nothing. */
void np_ladder_reboot(struct np_ladder *l)
{
    if (!l) {
        return;
    }
    l->boot++;
}

/* 1 when boot differs from armed, or when fresh is set and fw_have is 0 or less.
 * A null ladder returns 0. fw_have at or below 0 means no banner. */
int np_ladder_due(const struct np_ladder *l, int fw_have)
{
    if (!l) {
        return 0;
    }
    if (l->boot != l->armed) {
        return 1;
    }
    if (l->fresh && fw_have <= 0) {
        return 1;
    }
    return 0;
}

/* Set armed to the current boot and clear fresh. A null pointer does nothing. */
void np_ladder_mark(struct np_ladder *l)
{
    if (!l) {
        return;
    }
    l->armed = l->boot;
    l->fresh = 0;
}
