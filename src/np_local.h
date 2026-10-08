#ifndef NP_LOCAL_H
#define NP_LOCAL_H

/* First include of an engine .c file. It pulls the headers those files share
 * and the libc they use. NP_ALOG prints to logcat on Android and does nothing
 * on the desktop. _GNU_SOURCE is set here, before the system headers below. */
#define _GNU_SOURCE
#include "np_app.h"
#include "np_algo.h"
#include "np_sot.h"
#include "np_cube.h"
#include "np_font.h"
#include "np_host.h"
#include "np_fw.h"
#include "np_rate.h"
#include "np_version.h"
#include "np_stk500.h"
#include "np_link.h"
#include "np_peer.h"
#ifdef NP_ANDROID_UI
#include "sdl2_min.h"
#include <android/log.h>
#define NP_ALOG(...) __android_log_print(ANDROID_LOG_INFO, "exg-c", __VA_ARGS__)
#elif defined(__ANDROID__)
#include "SDL.h"
#include "SDL_system.h"
#include <android/log.h>
#define NP_ALOG(...) __android_log_print(ANDROID_LOG_INFO, "exg-c", __VA_ARGS__)
#else
#include "sdl2_min.h"
#define NP_ALOG(...) ((void)0)
#endif

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#ifndef __ANDROID__
#include <grp.h>
#endif
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>


#endif /* NP_LOCAL_H */
