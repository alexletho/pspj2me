#ifndef MP3_H
#define MP3_H
#include <stdint.h>
#include <stddef.h>
/* Streaming MP3 (MPEG-1/2 layer I-III) decoder on top of minimp3. */
typedef struct Mp3Stream Mp3Stream;
Mp3Stream *mp3_open(const uint8_t *data, int len);   /* copies the data; NULL if it isn't MP3 */
void mp3_close(Mp3Stream *s);
int mp3_rate(const Mp3Stream *s);
int mp3_channels(const Mp3Stream *s);                 /* 1 or 2 (fixed for the whole stream) */
int64_t mp3_total_frames(const Mp3Stream *s);         /* sample frames */
size_t mp3_bytes(const Mp3Stream *s);                 /* memory held */
/* decodes the next MP3 frame into pcm (MP3_MAX_FRAMES * channels samples); returns sample frames, 0 at the end */
#define MP3_MAX_FRAMES 1152
int mp3_read(Mp3Stream *s, int16_t *pcm);
/* positions the stream so that mp3_read returns the frame containing `frame`; returns that frame's first sample frame */
int64_t mp3_seek(Mp3Stream *s, int64_t frame);
#endif
