#include "np_local.h"
#include "np_mods.h"

/* Desk extras on the UI tick: cube offer, dialout re-exec, trace color,
 * site focus, and gain. The cube POST runs on a detached thread. */

const int PALETTE[NPAL][3] = {
    {80, 200, 255}, {255, 180, 70}, {120, 220, 140}, {240, 110, 140},
    {180, 150, 255}, {255, 230, 90}, {90, 230, 210}, {230, 140, 255},
    {255, 90, 90}, {90, 255, 140}, {255, 255, 255}, {255, 140, 40},
};

/* 1 while a detached cube POST is in flight. A second offer returns. */
static volatile int cube_busy;

/* NP_CUBE_URL, or "off" when unset or empty. Do not free the pointer. */
static const char *cube_url(void)
{
    const char *u = getenv("NP_CUBE_URL");
    if (u && u[0]) {
        return u;
    }
    return "off";
}

/* IPv4 POST, 200 ms polls. 1 after the body is written.
 * 0 for "off", a bad host, or any miss. resp may be NULL. */
static int cube_post_json(const char *base, const char *path, const char *json, char *resp,
                          int rcap)
{
    char host[128], req[1400], hdr[256];
    const char *p;
    int port = 80, fd = -1, n, blen, woff, got = 0;
    struct addrinfo hints, *ai = NULL;
    struct pollfd pfd;
    char portstr[12];

    if (resp && rcap > 0) {
        resp[0] = 0;
    }
    if (!base || !json || !path) {
        return 0;
    }
    if (!strncmp(base, "off", 3) || !strcmp(base, "0")) {
        return 0;
    }
    p = base;
    if (!strncmp(p, "http://", 7)) {
        p += 7;
    }
    {
        const char *col = strchr(p, ':');
        const char *sl = strchr(p, '/');
        size_t hl;
        if (col && (!sl || col < sl)) {
            hl = (size_t)(col - p);
            port = atoi(col + 1);
            if (port <= 0) {
                port = 17333;
            }
        } else {
            hl = sl ? (size_t)(sl - p) : strlen(p);
        }
        if (hl >= sizeof(host) || hl == 0) {
            return 0;
        }
        memcpy(host, p, hl);
        host[hl] = 0;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (getaddrinfo(host, portstr, &hints, &ai) != 0) {
        return 0;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        freeaddrinfo(ai);
        return 0;
    }
    {
        int fl = fcntl(fd, F_GETFL, 0);
        if (fl >= 0) {
            fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        }
    }
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) < 0 && errno != EINPROGRESS) {
        freeaddrinfo(ai);
        close(fd);
        return 0;
    }
    freeaddrinfo(ai);
    pfd.fd = fd;
    pfd.events = POLLOUT;
    if (poll(&pfd, 1, 200) <= 0) {
        close(fd);
        return 0;
    }
    {
        int err = 0;
        socklen_t el = sizeof(err);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) < 0 || err) {
            close(fd);
            return 0;
        }
    }
    blen = (int)strlen(json);
    n = snprintf(hdr, sizeof(hdr),
                 "POST %s HTTP/1.0\r\nHost: %s:%d\r\nContent-Type: application/json\r\n"
                 "Content-Length: %d\r\nConnection: close\r\n\r\n",
                 path, host, port, blen);
    if (n < 0 || n + blen >= (int)sizeof(req)) {
        close(fd);
        return 0;
    }
    memcpy(req, hdr, (size_t)n);
    memcpy(req + n, json, (size_t)blen);
    n += blen;
    woff = 0;
    while (woff < n) {
        int w;
        pfd.events = POLLOUT;
        if (poll(&pfd, 1, 200) <= 0) {
            close(fd);
            return 0;
        }
        w = (int)write(fd, req + woff, (size_t)(n - woff));
        if (w < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            close(fd);
            return 0;
        }
        woff += w;
    }
    if (resp && rcap > 1) {
        pfd.events = POLLIN;
        while (got < rcap - 1) {
            int r;
            if (poll(&pfd, 1, 200) <= 0) {
                break;
            }
            r = (int)read(fd, resp + got, (size_t)(rcap - 1 - got));
            if (r <= 0) {
                break;
            }
            got += r;
        }
        resp[got] = 0;
    }
    close(fd);
    return 1;
}

/* Detached. Posts /v1/coord, then sets g.cube_ok under g.mu. Frees json and clears cube_busy. */
static void *cube_post_thr(void *arg)
{
    char *json = arg;
    char resp[320];
    const char *url = cube_url();
    int ok;

    memset(resp, 0, sizeof(resp));
    ok = cube_post_json(url, "/v1/coord", json, resp, sizeof(resp));
    pthread_mutex_lock(&g.mu);
    if (ok && strstr(resp, "\"stored\":true")) {
        snprintf(g.cube_ack, sizeof(g.cube_ack), "cube stored");
        g.cube_ok = 1;
    } else if (ok && strstr(resp, "\"ok\":true")) {
        snprintf(g.cube_ack, sizeof(g.cube_ack), "cube ack");
        g.cube_ok = 1;
    } else {
        snprintf(g.cube_ack, sizeof(g.cube_ack), "cube offline");
        g.cube_ok = 0;
    }
    pthread_mutex_unlock(&g.mu);
    cube_busy = 0;
    free(json);
    return NULL;
}

/* UI tick. Packs the SMX and starts one POST. Returns if the URL is off, a post
 * is already in flight, or malloc fails. */
static void cube_offer(void)
{
    char bits[NP_CUBE3_N + 4];
    char *json;
    int n, cap;
    pthread_t th;
    pthread_attr_t at;
    const char *url = cube_url();

    if (!strncmp(url, "off", 3) || !strcmp(url, "0")) {
        snprintf(g.cube_ack, sizeof(g.cube_ack), "offer off");
        g.cube_ok = 0;
        return;
    }
    if (cube_busy) {
        return;
    }
    n = np_cube_pack(&g.smx, bits, sizeof(bits));
    cap = n + 280;
    json = malloc((size_t)cap);
    if (!json) {
        return;
    }
    snprintf(json, (size_t)cap,
             "{\"plate\":\"NEXUS_COORD v1 | from=exg-c | type=smx | topic=channel_stim | "
             "seq=%u | unity=1.0 | hold_flash=1 | share=state_matrix_only | pii=0 | "
             "n=8 | cells=512 | nch=%u | have=%u | sot_bits=%s |\"}",
             g.smx.seq, (unsigned)g.smx.nch, g.smx.have, bits);
    cube_busy = 1;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &at, cube_post_thr, json) != 0) {
        cube_busy = 0;
        free(json);
    }
    pthread_attr_destroy(&at);
}

/* UI tick, at most once a second, and only while connected. Folds live channels
 * into the cube and may store one SMX byte. */
void smx_tick(void)
{
    static uint64_t last;
    struct timespec ts;
    uint64_t now;
    uint8_t bits[NP_NCHAN];
    uint8_t mask = 0;
    int nch = 0, c;
    uint32_t want;
    float buf[NP_RING];

    if (!g.connected) {
        last = 0;
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000ull);
    if (last && now - last < 1000ull) {
        return;
    }
    last = now;

    want = (uint32_t)design_sps();
    if (want < 32) {
        want = 32;
    }
    if (want > NP_RING) {
        want = NP_RING;
    }
    memset(bits, 0, sizeof(bits));
    {
        uint8_t chbits[NP_NCHAN];
        uint64_t atom = 0;
        learn_fold_byte(chbits);
        for (c = 0; c < NP_NCHAN; c++) {
            uint32_t n;
            float dc = 0, rms = 0, pk = 0, sc;
            uint8_t row = 0;
            if (!g.active[c]) {
                continue;
            }
            mask |= (uint8_t)(1u << c);
            n = view_copy(c, buf, want);
            if (n > 32) {
                memmove(buf, buf + (n - 32), 32 * sizeof(float));
                n = 32;
            }
            ch_stats(buf, n, &dc, &rms, &pk);
            sc = cook_scale_ch(c);
            if (n >= 8) {
                uint64_t one = np_atom_pack_rel(buf, 1, (int)n, (int)n, &sc);
                row = (uint8_t)(one & 0xffu);
            }
            atom |= (uint64_t)row << (8 * c);
            bits[nch] = chbits[c];
            nch++;
        }
        np_atom_faces8(atom, g.cube_bits);
    }
    if (nch > 0) {
        int ix, iy, iz;
        float acc[3], gyr[3], mag[3];
        int imu_ok = 0, used = 0;
        np_smx_push(&g.smx, bits, nch, mask);
        np_cube_clear_kind(&g.smx, NP_CELL_EEG);
        for (c = 0; c < NP_NCHAN; c++) {
            if (!g.active[c]) {
                continue;
            }
            if (g.elec[c].site >= 0) {
                np_1010_ijk(g.elec[c].site, &ix, &iy, &iz);
                np_cube_set(&g.smx, ix, iy, iz, bits[used] ? 1 : 0, NP_CELL_EEG);
            }
            used++;
        }
        np_ring_imu(&g.ring, acc, gyr, mag, &imu_ok);
        if (imu_ok) {
            np_cube_imu(&g.smx, acc, gyr, mag);
        }
        {
            char bits01[NP_CUBE3_N + 1];
            np_cube_pack(&g.smx, bits01, (int)sizeof(bits01));
            np_sot_write_bits01(bits01);
        }
        cube_offer();
        if (g.rec_t0 && rec_smx_n < NPL_SMX_SEC) {
            uint8_t fold = 0;
            int used = 0;
            for (c = 0; c < NP_NCHAN; c++) {
                if (!g.active[c]) {
                    continue;
                }
                if (bits[used]) {
                    fold |= (uint8_t)(1u << c);
                }
                used++;
            }
            rec_smx[rec_smx_n++] = fold;
        }
    }
}

/* 1 if gid is the real, effective, or a supplementary group. 0 if the group list cannot be read. */
static int in_group(gid_t gid)
{
    int n = getgroups(0, NULL);
    gid_t *list;
    int i, ok = 0;
    if (getgid() == gid || getegid() == gid) {
        return 1;
    }
    if (n <= 0) {
        return 0;
    }
    list = calloc((size_t)n, sizeof(*list));
    if (!list) {
        return 0;
    }
    n = getgroups(n, list);
    for (i = 0; i < n; i++) {
        if (list[i] == gid) {
            ok = 1;
        }
    }
    free(list);
    return ok;
}

/* Desktop only. Re-execs under sg dialout once. Android, NP_EXG_NOSG, or an
 * existing dialout membership returns. A failed sg does not loop. */
void ensure_dialout(int argc, char **argv)
{
#ifdef __ANDROID__
    (void)argc;
    (void)argv;
    return;
#else
    struct group *gr = getgrnam("dialout");
    char cmd[2048];
    int i, off = 0;
    if (!gr || in_group(gr->gr_gid) || getenv("NP_EXG_NOSG")) {
        return;
    }
    cmd[0] = 0;
    for (i = 0; i < argc && off < (int)sizeof(cmd) - 8; i++) {
        const char *p = argv[i] ? argv[i] : "";
        if (i) {
            cmd[off++] = ' ';
        }
        cmd[off++] = '\'';
        while (*p && off < (int)sizeof(cmd) - 6) {
            if (*p == '\'') {
                off += snprintf(cmd + off, sizeof(cmd) - (size_t)off, "'\\''");
            } else {
                cmd[off++] = *p;
            }
            p++;
        }
        cmd[off++] = '\'';
        cmd[off] = 0;
    }
    setenv("NP_EXG_NOSG", "1", 1);
    execlp("sg", "sg", "dialout", "-c", cmd, (char *)NULL);
#endif
}

/* Next palette RGB for channel c (0..7). An unknown color jumps to swatch 0.
 * Out of range returns. */
void chcol_cycle(int c)
{
    int i, k;
    if (c < 0 || c >= NP_NCHAN) {
        return;
    }
    k = 0;
    for (i = 0; i < NPAL; i++) {
        if (g.chrgb[c][0] == PALETTE[i][0] && g.chrgb[c][1] == PALETTE[i][1] &&
            g.chrgb[c][2] == PALETTE[i][2]) {
            k = (i + 1) % NPAL;
            break;
        }
    }
    g.chrgb[c][0] = PALETTE[k][0];
    g.chrgb[c][1] = PALETTE[k][1];
    g.chrgb[c][2] = PALETTE[k][2];
}
/* Pulls the cube zoom into 0.70 .. 2.80. */
void cube_zoom_clamp(void)
{
    if (g.cube_zoom < 0.70f) {
        g.cube_zoom = 0.70f;
    }
    if (g.cube_zoom > 2.80f) {
        g.cube_zoom = 2.80f;
    }
}

/* dir > 0 steps in by 0.20. Any other dir steps out by 0.20, then clamps. */
void cube_zoom_by(int dir)
{
    g.cube_zoom += dir > 0 ? 0.20f : -0.20f;
    cube_zoom_clamp();
}

/* Steps the 10-10 focus by dir, wrapping. Returns if the site list is empty. */
void cube_site_by(int dir)
{
    int n = np_1010_count();
    if (n < 1) {
        return;
    }
    g.site_focus += dir;
    while (g.site_focus < 0) {
        g.site_focus += n;
    }
    g.site_focus %= n;
}

/* How many virtual cells are marked used. 0 .. NP_VIRT_MAX. */
static int cube_virt_n(void)
{
    int i, n = 0;
    for (i = 0; i < NP_VIRT_MAX; i++) {
        if (g.smx.virt[i].used) {
            n++;
        }
    }
    return n;
}

/* Storage index of the focus-th used virtual cell. -1 if focus is past that set. */
int cube_virt_slot(int focus)
{
    int i, n = 0;
    for (i = 0; i < NP_VIRT_MAX; i++) {
        if (!g.smx.virt[i].used) {
            continue;
        }
        if (n == focus) {
            return i;
        }
        n++;
    }
    return -1;
}

/* Steps virt_focus across used cells, wrapping. Sets 0 when none are used. */
void cube_virt_by(int dir)
{
    int n = cube_virt_n();
    if (n < 1) {
        g.virt_focus = 0;
        return;
    }
    g.virt_focus += dir;
    while (g.virt_focus < 0) {
        g.virt_focus += n;
    }
    g.virt_focus %= n;
}

/* Writes the focused 10-10 site onto the selected channel and saves. With neg_pick
 * and a channel selected, sets that NEG site and returns. Out of range returns. */
void cube_assign_focus(void)
{
    int ix, iy, iz, share[8], ns, i, other = -1;
    if (g.site_focus < 0 || g.site_focus >= np_1010_count()) {
        return;
    }
    if (g.neg_pick && g.elec_sel >= 0 && g.elec_sel < NP_NCHAN) {
        np_host_set_neg_site(g.elec_sel, g.site_focus);
        return;
    }
    if (g.elec_sel < 0 || g.elec_sel >= NP_NCHAN) {
        g.elec_sel = 0;
    }
    np_elec_set_site(&g.elec[g.elec_sel], g.site_focus);
    cfg_save();
    np_1010_ijk(g.site_focus, &ix, &iy, &iz);
    ns = np_1010_sites_at(ix, iy, iz, share, 8);
    for (i = 0; i < NP_NCHAN; i++) {
        if (i != g.elec_sel && g.elec[i].site == g.site_focus) {
            other = i;
        }
    }
    if (other >= 0) {
        set_status(1, "ch%d @ %s  cell %d,%d,%d  also ch%d", g.elec_sel + 1,
                   np_1010_name(g.site_focus), ix, iy, iz, other + 1);
    } else if (ns > 1) {
        set_status(1, "ch%d @ %s  cell %d,%d,%d  (%d names on this cell)", g.elec_sel + 1,
                   np_1010_name(g.site_focus), ix, iy, iz, ns);
    } else {
        set_status(1, "ch%d @ %s  cell %d,%d,%d", g.elec_sel + 1, np_1010_name(g.site_focus),
                   ix, iy, iz);
    }
}
/* Next ADS gain code for ch. Does nothing if the current code is not in the table.
 * ch is not bounds-checked. */
void next_gain(int ch)
{
    int i;
    for (i = 0; i < NP_NGAINS; i++) {
        if (NP_GAINS[i] == g.gain[ch]) {
            g.gain[ch] = NP_GAINS[(i + 1) % NP_NGAINS];
            break;
        }
    }
}
