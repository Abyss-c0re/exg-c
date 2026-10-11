#include "nplearn.h"

#include <math.h>
#include <string.h>

/* Eight-channel template matcher, no heap.
 * Each saved channel keeps a tone, a tone pair, or a bump.
 * npl_score compares the live wave with that function.
 * Cube and SMX are stored beside the pose and do not change the match. */

/* Bins 1..16 of the 64-bin wave. A repeat of a kept tone scores this fraction
 * or better, which is above the 0.55 gate. A weaker fit stays a lag cosine. */
#define NPL_FIT_K 16
#define NPL_FIT_KEEP 0.70f
#define NPL_FIT_MORE 0.06f

static const int bump_sig[6] = {2, 3, 4, 6, 8, 12};
static float bump_kern[6][NPL_LEN];

/* Zero the library, select nothing, and turn matching on. L must be non-null. */
void npl_init(struct npl *L)
{
    memset(L, 0, sizeof(*L));
    L->sel = -1;
    L->best = -1;
    L->match = 1;
}

/* Copy src into a 24-byte name and pad the rest with zeros. A null src stores an empty name. */
static void copy_name(char *dst, const char *src)
{
    int i;
    for (i = 0; i < NPL_NAME - 1 && src && src[i]; i++) {
        dst[i] = src[i];
    }
    for (; i < NPL_NAME; i++) {
        dst[i] = 0;
    }
}

/* Fills the six Gaussian widths once. Score and fit share them, so a live
 * compare does not call expf. */
static void warm_kern(void)
{
    static int ready;
    int s, d;

    if (ready) {
        return;
    }
    for (s = 0; s < 6; s++) {
        float sg = (float)bump_sig[s];
        for (d = 0; d < NPL_LEN; d++) {
            float x = (float)d / sg;
            bump_kern[s][d] = expf(-0.5f * x * x);
        }
    }
    ready = 1;
}

/* Index of sig in bump_sig, or -1. */
static int sig_slot(int sig)
{
    int i;
    for (i = 0; i < 6; i++) {
        if (bump_sig[i] == sig) {
            return i;
        }
    }
    return -1;
}

/* Energy of orthonormal bin k. k outside 1..31 returns 0.
 * One cos/sin, then a rotation, so a compare is not a libm call per sample. */
static float bin_energy(const float *w, int k)
{
    float scale, ang, cs, sn, c, s;
    double A = 0, B = 0;
    int n;

    if (k <= 0 || k >= NPL_LEN / 2) {
        return 0.f;
    }
    scale = sqrtf(2.f / (float)NPL_LEN);
    ang = (2.f * (float)M_PI * (float)k) / (float)NPL_LEN;
    cs = cosf(ang);
    sn = sinf(ang);
    c = scale;
    s = 0.f;
    for (n = 0; n < NPL_LEN; n++) {
        float nc, ns;
        A += (double)w[n] * (double)c;
        B += (double)w[n] * (double)s;
        nc = c * cs - s * sn;
        ns = s * cs + c * sn;
        c = nc;
        s = ns;
    }
    return (float)(A * A + B * B);
}

/* Greatest-square correlation of w with a unit Gaussian of width sig, and the
 * center that produced it. corr is signed. A missing width leaves both at 0. */
static void bump_peak(const float *w, int sig, float *corr, int *mu)
{
    const float *kern;
    double live_e = 0;
    float best_sq = 0.f;
    int si, c, t;

    *corr = 0.f;
    *mu = 0;
    warm_kern();
    si = sig_slot(sig);
    if (si < 0) {
        return;
    }
    kern = bump_kern[si];
    for (t = 0; t < NPL_LEN; t++) {
        live_e += (double)w[t] * (double)w[t];
    }
    if (live_e < 1e-18) {
        return;
    }
    for (c = 0; c < NPL_LEN; c++) {
        double dot = 0, e = 0;
        float cr, sq;
        for (t = 0; t < NPL_LEN; t++) {
            int d = t - c;
            float g;
            if (d < 0) {
                d = -d;
            }
            g = kern[d];
            dot += (double)w[t] * (double)g;
            e += (double)g * (double)g;
        }
        if (e < 1e-18) {
            continue;
        }
        cr = (float)(dot / sqrt(e * live_e));
        sq = cr * cr;
        if (sq > best_sq) {
            best_sq = sq;
            *corr = cr;
            *mu = c;
        }
    }
}

/* Max positive correlation against a unit Gaussian of width sig.
 * neg flips the kernel, so a downward template matches a downward live wave.
 * Every center in the 64-bin window is tried. sig outside the table returns 0. */
static float bump_match(const float *live, int sig, int neg)
{
    const float *kern;
    double live_e = 0;
    float best = 0.f;
    int si, c, t;

    warm_kern();
    si = sig_slot(sig);
    if (si < 0) {
        return 0.f;
    }
    kern = bump_kern[si];
    for (t = 0; t < NPL_LEN; t++) {
        live_e += (double)live[t] * (double)live[t];
    }
    if (live_e < 1e-18) {
        return 0.f;
    }
    for (c = 0; c < NPL_LEN; c++) {
        double dot = 0, e = 0;
        float cr;
        for (t = 0; t < NPL_LEN; t++) {
            int d = t - c;
            float g;
            if (d < 0) {
                d = -d;
            }
            g = kern[d];
            dot += (double)live[t] * (double)g;
            e += (double)g * (double)g;
        }
        if (e < 1e-18) {
            continue;
        }
        cr = (float)(dot / sqrt(e * live_e));
        if (neg) {
            cr = -cr;
        }
        if (cr > best) {
            best = cr;
        }
    }
    return best;
}

/* Best positive overlap cosine for lags -32..32.
 * Divides by the overlapping samples only. Overlap under 8 is skipped.
 * A negative cosine is ignored. */
static float slide_cosine(const float *live, const float *tmpl)
{
    int lag;
    float best = 0.f;

    for (lag = -32; lag <= 32; lag++) {
        int ov = NPL_LEN - (lag < 0 ? -lag : lag);
        int i;
        double dot = 0, el = 0, et = 0;
        float c;
        if (ov < 8) {
            continue;
        }
        for (i = 0; i < NPL_LEN; i++) {
            int j = i - lag;
            if (j < 0 || j >= NPL_LEN) {
                continue;
            }
            dot += (double)live[i] * (double)tmpl[j];
            el += (double)live[i] * (double)live[i];
            et += (double)tmpl[j] * (double)tmpl[j];
        }
        if (el < 1e-18 || et < 1e-18) {
            continue;
        }
        c = (float)(dot / sqrt(el * et));
        if (c > best) {
            best = c;
        }
    }
    return best;
}

/* Pick the function for one masked channel.
 * One tone first. A bump replaces it when the bump captures 0.06 more energy.
 * A second tone replaces the winner only when that extra bin is above 0.06 and
 * the pair beats the winner by 0.06, so a blink stays a bump.
 * The model is kept only when a repeat of it would score at least 0.70.
 * Below that, kind stays 0 and the lag cosine is used. A near-zero wave returns. */
static void fit_channel(struct npl_sample *s, int ch)
{
    const float *w = s->wave[ch];
    double energy = 0;
    int i, k, bestk = 1, secondk = 0;
    int bump_mu = 0, bump_sg = bump_sig[0];
    float best = 0.f, sec = 0.f;
    float bump_e = 0.f, bump_c = 0.f;
    float captured, self;
    int kind;

    for (i = 0; i < NPL_LEN; i++) {
        energy += (double)w[i] * (double)w[i];
    }
    if (energy < 1e-8) {
        return;
    }
    for (k = 1; k <= NPL_FIT_K; k++) {
        float ek = bin_energy(w, k);
        if (ek > best) {
            if (best > 0.f) {
                sec = best;
                secondk = bestk;
            }
            best = ek;
            bestk = k;
        } else if (ek > sec) {
            sec = ek;
            secondk = k;
        }
    }
    for (i = 0; i < 6; i++) {
        float cr;
        int mu;
        bump_peak(w, bump_sig[i], &cr, &mu);
        if (cr * cr > bump_e) {
            bump_e = cr * cr;
            bump_c = cr;
            bump_mu = mu;
            bump_sg = bump_sig[i];
        }
    }
    kind = 1;
    captured = best;
    if (bump_e > captured + NPL_FIT_MORE) {
        kind = 3;
        captured = bump_e;
    }
    if (sec > NPL_FIT_MORE && secondk > 0 && secondk != bestk &&
        (best + sec) > captured + NPL_FIT_MORE) {
        kind = 2;
        captured = best + sec;
    }
    if (kind == 3) {
        self = bump_c < 0.f ? -bump_c : bump_c;
    } else {
        self = captured;
    }
    if (self < NPL_FIT_KEEP) {
        return;
    }
    s->fit_kind[ch] = (uint8_t)kind;
    if (kind == 3) {
        s->fit_k0[ch] = bump_c < 0.f ? 1 : 0;
        s->fit_mu[ch] = (uint8_t)bump_mu;
        s->fit_sig[ch] = (uint8_t)bump_sg;
        return;
    }
    s->fit_k0[ch] = (uint8_t)bestk;
    if (kind == 2) {
        s->fit_k1[ch] = (uint8_t)secondk;
    }
}

/* Rebuild every masked channel. Others stay kind 0. */
static void fit_sample(struct npl_sample *s)
{
    int c;
    if (!s) {
        return;
    }
    for (c = 0; c < NPL_NCHAN; c++) {
        s->fit_kind[c] = 0;
        s->fit_k0[c] = 0;
        s->fit_k1[c] = 0;
        s->fit_mu[c] = 0;
        s->fit_sig[c] = 0;
        if (s->mask & (uint8_t)(1u << c)) {
            fit_channel(s, c);
        }
    }
}

/* Tone fidelity, bump correlation, or a lag cosine. Result is 0..1.
 * Tone score is the sum of sqrt(template-bin energy × live-bin energy).
 * Phase cancels. Capped at 1. */
static float shape_channel(const struct npl_sample *s, int ch, const float *live)
{
    uint8_t kind = s->fit_kind[ch];

    if (kind == 1 || kind == 2) {
        int ks[2];
        int nbin = (int)kind;
        int i;
        float num = 0.f;
        ks[0] = s->fit_k0[ch];
        ks[1] = s->fit_k1[ch];
        if (kind == 2 && ks[1] == 0) {
            nbin = 1;
        }
        for (i = 0; i < nbin; i++) {
            float et = bin_energy(s->wave[ch], ks[i]);
            float el = bin_energy(live, ks[i]);
            if (et < 1e-8f || el <= 0.f) {
                continue;
            }
            num += sqrtf(et * el);
        }
        if (num > 1.f) {
            num = 1.f;
        }
        return num;
    }
    if (kind == 3) {
        return bump_match(live, s->fit_sig[ch], s->fit_k0[ch] ? 1 : 0);
    }
    return slide_cosine(live, s->wave[ch]);
}

/* Replace the sample with the same name, or append. Returns the slot, -1 for bad arguments including a zero mask, or -2 when 16 slots are full.
 * Cube and SMX on that slot are cleared. The tone or bump is fit from the wave. */
int npl_add(struct npl *L, const char *name, const float wave[NPL_NCHAN][NPL_LEN],
            const float rms[NPL_NCHAN], uint8_t mask)
{
    int i, slot = -1;

    if (!L || !name || !name[0] || !mask || !wave || !rms) {
        return -1;
    }
    for (i = 0; i < L->n; i++) {
        int k = 0;
        while (L->s[i].name[k] && name[k] && L->s[i].name[k] == name[k]) {
            k++;
        }
        if (L->s[i].name[k] == 0 && name[k] == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (L->n >= NPL_MAX) {
            return -2;
        }
        slot = L->n++;
    }
    memset(&L->s[slot], 0, sizeof(L->s[slot]));
    copy_name(L->s[slot].name, name);
    L->s[slot].mask = mask;
    memcpy(L->s[slot].rms, rms, sizeof(L->s[slot].rms));
    memcpy(L->s[slot].wave, wave, sizeof(L->s[slot].wave));
    L->s[slot].have_cube = 0;
    memset(L->s[slot].cube, 0, sizeof(L->s[slot].cube));
    fit_sample(&L->s[slot]);
    L->sel = slot;
    return slot;
}

/* Store 64 cube bytes on slot i and mark them present. A bad index or a null cube returns. */
void npl_set_cube(struct npl *L, int i, const uint8_t cube[64])
{
    if (!L || i < 0 || i >= L->n || !cube) {
        return;
    }
    memcpy(L->s[i].cube, cube, 64);
    L->s[i].have_cube = 1;
}

/* Store up to 8 row bytes and the fold byte. A null rows or n < 1 clears the rows and still stores fold.
 * n above 8 is clipped. */
void npl_set_smx(struct npl *L, int i, const uint8_t *rows, int n, uint8_t fold)
{
    if (!L || i < 0 || i >= L->n) {
        return;
    }
    L->s[i].fold = fold;
    L->s[i].smx_n = 0;
    memset(L->s[i].smx, 0, NPL_SMX_SEC);
    if (!rows || n < 1) {
        return;
    }
    if (n > NPL_SMX_SEC) {
        n = NPL_SMX_SEC;
    }
    memcpy(L->s[i].smx, rows, (size_t)n);
    L->s[i].smx_n = (uint8_t)n;
}

/* Bit Jaccard of two 64-byte cubes. A null side, or two empty cubes, returns 0. */
float npl_cube_jaccard(const uint8_t a[64], const uint8_t b[64])
{
    int i, both = 0, either = 0;
    if (!a || !b) {
        return 0.f;
    }
    for (i = 0; i < 64; i++) {
        unsigned A = a[i], B = b[i], k;
        for (k = 0; k < 8; k++) {
            unsigned ba = (A >> k) & 1u, bb = (B >> k) & 1u;
            both += (int)(ba & bb);
            either += (int)(ba | bb);
        }
    }
    if (either < 1) {
        return 0.f;
    }
    return (float)both / (float)either;
}

/* Remove slot i and slide the tail down. Selection follows the shift. best is cleared. Out of range returns. */
void npl_del(struct npl *L, int i)
{
    if (!L || i < 0 || i >= L->n) {
        return;
    }
    if (i + 1 < L->n) {
        memmove(&L->s[i], &L->s[i + 1], (size_t)(L->n - i - 1) * sizeof(L->s[0]));
    }
    L->n--;
    memset(&L->s[L->n], 0, sizeof(L->s[0]));
    if (L->sel == i) {
        L->sel = L->n ? (i < L->n ? i : L->n - 1) : -1;
    } else if (L->sel > i) {
        L->sel--;
    }
    L->best = -1;
}

/* Cosine over the channels set in mask. A near-zero side returns 0. A negative cosine is kept. */
static float cosine8(const float *a, const float *b, uint8_t mask)
{
    int c;
    double da = 0, db = 0, dot = 0;
    for (c = 0; c < NPL_NCHAN; c++) {
        if (!(mask & (uint8_t)(1u << c))) {
            continue;
        }
        da += (double)a[c] * (double)a[c];
        db += (double)b[c] * (double)b[c];
        dot += (double)a[c] * (double)b[c];
    }
    if (da < 1e-18 || db < 1e-18) {
        return 0.f;
    }
    return (float)(dot / (sqrt(da) * sqrt(db)));
}

/* Shape is the mean, over masked channels, of the fitted function: tone-bin
 * fidelity, bump correlation, or a lag cosine when the fit is weak.
 * The pose score is that mean times (0.55 + 0.45 times the RMS cosine).
 * A wrong shape stays under the gate even when the RMS matches.
 * best is set only when the top score is at least 0.55 and leads by 0.08.
 * Otherwise every score is zeroed and best is -1. Never print the score as a percent. */
void npl_score(struct npl *L, const float wave[NPL_NCHAN][NPL_LEN], const float rms[NPL_NCHAN],
               uint8_t mask)
{
    int i, c, best = -1, second = -1;
    float bests = -1.f, secs = -1.f;
    if (!L) {
        return;
    }
    L->best = -1;
    if (!wave || !rms) {
        return;
    }
    for (i = 0; i < L->n; i++) {
        uint8_t m = (uint8_t)(L->s[i].mask & mask);
        float sw = 0.f, sr, s;
        int nc = 0;
        if (!m) {
            L->score[i] = 0.f;
            continue;
        }
        for (c = 0; c < NPL_NCHAN; c++) {
            if (!(m & (uint8_t)(1u << c))) {
                continue;
            }
            sw += shape_channel(&L->s[i], c, wave[c]);
            nc++;
        }
        if (nc) {
            sw /= (float)nc;
        }
        sr = cosine8(rms, L->s[i].rms, m);
        if (sr < 0.f) {
            sr = 0.f;
        }
        /* RMS cosine is scale-blind. It scales the shape. It does not replace it. */
        s = sw * (0.55f + 0.45f * sr);
        L->score[i] = s;
        if (s > bests) {
            secs = bests;
            second = best;
            bests = s;
            best = i;
        } else if (s > secs) {
            secs = s;
            second = i;
        }
    }
    if (best >= 0 && bests >= 0.55f && (second < 0 || bests - secs >= 0.08f)) {
        L->best = best;
    } else {
        for (i = 0; i < L->n; i++) {
            L->score[i] = 0.f;
        }
    }
}

/* Jaccard of each stored cube against cube. A slot with no cube scores 0.
 * Wave scores and best stay as they were. A null L or cube returns. */
void npl_score_cube(struct npl *L, const uint8_t cube[64])
{
    int i;
    if (!L || !cube) {
        return;
    }
    for (i = 0; i < L->n; i++) {
        if (!L->s[i].have_cube) {
            L->score_cube[i] = 0.f;
            continue;
        }
        /* Occupied-cell Jaccard. Empty vs empty is 0. Does not replace wave score. */
        L->score_cube[i] = npl_cube_jaccard(L->s[i].cube, cube);
    }
}

/* Does nothing. The arguments are ignored, and MATCH is unchanged. */
void npl_score_smx(struct npl *L, const uint8_t *rows, int n)
{
    /* SMX is stored on the pose. It does not mix into MATCH. */
    (void)L;
    (void)rows;
    (void)n;
}

/* Byte size of a full version-3 export: header plus 16 templates with wave, RMS, cube, and SMX. */
int npl_bound(void)
{
    /* v3: cube + smx seconds + fold */
    return 6 + NPL_MAX * (NPL_NAME + 1 + 1 + 64 + NPL_SMX_SEC + 2 +
                          (int)sizeof(float) * (NPL_NCHAN + NPL_NCHAN * NPL_LEN));
}

/* Copy n bytes and advance. Returns -1 when fewer than n bytes remain, and then writes nothing. */
static int put(unsigned char **p, int *left, const void *src, int n)
{
    if (*left < n) {
        return -1;
    }
    memcpy(*p, src, (size_t)n);
    *p += n;
    *left -= n;
    return 0;
}

/* Write magic EXGL, version 3, and every template. Fit fields are not stored.
 * Returns the byte count, or -1 when L or buf is null or cap runs out. */
int npl_export(const struct npl *L, void *buf, int cap)
{
    unsigned char *p = (unsigned char *)buf;
    int left = cap, i;
    unsigned char ver = 3, n;
    if (!L || !buf) {
        return -1;
    }
    n = (unsigned char)L->n;
    if (put(&p, &left, "EXGL", 4) || put(&p, &left, &ver, 1) || put(&p, &left, &n, 1)) {
        return -1;
    }
    for (i = 0; i < L->n; i++) {
        if (put(&p, &left, L->s[i].name, NPL_NAME) || put(&p, &left, &L->s[i].mask, 1) ||
            put(&p, &left, L->s[i].rms, (int)sizeof(L->s[i].rms)) ||
            put(&p, &left, L->s[i].wave, (int)sizeof(L->s[i].wave)) ||
            put(&p, &left, &L->s[i].have_cube, 1) || put(&p, &left, L->s[i].cube, 64) ||
            put(&p, &left, L->s[i].smx, NPL_SMX_SEC) || put(&p, &left, &L->s[i].smx_n, 1) ||
            put(&p, &left, &L->s[i].fold, 1)) {
            return -1;
        }
    }
    return (int)(p - (unsigned char *)buf);
}

/* Accept EXGL version 1, 2, or 3. A short record re-inits L and returns -1.
 * Version 1 has no cube. Version 2 has no SMX. A good blob refits every channel. */
int npl_import(struct npl *L, const void *buf, int n)
{
    const unsigned char *p = (const unsigned char *)buf;
    unsigned char ver, cnt;
    int i;
    if (!L || !buf || n < 6 || memcmp(p, "EXGL", 4) != 0) {
        return -1;
    }
    ver = p[4];
    cnt = p[5];
    if ((ver != 1 && ver != 2 && ver != 3) || cnt > NPL_MAX) {
        return -1;
    }
    p += 6;
    n -= 6;
    npl_init(L);
    L->n = cnt;
    for (i = 0; i < L->n; i++) {
        int need = NPL_NAME + 1 + (int)sizeof(L->s[i].rms) + (int)sizeof(L->s[i].wave);
        if (ver >= 2) {
            need += 1 + 64;
        }
        if (ver >= 3) {
            need += NPL_SMX_SEC + 2;
        }
        if (n < need) {
            npl_init(L);
            return -1;
        }
        memcpy(L->s[i].name, p, NPL_NAME);
        p += NPL_NAME;
        L->s[i].mask = *p++;
        memcpy(L->s[i].rms, p, sizeof(L->s[i].rms));
        p += sizeof(L->s[i].rms);
        memcpy(L->s[i].wave, p, sizeof(L->s[i].wave));
        p += sizeof(L->s[i].wave);
        L->s[i].smx_n = 0;
        L->s[i].fold = 0;
        memset(L->s[i].smx, 0, NPL_SMX_SEC);
        if (ver >= 2) {
            L->s[i].have_cube = *p++;
            memcpy(L->s[i].cube, p, 64);
            p += 64;
        } else {
            L->s[i].have_cube = 0;
            memset(L->s[i].cube, 0, 64);
        }
        if (ver >= 3) {
            memcpy(L->s[i].smx, p, NPL_SMX_SEC);
            p += NPL_SMX_SEC;
            L->s[i].smx_n = *p++;
            L->s[i].fold = *p++;
            if (L->s[i].smx_n > NPL_SMX_SEC) {
                L->s[i].smx_n = NPL_SMX_SEC;
            }
        }
        n -= need;
        L->s[i].name[NPL_NAME - 1] = 0;
    }
    for (i = 0; i < L->n; i++) {
        fit_sample(&L->s[i]);
    }
    return 0;
}
