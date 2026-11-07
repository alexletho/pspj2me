#ifndef PSP_COMMON_H
#define PSP_COMMON_H
#include <stdint.h>
#include "../../gfx/gfx.h"

#ifndef PSPJ2ME_VERSION
#define PSPJ2ME_VERSION "dev"
#endif

#define SCR_W 480
#define SCR_H 272

enum { SCALE_FIT = 0, SCALE_STRETCH, SCALE_COUNT };

/* PSP buttons we map (index into Settings.keys) */
#include "../keymap.h"   /* B_* button indices */

typedef struct {
    int screenW, screenH;
    int scale;
    int filter;
    int showFps;
    int vsync;
    int keys[B_COUNT];
    int cpu333;
    int fullCanvas;
    int sleepScale;
    int shoulderCombos; /* L/R act as shift buttons for extra keys */   /* % of the time Thread.sleep really sleeps (100 = normal) */   /* report the whole screen to every Canvas (no soft-key strip) */
} Settings;

extern Settings g_settings;
extern char g_baseDir[512];

void settings_defaults(Settings *s);
void settings_load(Settings *s, const char *path);
void settings_save(const Settings *s, const char *path);
const char *scale_name(int s);
const char *key_name(int code);
const char *button_name(int b);
void draw_controls(Surface *s, const Settings *st, const char *title);

/* video */
void video_init(void);
/* draw the current game texture plus the overlay (if visible) and flip */
void video_frame(void);
void video_upload_game(const uint32_t *argb, int w, int h, int stride);
uint32_t *video_overlay_buffer(void);  /* 480x272 ARGB, alpha 0 = transparent */
void video_overlay_changed(int visible);
void video_draw_launcher(const uint32_t *argb); /* full-screen 480x272 opaque image */
void video_begin_dialog_frame(void);
void video_end_dialog_frame(void);

/* ui helpers drawing into a 480x272 ARGB surface */
void ui_surface(Surface *s, uint32_t *px);
void ui_text(Surface *s, int x, int y, const char *utf8, uint32_t argb, int size, int bold);
int ui_text_width(const char *utf8, int size, int bold);

/* launcher */
int launcher_run(char *outJar, int outLen);

/* audio */
void audio_init(void);
void audio_shutdown(void);

#endif
