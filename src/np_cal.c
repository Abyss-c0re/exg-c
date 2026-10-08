#include "np_local.h"
#include "np_mods.h"

/* Noise and still plates, the CSV the reader appends, and the strip FFT.
 * Capture and the phase timer run on the UI tick. */

float fft_hold[FFT_STRIP_BINS];
/* SDL ticks of the last strip FFT. A refresh inside 80 ms returns. */
uint32_t fft_t;
void ch_stats(const float *buf, uint32_t n, float *dc, float *rms, float *pk);
void cal_tick(void);

/* Stops recording, fsyncs, and closes the CSV. Safe when none is open. */
void csv_close(void)
{
    g.recording = 0;
    pthread_mutex_lock(&g.csv_mu);
    if (g.csv) {
        int fd = fileno(g.csv);
        fflush(g.csv);
        if (fd >= 0) {
            fsync(fd);
        }
        fclose(g.csv);
        g.csv = NULL;
    }
    pthread_mutex_unlock(&g.csv_mu);
}

/* Takes ownership of f, writes the header, and starts recording. -1 if f is NULL.
 * A previous file is closed without fsync. */
int csv_open(FILE *f, const char *label)
{
    if (!f) {
        return -1;
    }
    setvbuf(f, NULL, _IOFBF, 8192);
    fprintf(f, "time,seq,ch1,ch2,ch3,ch4,ch5,ch6,ch7,ch8,loff_p,loff_n\n");
    fflush(f);
    pthread_mutex_lock(&g.csv_mu);
    if (g.csv) {
        fclose(g.csv);
    }
    g.csv = f;
    pthread_mutex_unlock(&g.csv_mu);
    g.recording = 1;
    snprintf(g.csv_path, sizeof(g.csv_path), "%s", label && label[0] ? label : "csv");
    set_status(1, "recording %s", g.csv_path);
    return 0;
}

/* UI. Opens a timestamped CSV under the config root, or closes the current one.
 * Refuses when not connected. */
void toggle_record(void)
{
    if (!g.connected) {
        set_status(0, "connect before record");
        return;
    }
    if (g.recording) {
        csv_close();
        set_status(1, "saved %s", g.csv_path);
        return;
    }
    {
        time_t t = time(NULL);
        struct tm tm;
        FILE *f;
        char stamp[40], root[NP_MAX_PATH];
        localtime_r(&t, &tm);
        strftime(stamp, sizeof(stamp), "knight-%Y%m%d-%H%M%S.csv", &tm);
        np_cfg_root(root, sizeof(root));
        mkdir(root, 0755);
        snprintf(g.csv_path, sizeof(g.csv_path), "%s/%s", root, stamp);
        f = fopen(g.csv_path, "w");
        if (!f) {
            set_status(0, "cannot write %s", g.csv_path);
            return;
        }
        csv_open(f, g.csv_path);
    }
}
/* Open-input / rail is not EEG. Autoscale of ±0.13 V looks like a brainwave. */
#define Q_OFF 0
#define Q_ZERO 1
#define Q_LEADOFF 2
#define Q_OPEN 3
#define Q_LIVE 4

/* 0 off, 1 flat, 2 lead-off, 3 open above 250 mV, 4 live. buf is µV.
 * c is not checked. Bit 0 of lp and ln is channel 1. */
int ch_quality(int c, const float *buf, uint32_t n, uint8_t lp, uint8_t ln)
{
    uint32_t i;
    float mx = 0.f;
    uint8_t bit = (uint8_t)(1u << c);

    if (!g.active[c]) {
        return Q_OFF;
    }
    if (n < 4) {
        return Q_ZERO;
    }
    /* Knight P/N contact bytes (bit 0 = ch1). If both stay 0 the
     * firmware is not reporting contact and we fall through to amplitude. */
    if ((lp | ln) != 0 && ((lp & bit) || (ln & bit))) {
        return Q_LEADOFF;
    }
    for (i = 0; i < n; i++) {
        float a = fabsf(buf[i]);
        if (a > mx) {
            mx = a;
        }
    }
    if (mx < 0.5f) {
        return Q_ZERO;
    }
    /* Near ADS1299 full-scale only. 3 mV is not "no skin" — EMG and
     * a worn but noisy headset both exceed that. */
    if (mx > 250000.f) {
        return Q_OPEN;
    }
    return Q_LIVE;
}

/* dc and rms of buf, pk the max absolute sample. Same unit as buf.
 * n of 0 writes zeros. */
void ch_stats(const float *buf, uint32_t n, float *dc, float *rms, float *pk)
{
    uint32_t i;
    double s = 0, e = 0;
    float p = 0.f;
    *dc = 0;
    *rms = 0;
    *pk = 0;
    if (!n) {
        return;
    }
    for (i = 0; i < n; i++) {
        float a = fabsf(buf[i]);
        s += buf[i];
        e += (double)buf[i] * (double)buf[i];
        if (a > p) {
            p = a;
        }
    }
    *dc = (float)(s / (double)n);
    *rms = sqrtf((float)(e / (double)n));
    *pk = p;
}

/* <config root>/exg-c.cal. Creates the root directory. */
static void cal_path(char *out, size_t n)
{
    char root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    np_mkdir_p(root);
    snprintf(out, n, "%s/exg-c.cal", root);
}

/* Writes the noise and still plates to exg-c.cal. 0 on success, -1 if it cannot open. */
int cal_save(void)
{
    char path[NP_MAX_PATH];
    FILE *f;
    int c;
    time_t t = time(NULL);
    cal_path(path, sizeof(path));
    f = fopen(path, "w");
    if (!f) {
        return -1;
    }
    fprintf(f, "# exg-c plates: NOISE (desk/off) then CALM (worn still)\n");
    fprintf(f, "# time %ld  noise_n %u  calm %d\n", (long)t, g.cal.n, g.calm.have);
    fprintf(f, "tone_hz=%.3f\n", g.cal_hz);
    if (g.noise_psd_ok) {
        int k;
        fprintf(f, "psd");
        for (k = 0; k < NP_PSD_BINS; k++) {
            fprintf(f, " %.5g", g.noise_psd[k]);
        }
        fputc('\n', f);
    }
    fprintf(f, "ch,dc_uV,rms_uV,pk_uV\n");
    for (c = 0; c < NP_NCHAN; c++) {
        fprintf(f, "%d,%.3f,%.3f,%.3f\n", c + 1, g.cal.dc[c], g.cal.rms[c], g.cal.pk[c]);
    }
    if (g.calm.have) {
        for (c = 0; c < NP_NCHAN; c++) {
            fprintf(f, "calm%d=%.3f,%.3f,%.3f\n", c + 1, g.calm.dc[c], g.calm.rms[c],
                    g.calm.pk[c]);
        }
    }
    fclose(f);
    return 0;
}

/* Reads exg-c.cal into the plates. 0 if at least one channel row loaded.
 * -1 if the file is missing or empty. */
int cal_load(void)
{
    char path[NP_MAX_PATH], line[128];
    FILE *f;
    int got = 0;
    cal_path(path, sizeof(path));
    f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    memset(&g.cal, 0, sizeof(g.cal));
    while (fgets(line, sizeof(line), f)) {
        int ch;
        float dc, rms, pk;
        if (line[0] == '#' || (line[0] == 'c' && line[1] == 'h')) {
            continue;
        }
        if (sscanf(line, "tone_hz=%f", &dc) == 1) {
            g.cal_hz = dc;
            continue;
        }
        if (!strncmp(line, "psd ", 4) || !strncmp(line, "psd\t", 4)) {
            const char *s = line + 3;
            int k = 0;
            g.noise_psd_ok = 0;
            memset(g.noise_psd, 0, sizeof(g.noise_psd));
            while (k < NP_PSD_BINS) {
                char *end = NULL;
                float v = strtof(s, &end);
                if (end == s) {
                    break;
                }
                g.noise_psd[k++] = v;
                s = end;
            }
            if (k >= 8) {
                g.noise_psd_ok = 1;
            }
            continue;
        }
        if (sscanf(line, "calm%d=%f,%f,%f", &ch, &dc, &rms, &pk) == 4 && ch >= 1 &&
            ch <= NP_NCHAN) {
            g.calm.dc[ch - 1] = dc;
            g.calm.rms[ch - 1] = rms;
            g.calm.pk[ch - 1] = pk;
            g.calm.have = 1;
        } else if (sscanf(line, "%d,%f,%f,%f", &ch, &dc, &rms, &pk) == 4 && ch >= 1 &&
                   ch <= NP_NCHAN) {
            g.cal.dc[ch - 1] = dc;
            g.cal.rms[ch - 1] = rms;
            g.cal.pk[ch - 1] = pk;
            got++;
        }
    }
    fclose(f);
    if (got > 0) {
        g.cal.have = 1;
        g.cal.n = 1;
        g.cal_phase = g.calm.have ? 4 : 2;
    }
    return got > 0 ? 0 : -1;
}

/* Noise plate from the live ring, plus a raw dump. Sets the plate even if the
 * file write fails. */
void cal_capture(void)
{
    int c;
    uint32_t want = plate_want();
    {
        char rp[NP_MAX_PATH];
        raw_plate_path("noise", rp, (int)sizeof(rp));
        raw_dump_ring(rp, want);
    }
    memset(&g.cal, 0, sizeof(g.cal));
    g.cal_hz = 0.f;
    g.noise_psd_ok = 0;
    g.noise_psd_ch_ok = 0;
    memset(g.noise_psd, 0, sizeof(g.noise_psd));
    memset(g.noise_psd_ch, 0, sizeof(g.noise_psd_ch));
    {
        float acc[NP_PLATE_N];
        int accn = 0, used = 0;
        memset(acc, 0, sizeof(acc));
        for (c = 0; c < NP_NCHAN; c++) {
            float buf[NP_RING];
            uint32_t n = np_ring_copy(&g.ring, c, buf, want);
            uint32_t i, take;
            ch_stats(buf, n, &g.cal.dc[c], &g.cal.rms[c], &g.cal.pk[c]);
            if (n > g.cal.n) {
                g.cal.n = n;
            }
            take = n > (uint32_t)NP_PLATE_N ? (uint32_t)NP_PLATE_N : n;
            if (take < 32) {
                continue;
            }
            if (accn < (int)take) {
                accn = (int)take;
            }
            for (i = 0; i < take; i++) {
                acc[i] += buf[i];
            }
            if (take >= (uint32_t)NP_FFT_N) {
                np_welch_psd(buf, (int)take, g.noise_psd_ch[c]);
                g.noise_psd_ch_ok |= 1u << c;
            }
            used++;
        }
        if (used > 0 && accn >= NP_FFT_N) {
            float hz = 0.f;
            np_welch_psd(acc, accn, g.noise_psd);
            g.noise_psd_ok = 1;
            if (np_tone_from_psd(g.noise_psd, design_sps(), &hz) == 0) {
                g.cal_hz = hz;
            } else if (np_tone_hz(acc, accn > NP_FFT_N ? NP_FFT_N : accn, design_sps(),
                                 &hz) == 0) {
                g.cal_hz = hz;
            }
        } else if (used > 0) {
            float hz = 0.f;
            if (np_tone_hz(acc, accn, design_sps(), &hz) == 0) {
                g.cal_hz = hz;
            }
        }
    }
    g.cal.have = 1;
    g.cal_arm = 0;
    if (cal_save() != 0) {
        set_status(0, "calibrate captured but could not write exg-c.cal");
        return;
    }
    if (g.cal_hz > 1.f) {
        set_status(1, "NOISE plate  ch1 rms %.0f uV  line %.1f Hz", g.cal.rms[0], g.cal_hz);
    } else {
        set_status(1, "NOISE plate  ch1 rms %.0f uV  no line tone", g.cal.rms[0]);
    }
}

/* Still plate. Returns without writing if no noise plate exists. Notches a
 * tone above 1 Hz, then stores DC and the residual RMS. */
void calm_capture(void)
{
    int c;
    uint32_t want = plate_want();
    if (!g.cal.have) {
        set_status(0, "NOISE first (desk / headset off, then OK)");
        return;
    }
    {
        char rp[NP_MAX_PATH];
        raw_plate_path("calm", rp, (int)sizeof(rp));
        raw_dump_ring(rp, want);
    }
    memset(&g.calm, 0, sizeof(g.calm));
    for (c = 0; c < NP_NCHAN; c++) {
        float buf[NP_RING], dc, rms, pk;
        uint32_t n = np_ring_copy(&g.ring, c, buf, want);
        if (n < 16) {
            continue;
        }
        if (g.cal_hz > 1.f) {
            struct np_notch nt;
            uint32_t i;
            np_notch_init(&nt, g.cal_hz, design_sps(), 30.f);
            for (i = 0; i < n; i++) {
                buf[i] = np_notch_step(&nt, buf[i]);
            }
        }
        ch_stats(buf, n, &dc, &rms, &pk);
        g.calm.dc[c] = dc;
        np_sub_dc(buf, (int)n, dc);
        ch_stats(buf, n, &dc, &g.calm.rms[c], &g.calm.pk[c]);
        if (n > g.calm.n) {
            g.calm.n = n;
        }
    }
    g.calm.have = 1;
    g.cal_cut = 1;
    if (cal_save() != 0) {
        set_status(0, "calm captured but could not write exg-c.cal");
        return;
    }
    set_status(1, "Still plate  ch1 resid %.0f uV", g.calm.rms[0]);
}
/* UI. Rebuilds the strip magnitude at most every 80 ms from cooked windows.
 * Skips a channel that still looks like the noise plate. */
void fft_refresh(void)
{
    enum { N = FFT_STRIP_N };
    float mag[FFT_STRIP_BINS];
    float acc[N];
    int c, i, used = 0, open_n = 0, peak_i = 1;
    uint8_t lp = 0, ln = 0;
    uint32_t now = SDL_GetTicks();
    if (fft_t && now - fft_t < 80) {
        return;
    }
    memset(mag, 0, sizeof(mag));
    np_ring_loff(&g.ring, &lp, &ln);
    for (c = 0; c < NP_NCHAN; c++) {
        float buf[N];
        uint32_t n;
        int q;
        if (!g.active[c]) {
            continue;
        }
        n = view_copy(c, buf, N);
        if (n < N) {
            continue;
        }
        q = ch_quality(c, buf, n, lp, ln);
        if (g.cal.have && g.cal_cut && g.cal.rms[c] > 1.f) {
            float dc, rms, pk;
            ch_stats(buf, n, &dc, &rms, &pk);
            if (rms / g.cal.rms[c] > 0.70f && rms / g.cal.rms[c] < 1.40f) {
                open_n++;
                continue;
            }
        }
        if (q != Q_LIVE) {
            open_n++;
        }
        np_detrend(buf, (int)n);
        for (i = 0; i < (int)n; i++) {
            float win = 0.5f - 0.5f * cosf(2.f * (float)M_PI * (float)i / (float)(n - 1));
            acc[i] = buf[i] * win;
        }
        {
            float m[N / 2];
            np_fft_mag(acc, N, m);
            for (i = 0; i < N / 2; i++) {
                mag[i] += m[i];
            }
            used++;
        }
    }
    memcpy(fft_hold, mag, sizeof(fft_hold));
    fft_t = now;
    fft_used = used;
    fft_open = open_n;
    {
        float peak = 1e-12f;
        int sps = g.sps > 1.f ? (int)(g.sps + 0.5f) : NP_DEFAULT_SPS;
        for (i = 1; i < FFT_STRIP_BINS; i++) {
            if (mag[i] > peak) {
                peak = mag[i];
                peak_i = i;
            }
        }
        fft_peak_hz = (peak_i * sps) / N;
    }
}

/* UI tick. Advances phases 5, 1, and 3 on their timers. Returns unless connected,
 * warm, and one of those phases is running. */
void cal_tick(void)
{
    uint32_t now, dt;
    if (g.cal_phase != 1 && g.cal_phase != 3 && g.cal_phase != 5) {
        return;
    }
    if (!g.connected || (stream_cold())) {
        return;
    }
    now = SDL_GetTicks();
    if (!g.cal_t0) {
        g.cal_t0 = now ? now : 1;
    }
    dt = now - g.cal_t0;
    if (g.cal_phase == 5 && dt >= CAL_PLACE_MS) {
        g.cal_phase = 1;
        g.cal_t0 = now ? now : 1;
        set_status(1, "Desk plate — leave it down");
        return;
    }
    if (g.cal_phase == 1 && dt >= CAL_DESK_MS) {
        cal_capture();
        g.cal_cut = 1;
        g.cal_phase = 2;
        cfg_save();
        set_status(1, "Wear the headset, sit still, tap Calibrate");
    } else if (g.cal_phase == 3 && dt >= CAL_WEAR_MS) {
        calm_capture();
        g.cal_phase = 4;
        cfg_save();
        set_status(1, "Calibrated");
        clean_set_status();
    }
}

/* Plates, CSV, and the strip FFT. */

/* Starts a CSV at path, replacing an open one. -1 if disconnected, path is empty,
 * or the file cannot be created. */
int np_host_csv_begin(const char *path)
{
    FILE *f;
    if (!g.connected) {
        set_status(0, "connect before record");
        return -1;
    }
    if (!path || !path[0]) {
        return -1;
    }
    if (g.recording) {
        csv_close();
    }
    f = fopen(path, "w");
    if (!f) {
        set_status(0, "cannot write %s", path);
        return -1;
    }
    return csv_open(f, path);
}

/* Starts a CSV on fd. Closes fd on failure, including when not connected.
 * -1 if fd is bad or fdopen fails. */
int np_host_csv_begin_fd(int fd, const char *name)
{
    FILE *f;
    if (!g.connected) {
        if (fd >= 0) {
            close(fd);
        }
        set_status(0, "connect before record");
        return -1;
    }
    if (fd < 0) {
        return -1;
    }
    if (g.recording) {
        csv_close();
    }
    f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        set_status(0, "cannot write CSV");
        return -1;
    }
    return csv_open(f, name && name[0] ? name : "exg.csv");
}

/* Starts the desk wait, or the sit-still wait if a noise plate already exists.
 * Returns if a timed phase is already running. */
void np_host_cal_start(void)
{
    uint32_t now = SDL_GetTicks();
    if (!g.connected) {
        set_status(0, "connect first");
        return;
    }
    if (stream_cold()) {
        set_status(0, "wait for the stream");
        return;
    }
    if (g.cal_phase == 1 || g.cal_phase == 3 || g.cal_phase == 5) {
        return;
    }
    if (g.cal_phase == 2 || (g.cal.have && !g.calm.have)) {
        g.cal_phase = 3;
        g.cal_t0 = now ? now : 1;
        set_status(1, "Sit still…");
        return;
    }
    g.cal_phase = 5;
    g.cal_t0 = now ? now : 1;
    set_status(1, "Put it on the desk… 5s");
}

/* 0 idle, 1 desk, 2 wear prompt, 3 sit still, 4 done, 5 put-down. */
int np_host_cal_phase(void)
{
    return g.cal_phase;
}

/* 0..99 during a timed phase, 100 when done, else 0. Does not finish the phase. */
int np_host_cal_progress(void)
{
    uint32_t now, dt, need;
    if (g.cal_phase != 1 && g.cal_phase != 3 && g.cal_phase != 5) {
        return g.cal_phase == 4 ? 100 : 0;
    }
    now = SDL_GetTicks();
    dt = now - g.cal_t0;
    need = g.cal_phase == 5 ? CAL_PLACE_MS : (g.cal_phase == 1 ? CAL_DESK_MS : CAL_WEAR_MS);
    if (dt >= need) {
        return 99;
    }
    return (int)(dt * 100u / need);
}

/* Short prompt for the current phase, with seconds left. Returns if out is NULL or n < 4. */
void np_host_cal_line(char *out, int n)
{
    int left;
    if (!out || n < 4) {
        return;
    }
    if (g.cal_phase == 5) {
        left = (int)(CAL_PLACE_MS / 1000u) - (int)((SDL_GetTicks() - g.cal_t0) / 1000u);
        if (left < 1) {
            left = 1;
        }
        snprintf(out, (size_t)n, "Put it down… %ds", left);
    } else if (g.cal_phase == 1) {
        left = (int)(CAL_DESK_MS / 1000u) - (int)((SDL_GetTicks() - g.cal_t0) / 1000u);
        if (left < 0) {
            left = 0;
        }
        snprintf(out, (size_t)n, "Desk… %ds  leave it down", left);
    } else if (g.cal_phase == 2) {
        snprintf(out, (size_t)n, "Wear it — tap");
    } else if (g.cal_phase == 3) {
        left = (int)(CAL_WEAR_MS / 1000u) - (int)((SDL_GetTicks() - g.cal_t0) / 1000u);
        if (left < 0) {
            left = 0;
        }
        snprintf(out, (size_t)n, "Sit still… %ds", left);
    } else if (g.cal.have && g.calm.have) {
        snprintf(out, (size_t)n, "Calibrated");
    } else {
        snprintf(out, (size_t)n, "Calibrate");
    }
}

/* Same entry as cal start. An older button still lands in the timed plate. */
void np_host_noise_arm(void)
{
    np_host_cal_start();
}
/* During the desk phase, captures the noise plate now. Otherwise starts cal. */
void np_host_noise_ok(void)
{
    if (g.cal_phase == 1) {
        cal_capture();
        g.cal_cut = 1;
        g.cal_phase = 2;
        cfg_save();
        set_status(1, "Wear the headset, sit still, tap Calibrate");
        return;
    }
    np_host_cal_start();
}
/* If a noise plate is in memory, starts the 8 s sit-still timer again.
 * Otherwise the capture asks for noise first. */
void np_host_calm(void)
{
    if (g.cal_phase == 2 || g.cal.have) {
        g.cal_phase = 3;
        g.cal_t0 = SDL_GetTicks();
        if (!g.cal_t0) {
            g.cal_t0 = 1;
        }
        set_status(1, "Sit still…");
        return;
    }
    calm_capture();
}
/* Flips DC/CLEAN and saves. Does not capture a plate. */
void np_host_toggle_clean(void)
{
    g.cal_cut = !g.cal_cut;
    cfg_save();
    clean_set_status();
}
/* 1 after a noise plate is stored in memory. */
int np_host_cal_have(void)
{
    return g.cal.have;
}
/* 1 after a still plate is stored in memory. */
int np_host_calm_have(void)
{
    return g.calm.have;
}
/* 1 when DC subtract / CLEAN is switched on. Not whether the Wiener fit can run. */
int np_host_clean(void)
{
    return g.cal_cut;
}
/* 1 only when CLEAN will Wiener-filter. Needs the noise plate and a window of at least NP_FFT_N. */
int np_host_clean_live(void)
{
    return clean_wiener_ready();
}

/* 1 while a CSV file is open. */
int np_host_csv(void)
{
    return g.recording;
}

/* Opens or closes the timestamped CSV. Same rules as the on-screen record control. */
void np_host_toggle_csv(void)
{
    toggle_record();
}

/* Copies up to max summed-magnitude bins. peak_hz, if not NULL, is the peak in Hz.
 * The strip may be up to 80 ms old. Returns the count copied. */
int np_host_fft(float *dst, int max, int *peak_hz)
{
    int n;
    fft_refresh();
    n = max < FFT_STRIP_BINS ? max : FFT_STRIP_BINS;
    if (n < 0) {
        n = 0;
    }
    if (dst && n > 0) {
        memcpy(dst, fft_hold, (size_t)n * sizeof(float));
    }
    if (peak_hz) {
        *peak_hz = fft_peak_hz;
    }
    return n;
}
