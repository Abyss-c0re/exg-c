#ifdef NP_ANDROID_UI
/* Android has no SDL window. The engine still calls these names for time,
 * sleep, and a few draw calls that the Java UI ignores. Draw calls succeed
 * and paint nothing. */
#include "sdl2_min.h"

#include <time.h>
#include <unistd.h>

/* Succeeds. flags is ignored. Android has no SDL subsystem. */
int SDL_Init(Uint32 flags)
{
    (void)flags;
    return 0;
}
/* No window to tear down. */
void SDL_Quit(void) {}
/* Returns a dummy non-NULL window. Title, position, and size are ignored. */
SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags)
{
    (void)title;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)flags;
    return (SDL_Window *)1;
}
/* Ignores the dummy window. */
void SDL_DestroyWindow(SDL_Window *w)
{
    (void)w;
}
/* Returns a dummy non-NULL renderer. Nothing is drawn with it. */
SDL_Renderer *SDL_CreateRenderer(SDL_Window *w, int index, Uint32 flags)
{
    (void)w;
    (void)index;
    (void)flags;
    return (SDL_Renderer *)1;
}
/* Ignores the dummy renderer. */
void SDL_DestroyRenderer(SDL_Renderer *r)
{
    (void)r;
}
/* Succeeds and paints nothing. */
int SDL_SetRenderDrawColor(SDL_Renderer *r, Uint8 R, Uint8 G, Uint8 B, Uint8 A)
{
    (void)r;
    (void)R;
    (void)G;
    (void)B;
    (void)A;
    return 0;
}
/* Succeeds and paints nothing. */
int SDL_RenderClear(SDL_Renderer *r)
{
    (void)r;
    return 0;
}
/* Succeeds and paints nothing. */
int SDL_RenderFillRect(SDL_Renderer *r, const SDL_Rect *rect)
{
    (void)r;
    (void)rect;
    return 0;
}
/* Succeeds and paints nothing. */
int SDL_RenderDrawLine(SDL_Renderer *r, int x1, int y1, int x2, int y2)
{
    (void)r;
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    return 0;
}
/* Succeeds and paints nothing. */
int SDL_RenderDrawPoint(SDL_Renderer *r, int x, int y)
{
    (void)r;
    (void)x;
    (void)y;
    return 0;
}
/* Does not present. The Java UI draws the traces. */
void SDL_RenderPresent(SDL_Renderer *r)
{
    (void)r;
}
/* No events. The Java UI reads the touch screen itself. */
int SDL_PollEvent(SDL_Event *e)
{
    (void)e;
    return 0;
}
/* Milliseconds since boot, from CLOCK_MONOTONIC. Calibration and the ladder use this. */
Uint32 SDL_GetTicks(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (Uint32)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}
/* Sleep. ms is milliseconds. A large value stalls whatever thread called it. */
void SDL_Delay(Uint32 ms)
{
    usleep(ms * 1000);
}
/* Always an empty string. These stubs do not record an error. */
const char *SDL_GetError(void)
{
    return "";
}
/* Accepts the hint and does nothing. Returns 1, which SDL treats as success. */
int SDL_SetHint(const char *name, const char *value)
{
    (void)name;
    (void)value;
    return 1;
}
/* Succeeds and paints nothing. */
int SDL_SetRenderDrawBlendMode(SDL_Renderer *r, int mode)
{
    (void)r;
    (void)mode;
    return 0;
}
/* Pretends the window is 1280×720. The Java layout does not read this. */
void SDL_GetWindowSize(SDL_Window *w, int *wi, int *he)
{
    (void)w;
    if (wi) {
        *wi = 1280;
    }
    if (he) {
        *he = 720;
    }
}
/* Ignores the size. The Java layout owns the screen. */
void SDL_SetWindowSize(SDL_Window *w, int wi, int he)
{
    (void)w;
    (void)wi;
    (void)he;
}
/* Ignores the title. */
void SDL_SetWindowTitle(SDL_Window *w, const char *title)
{
    (void)w;
    (void)title;
}
/* Succeeds. The Java UI does not read this scale. */
int SDL_RenderSetScale(SDL_Renderer *r, float sx, float sy)
{
    (void)r;
    (void)sx;
    (void)sy;
    return 0;
}
/* Succeeds and does not clip. */
int SDL_RenderSetClipRect(SDL_Renderer *r, const SDL_Rect *rect)
{
    (void)r;
    (void)rect;
    return 0;
}
/* No texture store on Android. Callers must accept NULL. */
SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, Uint32 format, int access, int w, int h)
{
    (void)r;
    (void)format;
    (void)access;
    (void)w;
    (void)h;
    return NULL;
}
/* Always fails. There is no render target. */
int SDL_SetRenderTarget(SDL_Renderer *r, SDL_Texture *t)
{
    (void)r;
    (void)t;
    return -1;
}
/* Succeeds and paints nothing. */
int SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_Rect *dst)
{
    (void)r;
    (void)t;
    (void)src;
    (void)dst;
    return 0;
}
/* Ignores the pointer. CreateTexture never allocated a texture. */
void SDL_DestroyTexture(SDL_Texture *t)
{
    (void)t;
}
/* Reports the pointer at 0,0. Touch is handled in Java. */
Uint32 SDL_GetMouseState(int *x, int *y)
{
    if (x) {
        *x = 0;
    }
    if (y) {
        *y = 0;
    }
    return 0;
}
/* No SDL keyboard. Java text fields take the keys. */
void SDL_StartTextInput(void) {}
/* No SDL keyboard to close. */
void SDL_StopTextInput(void) {}
#endif
