#ifndef NP_CAL_H
#define NP_CAL_H

#include "np_app.h"

/* Internal. Owner: src/np_cal.c. Product API stays in np_host.h. */

#define CAL_DESK_MS 8000u
#define CAL_WEAR_MS 8000u
#define CAL_PLACE_MS 5000u

/* Stops recording, fsyncs, and closes the CSV. Safe when none is open. */
void csv_close(void);
/* Takes ownership of f, writes the header, and starts recording. -1 if f is NULL. A previous file is closed without fsync. */
int csv_open(FILE *f, const char *label);
/* UI tick. Advances phases 5, 1, and 3 on their timers. Returns unless connected, warm, and one of those phases is running. */
void cal_tick(void);

#endif /* NP_CAL_H */
