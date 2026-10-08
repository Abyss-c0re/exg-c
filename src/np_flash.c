#include "np_local.h"
#include "np_mods.h"

/* Knight flash. A detached thread owns the UART for STK500.
 * The UI tick only arms, reads phase, and copies the log. */

void debug_log_add(const char *s);
static void flash_file_add(const char *s);

/* 1 only while the flash thread may disconnect or reconnect.
 * Other callers must not open the port. */
int flash_owner;

struct np_log_ring flash_log_ring;
struct np_log_ring debug_log_ring;
pthread_mutex_t log_mu = PTHREAD_MUTEX_INITIALIZER;

/* Appends one line into an NP_LOG_N ring under log_mu. NULL or empty returns;
 * a full ring overwrites the oldest line. */
static void log_add(struct np_log_ring *r, const char *s)
{
    int i;
    if (!r || !s || !s[0]) {
        return;
    }
    pthread_mutex_lock(&log_mu);
    i = r->n % NP_LOG_N;
    snprintf(r->line[i], NP_LOG_L, "%s", s);
    r->n++;
    pthread_mutex_unlock(&log_mu);
}

/* Serial text into the debug ring and logcat. Does not strip grants. */
void debug_log_add(const char *s)
{
    log_add(&debug_log_ring, s);
    if (s && s[0]) {
        NP_ALOG("serial: %s", s);
    }
}

/* Share grants and lock words stay out of the flash log and logcat. */
static int flash_line_public(const char *s)
{
    if (!s || !s[0]) {
        return 0;
    }
    if (strstr(s, "grant") || strstr(s, "token") || strstr(s, "pairing")) {
        return 0;
    }
    return 1;
}

/* Flash ring, flash.log, and logcat. Drops a line the public filter rejects. */
static void flash_log_add(const char *s)
{
    if (!flash_line_public(s)) {
        return;
    }
    log_add(&flash_log_ring, s);
    flash_file_add(s);
    NP_ALOG("flash: %s", s);
}

/* 0 idle, 1 armed, 2 flashing, 3 flashed, 4 failed. Stays until the next attempt. */
int flash_phase;
char flash_phase_line[160];
pthread_mutex_t phase_mu = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t flash_file_mu = PTHREAD_MUTEX_INITIALIZER;

/* Appends one local-time line. Returns if path cannot be opened. Does not flush. */
static void flash_file_write(const char *path, const char *s)
{
    FILE *f;
    time_t now;
    struct tm tm;

    f = fopen(path, "a");
    if (!f) {
        return;
    }
    now = time(NULL);
    if (localtime_r(&now, &tm)) {
        fprintf(f, "%02d:%02d:%02d %s\n", tm.tm_hour, tm.tm_min, tm.tm_sec, s);
    } else {
        fprintf(f, "%s\n", s);
    }
    fclose(f);
}

/* Appends to <root>/exg-c/flash.log and, when set, <temp>/flash.log. Holds flash_file_mu. */
static void flash_file_add(const char *s)
{
    char root[NP_MAX_PATH];
    char path[NP_MAX_PATH];
    char temp[NP_MAX_PATH];

    if (!flash_line_public(s)) {
        return;
    }
    pthread_mutex_lock(&flash_file_mu);
    np_cfg_root(root, sizeof(root));
    snprintf(path, sizeof(path), "%s/exg-c/flash.log", root);
    flash_file_write(path, s);
    if (flash_temp_dir[0]) {
        snprintf(temp, sizeof(temp), "%s/flash.log", flash_temp_dir);
        flash_file_write(temp, s);
    }
    pthread_mutex_unlock(&flash_file_mu);
}

/* Stores phase and the line, then logs it. Phase 4 marks status as a fault.
 * line may be NULL. */
void flash_phase_set(int phase, const char *line)
{
    char copy[160];

    snprintf(copy, sizeof(copy), "%s", line ? line : "");
    pthread_mutex_lock(&phase_mu);
    flash_phase = phase;
    snprintf(flash_phase_line, sizeof(flash_phase_line), "%s", copy);
    pthread_mutex_unlock(&phase_mu);
    flash_log_add(copy);
    set_status(phase == 4 ? 0 : 1, "%s", copy);
}

/* Formats one flash line and sets status. ok 0 is a fault. Does not change phase. */
static void flash_say(int ok, const char *fmt, ...)
{
    char b[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    flash_log_add(b);
    set_status(ok, "%s", b);
}

/* Settings preset. A full-image upload does not change it. */
int fw_sel;
/* Armed mode key, or -1. A second tap of the same key within 8 s confirms. */
static int flash_arm = -1;
static uint32_t flash_arm_ms;
static int flash_eeprom_only;

struct flash_port {
    int fd;
};

/* STK500 write hook. n is bytes. The count may be short of n. */
static int flash_write(void *ctx, const unsigned char *buf, int n)
{
    struct flash_port *p = (struct flash_port *)ctx;
    return np_serial_write(p->fd, buf, n);
}

/* STK500 read hook. Waits in slices until n bytes or timeout_ms.
 * Returns the count, or -1 on a serial error. */
static int flash_read(void *ctx, unsigned char *buf, int n, int timeout_ms)
{
    struct flash_port *p = (struct flash_port *)ctx;
    int got = 0;
    int spent = 0;

    if (timeout_ms < 1) {
        timeout_ms = 1;
    }
    while (got < n && spent < timeout_ms) {
        int slice = timeout_ms - spent;
        int r;
        uint32_t t0;
        uint32_t dt;
        if (slice > 20) {
            slice = 20;
        }
        if (slice < 1) {
            slice = 1;
        }
        t0 = SDL_GetTicks();
        r = np_serial_read_wait(p->fd, buf + got, n - got, slice);
        if (r < 0) {
            return -1;
        }
        if (r > 0) {
            got += r;
            continue;
        }
        /* FTDI answers an idle UART with a 2-byte status packet, so the
         * read can return before the slice is over. Count real time, or
         * the bootloader's one-second window is a handful of empty polls. */
        dt = SDL_GetTicks() - t0;
#ifdef __ANDROID__
        if (dt < (uint32_t)slice) {
            usleep(((uint32_t)slice - dt) * 1000u);
            dt = (uint32_t)slice;
        }
#else
        if (dt < 2) {
            usleep(2000);
            dt = 2;
        }
#endif
        if (dt < 1) {
            dt = 1;
        }
        spent += (int)dt;
    }
    return got;
}

/* Pulses DTR on the flash port. The bootloader treats that as reset. */
static void flash_dtr(void *ctx)
{
    struct flash_port *p = (struct flash_port *)ctx;
    np_serial_pulse_dtr(p->fd);
}

/* Sets the flash-port baud. Passes through the serial result, which may be failure. */
static int flash_baud(void *ctx, int baud)
{
    struct flash_port *p = (struct flash_port *)ctx;
    return np_serial_set_baud(p->fd, baud);
}

/* STK500 progress text. ctx is unused. Goes through the public flash log. */
static void flash_note(void *ctx, const char *line)
{
    (void)ctx;
    flash_log_add(line);
}

/* Detached. Disconnects, programs knight.hex or one EEPROM mode byte, then
 * reconnects. arg outside 0..2 is treated as 0. Clears g.flashing on the way out. */
static void *flash_thread(void *arg)
{
    int mode = (int)(intptr_t)arg;
    int eeprom_only = flash_eeprom_only;
    char err[200];
    char root[NP_MAX_PATH];
    char dir[NP_MAX_PATH];
    char path[NP_MAX_PATH];
    unsigned char *image = NULL;
    int len = 0;
    int fd = -1;
    struct flash_port port;
    struct np_stk_io io;

    if (mode < 0 || mode > 2) {
        mode = 0;
    }
    flash_owner = 1;
    if (g.connected) {
        do_disconnect();
    }
    flash_owner = 0;
    flash_phase_set(2, eeprom_only ? "FLASHING mode byte" : "FLASHING knight.hex");
    if (g.link == 1) {
        flash_phase_set(4, "FAILED — flash needs the USB cable");
        goto done;
    }
    g.nports = np_list_ports(g.ports, NP_MAX_PORTS);
    if (g.nports <= 0) {
        flash_phase_set(4, "FAILED — no Knight port");
        goto done;
    }
    if (g.port_i < 0 || g.port_i >= g.nports) {
        g.port_i = 0;
    }
    snprintf(path, sizeof(path), "%s", g.ports[g.port_i]);
    flash_log_add(path);
    np_cfg_root(root, sizeof(root));
    firmware_dir(dir, (int)sizeof(dir));
    (void)dir;
    if (!eeprom_only) {
        image = (unsigned char *)malloc((size_t)NP_STK_APP_MAX);
        if (!image) {
            flash_phase_set(4, "FAILED — out of memory");
            goto done;
        }
        if (np_fw_load(mode, root, image, NP_STK_APP_MAX, &len, err, (int)sizeof(err)) != 0) {
            flash_phase_set(4, err[0] ? err : "FAILED — no firmware image");
            goto done;
        }
        flash_say(1, "image %d bytes", len);
    }
    fd = np_serial_open(path);
    if (fd < 0) {
        flash_phase_set(4, "FAILED — could not open the Knight");
        goto done;
    }
    np_serial_flush(fd);
    memset(&io, 0, sizeof(io));
    port.fd = fd;
    io.ctx = &port;
    io.write = flash_write;
    io.read = flash_read;
    io.pulse_dtr = flash_dtr;
    io.set_baud = flash_baud;
    io.note = flash_note;
    if (np_stk_program_ex(&io, image, len, eeprom_only ? mode : -1, err, (int)sizeof(err)) != 0) {
        char fail[160];
        snprintf(fail, sizeof(fail), "FAILED — %s", err[0] ? err : "bootloader");
        flash_phase_set(4, fail);
        np_serial_close(fd);
        fd = -1;
        goto done;
    }
    np_serial_close(fd);
    fd = -1;
    if (eeprom_only) {
        g.fw_mode = mode;
    }
    if (!eeprom_only) {
        g.fw_have = EXG_FW_NEED;
    }
    cfg_save();
    flash_phase_set(3, "FLASHED");
    flash_owner = 1;
    do_connect();
    flash_owner = 0;
    /* Reconnect status must not replace the FLASHED banner. */
    flash_log_add(g.connected ? "FLASHED — USB open again" : "FLASHED — USB did not reopen");
done:
    if (fd >= 0) {
        np_serial_close(fd);
    }
    free(image);
    flash_owner = 0;
    g.flashing = 0;
    return NULL;
}

/* Without confirmed, the first call within 8 s only arms and returns 0. Otherwise
 * starts the thread and returns 1, or 0 if mode is bad or a flash is already running. */
int flash_arm_start(int mode, int confirmed, int eeprom_only, const char *again)
{
    pthread_t thr;
    char label[80];
    uint32_t now;
    int arm_key;

    if (mode < 0 || mode >= np_fw_count()) {
        return 0;
    }
    if (g.flashing) {
        flash_phase_set(4, "FAILED — flash already running");
        return 0;
    }
    np_host_fw_label(mode, label, (int)sizeof(label));
    now = SDL_GetTicks();
    arm_key = mode + (eeprom_only ? 16 : 0);
    if (!confirmed) {
        if (flash_arm == arm_key && now - flash_arm_ms < 8000u) {
            confirmed = 1;
            flash_arm = -1;
        } else {
            flash_arm = arm_key;
            flash_arm_ms = now ? now : 1;
            /* Upload does not change the Settings mode. Write-mode does. */
            if (eeprom_only) {
                fw_sel = mode;
                g.fw_mode = mode;
            }
            flash_phase_set(1, again);
            return 0;
        }
    }
    if (eeprom_only) {
        fw_sel = mode;
        g.fw_mode = mode;
    }
    flash_arm = -1;
    flash_eeprom_only = eeprom_only;
    g.flashing = 1;
    if (pthread_create(&thr, NULL, flash_thread, (void *)(intptr_t)mode) != 0) {
        g.flashing = 0;
        flash_phase_set(4, "FAILED — could not start flash");
        return 0;
    }
    pthread_detach(thr);
    return 1;
}

/* Firmware status and Upload. */
char flash_temp_dir[NP_MAX_PATH];

/* Extra directory for a second flash.log. NULL or empty clears it. Holds flash_file_mu. */
void np_host_set_temp_dir(const char *dir)
{
    pthread_mutex_lock(&flash_file_mu);
    if (!dir || !dir[0]) {
        flash_temp_dir[0] = 0;
    } else {
        snprintf(flash_temp_dir, sizeof(flash_temp_dir), "%s", dir);
    }
    pthread_mutex_unlock(&flash_file_mu);
}

/* Two lines: idle, arm, run, ok, or err, then the phase text.
 * Returns if out is NULL or n < 8. */
void np_host_flash_state(char *out, int n)
{
    char line[160];
    const char *tag = "idle";
    int phase;

    if (!out || n < 8) {
        return;
    }
    pthread_mutex_lock(&phase_mu);
    phase = flash_phase;
    snprintf(line, sizeof(line), "%s", flash_phase_line);
    pthread_mutex_unlock(&phase_mu);
    if (phase == 1) {
        tag = "arm";
    } else if (phase == 2) {
        tag = "run";
    } else if (phase == 3) {
        tag = "ok";
    } else if (phase == 4) {
        tag = "err";
    }
    snprintf(out, (size_t)n, "%s\n%s", tag, line);
}

/* Oldest retained line first, one per line. which 0 is the flash log; anything
 * else is the debug log. Holds log_mu. */
void np_host_log_copy(int which, char *out, int n)
{
    struct np_log_ring *r = which ? &debug_log_ring : &flash_log_ring;
    int count, start, i, o = 0;
    if (!out || n < 2) {
        return;
    }
    out[0] = 0;
    pthread_mutex_lock(&log_mu);
    count = r->n < NP_LOG_N ? r->n : NP_LOG_N;
    start = r->n - count;
    for (i = 0; i < count; i++) {
        const char *s = r->line[(start + i) % NP_LOG_N];
        int k = (int)strlen(s);
        if (o + k + 2 >= n) {
            break;
        }
        memcpy(out + o, s, (size_t)k);
        o += k;
        out[o++] = '\n';
    }
    out[o] = 0;
    pthread_mutex_unlock(&log_mu);
}

/* How many firmware presets exist. */
int np_host_fw_count(void)
{
    return np_fw_count();
}

/* Preset name for that mode. Truncates to n. */
void np_host_fw_label(int preset, char *out, int n)
{
    np_fw_label(preset, out, n);
}

/* Next firmware mode, wrapping. Stores and saves it. Does not write the board. */
void np_host_cycle_fw(void)
{
    np_host_set_fw_mode((g.fw_mode + 1) % np_fw_count());
}

/* Firmware version this build expects. Not what the board last reported. */
int np_host_fw_need(void)
{
    return EXG_FW_NEED;
}

/* Last stored version. 0 means never saved, not firmware 0. */
int np_host_fw_have(void)
{
    return g.fw_have;
}

/* Version parsed from this boot's banner. 0 if that line has not arrived. */
int np_host_fw_seen(void)
{
    return g.fw_seen;
}

/* 1 when a USB Knight looks older than the version this build expects.
 * 0 on LAN, while disconnected, or before a banner verdict. */
int np_host_fw_behind(void)
{
    /* No cable, no verdict. fw_have 0 means "never seen", not firmware 0. */
    if (!g.connected || g.link == 1) {
        return 0;
    }
    if (g.fw_seen > 0) {
        return g.fw_seen < EXG_FW_NEED;
    }
    /* A finished upload, or an earlier EXG-FW line, is stored in fw_have.
     * Frames with no banner are not another request to flash. */
    if (g.fw_have >= EXG_FW_NEED) {
        return 0;
    }
    /* A line before EXG-FW is not a verdict. Frames mean boot text is done. */
    if (fw_stock_boot && g.parser.locked) {
        return 1;
    }
    return 0;
}

/* Saved stream mode, 0..2. An out-of-range value reads as 0. */
int np_host_fw_mode(void)
{
    if (g.fw_mode < 0 || g.fw_mode > 2) {
        return 0;
    }
    return g.fw_mode;
}

/* Stores the mode and saves. Does not write the board. Out of range returns. */
void np_host_set_fw_mode(int mode)
{
    char label[80];
    if (mode < 0 || mode >= np_fw_count()) {
        return;
    }
    g.fw_mode = mode;
    fw_sel = mode;
    cfg_save();
    np_host_fw_label(mode, label, (int)sizeof(label));
    set_status(1, "%s", label);
}

/* Once per process, if the board is behind: sets a fault line and returns 1.
 * Later calls return 0. */
int np_host_fw_prompt(void)
{
    static int done;
    if (done || !np_host_fw_behind()) {
        return 0;
    }
    done = 1;
    set_status(0, "Connected Knight is not firmware %d. Electrodes off, then Upload.",
               EXG_FW_NEED);
    return 1;
}

/* Short label of the saved mode, into out. */
void np_host_fw_button(char *out, int n)
{
    np_fw_short(np_host_fw_mode(), out, n);
}

/* Arms a full-image flash of the saved mode. A second tap within 8 s starts it. */
void np_host_flash_upload(void)
{
    np_host_flash_preset(np_host_fw_mode(), 0);
}

/* Arms a full hex upload of mode. confirmed non-zero skips the second tap. */
void np_host_flash_preset(int mode, int confirmed)
{
    flash_arm_start(mode, confirmed, 0, "Electrodes off. Tap Upload again to flash");
}

/* Arms an EEPROM mode-byte write. Refuses, phase 4, when the image itself is behind. */
void np_host_flash_mode_only(int mode, int confirmed)
{
    if (np_host_fw_behind()) {
        flash_phase_set(4, "FAILED — upload the image first");
        return;
    }
    flash_arm_start(mode, confirmed, 1, "Electrodes off. Tap Write mode again for");
}
