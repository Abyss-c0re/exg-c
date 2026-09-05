#ifndef NP_LINK_H
#define NP_LINK_H

/*
 * API client. Dest is typed host:http[/udp]. No baked address or token.
 * Live path is EXG1 UDP. /cfg is the settings mirror.
 */

#include "np_api.h"

int np_link_parse_dest(const char *dest, char *host, int hostn, int *http, int *udp);
int np_link_pair(const char *dest, const char *myname, char *grant, int gn);
int np_link_start(const char *dest, const char *token);
void np_link_stop(void);
int np_link_on(void);
int np_link_alive(void);

typedef void (*np_link_sample_fn)(const struct np_api_sample *s);
typedef void (*np_link_cfg_fn)(const char *json);
void np_link_set_hooks(np_link_sample_fn samp, np_link_cfg_fn cfg);
/* Drain queued frames + apply a fresh /cfg if one arrived. */
void np_link_poll(void);

#endif
