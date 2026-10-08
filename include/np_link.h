#ifndef NP_LINK_H
#define NP_LINK_H

/*
 * API client. Dest is typed host:http[/udp]. No baked address or token.
 * Live path is EXG1 UDP. /cfg is the settings mirror.
 */

#include "np_api.h"

/* Split host, host:http (udp is http+1), or host:http/udp. Defaults are 8765 and 8766. Ports must be 1..65535. Returns -1 on a bad dest or a short host buffer. */
int np_link_parse_dest(const char *dest, char *host, int hostn, int *http, int *udp);
/* POST {"name":...} then poll GET /pair up to 240 times, 250 ms apart, until state is 2. */
int np_link_pair(const char *dest, const char *myname, char *grant, int gn);
/* Stop any running link, then open a UDP socket (TOS 0x10) and the reader thread. Returns -1 when dest, DNS, the socket, or the thread fails. */
int np_link_start(const char *dest, const char *token);
/* Join the reader when it is running, close the socket, and clear the queue. */
void np_link_stop(void);
/* 1 when the thread flag is set and the socket is open. */
int np_link_on(void);
/* 1 when a frame arrived within the last 1500 ms. */
int np_link_alive(void);

/* One unpacked EXG1 sample, called from poll. */
typedef void (*np_link_sample_fn)(const struct np_api_sample *s);
/* One /cfg JSON body, called from poll. */
typedef void (*np_link_cfg_fn)(const char *json);
/* Store the sample and cfg callbacks. Either may be null. They run later from poll, without the lock. */
void np_link_set_hooks(np_link_sample_fn samp, np_link_cfg_fn cfg);
/* Drain queued frames + apply a fresh /cfg if one arrived. */
void np_link_poll(void);

#endif
