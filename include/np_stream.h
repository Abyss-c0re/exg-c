#ifndef NP_STREAM_H
#define NP_STREAM_H

#include "np_app.h"

/* Internal. Owner: src/np_stream.c. Product API stays in np_host.h. */

extern int fw_save_pending;
extern int fw_stock_boot;
extern int mode_pending;
extern int ladder_kick;
/* Process lifetime. Pops the queue and writes the UART. Skips the write while flashing or disconnected, so a chon_ cannot hit the bootloader. */
void *cmd_thread(void *arg);
/* Zeros the SPS window, the snap, and the banner. Does not touch the ring. */
void rate_reset(void);
/* Rebuilds the parser for the preferred board and the current gains. Holds parse_mu. */
void parser_rearm(void);
/* Detached. After frames lock, sends channel and RLD commands and clears en_running. Android may pulse DTR once if the first wait misses and flash does not own the port. */
void *enable_thread(void *arg);

#endif /* NP_STREAM_H */
