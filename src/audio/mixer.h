#ifndef MIXER_H
#define MIXER_H
#include <stdint.h>

#define MIX_RATE 44100

void mix_init(void (*lock)(void), void (*unlock)(void));
/* render interleaved stereo int16 */
void mix_render(int16_t *out, int frames);

/* ---- players: a loaded sound keeps its position while paused ----
 * Handles stay valid until mix_free, or until an idle sound is evicted to make room
 * (mix_valid() turns false; the caller loads it again). */
int mix_load(const uint8_t *data, int len, int nokiaTone);  /* WAV, MIDI, tone sequence; nokiaTone: Nokia OTA ringtone. -1 if unsupported */
int mix_load_beep(int freqHz, int durationMs);
int mix_load_midi_device(void);                             /* empty synth for MIDIControl */
void mix_start(int h);                                      /* resumes; rewinds first if at the end */
void mix_pause(int h);
void mix_free(int h);
int mix_valid(int h);
int mix_state(int h);                                       /* bit 0 playing, bit 1 stopped at stop time, bit 2 at end */
void mix_set_loops(int h, int loops);                       /* plays in total; -1 = forever */
int mix_loops_done(int h);                                  /* times the end was reached */
int64_t mix_time_us(int h);
int64_t mix_duration_us(int h);                             /* -1 = unknown */
int64_t mix_seek_us(int h, int64_t us);                     /* returns the new time */
void mix_stop_at(int h, int64_t us);                        /* -1 = no stop time */
void mix_volume(int h, int volume);                         /* 0..100 */
void mix_rate(int h, int milli);                            /* 100000 = normal speed */
void mix_pitch(int h, int milliSemitones);                  /* MIDI and tone sequences */
void mix_midi_event(int h, int status, int d1, int d2);
int mix_midi_query(int h, int what, int ch);                /* what 0: program, 1: channel volume */

/* fire-and-forget */
int mix_play(const uint8_t *data, int len, int loops, int volume);
void mix_stop(int handle);
int mix_playing(int handle);
void mix_tone(int note, int durationMs, int volume);
void mix_stop_all(void);

#endif
