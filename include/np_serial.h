#ifndef NP_SERIAL_H
#define NP_SERIAL_H

#include "np_types.h"

/* Desktop opens a raw tty at 115200 and returns the fd. Android calls UsbSerial.open and returns 100, never 0 or 1. */
int np_serial_open(const char *path);
/* Desktop drops DTR and RTS, then raises them. Android calls UsbSerial.pulseDtr and ignores fd. */
void np_serial_pulse_dtr(int fd);
/* Desktop accepts 57600 or 115200. Android passes baud through and does not clamp it. 0 on success, -1 if the port cannot change. */
int np_serial_set_baud(int fd, int baud);
/* Desktop closes fd. Android calls UsbSerial.close and ignores fd. */
void np_serial_close(int fd);
/* Desktop writes n bytes. Android copies them into a Java array. n <= 0 returns 0. -1 if the write fails. */
int np_serial_write(int fd, const void *buf, int n);
/* Desktop does one read; EAGAIN is 0. Android calls UsbSerial.read and clamps a count longer than n. */
int np_serial_read(int fd, void *buf, int n);
/* Desktop ignores timeout_ms and does one read. Android calls UsbSerial.readFor and clamps the wait to 5..80 ms. */
int np_serial_read_wait(int fd, void *buf, int n, int timeout_ms);
/* One byte through np_serial_read. The return is 1, 0, or -1. */
int np_serial_read_byte(int fd, unsigned char *b);
/* Desktop flushes the tty. Android calls UsbSerial.flush and ignores fd. */
void np_serial_flush(int fd);
/* Desktop scans /dev for ttyUSB* and ttyACM*. Android copies UsbSerial.listPorts and skips a null name. */
int np_list_ports(char out[][NP_MAX_PATH], int max);

#endif
