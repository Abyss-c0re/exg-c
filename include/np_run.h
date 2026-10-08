#ifndef NP_RUN_H
#define NP_RUN_H

#include "np_app.h"

/* Cross-module state. Each symbol is defined next to the module that owns it.
 * Product API stays in np_host.h. */

extern int host_ready;
extern int mode_enable;
extern uint32_t mode_quiet_until;
extern int last_clip[NP_NCHAN];
extern struct np_peers peers;
extern char self_name[NP_PEER_NAME];
/* <config root>/exg-c/firmware, created. out is the directory, not a hex path. */
void firmware_dir(char *out, int n);
#define NP_ATOM_MAX 32
extern char atom_listed[NP_ATOM_MAX][NP_ATOM_NAME];
extern int atom_listed_n;
extern char flash_temp_dir[NP_MAX_PATH];

#endif /* NP_RUN_H */
