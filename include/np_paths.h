#ifndef NP_PATHS_H
#define NP_PATHS_H

#include "np_app.h"

/* Internal. Owner: src/np_paths.c. Product API stays in np_host.h. */

/* <raw>/<kind>/<sanitized name>.nprw. Creates the kind directory. */
void raw_named_path(const char *kind, const char *name, char *out, int n);
/* Fills buf and nn from <which>.nprw. 0 on success. -1 if missing, shorter than 16 samples, or malloc fails. The file's sample rate is ignored. */
int raw_load_plate(const char *which, float buf[NP_NCHAN][NP_RING], uint32_t nn[NP_NCHAN]);

#endif /* NP_PATHS_H */
