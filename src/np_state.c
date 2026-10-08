#include "np_local.h"
#include "np_mods.h"

/* One struct np_app g. The tables under it are the legal plot steps:
 * amplitude in µV, and the time window in seconds. CHCOL is the first-run
 * trace color, copied into g.chrgb when set_gen passes 4. Do not declare
 * a second g. */

struct np_app g;
const int SCALE_UV[NSCALE] = {50, 100, 200, 500, 1000, 2000, 5000};
const int WIN_S[NWINS] = {1, 2, 4, 8};
const int CHCOL[NP_NCHAN][3] = {
    {255, 255, 255}, {255, 255, 255}, {255, 230, 90}, {255, 230, 90},
    {80, 200, 255}, {80, 200, 255}, {255, 90, 90}, {255, 90, 90},
};
int mode_enable;
uint32_t mode_quiet_until;
int last_clip[NP_NCHAN];
struct np_peers peers;
char self_name[NP_PEER_NAME];
