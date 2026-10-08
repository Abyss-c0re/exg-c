#ifndef __ANDROID__
#define _GNU_SOURCE
#include "np_serial.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* POSIX tty. Open is raw 8N1 at 115200.
 * Hangup is left off so close does not reset the Nano. */

/* Open path non-blocking, raw 8N1 at 115200, no parity and no RTS/CTS.
 * HUPCL is cleared so close does not drop DTR. Returns the fd, or -1, and flushes both directions. */
int np_serial_open(const char *path)
{
    int fd;
    struct termios tio;

    fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return -1;
    }
    if (tcgetattr(fd, &tio) != 0) {
        close(fd);
        return -1;
    }
    cfmakeraw(&tio);
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
#ifdef HUPCL
    tio.c_cflag &= ~HUPCL; /* don't pulse DTR on close — avoids extra Nano resets */
#endif
    tio.c_cflag = (tio.c_cflag & ~CSIZE) | CS8;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        close(fd);
        return -1;
    }
    tcflush(fd, TCIOFLUSH);
    return fd;
}

/* Drop DTR and RTS for 250 ms, then raise them and wait 50 ms.
 * That discharges the Nano RESET capacitor. */
void np_serial_pulse_dtr(int fd)
{
    int bits = TIOCM_DTR | TIOCM_RTS;
    /* Avrdude: both lines low discharge the Nano RESET capacitor. */
    ioctl(fd, TIOCMBIC, &bits);
    usleep(250000);
    ioctl(fd, TIOCMBIS, &bits);
    usleep(50000);
}

/* Switch the open tty to 57600 or 115200.
 * Any other baud, a bad fd, or a termios failure returns -1. */
int np_serial_set_baud(int fd, int baud)
{
    struct termios tio;
    speed_t sp;

    if (fd < 0) {
        return -1;
    }
    if (baud == 57600) {
        sp = B57600;
    } else if (baud == 115200) {
        sp = B115200;
    } else {
        return -1;
    }
    if (tcgetattr(fd, &tio) != 0) {
        return -1;
    }
    cfsetispeed(&tio, sp);
    cfsetospeed(&tio, sp);
    return tcsetattr(fd, TCSANOW, &tio) == 0 ? 0 : -1;
}

/* Close fd. fd below 0 is ignored. */
void np_serial_close(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

/* Write all n bytes, or return -1. EINTR is retried. EAGAIN waits up to 200 ms for POLLOUT. */
int np_serial_write(int fd, const void *buf, int n)
{
    const unsigned char *p = buf;
    int off = 0;
    while (off < n) {
        ssize_t w = write(fd, p + off, (size_t)(n - off));
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd = {fd, POLLOUT, 0};
                if (poll(&pfd, 1, 200) <= 0) {
                    return -1;
                }
                continue;
            }
            return -1;
        }
        off += (int)w;
    }
    return off;
}

/* One read. EAGAIN, EWOULDBLOCK, and EINTR return 0. Other errors return -1. */
int np_serial_read(int fd, void *buf, int n)
{
    ssize_t r = read(fd, buf, (size_t)n);
    if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return 0;
        }
        return -1;
    }
    return (int)r;
}

/* timeout_ms is ignored. This is one non-blocking read. */
int np_serial_read_wait(int fd, void *buf, int n, int timeout_ms)
{
    (void)timeout_ms;
    return np_serial_read(fd, buf, n);
}

/* Read one byte. The return is 1, 0, or -1, same as a one-byte read. */
int np_serial_read_byte(int fd, unsigned char *b)
{
    return np_serial_read(fd, b, 1);
}

/* Flush both directions on fd. */
void np_serial_flush(int fd)
{
    tcflush(fd, TCIOFLUSH);
}

/* 1 when the name starts with ttyUSB or ttyACM. */
static int is_tty_name(const char *n)
{
    return strncmp(n, "ttyUSB", 6) == 0 || strncmp(n, "ttyACM", 6) == 0;
}

/* Scan /dev for ttyUSB* and ttyACM*, in readdir order, up to max.
 * Returns 0 when /dev will not open. */
int np_list_ports(char out[][NP_MAX_PATH], int max)
{
    DIR *d;
    struct dirent *e;
    int n = 0;

    d = opendir("/dev");
    if (!d) {
        return 0;
    }
    while ((e = readdir(d)) != NULL && n < max) {
        if (!is_tty_name(e->d_name)) {
            continue;
        }
        snprintf(out[n], NP_MAX_PATH, "/dev/%.240s", e->d_name);
        n++;
    }
    closedir(d);
    return n;
}

#endif /* !__ANDROID__ */
