#include "np_local.h"
#include "np_mods.h"

/* Config root and raw plate files. Callers are the UI tick and cal
 * capture. The weak window hook is a no-op until the UI defines it. */

/* Strip FFT result. fft_refresh writes these. fft_peak_hz is Hz, not a bin. */
int fft_used, fft_open, fft_peak_hz;

#if defined(__GNUC__)
void np_ui_apply_window_size(int w, int h) __attribute__((weak));
#endif
/* Weak no-op. The UI's own definition replaces it. w and h are ignored here. */
void np_ui_apply_window_size(int w, int h)
{
    (void)w;
    (void)h;
}

/* If set, this is the config root. Empty falls through to HOME or ~/.config. */
static char g_files_dir[NP_MAX_PATH];

/* Copies p over the files dir. NULL or empty leaves the previous root. */
void np_set_files_dir(const char *p)
{
    if (p && p[0]) {
        snprintf(g_files_dir, sizeof(g_files_dir), "%s", p);
    }
}

/* Makes every directory along path, mode 0755. NULL or empty returns.
 * mkdir errors are ignored. */
void np_mkdir_p(const char *path)
{
    char buf[NP_MAX_PATH];
    char *s;
    if (!path || !path[0]) {
        return;
    }
    snprintf(buf, sizeof(buf), "%s", path);
    for (s = buf + 1; *s; s++) {
        if (*s == '/') {
            *s = 0;
            mkdir(buf, 0755);
            *s = '/';
        }
    }
    mkdir(buf, 0755);
}

/* files dir if set, else desktop ~/.config or Android HOME. The non-UI Android
 * build tries SDL internal storage first. Does not create it. */
void np_cfg_root(char *out, size_t n)
{
    if (g_files_dir[0]) {
        snprintf(out, n, "%s", g_files_dir);
        return;
    }
#if defined(__ANDROID__) && !defined(NP_ANDROID_UI)
    {
        const char *p = SDL_AndroidGetInternalStoragePath();
        if (p && p[0]) {
            snprintf(out, n, "%s", p);
            return;
        }
    }
#endif
#ifdef __ANDROID__
    {
        const char *h = getenv("HOME");
        if (h && h[0]) {
            snprintf(out, n, "%s", h);
            return;
        }
    }
#else
    {
        const char *h = getenv("HOME");
        if (h && h[0]) {
            snprintf(out, n, "%s/.config", h);
            return;
        }
    }
#endif
    snprintf(out, n, ".");
}

/* <config root>/exg-c/raw, and creates it. out receives the directory. */
static void raw_root(char *out, int n)
{
    char root[NP_MAX_PATH];
    np_cfg_root(root, sizeof(root));
    mkdir(root, 0755);
    snprintf(out, (size_t)n, "%s/exg-c/raw", root);
    mkdir(out, 0755);
}

/* <raw>/<which>.nprw. which is the file name as given, with no sanitize. */
void raw_plate_path(const char *which, char *out, int n)
{
    char dir[NP_MAX_PATH];
    raw_root(dir, (int)sizeof(dir));
    snprintf(out, (size_t)n, "%s/%s.nprw", dir, which);
}

/* <raw>/<kind>/<sanitized name>.nprw. Creates the kind directory. */
void raw_named_path(const char *kind, const char *name, char *out, int n)
{
    char dir[NP_MAX_PATH], sub[NP_MAX_PATH], safe[NP_ATOM_NAME];
    raw_root(dir, (int)sizeof(dir));
    snprintf(sub, sizeof(sub), "%s/%s", dir, kind);
    mkdir(sub, 0755);
    atom_sanitize(safe, (int)sizeof(safe), name);
    snprintf(out, (size_t)n, "%s/%s.nprw", sub, safe);
}

/* Writes the newest n_samp live samples as planar µV. Returns if n_samp < 16
 * or malloc fails. Caps at NP_RING. */
void raw_dump_ring(const char *path, uint32_t n_samp)
{
    float *planar;
    int c;
    if (!path || n_samp < 16) {
        return;
    }
    if (n_samp > NP_RING) {
        n_samp = NP_RING;
    }
    planar = (float *)malloc((size_t)NP_NCHAN * n_samp * sizeof(float));
    if (!planar) {
        return;
    }
    memset(planar, 0, (size_t)NP_NCHAN * n_samp * sizeof(float));
    for (c = 0; c < NP_NCHAN; c++) {
        float tmp[NP_RING];
        uint32_t n = np_ring_copy(&g.ring, c, tmp, n_samp);
        if (n > n_samp) {
            n = n_samp;
        }
        if (n > 0) {
            memcpy(planar + c * n_samp, tmp, (size_t)n * sizeof(float));
        }
    }
    np_raw_save(path, planar, NP_NCHAN, (int)n_samp, design_sps());
    free(planar);
}

/* Fills buf and nn from <which>.nprw. 0 on success. -1 if missing, shorter than
 * 16 samples, or malloc fails. The file's sample rate is ignored. */
int raw_load_plate(const char *which, float buf[NP_NCHAN][NP_RING], uint32_t nn[NP_NCHAN])
{
    char path[NP_MAX_PATH];
    float *planar;
    int ch = 0, ns = 0, c;
    float sps = 0.f;
    raw_plate_path(which, path, (int)sizeof(path));
    planar = (float *)malloc((size_t)NP_NCHAN * NP_RING * sizeof(float));
    if (!planar) {
        return -1;
    }
    if (np_raw_load(path, planar, NP_NCHAN * NP_RING, &ch, &ns, &sps) < 1 || ns < 16) {
        free(planar);
        return -1;
    }
    if (ns > NP_RING) {
        ns = NP_RING;
    }
    memset(nn, 0, NP_NCHAN * sizeof(nn[0]));
    memset(buf, 0, sizeof(float) * (size_t)NP_NCHAN * NP_RING);
    for (c = 0; c < ch && c < NP_NCHAN; c++) {
        memcpy(buf[c], planar + c * ns, (size_t)ns * sizeof(float));
        nn[c] = (uint32_t)ns;
    }
    free(planar);
    return 0;
}
