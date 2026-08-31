#include "np_learn.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void np_learn_init(struct np_learn *L)
{
    memset(L, 0, sizeof(*L));
    L->sel = -1;
    L->best = -1;
    L->match = 1;
}

void np_learn_pack(float *dst, const float *src, int n)
{
    int i, j;
    double e = 0;

    memset(dst, 0, NP_LEARN_LEN * sizeof(float));
    if (n < 4) {
        return;
    }
    for (i = 0; i < NP_LEARN_LEN; i++) {
        int a = (int)((long)i * n / NP_LEARN_LEN);
        int b = (int)((long)(i + 1) * n / NP_LEARN_LEN);
        float s = 0.f;
        int c = 0;
        if (b <= a) {
            b = a + 1;
        }
        if (b > n) {
            b = n;
        }
        for (j = a; j < b; j++) {
            s += src[j];
            c++;
        }
        dst[i] = c ? s / (float)c : 0.f;
    }
    for (i = 0; i < NP_LEARN_LEN; i++) {
        e += (double)dst[i] * (double)dst[i];
    }
    if (e < 1e-12) {
        return;
    }
    {
        float inv = 1.f / sqrtf((float)e);
        for (i = 0; i < NP_LEARN_LEN; i++) {
            dst[i] *= inv;
        }
    }
}

float np_learn_rms(const float *w)
{
    int i;
    double e = 0;
    for (i = 0; i < NP_LEARN_LEN; i++) {
        e += (double)w[i] * (double)w[i];
    }
    return sqrtf((float)(e / NP_LEARN_LEN));
}

int np_learn_add(struct np_learn *L, const char *name, const float wave[NP_NCHAN][NP_LEARN_LEN],
                 const float rms[NP_NCHAN], uint8_t mask)
{
    int i, slot = -1;
    char nm[NP_LEARN_NAME];

    if (!name || !name[0] || !mask) {
        return -1;
    }
    memset(nm, 0, sizeof(nm));
    snprintf(nm, sizeof(nm), "%s", name);
    for (i = 0; i < L->n; i++) {
        if (strcmp(L->t[i].name, nm) == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (L->n >= NP_LEARN_MAX) {
            return -2;
        }
        slot = L->n++;
    }
    memset(&L->t[slot], 0, sizeof(L->t[slot]));
    memcpy(L->t[slot].name, nm, NP_LEARN_NAME);
    L->t[slot].mask = mask;
    memcpy(L->t[slot].rms, rms, sizeof(L->t[slot].rms));
    memcpy(L->t[slot].wave, wave, sizeof(L->t[slot].wave));
    L->sel = slot;
    return slot;
}

void np_learn_del(struct np_learn *L, int i)
{
    if (i < 0 || i >= L->n) {
        return;
    }
    if (i + 1 < L->n) {
        memmove(&L->t[i], &L->t[i + 1], (size_t)(L->n - i - 1) * sizeof(L->t[0]));
    }
    L->n--;
    memset(&L->t[L->n], 0, sizeof(L->t[0]));
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
    for (c = 0; c < NP_NCHAN; c++) {
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

void np_learn_score(struct np_learn *L, const float wave[NP_NCHAN][NP_LEARN_LEN],
                    const float rms[NP_NCHAN], uint8_t mask)
{
    int i, c;
    L->best = -1;
    for (i = 0; i < L->n; i++) {
        uint8_t m = (uint8_t)(L->t[i].mask & mask);
        float sw = 0.f, sr;
        int nc = 0;
        if (!m) {
            L->score[i] = 0.f;
            continue;
        }
        for (c = 0; c < NP_NCHAN; c++) {
            int k;
            double dot = 0;
            if (!(m & (uint8_t)(1u << c))) {
                continue;
            }
            for (k = 0; k < NP_LEARN_LEN; k++) {
                dot += (double)wave[c][k] * (double)L->t[i].wave[c][k];
            }
            if (dot > 0.0) {
                sw += (float)dot;
            }
            nc++;
        }
        if (nc) {
            sw /= (float)nc;
        }
        sr = cosine8(rms, L->t[i].rms, m);
        if (sr < 0.f) {
            sr = 0.f;
        }
        /* Wave shape + amplitude mix. Both are after the learn filter. */
        L->score[i] = 0.60f * sw + 0.40f * sr;
        if (L->best < 0 || L->score[i] > L->score[L->best]) {
            L->best = i;
        }
    }
}

int np_learn_save(const struct np_learn *L, const char *path)
{
    FILE *f = fopen(path, "wb");
    unsigned char ver = 1;
    int i;
    if (!f) {
        return -1;
    }
    if (fwrite("EXGL", 1, 4, f) != 4 || fwrite(&ver, 1, 1, f) != 1) {
        fclose(f);
        return -1;
    }
    {
        unsigned char n = (unsigned char)L->n;
        fwrite(&n, 1, 1, f);
    }
    for (i = 0; i < L->n; i++) {
        fwrite(L->t[i].name, 1, NP_LEARN_NAME, f);
        fwrite(&L->t[i].mask, 1, 1, f);
        fwrite(L->t[i].rms, sizeof(float), NP_NCHAN, f);
        fwrite(L->t[i].wave, sizeof(float), NP_NCHAN * NP_LEARN_LEN, f);
    }
    fclose(f);
    return 0;
}

int np_learn_load(struct np_learn *L, const char *path)
{
    FILE *f = fopen(path, "rb");
    char mag[4];
    unsigned char ver, n;
    int i;
    if (!f) {
        return -1;
    }
    if (fread(mag, 1, 4, f) != 4 || memcmp(mag, "EXGL", 4) != 0 || fread(&ver, 1, 1, f) != 1 ||
        ver != 1 || fread(&n, 1, 1, f) != 1 || n > NP_LEARN_MAX) {
        fclose(f);
        return -1;
    }
    np_learn_init(L);
    L->n = n;
    for (i = 0; i < L->n; i++) {
        if (fread(L->t[i].name, 1, NP_LEARN_NAME, f) != NP_LEARN_NAME ||
            fread(&L->t[i].mask, 1, 1, f) != 1 ||
            fread(L->t[i].rms, sizeof(float), NP_NCHAN, f) != (size_t)NP_NCHAN ||
            fread(L->t[i].wave, sizeof(float), NP_NCHAN * NP_LEARN_LEN, f) !=
                (size_t)(NP_NCHAN * NP_LEARN_LEN)) {
            np_learn_init(L);
            fclose(f);
            return -1;
        }
        L->t[i].name[NP_LEARN_NAME - 1] = 0;
    }
    fclose(f);
    return 0;
}
