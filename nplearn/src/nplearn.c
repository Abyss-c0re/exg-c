#include "nplearn.h"

#include <math.h>
#include <string.h>

void npl_init(struct npl *L)
{
    memset(L, 0, sizeof(*L));
    L->sel = -1;
    L->best = -1;
    L->match = 1;
}

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
    L->sel = slot;
    return slot;
}

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

void npl_score(struct npl *L, const float wave[NPL_NCHAN][NPL_LEN], const float rms[NPL_NCHAN],
               uint8_t mask)
{
    int i, c;
    if (!L) {
        return;
    }
    L->best = -1;
    for (i = 0; i < L->n; i++) {
        uint8_t m = (uint8_t)(L->s[i].mask & mask);
        float sw = 0.f, sr;
        int nc = 0;
        if (!m) {
            L->score[i] = 0.f;
            continue;
        }
        for (c = 0; c < NPL_NCHAN; c++) {
            int k;
            double dot = 0;
            if (!(m & (uint8_t)(1u << c))) {
                continue;
            }
            for (k = 0; k < NPL_LEN; k++) {
                dot += (double)wave[c][k] * (double)L->s[i].wave[c][k];
            }
            if (dot > 0.0) {
                sw += (float)dot;
            }
            nc++;
        }
        if (nc) {
            sw /= (float)nc;
        }
        sr = cosine8(rms, L->s[i].rms, m);
        if (sr < 0.f) {
            sr = 0.f;
        }
        L->score[i] = 0.60f * sw + 0.40f * sr;
        if (L->best < 0 || L->score[i] > L->score[L->best]) {
            L->best = i;
        }
    }
}

int npl_bound(void)
{
    /* magic4 + ver1 + n1 + NPL_MAX * (name + mask + rms + wave) */
    return 6 + NPL_MAX * (NPL_NAME + 1 + (int)sizeof(float) * (NPL_NCHAN + NPL_NCHAN * NPL_LEN));
}

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

int npl_export(const struct npl *L, void *buf, int cap)
{
    unsigned char *p = (unsigned char *)buf;
    int left = cap, i;
    unsigned char ver = 1, n;
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
            put(&p, &left, L->s[i].wave, (int)sizeof(L->s[i].wave))) {
            return -1;
        }
    }
    return (int)(p - (unsigned char *)buf);
}

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
    if (ver != 1 || cnt > NPL_MAX) {
        return -1;
    }
    p += 6;
    n -= 6;
    npl_init(L);
    L->n = cnt;
    for (i = 0; i < L->n; i++) {
        int need = NPL_NAME + 1 + (int)sizeof(L->s[i].rms) + (int)sizeof(L->s[i].wave);
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
        n -= need;
        L->s[i].name[NPL_NAME - 1] = 0;
    }
    return 0;
}
