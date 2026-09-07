#include "np_sot.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __ANDROID__
#define NP_SOT_DIR_DEF "/data/local/tmp/cubebrain_viz"
#else
#define NP_SOT_DIR_DEF_HOME "/.local/share/cubebrain_viz"
#define NP_SOT_DIR_DEF_TMP "/tmp/cubebrain_viz"
#endif

static char g_dir[256];
static char g_path[280];
static char g_name[NP_SOT_NAMES][NP_SOT_NAME];
static uint8_t g_on[NP_SOT_NAMES];
static int g_nn;

static int name_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static unsigned name_cell(const char *n)
{
    unsigned h = 2166136261u;
    while (*n) {
        h ^= (unsigned char)tolower((unsigned char)*n++);
        h *= 16777619u;
    }
    return h % (unsigned)NP_SOT_CELLS;
}

static void ensure_dir(const char *d)
{
    char tmp[256], *p;
    snprintf(tmp, sizeof(tmp), "%s", d);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

const char *np_sot_dir(void)
{
    const char *env;
    if (g_dir[0]) {
        return g_dir;
    }
    env = getenv("NP_SOT_DIR");
    if (env && env[0]) {
        snprintf(g_dir, sizeof(g_dir), "%s", env);
        return g_dir;
    }
#ifdef __ANDROID__
    snprintf(g_dir, sizeof(g_dir), "%s", NP_SOT_DIR_DEF);
#else
    {
        const char *h = getenv("HOME");
        if (h && h[0]) {
            snprintf(g_dir, sizeof(g_dir), "%s%s", h, NP_SOT_DIR_DEF_HOME);
        } else {
            snprintf(g_dir, sizeof(g_dir), "%s", NP_SOT_DIR_DEF_TMP);
        }
    }
#endif
    return g_dir;
}

const char *np_sot_path(void)
{
    if (!g_path[0]) {
        snprintf(g_path, sizeof(g_path), "%s/cells.bin", np_sot_dir());
    }
    return g_path;
}

void np_sot_clear(void)
{
    memset(g_name, 0, sizeof(g_name));
    memset(g_on, 0, sizeof(g_on));
    g_nn = 0;
}

void np_sot_set(const char *name, int on)
{
    int i;
    if (!name || !name[0]) {
        return;
    }
    for (i = 0; i < g_nn; i++) {
        if (name_eq(g_name[i], name)) {
            g_on[i] = on ? 1 : 0;
            return;
        }
    }
    if (g_nn >= NP_SOT_NAMES) {
        return;
    }
    snprintf(g_name[g_nn], NP_SOT_NAME, "%.15s", name);
    g_on[g_nn] = on ? 1 : 0;
    g_nn++;
}

void np_sot_apply(uint8_t cells[NP_SOT_CELLS])
{
    int i;
    if (!cells) {
        return;
    }
    for (i = 0; i < g_nn; i++) {
        unsigned c = name_cell(g_name[i]);
        cells[c] = g_on[i] ? 5 : 0;
    }
}

int np_sot_write(const uint8_t cells[NP_SOT_CELLS])
{
    char tmp[300], nodes[300];
    FILE *f;
    int i;
    const char *dir = np_sot_dir();
    const char *path = np_sot_path();

    ensure_dir(dir);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f) {
        return -1;
    }
    fputc(NP_SOT_N, f);
    if (fwrite(cells, 1, NP_SOT_CELLS, f) != NP_SOT_CELLS) {
        fclose(f);
        unlink(tmp);
        return -1;
    }
    fclose(f);
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    snprintf(nodes, sizeof(nodes), "%s/nodes.tsv", dir);
    f = fopen(nodes, "w");
    if (f) {
        fprintf(f, "# idx\tname\n");
        for (i = 0; i < g_nn; i++) {
            fprintf(f, "%u\t%s\n", name_cell(g_name[i]), g_name[i]);
        }
        fclose(f);
    }
    return 0;
}

int np_sot_write_bits01(const char *bits512)
{
    uint8_t cells[NP_SOT_CELLS];
    int i;
    memset(cells, 0, sizeof(cells));
    if (bits512) {
        for (i = 0; i < NP_SOT_CELLS && bits512[i]; i++) {
            cells[i] = (bits512[i] == '1') ? 1 : 0;
        }
    }
    np_sot_apply(cells);
    return np_sot_write(cells);
}

int np_sot_write_cpu(void)
{
    uint8_t cells[NP_SOT_CELLS];
    double load = 0;
    FILE *f;
    unsigned long long u = 0, n = 0, s = 0, id = 0, tot;
    int i, pct = 0;

    memset(cells, 0, sizeof(cells));
#ifdef __linux__
    f = fopen("/proc/loadavg", "r");
    if (f) {
        if (fscanf(f, "%lf", &load) != 1) {
            load = 0;
        }
        fclose(f);
    }
    f = fopen("/proc/stat", "r");
    if (f) {
        if (fscanf(f, "cpu %llu %llu %llu %llu", &u, &n, &s, &id) >= 4) {
            tot = u + n + s + id;
            if (tot > 0) {
                pct = (int)((u + n + s) * 100ull / tot);
            }
        }
        fclose(f);
    }
#endif
    for (i = 0; i < 8; i++) {
        cells[i] = (load > (double)i * 0.25) ? 5 : 0;
    }
    for (i = 0; i < 8; i++) {
        cells[8 + i] = (pct > i * 12) ? 4 : 0;
    }
    np_sot_set("cpu", pct > 8);
    np_sot_apply(cells);
    return np_sot_write(cells);
}
