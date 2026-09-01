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

void np_serial_pulse_dtr(int fd)
{
    int bits = TIOCM_DTR;
    /* Stuck firmware (IMU scan loop, hung chon_) needs a Nano reset. */
    ioctl(fd, TIOCMBIC, &bits);
    usleep(100000);
    ioctl(fd, TIOCMBIS, &bits);
}

void np_serial_close(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

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

int np_serial_read_byte(int fd, unsigned char *b)
{
    return np_serial_read(fd, b, 1);
}

void np_serial_flush(int fd)
{
    tcflush(fd, TCIOFLUSH);
}

static int is_tty_name(const char *n)
{
    return strncmp(n, "ttyUSB", 6) == 0 || strncmp(n, "ttyACM", 6) == 0;
}

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
