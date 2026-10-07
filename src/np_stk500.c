#include "np_stk500.h"

#include <stdio.h>
#include <string.h>

static void err_set(char *err, int err_n, const char *msg)
{
    if (err && err_n > 0) {
        snprintf(err, (size_t)err_n, "%s", msg);
    }
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int hex_byte(const char *s, unsigned *out)
{
    int hi, lo;
    if (!s || !s[0] || !s[1]) {
        return -1;
    }
    hi = hex_nibble(s[0]);
    lo = hex_nibble(s[1]);
    if (hi < 0 || lo < 0) {
        return -1;
    }
    *out = (unsigned)((hi << 4) | lo);
    return 0;
}

int np_ihex_decode(const char *text, unsigned char *dst, int cap, int *out_len,
                   char *err, int err_n)
{
    const char *p;
    int highest = -1;
    int saw_eof = 0;
    unsigned base = 0;

    if (!text || !dst || cap < 1 || !out_len) {
        err_set(err, err_n, "bad hex buffer");
        return -1;
    }
    memset(dst, 0xFF, (size_t)cap);
    *out_len = 0;
    p = text;
    while (*p) {
        unsigned len = 0, addr = 0, type = 0, sum, got, i;
        unsigned data[256];

        while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t') {
            p++;
        }
        if (!*p) {
            break;
        }
        if (*p != ':') {
            err_set(err, err_n, "hex line does not start with ':'");
            return -1;
        }
        p++;
        if (hex_byte(p, &len) != 0 || hex_byte(p + 2, &addr) != 0) {
            err_set(err, err_n, "truncated hex record");
            return -1;
        }
        addr = (addr << 8);
        {
            unsigned lo = 0;
            if (hex_byte(p + 4, &lo) != 0 || hex_byte(p + 6, &type) != 0) {
                err_set(err, err_n, "truncated hex record");
                return -1;
            }
            addr |= lo;
        }
        p += 8;
        if (len > 255) {
            err_set(err, err_n, "hex record too long");
            return -1;
        }
        sum = (len + ((addr >> 8) & 0xFF) + (addr & 0xFF) + type) & 0xFF;
        for (i = 0; i < len; i++) {
            if (hex_byte(p, &data[i]) != 0) {
                err_set(err, err_n, "truncated hex data");
                return -1;
            }
            sum = (sum + data[i]) & 0xFF;
            p += 2;
        }
        if (hex_byte(p, &got) != 0) {
            err_set(err, err_n, "truncated hex checksum");
            return -1;
        }
        p += 2;
        sum = (sum + got) & 0xFF;
        if (sum != 0) {
            err_set(err, err_n, "hex checksum mismatch");
            return -1;
        }
        if (type == 0x01) {
            saw_eof = 1;
            break;
        }
        if (type == 0x04) {
            if (len != 2) {
                err_set(err, err_n, "bad extended address");
                return -1;
            }
            base = (data[0] << 8) | data[1];
            if (base != 0) {
                err_set(err, err_n, "hex leaves the 328P flash");
                return -1;
            }
            continue;
        }
        if (type == 0x05 || type == 0x03) {
            continue;
        }
        if (type != 0x00) {
            err_set(err, err_n, "unsupported hex record");
            return -1;
        }
        if (base != 0) {
            err_set(err, err_n, "hex leaves the 328P flash");
            return -1;
        }
        if ((int)addr + (int)len > cap || (int)addr + (int)len > NP_STK_APP_MAX) {
            err_set(err, err_n, "hex overlaps the bootloader");
            return -1;
        }
        for (i = 0; i < len; i++) {
            dst[addr + i] = (unsigned char)data[i];
        }
        if ((int)addr + (int)len - 1 > highest) {
            highest = (int)addr + (int)len - 1;
        }
    }
    if (!saw_eof || highest < 0) {
        err_set(err, err_n, "hex has no program");
        return -1;
    }
    *out_len = highest + 1;
    return 0;
}

#define STK_OK 0x10
#define STK_INSYNC 0x14
#define STK_CRC_EOP 0x20
#define STK_GET_SYNC 0x30
#define STK_ENTER_PROGMODE 0x50
#define STK_LEAVE_PROGMODE 0x51
#define STK_LOAD_ADDRESS 0x55
#define STK_PROG_PAGE 0x64
#define STK_READ_PAGE 0x74
#define STK_READ_SIGN 0x75

static int read_full(const struct np_stk_io *io, unsigned char *buf, int n, int timeout_ms)
{
    int got = 0;
    int left = timeout_ms < 1 ? 1 : timeout_ms;

    while (got < n && left > 0) {
        int slice = left > 40 ? 40 : left;
        int r = io->read(io->ctx, buf + got, n - got, slice);
        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            left -= slice;
            continue;
        }
        got += r;
    }
    return got;
}

static int stk_ok(const struct np_stk_io *io, char *err, int err_n)
{
    unsigned char b[2];
    int n = read_full(io, b, 2, 500);
    if (n != 2 || b[0] != STK_INSYNC || b[1] != STK_OK) {
        err_set(err, err_n, "bootloader rejected a command");
        return -1;
    }
    return 0;
}

static int stk_cmd(const struct np_stk_io *io, const unsigned char *cmd, int n, char *err, int err_n)
{
    if (!io->write || io->write(io->ctx, cmd, n) != n) {
        err_set(err, err_n, "serial write failed");
        return -1;
    }
    return stk_ok(io, err, err_n);
}

static void stk_note(const struct np_stk_io *io, const char *msg);

static int stk_sync(const struct np_stk_io *io, char *err, int err_n)
{
    int i;

    if (io->pulse_dtr) {
        io->pulse_dtr(io->ctx);
    }
    stk_note(io, "reset into bootloader");
    /* Each miss waits the 40 ms read. 25 tries stay inside optiboot's second. */
    for (i = 0; i < 25; i++) {
        unsigned char cmd[2] = {STK_GET_SYNC, STK_CRC_EOP};
        unsigned char b[2];
        int n;
        if (!io->write || io->write(io->ctx, cmd, 2) != 2) {
            err_set(err, err_n, "serial write failed");
            return -1;
        }
        n = read_full(io, b, 2, 40);
        if (n == 2 && b[0] == STK_INSYNC && b[1] == STK_OK) {
            return 0;
        }
    }
    err_set(err, err_n, "bootloader did not answer");
    return -1;
}

static int stk_signature(const struct np_stk_io *io, char *err, int err_n)
{
    unsigned char cmd[2] = {STK_READ_SIGN, STK_CRC_EOP};
    unsigned char b[5];
    int n;

    if (!io->write || io->write(io->ctx, cmd, 2) != 2) {
        err_set(err, err_n, "serial write failed");
        return -1;
    }
    n = read_full(io, b, 5, 500);
    if (n != 5 || b[0] != STK_INSYNC || b[4] != STK_OK) {
        err_set(err, err_n, "no chip signature");
        return -1;
    }
    /* ATmega328P. Refuse anything else so a wrong board is not erased. */
    if (b[1] != 0x1E || b[2] != 0x95 || b[3] != 0x0F) {
        err_set(err, err_n, "not an ATmega328P");
        return -1;
    }
    return 0;
}

static void stk_note(const struct np_stk_io *io, const char *msg)
{
    if (io && io->note && msg) {
        io->note(io->ctx, msg);
    }
}

/* Optiboot turns the word address into a byte address. Byte 0 is word 0.
 * Two bytes keeps the page length even. Byte 1 is left 0xFF. */
static int stk_eeprom_mode(const struct np_stk_io *io, unsigned char value, char *err, int err_n)
{
    unsigned char cmd[8];

    cmd[0] = STK_LOAD_ADDRESS;
    cmd[1] = 0;
    cmd[2] = 0;
    cmd[3] = STK_CRC_EOP;
    if (stk_cmd(io, cmd, 4, err, err_n) != 0) {
        return -1;
    }
    cmd[0] = STK_PROG_PAGE;
    cmd[1] = 0;
    cmd[2] = 2;
    cmd[3] = 'E';
    cmd[4] = value;
    cmd[5] = 0xFF;
    cmd[6] = STK_CRC_EOP;
    return stk_cmd(io, cmd, 7, err, err_n);
}

int np_stk_program(const struct np_stk_io *io, const unsigned char *image, int len,
                   char *err, int err_n)
{
    return np_stk_program_ex(io, image, len, -1, err, err_n);
}

int np_stk_program_ex(const struct np_stk_io *io, const unsigned char *image, int len,
                      int eeprom_byte, char *err, int err_n)
{
    unsigned char page[NP_STK_PAGE];
    unsigned char cmd[4 + NP_STK_PAGE + 1];
    unsigned char rd[1 + NP_STK_PAGE + 1];
    char note[64];
    int off;
    int pages;
    int page_i = 0;

    if (err && err_n > 0) {
        err[0] = 0;
    }
    if (!io || !io->write || !io->read) {
        err_set(err, err_n, "bad firmware image");
        return -1;
    }
    if (len < 0 || len > NP_STK_APP_MAX || (len > 0 && !image) || (len < 1 && eeprom_byte < 0)) {
        err_set(err, err_n, "bad firmware image");
        return -1;
    }
    if (stk_sync(io, err, err_n) != 0) {
        return -1;
    }
    stk_note(io, "bootloader sync");
    if (stk_signature(io, err, err_n) != 0) {
        return -1;
    }
    stk_note(io, "ATmega328P");
    cmd[0] = STK_ENTER_PROGMODE;
    cmd[1] = STK_CRC_EOP;
    if (stk_cmd(io, cmd, 2, err, err_n) != 0) {
        return -1;
    }
    pages = len > 0 ? (len + NP_STK_PAGE - 1) / NP_STK_PAGE : 0;
    for (off = 0; off < len; off += NP_STK_PAGE) {
        unsigned word;
        int n = len - off;
        int i;
        if (n > NP_STK_PAGE) {
            n = NP_STK_PAGE;
        }
        memset(page, 0xFF, sizeof(page));
        memcpy(page, image + off, (size_t)n);
        word = (unsigned)(off / 2);
        cmd[0] = STK_LOAD_ADDRESS;
        cmd[1] = (unsigned char)(word & 0xFF);
        cmd[2] = (unsigned char)((word >> 8) & 0xFF);
        cmd[3] = STK_CRC_EOP;
        if (stk_cmd(io, cmd, 4, err, err_n) != 0) {
            return -1;
        }
        cmd[0] = STK_PROG_PAGE;
        cmd[1] = 0;
        cmd[2] = NP_STK_PAGE;
        cmd[3] = 'F';
        memcpy(cmd + 4, page, NP_STK_PAGE);
        cmd[4 + NP_STK_PAGE] = STK_CRC_EOP;
        if (stk_cmd(io, cmd, 4 + NP_STK_PAGE + 1, err, err_n) != 0) {
            return -1;
        }
        cmd[0] = STK_LOAD_ADDRESS;
        cmd[1] = (unsigned char)(word & 0xFF);
        cmd[2] = (unsigned char)((word >> 8) & 0xFF);
        cmd[3] = STK_CRC_EOP;
        if (stk_cmd(io, cmd, 4, err, err_n) != 0) {
            return -1;
        }
        cmd[0] = STK_READ_PAGE;
        cmd[1] = 0;
        cmd[2] = NP_STK_PAGE;
        cmd[3] = 'F';
        cmd[4] = STK_CRC_EOP;
        if (!io->write || io->write(io->ctx, cmd, 5) != 5) {
            err_set(err, err_n, "serial write failed");
            return -1;
        }
        i = read_full(io, rd, 1 + NP_STK_PAGE + 1, 800);
        if (i != 1 + NP_STK_PAGE + 1 || rd[0] != STK_INSYNC || rd[NP_STK_PAGE + 1] != STK_OK) {
            err_set(err, err_n, "read-back failed");
            return -1;
        }
        if (memcmp(rd + 1, page, NP_STK_PAGE) != 0) {
            err_set(err, err_n, "read-back mismatch");
            return -1;
        }
        page_i++;
        if (page_i == 1 || page_i == pages || (page_i % 8) == 0) {
            snprintf(note, sizeof(note), "page %d/%d", page_i, pages);
            stk_note(io, note);
        }
    }
    if (eeprom_byte >= 0) {
        if (eeprom_byte > 2) {
            eeprom_byte = 0;
        }
        snprintf(note, sizeof(note), "mode byte %d", eeprom_byte);
        stk_note(io, note);
        if (stk_eeprom_mode(io, (unsigned char)eeprom_byte, err, err_n) != 0) {
            return -1;
        }
    }
    cmd[0] = STK_LEAVE_PROGMODE;
    cmd[1] = STK_CRC_EOP;
    if (stk_cmd(io, cmd, 2, err, err_n) != 0) {
        return -1;
    }
    stk_note(io, "left bootloader");
    return 0;
}
