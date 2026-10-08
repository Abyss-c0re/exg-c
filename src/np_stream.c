#include "np_local.h"
#include "np_mods.h"

/* USB stream. The reader thread parses Knight frames into the ring.
 * cmd_thread writes the command queue. enable_thread turns channels on
 * after boot. Connect and recover run from the UI tick. */

void set_status(int ok, const char *fmt, ...);
void cmd_push(int op, int ch, int gain);
/* Set on the reader when cfg must be saved. The UI tick clears it. */
int fw_save_pending;
/* 1 if boot text arrived with no firmware version line. */
int fw_stock_boot;
/* 1 while an exgmode_ change waits for the banner or the 8 s quiet window. */
int mode_pending;
/* Channel-enable generation. A reboot bumps it; enable runs once for that boot. */
static struct np_ladder ladder = {0, 0, 1};
/* UI tick starts enable_thread when the reader sees a fresh boot. */
int ladder_kick;

/* Status line under g.mu. ok 0 is a fault. fmt is printf-style and may truncate. */
void set_status(int ok, const char *fmt, ...)
{
    va_list ap;
    pthread_mutex_lock(&g.mu);
    g.status_ok = ok;
    va_start(ap, fmt);
    vsnprintf(g.status, sizeof(g.status), fmt, ap);
    va_end(ap);
    pthread_mutex_unlock(&g.mu);
}

/* Queues one Knight command. cmd_thread writes it. Drops it if the queue is already full. */
void cmd_push(int op, int ch, int gain)
{
    int n;
    pthread_mutex_lock(&g.qmu);
    n = (g.qh + 1) % QMAX;
    if (n != g.qt) {
        g.q[g.qh].op = op;
        g.q[g.qh].ch = ch;
        g.q[g.qh].gain = gain;
        g.qh = n;
        pthread_cond_signal(&g.qcv);
    }
    pthread_mutex_unlock(&g.qmu);
}

/* How many commands are still queued. Holds qmu. */
static int cmd_pending(void)
{
    int n;
    pthread_mutex_lock(&g.qmu);
    n = (g.qh - g.qt + QMAX) % QMAX;
    pthread_mutex_unlock(&g.qmu);
    return n;
}

/* Sleeps in 50 ms steps until the queue is empty, the link drops, or timeout_ms
 * is spent. A timeout under 50 ms does not wait. */
static void cmd_drain(int timeout_ms)
{
    int waits = timeout_ms / 50;
    while (waits-- > 0 && g.connected && cmd_pending() > 0) {
        usleep(50000);
    }
}

/* Process lifetime. Pops the queue and writes the UART. Skips the write while
 * flashing or disconnected, so a chon_ cannot hit the bootloader. */
void *cmd_thread(void *arg)
{
    (void)arg;
    while (g.running) {
        int op = 0, ch = 0, gain = 12;
        pthread_mutex_lock(&g.qmu);
        while (g.running && g.qh == g.qt) {
            pthread_cond_wait(&g.qcv, &g.qmu);
        }
        if (!g.running) {
            pthread_mutex_unlock(&g.qmu);
            break;
        }
        op = g.q[g.qt].op;
        ch = g.q[g.qt].ch;
        gain = g.q[g.qt].gain;
        g.qt = (g.qt + 1) % QMAX;
        pthread_mutex_unlock(&g.qmu);
        /* g.flashing: the programmer owns the UART. A chon_ here is
         * read as a bootloader command and resets the chip. */
        if (g.fd < 0 || !g.connected || g.flashing) {
            if (op == CMD_MODE) {
                NP_ALOG("mode: dropped exgmode_%d", ch);
            }
            continue;
        }
        if (op == CMD_CHON) {
            char msg[48];
            set_status(1, "enable ch%d", ch);
            pthread_mutex_lock(&g.parse_mu);
            np_parser_set_gain(&g.parser, ch, gain);
            pthread_mutex_unlock(&g.parse_mu);
            np_cmd_chon(g.fd, ch, gain);
            snprintf(msg, sizeof(msg), "chon_%d_%d", ch, gain);
            debug_log_add(msg);
        } else if (op == CMD_CHOFF) {
            char msg[32];
            np_cmd_choff(g.fd, ch);
            snprintf(msg, sizeof(msg), "choff_%d", ch);
            debug_log_add(msg);
        } else if (op == CMD_RLDADD) {
            char msg[32];
            np_cmd_rldadd(g.fd, ch);
            snprintf(msg, sizeof(msg), "rldadd_%d", ch);
            debug_log_add(msg);
        } else if (op == CMD_RLDRM) {
            char msg[32];
            np_cmd_rldremove(g.fd, ch);
            snprintf(msg, sizeof(msg), "rldremove_%d", ch);
            debug_log_add(msg);
        } else if (op == CMD_MODE) {
            char msg[16];
            np_cmd_mode(g.fd, ch);
            np_fmt_mode(msg, (int)sizeof(msg), ch);
            if (msg[0]) {
                char *nl = strchr(msg, '\n');
                if (nl) {
                    *nl = 0;
                }
            }
            debug_log_add(msg);
        }
    }
    return NULL;
}

/* Last firmware banner. Hold g.mu to read it. Not the status line. */
static char boot_note[96];

/* 0, 1, 2 for 125, 250, 500 SPS. -1 for 200 and anything else. */
static int mode_from_rate(int sps)
{
    if (sps == 125) {
        return 0;
    }
    if (sps == 250) {
        return 1;
    }
    if (sps == 500) {
        return 2;
    }
    return -1;
}

/* The button is the live Knight mode. A saved 500 must not stay on screen
 * while the board is streaming 125. A switch in flight keeps the request
 * until EXG-MODE confirms it or the quiet window ends. */
static void adopt_fw_mode(int mode)
{
    if (mode_pending || mode < 0 || mode > 2 || g.fw_mode == mode) {
        return;
    }
    g.fw_mode = mode;
    fw_save_pending = 1;
}

/* One firmware banner line. Updates SPS, version, and mode under g.mu.
 * A version line also kicks the channel ladder. */
static void note_boot(const char *s)
{
    int banner = np_banner_sps(s);
    int fwv = np_fw_version_line(s);
    int mm = np_fw_mode_line(s);
    pthread_mutex_lock(&g.mu);
    snprintf(boot_note, sizeof(boot_note), "%s", s);
    if (banner > 0) {
        g.banner_sps = banner;
    } else if (banner < 0) {
        g.banner_sps = -1;
    }
    if (fwv > 0) {
        g.fw_seen = fwv;
        fw_stock_boot = 0;
        if (g.fw_have != fwv) {
            g.fw_have = fwv;
            fw_save_pending = 1;
        }
        if (mode_pending) {
            mode_pending = 0;
            mode_quiet_until = 0;
            mode_enable = 1;
        }
        /* setup() turns channels off. The ladder runs once for this boot. */
        np_ladder_reboot(&ladder);
        ladder_kick = 1;
    } else if (g.fw_seen <= 0) {
        fw_stock_boot = 1;
    }
    if (mm >= 0) {
        if (g.fw_mode != mm) {
            g.fw_mode = mm;
            fw_save_pending = 1;
        }
    } else if (!mode_pending) {
        int from_banner = mode_from_rate(banner);
        if (from_banner >= 0 && g.fw_mode != from_banner) {
            g.fw_mode = from_banner;
            fw_save_pending = 1;
        }
    }
    pthread_mutex_unlock(&g.mu);
    debug_log_add(s);
    set_status(1, "%s", s);
}

/* Clears the last banner line. Holds g.mu. */
static void boot_note_clear(void)
{
    pthread_mutex_lock(&g.mu);
    boot_note[0] = 0;
    pthread_mutex_unlock(&g.mu);
}

/* Copies the last banner into dst. Holds g.mu. Empty if none arrived. */
static void boot_note_copy(char *dst, int n)
{
    pthread_mutex_lock(&g.mu);
    snprintf(dst, (size_t)n, "%s", boot_note);
    pthread_mutex_unlock(&g.mu);
}

/* Firmware prints ASCII status before the first 0xA0. Keep one line. */
static void boot_byte(unsigned char b, int locked, char *line, int *ln)
{
    int k, letters;

    if (locked) {
        *ln = 0;
        return;
    }
    if (b == '\n' || b == '\r') {
        if (*ln >= 4) {
            line[*ln] = 0;
            letters = 0;
            for (k = 0; k < *ln; k++) {
                if ((line[k] >= 'A' && line[k] <= 'Z') || (line[k] >= 'a' && line[k] <= 'z')) {
                    letters++;
                }
            }
            if (letters >= 2) {
                note_boot(line);
            }
        }
        *ln = 0;
        return;
    }
    if (b >= 32 && b < 127) {
        if (*ln < 95) {
            line[(*ln)++] = (char)b;
        }
        return;
    }
    *ln = 0;
}

/* Candidate SPS snap. rate_hits counts agreeing windows; a later change needs 2. */
static int rate_pending;
static int rate_hits;

/* Hold a snap across one odd window. The first lock is immediate. */
static int rate_consider(float delivered, float chip)
{
    int dsnap = np_rate_snap(delivered);
    int csnap = np_rate_snap(chip);
    int banner = g.banner_sps;
    int next = 0;
    int limited = 0;
    int changed;

    if (banner == 125 || banner == 250 || banner == 500) {
        csnap = banner;
    }
    if (csnap == 125 || csnap == 250 || csnap == 500) {
        g.chip_sps = csnap;
        if (dsnap == csnap ||
            (delivered >= 0.85f * (float)csnap && delivered <= 1.15f * (float)csnap)) {
            next = csnap;
        } else if (dsnap == 200 && csnap >= 250) {
            next = 200;
            limited = 1;
        } else if (dsnap) {
            next = dsnap;
        }
    } else if (dsnap) {
        next = dsnap;
        if (dsnap != 200) {
            g.chip_sps = dsnap;
        }
    }
    if (!next) {
        return 0;
    }
    if (next == g.rate_snap && limited == g.link_limited) {
        adopt_fw_mode(mode_from_rate(limited ? g.chip_sps : next));
        return 0;
    }
    if (g.rate_snap != 0) {
        if (next == rate_pending) {
            rate_hits++;
        } else {
            rate_pending = next;
            rate_hits = 1;
        }
        if (rate_hits < 2) {
            return 0;
        }
    }
    changed = (next != g.rate_snap) || (limited != g.link_limited);
    g.rate_snap = next;
    g.link_limited = limited;
    rate_pending = 0;
    rate_hits = 0;
    if (!changed) {
        return 0;
    }
    if (limited && g.chip_sps > 0) {
        set_status(0, "USB full — chip %d SPS, about %d frames arrive", g.chip_sps, next);
    } else {
        set_status(1, "stream %d SPS", next);
    }
    adopt_fw_mode(mode_from_rate(limited ? g.chip_sps : next));
    return 1;
}

/* Zeros the SPS window, the snap, and the banner. Does not touch the ring. */
void rate_reset(void)
{
    g.sps = 0.f;
    g.sps_n = 0;
    g.chip_n = 0;
    g.sps_t.tv_sec = 0;
    g.sps_t.tv_nsec = 0;
    g.rate_snap = 0;
    g.chip_sps = 0;
    g.link_limited = 0;
    g.banner_sps = 0;
    g.fw_seen = 0;
    fw_stock_boot = 0;
    rate_pending = 0;
    rate_hits = 0;
}

/* Push one USB read, then cook it once so EXG1 timestamps share that burst. */
static void commit_frames(const struct np_sample *fr, int n)
{
    int i;
    int retune = 0;
    if (n <= 0 || !fr) {
        return;
    }
    for (i = 0; i < n; i++) {
        struct timespec now;
        unsigned add = 1u + (unsigned)fr[i].drops;
        if (add > 8u) {
            add = 8u;
        }
        g.sps_n++;
        g.chip_n += add;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (g.sps_t.tv_sec == 0) {
            g.sps_t = now;
        } else {
            double dt = (double)(now.tv_sec - g.sps_t.tv_sec) +
                (now.tv_nsec - g.sps_t.tv_nsec) / 1e9;
            if (dt >= 1.0) {
                float del = (float)g.sps_n / (float)dt;
                float chip = (float)g.chip_n / (float)dt;
                g.sps = del;
                g.sps_n = 0;
                g.chip_n = 0;
                g.sps_t = now;
                if (rate_consider(del, chip)) {
                    retune = 1;
                }
            }
        }
    }
    if (retune) {
        filt_reset();
    }
    pthread_mutex_lock(&live_mu);
    for (i = 0; i < n; i++) {
        np_ring_push(&g.ring, &fr[i]);
    }
    live_sync_u();
    pthread_mutex_unlock(&live_mu);
    for (i = 0; i < n; i++) {
        const struct np_sample *s = &fr[i];
        if (g.recording) {
            pthread_mutex_lock(&g.csv_mu);
            if (g.csv) {
                int c;
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                fprintf(g.csv, "%ld.%09ld,%u", (long)ts.tv_sec, ts.tv_nsec, s->seq);
                for (c = 0; c < NP_NCHAN; c++) {
                    fprintf(g.csv, ",%.3f", s->uv[c]);
                }
                fprintf(g.csv, ",%u,%u\n", s->loff_p, s->loff_n);
                if ((s->seq % 125u) == 0u) {
                    fflush(g.csv);
                }
            }
            pthread_mutex_unlock(&g.csv_mu);
        }
    }
}

/* Read loop for this connection. Android bulk-reads and raises its priority;
 * desktop polls 8 ms. A read error sets a fault and returns; the link flag stays set. */
static void *reader_thread(void *arg)
{
    unsigned char buf[256];
    char line[96];
    int ln = 0;
    (void)arg;
#ifdef __ANDROID__
    {
        /* 500 SPS fills about 91% of 115200. A normal thread loses reads
         * when the phone is busy. URGENT_AUDIO is -19. */
        pid_t tid = gettid();
        if (setpriority(PRIO_PROCESS, tid, -19) != 0 &&
            setpriority(PRIO_PROCESS, tid, -16) != 0) {
            NP_ALOG("reader priority: %s", strerror(errno));
        } else {
            NP_ALOG("reader priority %d", getpriority(PRIO_PROCESS, tid));
        }
    }
#endif
    while (g.running && g.connected && g.fd >= 0) {
        int n, i;
#ifdef __ANDROID__
        /* USB is a JNI bulk transfer, not a real fd. poll() on the dummy
         * handle never sees Knight bytes, so the board looked dead. */
        n = np_serial_read(g.fd, buf, (int)sizeof(buf));
        if (n == 0) {
            usleep(400);
            continue;
        }
#else
        {
            struct pollfd pfd = {g.fd, POLLIN, 0};
            if (poll(&pfd, 1, 8) <= 0) {
                continue;
            }
        }
        n = np_serial_read(g.fd, buf, (int)sizeof(buf));
#endif
        if (n < 0) {
            set_status(0, "serial read failed");
            break;
        }
        if (n == 0) {
            continue;
        }
        {
            struct np_sample batch[16];
            int nb = 0;
            for (i = 0; i < n; i++) {
                struct np_sample s;
                int r, locked, idle_text;
                pthread_mutex_lock(&g.parse_mu);
                idle_text = g.parser.have == 0 && buf[i] != NP_START;
                r = np_parser_feed(&g.parser, buf[i], &s);
                locked = g.parser.locked;
                pthread_mutex_unlock(&g.parse_mu);
                /* Printable lines between frames are serial text, not samples. */
                boot_byte(buf[i], idle_text ? 0 : locked, line, &ln);
                if (r < 0) {
                    if (locked) {
                        pthread_mutex_lock(&g.ring.mu);
                        g.ring.bad++;
                        pthread_mutex_unlock(&g.ring.mu);
                    }
                } else if (r > 0) {
                    if (g.board_pref == NP_BOARD_AUTO) {
                        g.board = s.imu ? NP_BOARD_KNIGHT_IMU : NP_BOARD_KNIGHT;
                    }
                    if (nb == 16) {
                        commit_frames(batch, nb);
                        nb = 0;
                    }
                    batch[nb++] = s;
                }
            }
            if (nb) {
                commit_frames(batch, nb);
            }
        }
    }
    return NULL;
}

/* Polls up to tries times, sleeping gap_us. 1 only if the parser is locked,
 * the total reached frames, and it grew; otherwise 0. */
static int wait_live(int frames, int tries, int gap_us)
{
    int w;
    uint64_t tot = 0, last = 0;
    int grew = 0, locked = 0;
    for (w = 0; w < tries && g.connected; w++) {
        np_ring_stats(&g.ring, &tot, NULL, NULL);
        pthread_mutex_lock(&g.parse_mu);
        locked = g.parser.locked;
        pthread_mutex_unlock(&g.parse_mu);
        if (tot > last) {
            grew++;
            last = tot;
        }
        if (locked && (int)tot >= frames && grew >= 1) {
            return 1;
        }
        usleep(gap_us);
    }
    return 0;
}

/* Rebuilds the parser for the preferred board and the current gains. Holds parse_mu. */
void parser_rearm(void)
{
    pthread_mutex_lock(&g.parse_mu);
    np_parser_init(&g.parser, g.board_pref);
    np_parser_set_gains(&g.parser, g.gain);
    pthread_mutex_unlock(&g.parse_mu);
}

/* Detached. After frames lock, sends channel and RLD commands and clears en_running.
 * Android may pulse DTR once if the first wait misses and flash does not own the port. */
void *enable_thread(void *arg)
{
    int c;
    char note[96];
    (void)arg;
    boot_note_copy(note, (int)sizeof(note));
    if (!note[0]) {
        set_status(1, "waiting for stream...");
    }
#ifdef __ANDROID__
    if (!wait_live(20, 40, 100000)) {
        /* A kick here shares the FTDI port with Upload. Do not pulse
         * while the programmer owns the reset line. */
        if (!g.connected || g.flashing || g.fd < 0) {
            g.en_running = 0;
            return NULL;
        }
        set_status(1, "uart idle - one board kick");
        pthread_mutex_lock(&g.mu);
        np_ladder_reboot(&ladder);
        pthread_mutex_unlock(&g.mu);
        np_serial_pulse_dtr(g.fd);
        np_serial_flush(g.fd);
    }
#endif
    if (!wait_live(50, 120, 100000)) {
        boot_note_copy(note, (int)sizeof(note));
        if (note[0]) {
            set_status(0, "no live stream - %s", note);
        } else {
            set_status(0, "no live stream - tap Connect again");
        }
        g.en_running = 0;
        return NULL;
    }
    /* Channels start off only after setup(). A USB open that does not
     * reboot leaves them on, so the text ladder stays off the wire. */
    {
        int due;
        pthread_mutex_lock(&g.mu);
        due = np_ladder_due(&ladder, g.fw_have);
        if (!due) {
            np_ladder_mark(&ladder);
            ladder_kick = 0;
        }
        pthread_mutex_unlock(&g.mu);
        if (!due) {
            NP_ALOG("channels: leave (no reboot)");
            if (g.connected) {
                set_status(1, "connected %s", g.nports ? g.ports[g.port_i] : "");
                if (!g.cal.have) {
                    g.cal_phase = 5;
                    g.cal_t0 = 0;
                    set_status(1, "Put it on the desk… 5s");
                }
            }
            g.en_running = 0;
            return NULL;
        }
    }
    NP_ALOG("channels: enable after reboot");
    /* Command set: 2 s after the stream is up, before first chon_. */
    set_status(1, "board settling...");
    usleep(2000000);
    for (;;) {
        unsigned seen;
        pthread_mutex_lock(&g.mu);
        seen = ladder.boot;
        pthread_mutex_unlock(&g.mu);
        set_status(1, "enabling channels...");
        for (c = 0; c < NP_NCHAN && g.connected; c++) {
            if (!g.active[c]) {
                cmd_push(CMD_CHOFF, c + 1, 0);
                cmd_drain(8000);
                if (!g.connected) {
                    break;
                }
                cmd_push(CMD_RLDRM, c + 1, 0);
                cmd_drain(8000);
                continue;
            }
            cmd_push(CMD_CHON, c + 1, g.gain[c]);
            cmd_drain(8000);
            if (!g.connected) {
                break;
            }
            cmd_push(rld_want(c) ? CMD_RLDADD : CMD_RLDRM, c + 1, 0);
            cmd_drain(8000);
        }
        pthread_mutex_lock(&g.mu);
        if (!g.connected || ladder.boot == seen) {
            if (g.connected) {
                np_ladder_mark(&ladder);
                ladder_kick = 0;
            }
            pthread_mutex_unlock(&g.mu);
            break;
        }
        pthread_mutex_unlock(&g.mu);
        NP_ALOG("channels: reboot during enable");
    }
    if (g.connected) {
        set_status(1, "connected %s", g.nports ? g.ports[g.port_i] : "");
        if (!g.cal.have) {
            g.cal_phase = 5;
            g.cal_t0 = 0;
            set_status(1, "Put it on the desk… 5s");
        }
    }
    g.en_running = 0;
    return NULL;
}

/* After a stall, at most 3 resets. Returns while disconnected, enabling, or flashing.
 * Android does not pulse DTR, and a link with under 10 frames is not reset. */
void stream_recover(void)
{
    if (!g.connected || g.fd < 0 || g.en_running || g.flashing) {
        return;
    }
#ifdef __ANDROID__
    /* DTR reset loops keep the Nano in the bootloader. Never pulse
     * unless we already had frames (a real stall, not "never started"). */
    if (g.stall_tot < 10) {
        set_status(0, "USB open — no Knight bytes (not the cook)");
        return;
    }
#endif
    if (g.recover_n >= 3) {
        set_status(0, "stream dead (3 resets) - click Disconnect/Connect");
        return;
    }
    g.recover_n++;
    boot_note_clear();
    set_status(0, "stream stalled - board reset %d/3", g.recover_n);
    parser_rearm();
#ifndef __ANDROID__
    pthread_mutex_lock(&g.mu);
    np_ladder_reboot(&ladder);
    pthread_mutex_unlock(&g.mu);
    np_serial_pulse_dtr(g.fd);
    np_serial_flush(g.fd);
#endif
    g.en_running = 1;
    if (pthread_create(&g.en_thr, NULL, enable_thread, NULL) == 0) {
        pthread_detach(g.en_thr);
    } else {
        g.en_running = 0;
    }
}

/* USB opens the selected port and starts the reader and enable threads. A LAN
 * follow only starts the link. Returns if already up, or if a flash is running
 * and flash_owner is clear. */
void do_connect(void)
{
    const char *path;
    if (g.flashing && !flash_owner) {
        set_status(0, "flash in progress");
        return;
    }
    if (g.connected) {
        return;
    }
    if (g.link == 1) {
        char grant[NP_PEER_GRANT];
        char who[NP_PEER_NAME];
        char host[128];
        int hp = 8765, up = 8766;
        if (!g.link_dest[0] || !strncmp(g.link_dest, "bt:", 3)) {
            set_status(0, "type dest host:port");
            return;
        }
        np_link_set_hooks(link_on_sample, apply_link_cfg);
        if (!g.link_token[0]) {
            snprintf(who, sizeof(who), "%s", self_name[0] ? self_name : "exg");
            set_status(1, "waiting for Allow on the share…");
            if (np_link_pair(g.link_dest, who, grant, (int)sizeof(grant)) != 0) {
                set_status(0, "share refused or no answer");
                return;
            }
            snprintf(g.link_token, sizeof(g.link_token), "%s", grant);
            if (np_link_parse_dest(g.link_dest, host, (int)sizeof(host), &hp, &up) == 0) {
                np_host_follow_remember(host, g.link_dest, grant);
            } else {
                np_host_follow_remember(who, g.link_dest, grant);
            }
        }
        if (np_link_start(g.link_dest, g.link_token) != 0) {
            set_status(0, "could not reach EXG on LAN");
            return;
        }
        g.connected = 1;
        g.stall_t = SDL_GetTicks();
        set_status(1, "following EXG on LAN — waiting");
        return;
    }
    g.nports = np_list_ports(g.ports, NP_MAX_PORTS);
    if (g.nports <= 0) {
        set_status(0, NP_TOUCH ? "no Knight on USB — switch to LAN and type dest"
                               : "no /dev/ttyUSB* or /dev/ttyACM*");
        return;
    }
    if (g.port_i >= g.nports) {
        g.port_i = 0;
    }
    path = g.ports[g.port_i];
    g.fd = np_serial_open(path);
    if (g.fd < 0) {
        set_status(0, "open %s: %s", path, strerror(errno));
        return;
    }
    g.board = g.board_pref;
    rate_reset();
    np_parser_init(&g.parser, g.board_pref);
    np_parser_set_gains(&g.parser, g.gain);
    filt_reset();
    boot_note_clear();
#ifndef __ANDROID__
    /* Android: do not DTR-reset on the UI thread. The Knight is already
     * running; a pulse blacks the GL surface and reboots the Nano. */
    pthread_mutex_lock(&g.mu);
    np_ladder_reboot(&ladder);
    pthread_mutex_unlock(&g.mu);
    np_serial_pulse_dtr(g.fd);
    np_serial_flush(g.fd);
#endif
    g.connected = 1;
    g.stall_t = SDL_GetTicks();
    g.stall_n = 0;
    if (pthread_create(&g.thr, NULL, reader_thread, NULL) != 0) {
        np_serial_close(g.fd);
        g.fd = -1;
        g.connected = 0;
        set_status(0, "thread create failed");
        return;
    }
    g.en_running = 1;
    g.recover_n = 0;
    g.stall_n = 0;
    g.stall_tot = 0;
    if (pthread_create(&g.en_thr, NULL, enable_thread, NULL) != 0) {
        g.en_running = 0;
        set_status(0, "connected, but enable thread failed");
        return;
    }
    pthread_detach(g.en_thr);
    {
        char note[96];
        boot_note_copy(note, (int)sizeof(note));
        if (!note[0]) {
            set_status(1, "connected %s", path);
        }
    }
}

/* Drops the link and joins the reader. Closes an open CSV without fsync.
 * Returns if already down, or if a flash is running and flash_owner is clear. */
void do_disconnect(void)
{
    if (g.flashing && !flash_owner) {
        set_status(0, "flash in progress");
        return;
    }
    if (!g.connected) {
        return;
    }
    g.connected = 0;
    id_base_ok = 0;
    if (g.link) {
        np_link_stop();
        g.link_id[0] = 0;
        set_status(1, "disconnected");
        return;
    }
    /* enable_thread checks g.connected and exits; reader joins */
    pthread_join(g.thr, NULL);
    np_serial_close(g.fd);
    g.fd = -1;
    g.recording = 0;
    pthread_mutex_lock(&g.csv_mu);
    if (g.csv) {
        fclose(g.csv);
        g.csv = NULL;
    }
    pthread_mutex_unlock(&g.csv_mu);
    g.board = g.board_pref;
    rate_reset();
    set_status(1, "disconnected");
}

/* Board rate and IMU. */

/* Last IMU sample, ring units. 0 if this board sent no IMU. */
int np_host_imu(float acc[3], float gyr[3], float mag[3])
{
    int ok = 0;
    np_ring_imu(&g.ring, acc, gyr, mag, &ok);
    return ok;
}
/* 1 when the live board is the IMU frame. The preference alone does not count. */
int np_host_board_imu(void)
{
    return g.board == NP_BOARD_KNIGHT_IMU;
}
/* Preferred frame: auto, EXG, or IMU. Not the detected live board. */
int np_host_board_mode(void)
{
    return (int)g.board_pref;
}
/* Short board label, with the snapped SPS when connected. Returns if out is NULL or n < 1. */
void np_host_mode_label(char *out, int n)
{
    const char *kind;
    if (!out || n < 1) {
        return;
    }
    if (!g.connected) {
        if (g.board_pref == NP_BOARD_AUTO) {
            snprintf(out, (size_t)n, "Auto");
        } else if (g.board_pref == NP_BOARD_KNIGHT_IMU) {
            snprintf(out, (size_t)n, "8-ch + IMU");
        } else {
            snprintf(out, (size_t)n, "8-ch EXG");
        }
        return;
    }
    if (g.board == NP_BOARD_KNIGHT_IMU) {
        kind = "IMU";
    } else if (g.board == NP_BOARD_KNIGHT) {
        kind = "EXG";
    } else {
        kind = "Auto";
    }
    if (g.rate_snap > 0 && g.link_limited) {
        snprintf(out, (size_t)n, "%s %d link", kind, g.rate_snap);
    } else if (g.rate_snap > 0) {
        snprintf(out, (size_t)n, "%s %d", kind, g.rate_snap);
    } else if (g.sps > 1.f) {
        snprintf(out, (size_t)n, "%s %.0f", kind, (double)g.sps);
    } else {
        snprintf(out, (size_t)n, "%s", kind);
    }
}
/* Stores the frame preference and saves. Refuses while connected. Unknown mode returns. */
void np_host_set_board_mode(int mode)
{
    char label[48];
    if (g.connected) {
        set_status(0, "disconnect before switching the frame");
        return;
    }
    if (mode != (int)NP_BOARD_KNIGHT && mode != (int)NP_BOARD_KNIGHT_IMU &&
        mode != (int)NP_BOARD_AUTO) {
        return;
    }
    g.board_pref = (enum np_board)mode;
    g.board = g.board_pref;
    cfg_save();
    np_host_mode_label(label, (int)sizeof(label));
    set_status(1, "%s", label);
}
/* Auto, then IMU, then EXG, then Auto. Refuses while connected, same as set. */
void np_host_cycle_board(void)
{
    int next;
    if (g.board_pref == NP_BOARD_AUTO) {
        next = (int)NP_BOARD_KNIGHT_IMU;
    } else if (g.board_pref == NP_BOARD_KNIGHT_IMU) {
        next = (int)NP_BOARD_KNIGHT;
    } else {
        next = (int)NP_BOARD_AUTO;
    }
    np_host_set_board_mode(next);
}
/* Preference only: IMU frame if imu is non-zero, else plain EXG. Not auto.
 * Refuses while connected. */
void np_host_set_board_imu(int imu)
{
    np_host_set_board_mode(imu ? (int)NP_BOARD_KNIGHT_IMU : (int)NP_BOARD_KNIGHT);
}

/* Queues exgmode_ for 125, 250, or 500, and the board restarts. Refuses on LAN
 * or old firmware. Stays pending until a version banner or 8 s. */
void np_host_stream_mode(int mode)
{
    char label[80];
    int live;

    if (mode < 0 || mode > 2) {
        return;
    }
    np_host_fw_label(mode, label, (int)sizeof(label));
    if (!g.connected || g.link == 1) {
        NP_ALOG("mode: refuse %d connected=%d link=%d", mode, g.connected, g.link);
        set_status(0, "Connect the Knight on USB, then pick %s", label);
        return;
    }
    live = g.fw_seen > 0 ? g.fw_seen : g.fw_have;
    if (live < EXG_FW_NEED) {
        NP_ALOG("mode: refuse %d fw=%d need=%d", mode, live, EXG_FW_NEED);
        set_status(0, "Upload firmware %d once. Then Settings sends exgmode_%d",
                   EXG_FW_NEED, mode);
        return;
    }
    NP_ALOG("mode: queue exgmode_%d", mode);
    g.fw_mode = mode;
    g.board_pref = NP_BOARD_AUTO;
    g.board = NP_BOARD_AUTO;
    parser_rearm();
    rate_reset();
    mode_pending = 1;
    mode_quiet_until = SDL_GetTicks() + 8000u;
    g.stall_t = SDL_GetTicks();
    cfg_save();
    cmd_push(CMD_MODE, mode, 0);
    set_status(1, "USB %s — board restarts", label);
}
