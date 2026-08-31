#include "nplearn.h"

#include <stdio.h>

int npl_save(const struct npl *L, const char *path)
{
    unsigned char buf[8192];
    int n;
    FILE *f;
    if (!path) {
        return -1;
    }
    n = npl_export(L, buf, (int)sizeof(buf));
    if (n < 0) {
        return -1;
    }
    f = fopen(path, "wb");
    if (!f) {
        return -1;
    }
    if ((int)fwrite(buf, 1, (size_t)n, f) != n) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

int npl_load(struct npl *L, const char *path)
{
    unsigned char buf[8192];
    size_t n;
    FILE *f;
    if (!path) {
        return -1;
    }
    f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    return npl_import(L, buf, (int)n);
}
