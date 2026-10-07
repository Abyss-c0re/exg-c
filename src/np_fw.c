#include "np_fw.h"

#include "np_stk500.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const k_name[] = {
    "125 + IMU",
    "250 EEG",
    "500 EEG",
};

static const char *const k_blurb[] = {
    "full 125, motion if the IMU answers",
    "EEG only, full 250 SPS",
    "EEG only, 500 SPS, little USB margin",
};

int np_fw_count(void)
{
    return 3;
}

const char *np_fw_file(int mode)
{
    (void)mode;
    return "knight.hex";
}

void np_fw_label(int mode, char *out, int n)
{
    if (!out || n < 1) {
        return;
    }
    if (mode < 0 || mode >= np_fw_count()) {
        out[0] = 0;
        return;
    }
    snprintf(out, (size_t)n, "%s — %s", k_name[mode], k_blurb[mode]);
}

void np_fw_short(int mode, char *out, int n)
{
    if (!out || n < 1) {
        return;
    }
    if (mode < 0 || mode >= np_fw_count()) {
        out[0] = 0;
        return;
    }
    snprintf(out, (size_t)n, "%s", k_name[mode]);
}

int np_fw_load(int mode, const char *root, unsigned char *dst, int cap, int *out_len,
               char *err, int err_n)
{
    char path[512];
    FILE *f;
    long sz;
    char *text = NULL;
    int rc;

    if (err && err_n > 0) {
        err[0] = 0;
    }
    if (mode < 0 || mode >= np_fw_count() || !dst || cap < NP_STK_APP_MAX || !out_len) {
        if (err && err_n > 0) {
            snprintf(err, (size_t)err_n, "bad firmware slot");
        }
        return -1;
    }
    if (!root || !root[0]) {
        if (err && err_n > 0) {
            snprintf(err, (size_t)err_n, "no config folder");
        }
        return -1;
    }
    snprintf(path, sizeof(path), "%s/exg-c/firmware/%s", root, np_fw_file(mode));
    f = fopen(path, "rb");
    if (!f) {
        if (err && err_n > 0) {
            snprintf(err, (size_t)err_n, "missing %s", path);
        }
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        if (err && err_n > 0) {
            snprintf(err, (size_t)err_n, "could not read %s", path);
        }
        return -1;
    }
    sz = ftell(f);
    if (sz < 11 || sz > 200000) {
        fclose(f);
        if (err && err_n > 0) {
            snprintf(err, (size_t)err_n, "hex size looks wrong");
        }
        return -1;
    }
    rewind(f);
    text = (char *)malloc((size_t)sz + 1u);
    if (!text || fread(text, 1, (size_t)sz, f) != (size_t)sz) {
        free(text);
        fclose(f);
        if (err && err_n > 0) {
            snprintf(err, (size_t)err_n, "could not read %s", path);
        }
        return -1;
    }
    text[sz] = 0;
    fclose(f);
    rc = np_ihex_decode(text, dst, cap, out_len, err, err_n);
    free(text);
    return rc;
}
