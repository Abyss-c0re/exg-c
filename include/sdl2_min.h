#ifndef SDL2_MIN_H
#define SDL2_MIN_H

#include <stdint.h>

typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;

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

#define SDL_QUIT 0x100
#define SDL_KEYDOWN 0x300
#define SDL_TEXTINPUT 0x303
#define SDL_MOUSEMOTION 0x400
#define SDL_MOUSEBUTTONDOWN 0x401
#define SDL_MOUSEBUTTONUP 0x402

#define SDL_BUTTON_LEFT 1
#define SDLK_ESCAPE 27
#define SDLK_BACKSPACE 8
#define SDLK_RETURN 13
#define SDLK_c 99
#define SDLK_d 100
#define SDLK_r 114
#define SDLK_q 113
#define SDLK_SPACE 32
#define SDLK_TAB 9
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

typedef union SDL_Event {
    Uint32 type;
    SDL_KeyboardEvent key;
    SDL_MouseButtonEvent button;
    SDL_MouseMotionEvent motion;
    SDL_TextInputEvent text;
    Uint8 pad[64];
} SDL_Event;

int SDL_Init(Uint32 flags);
void SDL_Quit(void);
SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags);
void SDL_DestroyWindow(SDL_Window *w);
SDL_Renderer *SDL_CreateRenderer(SDL_Window *w, int index, Uint32 flags);
void SDL_DestroyRenderer(SDL_Renderer *r);
int SDL_SetRenderDrawColor(SDL_Renderer *r, Uint8 R, Uint8 G, Uint8 B, Uint8 A);
int SDL_RenderClear(SDL_Renderer *r);
int SDL_RenderFillRect(SDL_Renderer *r, const SDL_Rect *rect);
int SDL_RenderDrawLine(SDL_Renderer *r, int x1, int y1, int x2, int y2);
int SDL_RenderDrawPoint(SDL_Renderer *r, int x, int y);
void SDL_RenderPresent(SDL_Renderer *r);
int SDL_PollEvent(SDL_Event *e);
Uint32 SDL_GetTicks(void);
void SDL_Delay(Uint32 ms);
const char *SDL_GetError(void);
int SDL_SetHint(const char *name, const char *value);
int SDL_SetRenderDrawBlendMode(SDL_Renderer *r, int mode);
void SDL_GetWindowSize(SDL_Window *w, int *wi, int *he);
void SDL_SetWindowSize(SDL_Window *w, int wi, int he);
void SDL_SetWindowTitle(SDL_Window *w, const char *title);
int SDL_RenderSetScale(SDL_Renderer *r, float sx, float sy);
void SDL_StartTextInput(void);
void SDL_StopTextInput(void);

#define SDL_BLENDMODE_BLEND 0x00000001

#endif
