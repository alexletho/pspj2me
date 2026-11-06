#ifndef MIXER_H
#define MIXER_H
#include <stdint.h>

#define MIX_RATE 44100

void mix_init(void (*lock)(void), void (*unlock)(void));
/* returns handle > 0, or -1 if the data format is not supported */
int mix_play(const uint8_t *data, int len, int loops, int volume);
void mix_stop(int handle);
int mix_playing(int handle);
void mix_volume(int handle, int volume);
void mix_tone(int note, int durationMs, int volume);
/* render interleaved stereo int16 */
void mix_render(int16_t *out, int frames);
void mix_stop_all(void);

#endif
