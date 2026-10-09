#ifndef NP_MINDSTORM_H
#define NP_MINDSTORM_H

#include <stdint.h>

/* Cooked microvolts. sps is samples per second. Only a rate that rounds to
 * 125, 250, or 500 is decimated. 200 is refused. A closed window is kept
 * for np_mindstorm_copy_window even when this process has no ball session. */
void np_mindstorm_on_sample(const float *v, int n, float sps);

/* Copies the newest closed window that has not been taken. Returns the
 * channel count, or 0 when there is nothing new, out is null, or cap is
 * too small. sps, when non-null, receives 125, 250, or 500. Does not
 * require auth. A short cap leaves the window for a later call. */
int np_mindstorm_copy_window(int16_t *out, int cap, int *sps);

/* 16-byte client id and 32-byte HMAC token. Returns 0, or -1 when a pointer
 * is null or a length is wrong. Success turns wind forwarding on. The token
 * is not logged. */
int np_mindstorm_set_identity(const uint8_t *id, int id_n,
                              const uint8_t *token, int token_n);

/* Bytes from the ball. A challenge queues a proof. AUTH_OK marks the session. */
void np_mindstorm_rx(const uint8_t *b, int n);

/* Copies the next frame to write. Returns its length, or 0 when none. */
int np_mindstorm_tx(uint8_t *dst, int cap);

/* Drops auth and queued frames, keeps the identity, and queues HELLO.
 * Returns 0, or -1 when the identity is missing or the queue is full.
 * BLE and TCP do not wait for the USB banner. */
int np_mindstorm_hello(void);

/* 1 after AUTH_OK until AUTH_NO, HELLO, or a new identity. */
int np_mindstorm_authed(void);

/* 1 when a closed window may be queued. Defaults on. */
int np_mindstorm_wind_enabled(void);

/* Non-zero allows WIND frames. Zero stops them. Traces are not changed. */
void np_mindstorm_set_wind(int on);

/* 1 when the last cooked rate rounded to 125, 250, or 500. */
int np_mindstorm_rate_ok(void);

/* Copies "rate not supported", "authed", or "disconnected". A refused rate
 * is reported ahead of auth. A null dst or n < 1 writes nothing. Otherwise
 * dst is NUL-terminated. */
void np_mindstorm_status(char *dst, int n);

#endif
