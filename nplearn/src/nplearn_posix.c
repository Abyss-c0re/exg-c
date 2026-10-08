#include "nplearn.h"

#include <stdio.h>

/* Path save and load around the template blob.
 * Core does not open files. The buffer is 8192 bytes. */

/* Export into an 8192-byte buffer and write that many bytes.
 * Returns -1 when path is null, export fails, or the write is short. */
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

/* Read at most 8192 bytes and import them. A longer file is cut off.
 * Returns 0 on a good blob, or -1 when path is null, the file is missing, or the blob is bad. */
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
