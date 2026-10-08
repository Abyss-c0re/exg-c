#ifndef SDL2_MIN_H
#define SDL2_MIN_H

#include <stdint.h>

typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;

typedef struct SDL_Rect {
    int x, y, w, h;
} SDL_Rect;

#define SDL_INIT_VIDEO 0x00000020u
#define SDL_WINDOWPOS_CENTERED 0x2FFF0000u
#define SDL_WINDOW_SHOWN 0x00000004u
#define SDL_WINDOW_RESIZABLE 0x00000020u
#define SDL_RENDERER_ACCELERATED 0x00000002u
#define SDL_RENDERER_PRESENTVSYNC 0x00000004u
#define SDL_RENDERER_SOFTWARE 0x00000001u
#define SDL_TEXTUREACCESS_TARGET 2
#define SDL_PIXELFORMAT_ARGB8888 372645892u

#define SDL_QUIT 0x100
#define SDL_KEYDOWN 0x300
#define SDL_TEXTINPUT 0x303
#define SDL_MOUSEMOTION 0x400
#define SDL_MOUSEBUTTONDOWN 0x401
#define SDL_MOUSEBUTTONUP 0x402
#define SDL_MOUSEWHEEL 0x403

#define SDL_BUTTON_LEFT 1
#define SDL_MOUSEWHEEL_NORMAL 0u
#define SDL_MOUSEWHEEL_FLIPPED 1u
#define SDLK_ESCAPE 27
#define SDLK_BACKSPACE 8
#define SDLK_RETURN 13
#define SDLK_c 99
#define SDLK_d 100
#define SDLK_r 114
#define SDLK_q 113
#define SDLK_SPACE 32
#define SDLK_TAB 9
#define SDLK_PAGEUP 1073741899
#define SDLK_PAGEDOWN 1073741902
#define SDLK_UP 1073741906
#define SDLK_DOWN 1073741905
#define SDLK_RIGHT 1073741903
#define SDLK_LEFT 1073741904
#define SDLK_MINUS 45
#define SDLK_EQUALS 61
#define SDLK_PLUS 43
#define SDLK_COMMA 44
#define SDLK_PERIOD 46
#define SDLK_KP_MINUS 1073741910
#define SDLK_KP_PLUS 1073741911
#define SDLK_m 109
#define SDLK_s 115
#define SDLK_b 98
#define SDLK_h 104
#define SDLK_f 102
#define SDLK_v 118
#define SDLK_1 49
#define SDLK_2 50
#define SDLK_3 51
#define SDLK_4 52
#define SDLK_5 53
#define SDLK_6 54
#define SDLK_7 55
#define SDLK_8 56

typedef struct SDL_Keysym {
    int scancode;
    int sym;
    Uint16 unused_mod;
    Uint32 unused;
} SDL_Keysym;

typedef struct SDL_KeyboardEvent {
    Uint32 type, timestamp, windowID;
    Uint8 state, repeat, padding2, padding3;
    SDL_Keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_MouseButtonEvent {
    Uint32 type, timestamp, windowID, which;
    Uint8 button, state, clicks, padding1;
    Sint32 x, y;
} SDL_MouseButtonEvent;

typedef struct SDL_MouseMotionEvent {
    Uint32 type, timestamp, windowID, which, state;
    Sint32 x, y, xrel, yrel;
} SDL_MouseMotionEvent;

typedef struct SDL_TextInputEvent {
    Uint32 type, timestamp, windowID;
    char text[32];
} SDL_TextInputEvent;

typedef struct SDL_MouseWheelEvent {
    Uint32 type, timestamp, windowID, which;
    Sint32 x, y;
    Uint32 direction;
} SDL_MouseWheelEvent;

typedef union SDL_Event {
    Uint32 type;
    SDL_KeyboardEvent key;
    SDL_MouseButtonEvent button;
    SDL_MouseMotionEvent motion;
    SDL_TextInputEvent text;
    SDL_MouseWheelEvent wheel;
    Uint8 pad[64];
} SDL_Event;

/* Entry points the engine actually calls. Linux links libSDL2.
 * The Android UI links src/sdl_stub.c, which paints nothing.
 * SDL_GetTicks is milliseconds since boot. SDL_Delay sleeps that many milliseconds. */
int SDL_Init(Uint32 flags);
/* Shuts the video subsystem down. The Android stub has nothing to free. */
void SDL_Quit(void);
/* Opens the desktop window. The Android stub returns a dummy pointer. */
SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags);
/* Closes the desktop window. The Android stub ignores w. */
void SDL_DestroyWindow(SDL_Window *w);
/* Desktop renderer. The Android stub returns a dummy pointer and paints nothing. */
SDL_Renderer *SDL_CreateRenderer(SDL_Window *w, int index, Uint32 flags);
/* Frees the desktop renderer. The Android stub ignores r. */
void SDL_DestroyRenderer(SDL_Renderer *r);
/* Draw color, 0–255 per channel. The Android stub succeeds and paints nothing. */
int SDL_SetRenderDrawColor(SDL_Renderer *r, Uint8 R, Uint8 G, Uint8 B, Uint8 A);
/* Clears the desktop frame. The Android stub succeeds and paints nothing. */
int SDL_RenderClear(SDL_Renderer *r);
/* Filled rectangle in pixels. The Android stub succeeds and paints nothing. */
int SDL_RenderFillRect(SDL_Renderer *r, const SDL_Rect *rect);
/* Line in pixels. The Android stub succeeds and paints nothing. */
int SDL_RenderDrawLine(SDL_Renderer *r, int x1, int y1, int x2, int y2);
/* One pixel. The Android stub succeeds and paints nothing. */
int SDL_RenderDrawPoint(SDL_Renderer *r, int x, int y);
/* Shows the desktop frame. The Android stub does not present. */
void SDL_RenderPresent(SDL_Renderer *r);
/* Next input event. 0 when the queue is empty. The Android stub always returns 0. */
int SDL_PollEvent(SDL_Event *e);
/* Milliseconds since the clock started. Calibration and the ladder read this. */
Uint32 SDL_GetTicks(void);
/* Sleep. ms is milliseconds. A large value stalls the calling thread. */
void SDL_Delay(Uint32 ms);
/* Last SDL error string. The Android stub returns an empty string. */
const char *SDL_GetError(void);
/* SDL hint. The Android stub accepts it and returns 1. */
int SDL_SetHint(const char *name, const char *value);
/* Blend mode for later draws. The Android stub succeeds and paints nothing. */
int SDL_SetRenderDrawBlendMode(SDL_Renderer *r, int mode);
/* Window size in pixels. The Android stub reports 1280 by 720 and Java ignores it. */
void SDL_GetWindowSize(SDL_Window *w, int *wi, int *he);
/* Requests a window size in pixels. The Android stub ignores it. */
void SDL_SetWindowSize(SDL_Window *w, int wi, int he);
/* Window title. The Android stub ignores it. */
void SDL_SetWindowTitle(SDL_Window *w, const char *title);
/* Draw scale. The desktop UI uses 1. The Android stub succeeds and is unread. */
int SDL_RenderSetScale(SDL_Renderer *r, float sx, float sy);
/* Clip rectangle in pixels. NULL clears it. The Android stub does not clip. */
int SDL_RenderSetClipRect(SDL_Renderer *r, const SDL_Rect *rect);
/* Offscreen target, width and height in pixels. The Android stub returns NULL. */
SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, Uint32 format, int access, int w, int h);
/* Draws into t, or the window when t is NULL. The Android stub always fails. */
int SDL_SetRenderTarget(SDL_Renderer *r, SDL_Texture *t);
/* Copies a texture. The Android stub succeeds and paints nothing. */
int SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_Rect *dst);
/* Frees a texture. The Android stub never allocated one. */
void SDL_DestroyTexture(SDL_Texture *t);
/* Pointer position in pixels. The Android stub reports 0,0. Touch is Java. */
Uint32 SDL_GetMouseState(int *x, int *y);
/* Shows the text keyboard. The Android stub does nothing. */
void SDL_StartTextInput(void);
/* Hides the text keyboard. The Android stub does nothing. */
void SDL_StopTextInput(void);

#define SDL_BLENDMODE_BLEND 0x00000001

#endif
