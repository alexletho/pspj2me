/* MP3 playback support: frame index for seeking/duration + streaming decode with minimp3
 * (third_party/minimp3, public domain / CC0). */
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#include "third_party/minimp3/minimp3.h"
#include "mp3.h"
#include <stdlib.h>
#include <string.h>

#define MP3_MAX_INDEX 200000   /* frames: ~90 minutes at 44.1 kHz */

struct Mp3Stream {
    mp3dec_t dec;
    uint8_t *data;
    int len;
    int rate, channels;
    int nFrames;              /* MP3 frames */
    uint32_t *offset;         /* byte offset of each MP3 frame */
    uint32_t *start;          /* first sample frame of each MP3 frame */
    int64_t total;
    int next;                 /* next MP3 frame to decode */
    int skip;                 /* frames to decode and drop after a seek (bit reservoir) */
    int64_t trimStart;        /* encoder + decoder delay to drop at the start (gapless playback) */
    int64_t rawEnd;           /* raw sample frame where the music ends (before encoder padding) */
};

/* Xing/Info frame with a LAME-style tag (LAME, ffmpeg's Lavc, ...): gives the exact encoder
 * delay and padding. Returns 1 if frame `p` is such an info frame. */
static int parseInfoFrame(const uint8_t *p, int len, int *delay, int *padding) {
    *delay = *padding = -1;
    int x = -1;
    for (int i = 4; i + 8 <= len && i < 48; i++)
        if (!memcmp(p + i, "Xing", 4) || !memcmp(p + i, "Info", 4)) { x = i; break; }
    if (x < 0) return 0;
    uint32_t flags = ((uint32_t)p[x + 4] << 24) | ((uint32_t)p[x + 5] << 16) | ((uint32_t)p[x + 6] << 8) | p[x + 7];
    int o = x + 8 + ((flags & 1) ? 4 : 0) + ((flags & 2) ? 4 : 0) + ((flags & 4) ? 100 : 0) + ((flags & 8) ? 4 : 0);
    if (o + 24 <= len) {
        const uint8_t *t = p + o;   /* 9-byte encoder name, then the LAME tag fields */
        *delay = (t[21] << 4) | (t[22] >> 4);
        *padding = ((t[22] & 15) << 8) | t[23];
    }
    return 1;
}

/* size of an ID3v2 tag at the start, 0 if none */
static int id3Size(const uint8_t *d, int len) {
    if (len < 10 || memcmp(d, "ID3", 3) != 0) return 0;
    int sz = ((d[6] & 0x7f) << 21) | ((d[7] & 0x7f) << 14) | ((d[8] & 0x7f) << 7) | (d[9] & 0x7f);
    sz += 10 + ((d[5] & 0x10) ? 10 : 0);
    return sz > len ? len : sz;
}

Mp3Stream *mp3_open(const uint8_t *data, int len) {
    if (len < 4) return NULL;
    int pos = id3Size(data, len);
    /* cheap check before the full scan: a frame sync must appear early */
    int sync = -1;
    for (int i = pos; i + 1 < len && i < pos + 4096; i++)
        if (data[i] == 0xff && (data[i + 1] & 0xe0) == 0xe0) { sync = i; break; }
    if (sync < 0) return NULL;
    Mp3Stream *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    int cap = 1024;
    s->offset = malloc(cap * sizeof(uint32_t));
    s->start = malloc(cap * sizeof(uint32_t));
    if (!s->offset || !s->start) goto fail;
    mp3dec_init(&s->dec);
    mp3dec_frame_info_t info;
    int64_t total = 0;
    while (pos < len && s->nFrames < MP3_MAX_INDEX) {
        memset(&info, 0, sizeof info);
        int n = mp3dec_decode_frame(&s->dec, data + pos, len - pos, NULL, &info);
        if (info.frame_bytes <= 0) break;                 /* no more frames */
        if (s->nFrames == 0 && info.frame_offset > 4096) break;   /* garbage that only happens to contain a sync */
        if (n > 0) {
            if (s->nFrames == 0) { s->rate = info.hz; s->channels = info.channels; }
            if (s->nFrames == cap) {
                cap *= 2;
                uint32_t *o = realloc(s->offset, cap * sizeof(uint32_t)), *st = o ? realloc(s->start, cap * sizeof(uint32_t)) : NULL;
                if (o) s->offset = o;
                if (!o || !st) goto fail;
                s->start = st;
            }
            s->offset[s->nFrames] = (uint32_t)(pos + info.frame_offset);
            s->start[s->nFrames] = (uint32_t)total;
            s->nFrames++;
            total += n;
        }
        pos += info.frame_offset + info.frame_bytes;
    }
    /* two frames at least: a lone "frame" in random data is not an MP3 */
    if (s->nFrames < 2 || s->rate <= 0 || s->channels < 1 || s->channels > 2) goto fail;
    /* gapless: drop the silent info frame, the encoder delay (+ the decoder's 529 samples) and the padding */
    int delay, padding;
    uint32_t f0 = s->offset[0], f1 = s->offset[1];
    if (parseInfoFrame(data + f0, (int)(f1 - f0), &delay, &padding)) {
        uint32_t first = s->start[1];
        memmove(s->offset, s->offset + 1, (s->nFrames - 1) * sizeof(uint32_t));
        memmove(s->start, s->start + 1, (s->nFrames - 1) * sizeof(uint32_t));
        s->nFrames--;
        for (int i = 0; i < s->nFrames; i++) s->start[i] -= first;
        total -= first;
        if (delay >= 0) {
            s->trimStart = delay + 529;
            int64_t tail = padding - 529;
            s->rawEnd = total - (tail > 0 ? tail : 0);
        }
    }
    if (s->rawEnd <= 0 || s->rawEnd > total) s->rawEnd = total;
    if (s->trimStart >= s->rawEnd) { s->trimStart = 0; s->rawEnd = total; }
    s->total = s->rawEnd - s->trimStart;
    if (s->total <= 0) goto fail;
    s->data = malloc(len);
    if (!s->data) goto fail;
    memcpy(s->data, data, len);
    s->len = len;
    mp3dec_init(&s->dec);
    return s;
fail:
    free(s->offset);
    free(s->start);
    free(s);
    return NULL;
}

void mp3_close(Mp3Stream *s) {
    if (!s) return;
    free(s->data);
    free(s->offset);
    free(s->start);
    free(s);
}

int mp3_rate(const Mp3Stream *s) { return s->rate; }
int mp3_channels(const Mp3Stream *s) { return s->channels; }
int64_t mp3_total_frames(const Mp3Stream *s) { return s->total; }
size_t mp3_bytes(const Mp3Stream *s) { return sizeof *s + (size_t)s->len + (size_t)s->nFrames * 8; }

int mp3_read(Mp3Stream *s, int16_t *pcm) {
    static int16_t tmp[MINIMP3_MAX_SAMPLES_PER_FRAME];   /* only used from the mixer, under its lock */
    while (s->next < s->nFrames) {
        int idx = s->next++;
        uint32_t off = s->offset[idx];
        uint32_t end = idx + 1 < s->nFrames ? s->offset[idx + 1] : (uint32_t)s->len;
        mp3dec_frame_info_t info;
        (void)end;
        int n = mp3dec_decode_frame(&s->dec, s->data + off, s->len - (int)off, tmp, &info);
        if (n <= 0) n = 0;
        if (n > MP3_MAX_FRAMES) n = MP3_MAX_FRAMES;
        /* frames that decode to nothing (or are being dropped after a seek) still count in time */
        int expect = (int)((idx + 1 < s->nFrames ? s->start[idx + 1] : (uint32_t)s->total) - s->start[idx]);
        if (expect > MP3_MAX_FRAMES) expect = MP3_MAX_FRAMES;
        if (s->skip > 0) { s->skip--; continue; }
        /* the part of this frame inside [trimStart, rawEnd) */
        int64_t rawStart = s->start[idx];
        int from = rawStart < s->trimStart ? (int)(s->trimStart - rawStart) : 0;
        int to = rawStart + expect > s->rawEnd ? (int)(s->rawEnd - rawStart) : expect;
        if (from >= to) { if (rawStart >= s->rawEnd) return 0; continue; }
        int ch = info.channels;
        for (int i = from; i < to; i++) {
            int l = 0, r = 0;
            if (i < n) {
                if (ch == 2) { l = tmp[i * 2]; r = tmp[i * 2 + 1]; }
                else l = r = tmp[i];
            }
            int o = i - from;
            if (s->channels == 2) { pcm[o * 2] = (int16_t)l; pcm[o * 2 + 1] = (int16_t)r; }
            else pcm[o] = (int16_t)((l + r) / 2);
        }
        return to - from;
    }
    return 0;
}

int64_t mp3_seek(Mp3Stream *s, int64_t frame) {
    if (frame <= 0 || s->nFrames == 0) { mp3dec_init(&s->dec); s->next = 0; s->skip = 0; return 0; }
    if (frame > s->total) frame = s->total;
    frame += s->trimStart;    /* logical -> raw */
    /* binary search for the MP3 frame containing `frame` */
    int lo = 0, hi = s->nFrames - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if ((int64_t)s->start[mid] <= frame) lo = mid; else hi = mid - 1;
    }
    /* start a few frames early so the bit reservoir is filled, and drop those */
    int from = lo > 3 ? lo - 3 : 0;
    mp3dec_init(&s->dec);
    s->next = from;
    s->skip = lo - from;
    int64_t st = s->start[lo];
    return (st > s->trimStart ? st : s->trimStart) - s->trimStart;
}
