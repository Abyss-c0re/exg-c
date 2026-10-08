#ifndef NP_SERIAL_H
#define NP_SERIAL_H

#include "np_types.h"

int np_serial_open(const char *path);
void np_serial_pulse_dtr(int fd);
/* 115200 or 57600. 0 on success, -1 if the port cannot change. */
int np_serial_set_baud(int fd, int baud);
void np_serial_close(int fd);
int np_serial_write(int fd, const void *buf, int n);
int np_serial_read(int fd, void *buf, int n);
int np_serial_read_wait(int fd, void *buf, int n, int timeout_ms);
int np_serial_read_byte(int fd, unsigned char *b);
void np_serial_flush(int fd);
int np_list_ports(char out[][NP_MAX_PATH], int max);

#endif
