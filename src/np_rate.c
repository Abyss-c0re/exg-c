#include "np_rate.h"

#include <stdio.h>
#include <string.h>

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
