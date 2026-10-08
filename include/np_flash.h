#ifndef NP_FLASH_H
#define NP_FLASH_H

#include "np_app.h"

/* Internal. Owner: src/np_flash.c. Product API stays in np_host.h. */

#define NP_LOG_N 40
#define NP_LOG_L 120

struct np_log_ring {
    char line[NP_LOG_N][NP_LOG_L];
    int n;
};

extern int flash_owner;
extern struct np_log_ring flash_log_ring;
extern struct np_log_ring debug_log_ring;
extern pthread_mutex_t log_mu;
/* Serial text into the debug ring and logcat. Does not strip grants. */
void debug_log_add(const char *s);
extern int flash_phase;
extern char flash_phase_line[160];
extern pthread_mutex_t phase_mu;
extern pthread_mutex_t flash_file_mu;
/* Stores phase and the line, then logs it. Phase 4 marks status as a fault. line may be NULL. */
void flash_phase_set(int phase, const char *line);
extern int fw_sel;
/* Without confirmed, the first call within 8 s only arms and returns 0. Otherwise starts the thread and returns 1, or 0 if mode is bad or a flash is already running. */
int flash_arm_start(int mode, int confirmed, int eeprom_only, const char *again);

#endif /* NP_FLASH_H */
