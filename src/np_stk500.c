#include "np_stk500.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* Optiboot STK500v1. One GET_SYNC at 115200 after the line is quiet.
 * Pages go out only after signature 1E 95 0F. */

/* Copy msg into err when err is non-null and err_n is above 0. */
static void err_set(char *err, int err_n, const char *msg)
{
    if (err && err_n > 0) {
        snprintf(err, (size_t)err_n, "%s", msg);
    }
}

/* 0..15 for one hex digit. Anything else returns -1. */
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

/* Two hex digits into *out. A short or non-hex pair returns -1. */
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

/* Intel hex into dst, filled with 0xFF first. The linear address must stay 0, and a record past cap or NP_STK_APP_MAX returns -1.
 * *out_len is one past the highest data byte. A missing EOF, or no data, returns -1. */
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

/* Read n bytes, in slices of at most 40 ms. timeout_ms below 1 is treated as 1 ms.
 * Returns the count received, which may be short, or -1 on a read error. */
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

/* Wait up to 500 ms for INSYNC 0x14 then OK 0x10. Any other reply returns -1. */
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

/* Write the whole command, then wait for INSYNC OK. A short write returns -1. */
static int stk_cmd(const struct np_stk_io *io, const unsigned char *cmd, int n, char *err, int err_n)
{
    if (!io->write || io->write(io->ctx, cmd, n) != n) {
        err_set(err, err_n, "serial write failed");
        return -1;
    }
    return stk_ok(io, err, err_n);
}

static void stk_note(const struct np_stk_io *io, const char *msg);
static int stk_drain(const struct np_stk_io *io);
static void stk_hex(const unsigned char *b, int n, char *out, int out_n);

/* Monotonic milliseconds, truncated to int. */
static int mono_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int)(ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL);
}

/* A read that already waited reports real time. A read that returns 0
 * immediately still consumes the slice, so a mock cannot spin the window. */
static int read_charged(const struct np_stk_io *io, unsigned char *buf, int n, int slice, int *spent)
{
    int t0, elapsed, r;

    if (slice < 1) {
        slice = 1;
    }
    t0 = mono_ms();
    r = io->read(io->ctx, buf, n, slice);
    elapsed = mono_ms() - t0;
    if (elapsed < 0) {
        elapsed = 0;
    }
    if (r == 0 && elapsed < slice) {
        elapsed = slice;
    }
    if (r > 0 && elapsed < 1) {
        elapsed = 1;
    }
    if (spent) {
        *spent = elapsed;
    }
    return r;
}

/* RESET release leaves the old sketch in the USB FIFO. Those bytes are not
 * a failed reset, and a 14 10 inside them is not optiboot. Sync bytes go
 * out only after 25 ms of silence. budget_ms is charged time, not a spin.
 * A sketch that never goes quiet returns "board did not reset". */
static int stk_sync_window(const struct np_stk_io *io, int budget_ms, char *err, int err_n)
{
    unsigned char cmd[2] = {STK_GET_SYNC, STK_CRC_EOP};
    unsigned char junk[12];
    char hex[48];
    char msg[96];
    int junk_n = 0;
    int since = 0;
    int quiet = 0;
    int nonzero = 0;
    int spent_total = 0;
    int left = budget_ms > 0 ? budget_ms : 1200;
    int tries;

    while (left > 0 && quiet < 25) {
        unsigned char b[32];
        int spent = 0;
        int slice = left > 20 ? 20 : left;
        int r;

        if (slice < 1) {
            slice = 1;
        }
        r = read_charged(io, b, (int)sizeof(b), slice, &spent);
        if (spent < 1) {
            spent = 1;
        }
        left -= spent;
        spent_total += spent;
        if (r < 0) {
            err_set(err, err_n, "serial read failed");
            return -1;
        }
        if (r == 0) {
            quiet += spent;
            continue;
        }
        quiet = 0;
        since += r;
        /* A USB backlog is a few packets. A sketch that is still clocking
         * frames never gives us 25 ms of silence. */
        if (since > 400 && spent_total > 250) {
            snprintf(msg, sizeof(msg), "sketch still streaming (%d bytes)", since);
            stk_note(io, msg);
            err_set(err, err_n, "board did not reset");
            return -1;
        }
    }
    if (quiet < 25) {
        err_set(err, err_n, "board did not reset");
        return -1;
    }
    if (since > 0) {
        snprintf(msg, sizeof(msg), "drained %d stream bytes", since);
        stk_note(io, msg);
    }
    stk_note(io, "line quiet");

    /* Optiboot flashes the LED before it reads, and drops what arrived
     * during that flash. The Knight answers about half a second after
     * reset. One command fills the 328P UART. A second GET_SYNC still
     * in that FIFO is read as the CRC of READ_SIGN, and the watchdog
     * starts the sketch ("Scanning for IMU..."). */
    {
        int held = 0;
        while (left > 0 && held < 480) {
            unsigned char b[32];
            int spent = 0;
            int slice = left > 20 ? 20 : left;
            int r;

            if (slice < 1) {
                slice = 1;
            }
            r = read_charged(io, b, (int)sizeof(b), slice, &spent);
            if (spent < 1) {
                spent = 1;
            }
            left -= spent;
            spent_total += spent;
            if (r < 0) {
                err_set(err, err_n, "serial read failed");
                return -1;
            }
            if (r == 0) {
                held += spent;
                continue;
            }
            since += r;
            if (since > 400 && spent_total > 250) {
                snprintf(msg, sizeof(msg), "sketch still streaming (%d bytes)", since);
                stk_note(io, msg);
                err_set(err, err_n, "board did not reset");
                return -1;
            }
        }
    }
    since = 0;
    nonzero = 0;
    tries = 0;
    if (!io->write || io->write(io->ctx, cmd, 2) != 2) {
        err_set(err, err_n, "serial write failed");
        return -1;
    }
    stk_note(io, "sync sent");
    {
    unsigned char prev = 0;
    int have = 0;
    int win = 0;
    while (left > 0 && tries < 40) {
        unsigned char b[32];
        int spent = 0;
        int slice = left > 20 ? 20 : left;
        int r, i;

        tries++;
        if (slice < 1) {
            slice = 1;
        }
        r = read_charged(io, b, (int)sizeof(b), slice, &spent);
        if (spent < 1) {
            spent = 1;
        }
        left -= spent;
        if (r < 0) {
            err_set(err, err_n, "serial read failed");
            return -1;
        }
        if (r == 0) {
            continue;
        }
        for (i = 0; i < r; i++) {
            if (have && prev == STK_INSYNC && b[i] == STK_OK && win <= 2) {
                int extra = stk_drain(io);
                if (extra > 0) {
                    snprintf(msg, sizeof(msg), "dropped %d extra boot bytes", extra);
                    stk_note(io, msg);
                }
                return 0;
            }
            if (junk_n < (int)sizeof(junk)) {
                junk[junk_n++] = b[i];
            }
            if (b[i] != 0) {
                nonzero++;
            }
            win++;
            since++;
            prev = b[i];
            have = 1;
        }
        if (since > 24) {
            snprintf(msg, sizeof(msg), "sketch still streaming (%d bytes)", since);
            stk_note(io, msg);
            err_set(err, err_n, "board did not reset");
            return -1;
        }
    }
    }
    if (nonzero <= 0) {
        err_set(err, err_n, "bootloader did not answer");
        return -1;
    }
    stk_hex(junk, junk_n, hex, (int)sizeof(hex));
    snprintf(msg, sizeof(msg), "bootloader said %d bytes nz=%d %s", since, nonzero, hex);
    err_set(err, err_n, msg);
    return -1;
}

/* Pulse DTR when the io provides it, then run one sync window on the open baud.
 * A second pulse happens only when the error text is "board did not reset". Any other error returns at once. */
static int stk_sync(const struct np_stk_io *io, char *err, int err_n)
{
    int attempt;

    /* One 14 10 at 115200 is optiboot. If the sketch is still streaming,
     * reset once more. Do not reset after a real answer. */
    for (attempt = 0; attempt < 2; attempt++) {
        if (attempt == 1) {
            stk_note(io, "stream still running, reset again");
        }
        if (io->pulse_dtr) {
            io->pulse_dtr(io->ctx);
        }
        stk_note(io, "reset into bootloader");
        if (stk_sync_window(io, 1200, err, err_n) == 0) {
            return 0;
        }
        if (!err || !strstr(err, "board did not reset")) {
            return -1;
        }
    }
    return -1;
}

/* Earlier GET_SYNC replies can still be in the UART. Eat them before
 * the next command, or the signature frame is read two bytes late. */
static int stk_drain(const struct np_stk_io *io)
{
    unsigned char b[64];
    int extra = 0;
    int i;

    if (!io || !io->read) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        int n = io->read(io->ctx, b, (int)sizeof(b), 10);
        if (n <= 0) {
            break;
        }
        extra += n;
    }
    return extra;
}

/* 1 when byte i is INSYNC 0x14 and byte i+4 is OK 0x10, and five bytes are in range. */
static int stk_sig_at(const unsigned char *b, int n, int i)
{
    return i >= 0 && i + 5 <= n && b[i] == STK_INSYNC && b[i + 4] == STK_OK;
}

/* Write up to 8 bytes as spaced hex. n at or below 0 stores "none". A null out or out_n below 2 returns. */
static void stk_hex(const unsigned char *b, int n, char *out, int out_n)
{
    int i, o = 0;

    if (!out || out_n < 2) {
        return;
    }
    if (n <= 0) {
        snprintf(out, (size_t)out_n, "none");
        return;
    }
    if (n > 8) {
        n = 8;
    }
    out[0] = 0;
    for (i = 0; i < n; i++) {
        int w = snprintf(out + o, (size_t)(out_n - o), "%s%02X", i ? " " : "", b[i]);
        if (w < 0 || o + w >= out_n) {
            break;
        }
        o += w;
    }
}

/* Send READ_SIGN and accept only 1E 95 0F inside an INSYNC..OK frame. Any other three bytes refuse the chip.
 * The wait is about 450 ms. A missing frame returns -1. */
static int stk_signature(const struct np_stk_io *io, char *err, int err_n)
{
    unsigned char cmd[2] = {STK_READ_SIGN, STK_CRC_EOP};
    unsigned char b[32];
    char hex[40];
    char msg[80];
    int n = 0;
    int spins = 0;
    int i;

    if (!io->write || io->write(io->ctx, cmd, 2) != 2) {
        err_set(err, err_n, "serial write failed");
        return -1;
    }
    /* A 5-byte reply must not wait out a 32-byte fill. Budget is wall time. */
    while (n < (int)sizeof(b) && spins < 450) {
        int spent = 0;
        int slice = 450 - spins;
        int r;
        if (slice > 25) {
            slice = 25;
        }
        if (slice < 1) {
            slice = 1;
        }
        r = read_charged(io, b + n, (int)sizeof(b) - n, slice, &spent);
        if (spent < 1) {
            spent = 1;
        }
        spins += spent;
        if (r < 0) {
            err_set(err, err_n, "serial read failed");
            return -1;
        }
        if (r > 0) {
            n += r;
        }
        for (i = 0; i + 5 <= n; i++) {
            if (!stk_sig_at(b, n, i)) {
                continue;
            }
            /* ATmega328P. Refuse anything else so a wrong board is not erased. */
            if (b[i + 1] == 0x1E && b[i + 2] == 0x95 && b[i + 3] == 0x0F) {
                return 0;
            }
            stk_hex(b + i + 1, 3, hex, (int)sizeof(hex));
            snprintf(msg, sizeof(msg), "not an ATmega328P %s", hex);
            err_set(err, err_n, msg);
            return -1;
        }
        if (r == 0 && n >= 5) {
            break;
        }
    }
    stk_hex(b, n, hex, (int)sizeof(hex));
    snprintf(msg, sizeof(msg), "no chip signature %s", hex);
    err_set(err, err_n, msg);
    return -1;
}

/* Forward msg through io->note when that hook is set. */
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

/* Program flash and skip EEPROM. Same path as the extended call with eeprom_byte -1. */
int np_stk_program(const struct np_stk_io *io, const unsigned char *image, int len,
                   char *err, int err_n)
{
    return np_stk_program_ex(io, image, len, -1, err, err_n);
}

/* Sync, require signature 1E 95 0F, then write 128-byte flash pages at word address byte_offset/2, padded 0xFF, and read each page back.
 * eeprom_byte below 0 skips EEPROM. A value above 2 is stored as 0. len may be 0 when only the EEPROM byte is requested. */
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
