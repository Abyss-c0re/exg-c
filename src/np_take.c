#include "np_local.h"
#include "np_mods.h"

/* Named takes. */


/* Stops a running take and drops the second count. Does not delete a saved file. */
void np_host_atom_discard(void)
{
    g.atom_on = 0;
    g.atom_rec_n = 0;
    set_status(1, "take discarded");
}

/* Starts counting take seconds from 0. Does not write a file until a later save. */
void np_host_atom_start(void)
{
    g.atom_rec_n = 0;
    g.atom_on = 1;
    set_status(1, "take running — watch the plot, then Stop");
}

/* Stops the take and returns how many seconds were counted. 0 if none. Does not
 * write a file. */
int np_host_atom_stop(void)
{
    int n = g.atom_rec_n;
    g.atom_on = 0;
    if (n < 1) {
        set_status(0, "take empty — hold at least 1 s");
    } else {
        set_status(1, "take %d s — name it to keep", n);
    }
    return n;
}

/* Starts the take timer, or stops it. Stopping does not save. */
void np_host_toggle_atom(void)
{
    if (g.atom_on) {
        np_host_atom_stop();
    } else {
        np_host_atom_start();
    }
}

/* 1 while a take is being counted. */
int np_host_atom(void)
{
    return g.atom_on;
}

/* Seconds counted in the running take, or 0 if it is not running. */
int np_host_atom_n(void)
{
    return g.atom_on ? g.atom_rec_n : 0;
}

/* Writes name.npat and the raw atom file from the typed name, and stamps the
 * current NEG RAIL bit. -1 if the name is empty or there are no seconds. A loud
 * take, many seconds at or above 4000 µV, is still saved. */
int np_host_atom_save(void)
{
    char path[NP_MAX_PATH];
    uint64_t live[NP_ATOM_RING];
    float rms[NP_ATOM_RING * 8];
    int n = 0;
    if (atom_path(path, (int)sizeof(path), g.namebuf) != 0) {
        set_status(0, "ATOM need a name");
        return -1;
    }
    n = g.atom_rec_n > 0 ? g.atom_rec_n : 0;
    if (n > g.atom_n) {
        n = g.atom_n;
    }
    atom_last(live, rms, n);
    if (n < 1) {
        set_status(0, "ATOM empty — wait for 1 s folds");
        return -1;
    }
    {
        int i, c, hot = 0;
        for (i = 0; i < n; i++) {
            for (c = 0; c < 8; c++) {
                if (rms[i * 8 + c] >= 4000.f) {
                    hot++;
                }
            }
        }
        if (hot >= n * 4) {
            set_status(0, "take is loud (%.0f uV) — saved anyway", (double)rms[0]);
        }
    }
    if (np_atom_save_m(path, live, rms, n, NP_ATOM_WIN, g.neg_rail) != 0) {
        set_status(0, "ATOM cannot write %s", path);
        return -1;
    }
    {
        char rpath[NP_MAX_PATH];
        float *planar = (float *)malloc((size_t)NP_NCHAN * n * NP_ATOM_WIN * sizeof(float));
        int i, c;
        if (planar) {
            memset(planar, 0, (size_t)NP_NCHAN * n * NP_ATOM_WIN * sizeof(float));
            for (i = 0; i < n; i++) {
                int idx = (g.atom_wr - n + i + NP_ATOM_RING) % NP_ATOM_RING;
                for (c = 0; c < NP_NCHAN; c++) {
                    memcpy(planar + c * (n * NP_ATOM_WIN) + i * NP_ATOM_WIN,
                           atom_raw[idx] + c * NP_ATOM_WIN,
                           (size_t)NP_ATOM_WIN * sizeof(float));
                }
            }
            raw_named_path("atoms", g.namebuf, rpath, (int)sizeof(rpath));
            np_raw_save(rpath, planar, NP_NCHAN, n * NP_ATOM_WIN, design_sps());
            free(planar);
        }
    }
    memcpy(g.atom_ref, live, (size_t)n * sizeof(uint64_t));
    g.atom_ref_n = n;
    atom_sanitize(g.atom_ref_name, (int)sizeof(g.atom_ref_name), g.namebuf);
    snprintf(g.atom_a, sizeof(g.atom_a), "%s", g.atom_ref_name);
    g.atom_b[0] = 0;
    g.atom_ab = 0.f;
    atom_score();
    g.atom_rec_n = 0;
    set_status(1, "kept %s  %d s — tap another take to compare", g.atom_ref_name, n);
    return 0;
}

/* Loads name.npat as the reference chain for unity, without raw or a recook.
 * -1 if the name is empty or the file has no seconds. */
int np_host_atom_load(void)
{
    char path[NP_MAX_PATH];
    int n, win = 0;
    if (atom_path(path, (int)sizeof(path), g.namebuf) != 0) {
        set_status(0, "ATOM need a name to compare");
        return -1;
    }
    n = np_atom_load(path, g.atom_ref, NP_ATOM_RING, &win);
    if (n < 1) {
        set_status(0, "ATOM no chain '%s'", g.namebuf);
        return -1;
    }
    g.atom_ref_n = n;
    atom_sanitize(g.atom_ref_name, (int)sizeof(g.atom_ref_name), g.namebuf);
    atom_score();
    set_status(1, "ATOM loaded %s  %d s", g.atom_ref_name, n);
    return 0;
}

/* Bit agreement of the live ring with the loaded take, 0..1. 0 if either side is empty. */
float np_host_atom_unity(void)
{
    return g.atom_unity;
}

/* One status sentence, or empty if nothing is recording and no take is picked.
 * A negative pair score reads as a different montage, and a short or null buffer returns. */
void np_host_atom_line(char *out, int n)
{
    if (!out || n < 4) {
        return;
    }
    if (g.atom_on) {
        snprintf(out, (size_t)n, "recording %d s", g.atom_n);
    } else if (g.atom_a[0] && g.atom_b[0]) {
        if (g.atom_ab < 0.f) {
            snprintf(out, (size_t)n, "%s vs %s  different montage", g.atom_a, g.atom_b);
        } else if (g.atom_ab >= 0.90f) {
            snprintf(out, (size_t)n, "%s vs %s  same head — not distinct",
                     g.atom_a, g.atom_b);
        } else if (g.atom_ab <= 0.f) {
            snprintf(out, (size_t)n, "%s vs %s  no RMS — cannot compare",
                     g.atom_a, g.atom_b);
        } else {
            snprintf(out, (size_t)n, "%s vs %s  distinct", g.atom_a, g.atom_b);
        }
    } else if (g.atom_a[0]) {
        snprintf(out, (size_t)n, "vs %s — tap another take", g.atom_a);
    } else {
        out[0] = 0;
    }
}

/* Name of the loaded take, or empty. A short or null buffer returns. */
void np_host_atom_ref(char *out, int n)
{
    if (!out || n < 2) {
        return;
    }
    snprintf(out, (size_t)n, "%s", g.atom_ref_name);
}

char atom_listed[NP_ATOM_MAX][NP_ATOM_NAME];
int atom_listed_n;

/* Rescans *.npat and returns how many names were kept. */
int np_host_atom_count(void)
{
    return atom_rescan();
}

/* Name at i from the last rescan. Does not rescan itself. Out of range writes an
 * empty string. */
void np_host_atom_at(int i, char *out, int n)
{
    if (!out || n < 2) {
        return;
    }
    if (i < 0 || i >= atom_listed_n) {
        out[0] = 0;
        return;
    }
    snprintf(out, (size_t)n, "%s", atom_listed[i]);
}

/* Seconds stored in take i, read from the file. 0 if i is out of range or the path is bad. */
int np_host_atom_secs(int i)
{
    char path[NP_MAX_PATH];
    uint64_t tmp[NP_ATOM_RING];
    int win = 0;
    if (i < 0 || i >= atom_listed_n) {
        return 0;
    }
    if (atom_path(path, (int)sizeof(path), atom_listed[i]) != 0) {
        return 0;
    }
    return np_atom_load(path, tmp, NP_ATOM_RING, &win);
}

/* Copies that take's name and loads it as the reference. -1 if i is out of
 * range. Otherwise the load result. */
int np_host_atom_select(int i)
{
    if (i < 0 || i >= atom_rescan()) {
        return -1;
    }
    snprintf(g.namebuf, sizeof(g.namebuf), "%s", atom_listed[i]);
    return np_host_atom_load();
}

/* Deletes the .npat and the raw file. Clears the reference or a compare slot if
 * it was that name. Out of range returns without deleting. */
void np_host_atom_del(int i)
{
    char path[NP_MAX_PATH];
    if (i < 0 || i >= atom_rescan()) {
        return;
    }
    if (atom_path(path, (int)sizeof(path), atom_listed[i]) != 0) {
        return;
    }
    {
        char rp[NP_MAX_PATH];
        raw_named_path("atoms", atom_listed[i], rp, (int)sizeof(rp));
        unlink(rp);
    }
    if (strcmp(g.atom_ref_name, atom_listed[i]) == 0) {
        g.atom_ref_n = 0;
        g.atom_ref_name[0] = 0;
        g.atom_unity = 0.f;
    }
    if (strcmp(g.atom_a, atom_listed[i]) == 0) {
        g.atom_a[0] = 0;
        g.atom_ab = 0.f;
    }
    if (strcmp(g.atom_b, atom_listed[i]) == 0) {
        g.atom_b[0] = 0;
        g.atom_ab = 0.f;
    }
    unlink(path);
    set_status(1, "deleted take %s", atom_listed[i]);
    atom_rescan();
}

/* First tap sets compare slot A and loads it; a second tap sets B and scores the
 * pair, and tapping A again clears both. A montage mismatch scores below 0, and
 * out of range returns. */
void np_host_atom_pick(int i)
{
    const char *name;
    if (i < 0 || i >= atom_rescan()) {
        return;
    }
    name = atom_listed[i];
    if (!g.atom_a[0] || (g.atom_a[0] && g.atom_b[0])) {
        snprintf(g.atom_a, sizeof(g.atom_a), "%s", name);
        g.atom_b[0] = 0;
        g.atom_ab = 0.f;
        snprintf(g.namebuf, sizeof(g.namebuf), "%s", name);
        np_host_atom_load();
        set_status(1, "%s — tap another take to compare", name);
        return;
    }
    if (strcmp(g.atom_a, name) == 0) {
        g.atom_a[0] = 0;
        g.atom_b[0] = 0;
        g.atom_ab = 0.f;
        set_status(1, "compare cleared");
        return;
    }
    snprintf(g.atom_b, sizeof(g.atom_b), "%s", name);
    atom_pair_score();
    if (g.atom_ab < 0.f) {
        set_status(0, "%s vs %s  different montage", g.atom_a, g.atom_b);
    } else if (g.atom_ab >= 0.90f) {
        set_status(0, "%s vs %s  same head — not distinct", g.atom_a, g.atom_b);
    } else if (g.atom_ab <= 0.f) {
        set_status(0, "%s vs %s  no RMS — cannot compare", g.atom_a, g.atom_b);
    } else {
        set_status(1, "%s vs %s  distinct", g.atom_a, g.atom_b);
    }
}

/* Compare sentence for the two slots. With nothing picked, says to tap two
 * takes. A short or null buffer returns. */
void np_host_atom_pair(char *out, int n)
{
    if (!out || n < 4) {
        return;
    }
    if (g.atom_a[0] && g.atom_b[0]) {
        if (g.atom_ab < 0.f) {
            snprintf(out, (size_t)n, "%s vs %s  different montage", g.atom_a, g.atom_b);
        } else if (g.atom_ab >= 0.90f) {
            snprintf(out, (size_t)n, "%s vs %s  same head — not distinct",
                     g.atom_a, g.atom_b);
        } else if (g.atom_ab <= 0.f) {
            snprintf(out, (size_t)n, "%s vs %s  no RMS — cannot compare",
                     g.atom_a, g.atom_b);
        } else {
            snprintf(out, (size_t)n, "%s vs %s  distinct", g.atom_a, g.atom_b);
        }
    } else if (g.atom_a[0]) {
        snprintf(out, (size_t)n, "%s — tap a second take", g.atom_a);
    } else {
        snprintf(out, (size_t)n, "tap two takes to compare");
    }
}

/* Name in compare slot A, or empty. A null buffer or n under 2 writes nothing. */
void np_host_atom_slot_a(char *out, int n)
{
    if (out && n > 1) {
        snprintf(out, (size_t)n, "%s", g.atom_a);
    }
}

/* Name in compare slot B, or empty. A null buffer or n under 2 writes nothing. */
void np_host_atom_slot_b(char *out, int n)
{
    if (out && n > 1) {
        snprintf(out, (size_t)n, "%s", g.atom_b);
    }
}

/* Winning take index while MATCH is on, or -1. Not a pose index. */
int np_host_atom_id_best(void)
{
    return g.atom_id_best;
}

/* Closeness of take i to the last second, 0..1, or 0 if i is outside 0..31.
 * Wiped to 0 when no take won. Not a percent. */
float np_host_atom_id_score(int i)
{
    if (i < 0 || i >= 32) {
        return 0.f;
    }
    return g.atom_id[i];
}

/* "now" plus the winning take name while MATCH is on. Empty if MATCH is off,
 * or "now —" if MATCH is on, the list is non-empty, and nothing won. */
void np_host_atom_id_line(char *out, int n)
{
    if (!out || n < 4) {
        return;
    }
    if (!g.learn.match || g.atom_id_best < 0 || g.atom_id_best >= atom_listed_n) {
        if (g.learn.match && atom_listed_n > 0) {
            snprintf(out, (size_t)n, "now —");
        } else {
            out[0] = 0;
        }
        return;
    }
    snprintf(out, (size_t)n, "now %s", atom_listed[g.atom_id_best]);
}
