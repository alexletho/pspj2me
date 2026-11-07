#ifndef PLATFORM_H
#define PLATFORM_H
#include <stdint.h>

/* Implemented per platform (plat_host.c / plat_psp.c). */
int64_t plat_time_ms(void);
int64_t plat_time_us(void);
void plat_idle(int ms);                 /* sleep while no thread is runnable */
void plat_poll(void);                   /* pump input; post key events via midp_post_key() */
void plat_present(const uint32_t *argb, int w, int h, int stride); /* show a frame (0xAARRGGBB) */
void plat_log(const char *s);
const char *plat_data_dir(void);        /* writable directory for RMS etc, no trailing slash */
void plat_vibrate(int ms);
/* text input: returns 1 and fills out (UTF-8) if the user accepted */
int plat_edit_text(const char *title, const char *initial, char *out, int outSize, int maxChars, int constraints);
/* audio: returns handle > 0, or -1 if the format is unsupported */
/* audio: the portable mixer (src/audio/mixer.h) does the work; the platform feeds its output */
int plat_audio_available(void);   /* 0 if there is no audio output */
void plat_audio_poll(void);       /* host: advance the mixer in real time (no output device) */
void plat_overlay_active(int active);

/* Provided by the core for the platform layer */
void midp_post_key(int type, int keyCode);  /* type: 1=press 2=release 3=repeat */
void midp_post_pointer(int type, int x, int y);
void midp_request_exit(void);
void midp_post_system(int type);       /* EV_PAUSE / EV_RESUME */
void midp_set_screen_size(int w, int h);

#define KEY_PRESS 1
#define KEY_RELEASE 2
#define KEY_REPEAT 3

/* J2ME key codes */
#define JK_UP    (-1)
#define JK_DOWN  (-2)
#define JK_LEFT  (-3)
#define JK_RIGHT (-4)
#define JK_FIRE  (-5)
#define JK_SOFT1 (-6)
#define JK_SOFT2 (-7)
#define JK_CLEAR (-8)

#endif
