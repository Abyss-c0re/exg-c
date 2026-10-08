#ifndef NP_VERSION_H
#define NP_VERSION_H

/* App version string. Not the firmware number. */
#define NP_APP_VER "3.04"

/* Firmware that accepts exgmode_N over USB. Older images need one upload.
 * 2 printed the lines. 3 takes exgmode_ before the library can drop it.
 * 4 drops a partial line on a later call. It does not spin inside available(). */
#define EXG_FW_NEED 4

#endif
