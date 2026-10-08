#include "np_local.h"
#include "np_mods.h"

/* Process start, the UI tick, and connect status. */

/* 1 after a successful start. A second start returns 0 and does not reset g. */
int host_ready;


/* <config root>/exg-c/firmware, created. out is the directory, not a hex path. */
void firmware_dir(char *out, int n)
{
    char root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    snprintf(out, (size_t)n, "%s/exg-c/firmware", root);
    np_mkdir_p(out);
}

/* Call once before the UI tick. 0 on success, including a second call. -1 if
 * cmd_thread could not start. files_dir may be NULL. */
int np_host_start(const char *files_dir)
{
    int i;
    if (host_ready) {
        return 0;
    }
    np_set_files_dir(files_dir);
    memset(&g, 0, sizeof(g));
    g.fd = -1;
    g.running = 1;
    g.board = NP_BOARD_AUTO;
    g.board_pref = NP_BOARD_AUTO;
    g.window_s = 2;
    g.autoscale = 0;
    g.og = 0;
    apply_readable_defaults();
    g.grid = 1;
    g.show_uv = 1;
    g.ui_scale = 15;
    g.pref_w = 1280;
    g.pref_h = 720;
    g.cube_yaw = 0.55f;
    g.cube_pitch = 0.40f;
    g.cube_zoom = 1.0f;
    g.cube_float = 1;
    api_defaults();
    snprintf(g.prof, sizeof(g.prof), "default");
    np_elec_default(g.elec);
    g.neg_rail = 0;
    g.neg_pick = 0;
    for (i = 0; i < NP_NCHAN; i++) {
        g.gain[i] = 12;
        g.active[i] = 1;
        g.rld[i] = 1;
        g.neg_site[i] = -1;
        g.chrgb[i][0] = CHCOL[i][0];
        g.chrgb[i][1] = CHCOL[i][1];
        g.chrgb[i][2] = CHCOL[i][2];
    }
    {
        char root[NP_MAX_PATH];
        np_cfg_root(root, sizeof(root));
        np_mkdir_p(root);
        np_mkdir_p(root);
        {
            char pdir[NP_MAX_PATH];
            snprintf(pdir, sizeof(pdir), "%s/exg-c/profiles", root);
            np_mkdir_p(pdir);
        }
    }
    cfg_load();
    {
        char pp[NP_MAX_PATH];
        peers_path(pp, (int)sizeof(pp));
        np_peers_load(&peers, pp);
    }
    if (g.set_gen < 1) {
        apply_readable_defaults();
        g.set_gen = 1;
        filt_reset();
        cfg_save();
    }
    if (g.set_gen < 2) {
        g.api_on = 0;
        g.set_gen = 2;
        cfg_save();
    }
    if (g.set_gen < 3) {
        np_elec_default(g.elec);
        g.set_gen = 3;
        cfg_save();
    }
    if (g.set_gen < 4) {
        int c;
        for (c = 0; c < NP_NCHAN; c++) {
            g.chrgb[c][0] = CHCOL[c][0];
            g.chrgb[c][1] = CHCOL[c][1];
            g.chrgb[c][2] = CHCOL[c][2];
        }
        g.set_gen = 4;
        cfg_save();
    }
    if (g.set_gen < 5) {
        apply_readable_defaults();
        g.set_gen = 5;
        filt_reset();
        cfg_save();
    }
    if (g.set_gen < 6) {
        np_host_montage_default();
        g.set_gen = 6;
        cfg_save();
    }
    if (g.set_gen < 7) {
        /* Old ini stored the IMU default as a choice. Detect from the wire. */
        g.board_pref = NP_BOARD_AUTO;
        g.board = NP_BOARD_AUTO;
        g.set_gen = 7;
        cfg_save();
    }
    {
        char fw[NP_MAX_PATH];
        firmware_dir(fw, (int)sizeof(fw));
    }
    if (g.api_http == 8788) {
        g.api_http = 8765;
    }
    if (!strncmp(g.api_push, "192.", 4)) {
        g.api_push[0] = 0;
    }
    prof_scan();
    filt_reset();
    pthread_mutex_init(&g.mu, NULL);
    pthread_mutex_init(&g.qmu, NULL);
    pthread_mutex_init(&g.csv_mu, NULL);
    pthread_mutex_init(&g.parse_mu, NULL);
    pthread_cond_init(&g.qcv, NULL);
    np_ring_init(&g.ring);
    np_smx_init(&g.smx);
    snprintf(g.cube_ack, sizeof(g.cube_ack), "offer off");
    npl_init(&g.learn);
    {
        char lp[NP_MAX_PATH];
        learn_path(lp, sizeof(lp));
        npl_load(&g.learn, lp);
    }
    cal_load();
    if (pthread_create(&g.cmd_thr, NULL, cmd_thread, NULL) != 0) {
        return -1;
    }
    g.nports = np_list_ports(g.ports, NP_MAX_PORTS);
    host_ready = 1;
    api_apply();
    set_status(1, g.nports ? "ready - tap Connect" : "plug Knight, grant USB, tap Connect");
    return 0;
}

/* Stops cmd_thread, drops the link, and stops the share API. A second call returns. */
void np_host_shutdown(void)
{
    if (!host_ready) {
        return;
    }
    g.running = 0;
    pthread_cond_signal(&g.qcv);
    pthread_join(g.cmd_thr, NULL);
    do_disconnect();
    np_api_stop();
    host_ready = 0;
}

/* UI tick. Rewrites live-snap.txt and .csv at most every 800 ms while connected.
 * A strong clench also rewrites clench-live. */
static void live_snap(void)
{
    static uint32_t last;
    static uint64_t last_tot;
    uint32_t now = SDL_GetTicks(), want;
    char root[NP_MAX_PATH], path[NP_MAX_PATH], id[48];
    FILE *f;
    int c;
    uint64_t tot = 0;
    float raw[NP_NCHAN][256];
    uint32_t n0 = 0;

    if (!g.connected) {
        return;
    }
    if (last && now - last < 800) {
        return;
    }
    last = now;
    np_ring_stats(&g.ring, &tot, NULL, NULL);
    np_cfg_root(root, sizeof(root));
    snprintf(path, sizeof(path), "%s/live-snap.txt", root);
    f = fopen(path, "w");
    if (!f) {
        return;
    }
    id_label(id, sizeof(id));
    fprintf(f, "t_ms=%u frames=%llu dframes=%llu sps=%.1f %s\n", now,
            (unsigned long long)tot, (unsigned long long)(tot - last_tot),
            g.sps > 1.f ? g.sps : 0.f, id);
    last_tot = tot;
    want = (uint32_t)(0.50f * design_sps());
    if (want > 256) {
        want = 256;
    }
    fprintf(f, "ch,dc,rms,pk,uniq,n\n");
    for (c = 0; c < NP_NCHAN; c++) {
        float dc = 0, rms = 0, pk = 0;
        uint32_t n, i, uniq = 1;
        n = np_ring_copy(&g.ring, c, raw[c], want);
        if (c == 0) {
            n0 = n;
        }
        ch_stats(raw[c], n, &dc, &rms, &pk);
        for (i = 1; i < n; i++) {
            if (raw[c][i] != raw[c][i - 1]) {
                uniq++;
            }
        }
        fprintf(f, "%d,%.3f,%.3f,%.3f,%u,%u\n", c + 1, dc, rms, pk, uniq, n);
    }
    {
        uint32_t nn[NP_NCHAN];
        float left[NP_NCHAN][NP_RING];
        int k;
        fprintf(f, "after_car\n");
        fprintf(f, "ch,resid_rms,resid_pk\n");
        for (k = 0; k < NP_NCHAN; k++) {
            nn[k] = n0;
            memcpy(left[k], raw[k], (size_t)n0 * sizeof(float));
        }
        cook_id(left, nn);
        for (k = 0; k < NP_NCHAN; k++) {
            float dc = 0, rms = 0, pk = 0;
            ch_stats(left[k], nn[k], &dc, &rms, &pk);
            fprintf(f, "%d,%.3f,%.3f\n", k + 1, rms, pk);
        }
    }
    fclose(f);
    snprintf(path, sizeof(path), "%s/live-snap.csv", root);
    f = fopen(path, "w");
    if (!f) {
        return;
    }
    fprintf(f, "i,ch1,ch2,ch3,ch4,ch5,ch6,ch7,ch8\n");
    for (c = 0; (uint32_t)c < n0; c++) {
        fprintf(f, "%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", c, raw[0][c],
                raw[1][c], raw[2][c], raw[3][c], raw[4][c], raw[5][c], raw[6][c],
                raw[7][c]);
    }
    fclose(f);
    {
        float ratio = 0.f;
        int ev = stream_id(&ratio);
        static float best;
        if (g.sps >= 80.f && (ev == NP_ID_CLENCH || ev == NP_ID_BURST) &&
            ratio >= 2.50f && ratio > best) {
            best = ratio;
            snprintf(path, sizeof(path), "%s/clench-live.txt", root);
            f = fopen(path, "w");
            if (f) {
                fprintf(f, "t_ms=%u frames=%llu sps=%.1f %s ratio=%.2f\n", now,
                        (unsigned long long)tot, g.sps > 1.f ? g.sps : 0.f, id,
                        (double)ratio);
                fclose(f);
            }
            snprintf(path, sizeof(path), "%s/clench-live.csv", root);
            f = fopen(path, "w");
            if (f) {
                fprintf(f, "i,ch1,ch2,ch3,ch4,ch5,ch6,ch7,ch8\n");
                for (c = 0; (uint32_t)c < n0; c++) {
                    fprintf(f, "%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", c,
                            raw[0][c], raw[1][c], raw[2][c], raw[3][c], raw[4][c],
                            raw[5][c], raw[6][c], raw[7][c]);
                }
                fclose(f);
            }
        }
    }
}

/* 1 for 125, 200, 250, or 500. Any other rate must not replace the share Hz. */
static int share_rate_is_design(int hz)
{
    return hz == 125 || hz == 200 || hz == 250 || hz == 500;
}

/* UI thread, every frame. Drains share ops, saves a pending mode, and restarts
 * a stalled USB stream. Returns at once if start never finished. */
void np_host_tick(void)
{
    int sps;
    if (!host_ready) {
        return;
    }
    api_drain();
    if (fw_save_pending) {
        fw_save_pending = 0;
        cfg_save();
    }
    if (mode_pending && mode_quiet_until && SDL_GetTicks() > mode_quiet_until) {
        mode_pending = 0;
        mode_quiet_until = 0;
        set_status(0, "mode did not change");
    }
    sps = (int)design_sps();
    if (g.connected && g.rate_snap > 0 && share_rate_is_design(g.api_hz) && g.api_hz != sps &&
        share_rate_is_design(sps)) {
        g.api_hz = sps;
        cfg_save();
        api_apply();
    }
    if (g.link) {
        np_link_poll();
    } else {
        live_sync();
    }
    smx_tick();
    atom_tick();
    learn_tick();
    live_snap();
    cal_tick();
    if (g.connected && !g.en_running && !g.link) {
        uint64_t tot = 0;
        uint32_t now = SDL_GetTicks();
        np_ring_stats(&g.ring, &tot, NULL, NULL);
        if (tot != g.stall_tot) {
            g.stall_tot = tot;
            g.stall_t = now;
            g.stall_n = 0;
            if (g.recover_n && tot > 50) {
                g.recover_n = 0;
            }
        } else if (g.stall_t && now - g.stall_t > 4000 &&
                   !(mode_quiet_until && now < mode_quiet_until)) {
            stream_recover();
            g.stall_t = now;
        }
        {
            int kick;
            pthread_mutex_lock(&g.mu);
            kick = mode_enable || ladder_kick;
            mode_enable = 0;
            ladder_kick = 0;
            pthread_mutex_unlock(&g.mu);
            if (kick) {
                g.en_running = 1;
                if (pthread_create(&g.en_thr, NULL, enable_thread, NULL) == 0) {
                    pthread_detach(g.en_thr);
                } else {
                    g.en_running = 0;
                }
            }
        }
    }
}

/* Opens the selected port, or the LAN follow. 1 if the link is up afterwards. */
int np_host_connect(void)
{
    do_connect();
    return g.connected;
}
/* Closes USB or the LAN follow. Returns if a flash is running and flash_owner is clear. */
void np_host_disconnect(void)
{
    do_disconnect();
}
/* 1 while USB or a LAN follow is up. Not the same as frames arriving. */
int np_host_connected(void)
{
    return g.connected;
}
/* Copies the status line. Holds g.mu. Truncates to n-1. */
void np_host_status(char *out, int n)
{
    pthread_mutex_lock(&g.mu);
    snprintf(out, (size_t)n, "%s", g.status);
    pthread_mutex_unlock(&g.mu);
}
/* 1 when the last status line is a success. 0 marks a fault. */
int np_host_status_ok(void)
{
    return g.status_ok;
}
/* Measured frames per second over the last full second. At most 1 reads as 0. */
float np_host_sps(void)
{
    return g.sps > 1.f ? g.sps : 0.f;
}
/* Samples seen. Non-zero g.link uses the live cursor; USB uses the ring total.
 * Truncates to unsigned int. */
unsigned int np_host_frames(void)
{
    uint64_t tot = 0;
    if (g.link) {
        return (unsigned int)(live_seen > 0 ? live_seen : 0);
    }
    np_ring_stats(&g.ring, &tot, NULL, NULL);
    return (unsigned int)tot;
}
/* Samples the ring overwrote. Not USB packet loss. */
unsigned int np_host_drops(void)
{
    return np_ring_drops(&g.ring);
}
/* Rescans and writes one port path per line. out is empty when none exist. */
void np_host_ports(char *out, int n)
{
    int i, off = 0;
    g.nports = np_list_ports(g.ports, NP_MAX_PORTS);
    out[0] = 0;
    for (i = 0; i < g.nports && off < n - 2; i++) {
        off += snprintf(out + off, (size_t)(n - off), "%s%s", i ? "\n" : "", g.ports[i]);
    }
}
/* Next port, wrapping. Does nothing on a LAN follow. */
void np_host_cycle_port(void)
{
    if (g.link) {
        return;
    }
    g.nports = np_list_ports(g.ports, NP_MAX_PORTS);
    if (g.nports) {
        g.port_i = (g.port_i + 1) % g.nports;
    }
}
/* Selects a port by index after a rescan. Clamps into range. No-op if the list is empty. */
void np_host_set_port_i(int i)
{
    g.nports = np_list_ports(g.ports, NP_MAX_PORTS);
    if (g.nports < 1) {
        return;
    }
    if (i < 0) {
        i = 0;
    }
    if (i >= g.nports) {
        i = g.nports - 1;
    }
    g.port_i = i;
}

/* Held rate in SPS: snap 125/200/250/500, else a measured 100–160, else 125. */
int np_host_design_sps(void)
{
    return (int)design_sps();
}

/* 1 when frames arrive but stay under 64% of the design rate, and never under 80 SPS. */
int np_host_stream_cold(void)
{
    return stream_cold();
}
