/* Portable audio mixer: WAV (PCM / IMA ADPCM), Standard MIDI files via a small
 * software synthesizer, and J2ME tone sequences. Rendering is driven by the platform. */
#include "mixer.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void nolock(void) {}
static void (*lockFn)(void) = nolock, (*unlockFn)(void) = nolock;

/* ---------------- shared synth tables ---------------- */
static int16_t sineTab[1024];
static uint32_t noteInc[128];   /* 16.16 phase increment for a 1024-entry table */
static int tablesReady;

static void buildWaves(void);
static void initTables(void) {
    if (tablesReady) return;
    for (int i = 0; i < 1024; i++) sineTab[i] = (int16_t)(sin(i * 2 * M_PI / 1024) * 32767);
    for (int n = 0; n < 128; n++) {
        double f = 440.0 * pow(2.0, (n - 69) / 12.0);
        noteInc[n] = (uint32_t)(f * 1024.0 * 65536.0 / MIX_RATE);
    }
    tablesReady = 1;
    buildWaves();
}

/* ---------------- WAV ---------------- */
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }

static const int imaIndex[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
static const int imaStep[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282,
    1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };

/* decode to mono/stereo int16; returns sample frames */
static int16_t *decodeWav(const uint8_t *d, int len, int *outFrames, int *outCh, int *outRate) {
    if (len < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return NULL;
    int pos = 12, fmt = 0, ch = 1, rate = 8000, bits = 8, block = 0;
    const uint8_t *data = NULL;
    int dataLen = 0;
    while (pos + 8 <= len) {
        uint32_t cl = rd32(d + pos + 4);
        const uint8_t *c = d + pos + 8;
        if (cl > (uint32_t)(len - pos - 8)) cl = len - pos - 8;
        if (!memcmp(d + pos, "fmt ", 4) && cl >= 16) {
            fmt = rd16(c); ch = rd16(c + 2); rate = rd32(c + 4); block = rd16(c + 12); bits = rd16(c + 14);
        } else if (!memcmp(d + pos, "data", 4)) { data = c; dataLen = cl; }
        pos += 8 + cl + (cl & 1);
    }
    if (!data || ch < 1 || ch > 2 || rate <= 0) return NULL;
    int16_t *out = NULL;
    int frames = 0;
    if (fmt == 1) {
        int bps = bits / 8;
        if (bps != 1 && bps != 2) return NULL;
        frames = dataLen / (bps * ch);
        out = malloc((size_t)frames * ch * 2 + 2);
        if (!out) return NULL;
        for (int i = 0; i < frames * ch; i++)
            out[i] = bps == 1 ? (int16_t)((data[i] - 128) * 256) : (int16_t)rd16(data + i * 2);
    } else if (fmt == 0x11 && bits == 4 && block > 4 * ch) {
        int perBlock = (block - 4 * ch) * 8 / (4 * ch) + 1;
        int nBlocks = (dataLen + block - 1) / block;
        out = malloc((size_t)nBlocks * perBlock * ch * 2 + 2);
        if (!out) return NULL;
        for (int b = 0; b < nBlocks; b++) {
            const uint8_t *bp = data + b * block;
            int avail = dataLen - b * block;
            if (avail < 4 * ch) break;
            int pred[2], idx[2];
            for (int c = 0; c < ch; c++) {
                pred[c] = (int16_t)rd16(bp + c * 4);
                idx[c] = bp[c * 4 + 2];
                if (idx[c] > 88) idx[c] = 88;
                out[(frames) * ch + c] = (int16_t)pred[c];
            }
            int base = frames;
            int n = 1;
            const uint8_t *p = bp + 4 * ch;
            const uint8_t *end = bp + (avail < block ? avail : block);
            /* data is interleaved in 4-byte chunks per channel */
            while (p + 4 * ch <= end) {
                for (int c = 0; c < ch; c++) {
                    for (int k = 0; k < 8; k++) {
                        int nib = (p[c * 4 + k / 2] >> ((k & 1) * 4)) & 15;
                        int step = imaStep[idx[c]];
                        int diff = step >> 3;
                        if (nib & 1) diff += step >> 2;
                        if (nib & 2) diff += step >> 1;
                        if (nib & 4) diff += step;
                        if (nib & 8) pred[c] -= diff; else pred[c] += diff;
                        if (pred[c] > 32767) pred[c] = 32767;
                        if (pred[c] < -32768) pred[c] = -32768;
                        idx[c] += imaIndex[nib];
                        if (idx[c] < 0) idx[c] = 0;
                        if (idx[c] > 88) idx[c] = 88;
                        out[(base + n + k) * ch + c] = (int16_t)pred[c];
                    }
                }
                n += 8;
                p += 4 * ch;
            }
            frames = base + n;
        }
    } else return NULL;
    *outFrames = frames; *outCh = ch; *outRate = rate;
    return out;
}

/* ---------------- MIDI ---------------- */
typedef struct { uint32_t tick; uint8_t st, d1, d2; uint8_t meta; uint32_t tempo; uint32_t us; } MEv;

typedef struct {
    MEv *ev;
    int nEv;
    int division;
    uint32_t durationUs;
} Midi;

static uint32_t varlen(const uint8_t **p, const uint8_t *end) {
    uint32_t v = 0;
    while (*p < end) {
        uint8_t b = *(*p)++;
        v = (v << 7) | (b & 0x7f);
        if (!(b & 0x80)) break;
    }
    return v;
}

static int cmpEv(const void *a, const void *b) {
    const MEv *x = a, *y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    /* note-offs before note-ons at the same tick */
    int px = (x->st & 0xf0) == 0x80 || ((x->st & 0xf0) == 0x90 && x->d2 == 0);
    int py = (y->st & 0xf0) == 0x80 || ((y->st & 0xf0) == 0x90 && y->d2 == 0);
    return py - px;
}

#define MIDI_MAX_EVENTS 262144 /* 4 MB of events: far beyond any real phone tune */

/* make room for n more events; 0 when the cap is hit or memory runs out */
static int midiReserve(Midi *m, int *cap, int n) {
    if (m->nEv + n <= *cap) return 1;
    if (m->nEv + n > MIDI_MAX_EVENTS) return 0;
    int nc = *cap * 2;
    while (nc < m->nEv + n) nc *= 2;
    if (nc > MIDI_MAX_EVENTS) nc = MIDI_MAX_EVENTS;
    MEv *ne = realloc(m->ev, (size_t)nc * sizeof(MEv));
    if (!ne) return 0;
    m->ev = ne;
    *cap = nc;
    return 1;
}

static uint32_t be32m(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static Midi *newMidi(int cap) {
    Midi *m = calloc(1, sizeof(Midi));
    if (!m) return NULL;
    m->ev = malloc((size_t)cap * sizeof(MEv));
    if (!m->ev) { free(m); return NULL; }
    return m;
}

/* sorts the events and computes each one's time in microseconds (tempo changes applied) */
static void finalizeMidi(Midi *m) {
    qsort(m->ev, m->nEv, sizeof(MEv), cmpEv);
    double us = 0, usPerTick = 500000.0 / m->division;
    uint32_t last = 0;
    for (int i = 0; i < m->nEv; i++) {
        MEv *e = &m->ev[i];
        us += (double)(e->tick - last) * usPerTick;
        last = e->tick;
        e->us = us > 4.0e9 ? 4000000000u : (uint32_t)us;
        if (e->meta && e->tempo) usPerTick = (double)e->tempo / m->division;
    }
    m->durationUs = m->nEv ? m->ev[m->nEv - 1].us : 0;
}

static Midi *parseMidi(const uint8_t *d, int len) {
    if (len < 14 || memcmp(d, "MThd", 4)) return NULL;
    uint32_t hl = be32m(d + 4);
    int ntracks = (d[10] << 8) | d[11];
    int division = (d[12] << 8) | d[13];
    if (division & 0x8000) division = 480; /* SMPTE: approximate */
    if (division <= 0) division = 480;
    if (hl > (uint32_t)len - 8) return NULL;
    size_t pos = 8 + (size_t)hl;
    int cap = 1024;
    Midi *m = newMidi(cap);
    if (!m) return NULL;
    m->division = division;
    int full = 0;
    for (int t = 0; t < ntracks && !full && pos + 8 <= (size_t)len; t++) {
        uint32_t tl = be32m(d + pos + 4);
        size_t avail = (size_t)len - pos - 8;
        size_t tlen = tl < avail ? tl : avail;
        if (memcmp(d + pos, "MTrk", 4)) { pos += 8 + tlen; continue; }
        const uint8_t *p = d + pos + 8, *end = p + tlen;
        uint32_t tick = 0;
        uint8_t running = 0;
        while (p < end) {
            tick += varlen(&p, end);
            if (p >= end) break;
            uint8_t st = *p;
            if (st & 0x80) p++; else st = running;
            if (st == 0xff) {
                if (p >= end) break;
                uint8_t type = *p++;
                uint32_t l = varlen(&p, end);
                if (l > (size_t)(end - p)) break;
                if (type == 0x51 && l == 3) {
                    if (!midiReserve(m, &cap, 1)) { full = 1; break; }
                    MEv *e = &m->ev[m->nEv++];
                    memset(e, 0, sizeof *e);
                    e->tick = tick; e->meta = 1; e->tempo = ((uint32_t)p[0] << 16) | (p[1] << 8) | p[2];
                }
                if (type == 0x2f) break;
                p += l;
                continue;
            }
            if (st == 0xf0 || st == 0xf7) {
                uint32_t l = varlen(&p, end);
                if (l > (size_t)(end - p)) break;
                p += l;
                continue;
            }
            if (!(st & 0x80)) break;
            running = st;
            int hi = st & 0xf0;
            uint8_t d1 = p < end ? *p++ : 0, d2 = 0;
            if (hi != 0xc0 && hi != 0xd0) d2 = p < end ? *p++ : 0;
            if (hi == 0x80 || hi == 0x90 || hi == 0xb0 || hi == 0xc0 || hi == 0xe0) {
                if (!midiReserve(m, &cap, 1)) { full = 1; break; }
                MEv *e = &m->ev[m->nEv++];
                e->tick = tick; e->st = st; e->d1 = d1 & 0x7f; e->d2 = d2 & 0x7f; e->meta = 0; e->tempo = 0;
            }
        }
        pos += 8 + tlen;
    }
    finalizeMidi(m);
    return m;
}

/* tone sequence (ToneControl) -> Midi */
static Midi *parseToneSeq(const uint8_t *d, int len) {
    if (len < 2 || (int8_t)d[0] != -2 || d[1] != 1) return NULL;
    int cap = 256;
    Midi *m = newMidi(cap);
    if (!m) return NULL;
    m->division = 64; /* resolution: 1/64 notes by default */
    int tempo = 30, res = 64, vol = 100;
    int blockStart[128] = { 0 };
    int pos = 2;
    uint32_t tick = 0;
    /* header: optional TEMPO, RESOLUTION, blocks */
    int stack[16], sp = 0;
    int guard = 0;
    while (pos + 1 < len && guard++ < 100000) {
        int8_t c = (int8_t)d[pos];
        int v = d[pos + 1];
        if (c == -3) { tempo = v; pos += 2; continue; }
        if (c == -4) { res = v ? v : 64; pos += 2; continue; }
        if (c == -5) { blockStart[v & 127] = pos + 2; /* skip to block end */
            pos += 2;
            while (pos + 1 < len && !((int8_t)d[pos] == -6 && d[pos + 1] == v)) pos += 2;
            pos += 2;
            continue; }
        if (c == -6) { if (sp > 0) pos = stack[--sp]; else pos += 2; continue; }
        if (c == -7) { if (sp < 16 && blockStart[v & 127]) { stack[sp++] = pos + 2; pos = blockStart[v & 127]; } else pos += 2; continue; }
        if (c == -8) { vol = v > 100 ? 100 : v; pos += 2; continue; }
        if (c == -9) { /* repeat next note v times */
            if (pos + 3 < len) {
                int note = (int8_t)d[pos + 2], dur = d[pos + 3];
                for (int r = 0; r < v; r++) {
                    if (!midiReserve(m, &cap, 3)) break;
                    if (note >= 0) {
                        m->ev[m->nEv++] = (MEv){ tick, 0x90, (uint8_t)note, (uint8_t)(vol * 127 / 100), 0, 0, 0 };
                        m->ev[m->nEv++] = (MEv){ tick + dur, 0x80, (uint8_t)note, 0, 0, 0, 0 };
                    }
                    tick += dur;
                }
            }
            pos += 4;
            continue;
        }
        /* note or silence */
        if (!midiReserve(m, &cap, 3)) break;
        if (c >= 0) {
            m->ev[m->nEv++] = (MEv){ tick, 0x90, (uint8_t)c, (uint8_t)(vol * 127 / 100), 0, 0, 0 };
            m->ev[m->nEv++] = (MEv){ tick + v, 0x80, (uint8_t)c, 0, 0, 0, 0 };
        }
        tick += v;
        pos += 2;
    }
    /* tempo: beats per minute = tempo*4; resolution res per whole note -> ticks per quarter = res/4 */
    m->division = res / 4 > 0 ? res / 4 : 16;
    if (m->nEv >= cap) {
        MEv *ne = realloc(m->ev, ((size_t)m->nEv + 1) * sizeof(MEv));
        if (!ne) { free(m->ev); free(m); return NULL; }
        m->ev = ne;
    }
    memmove(m->ev + 1, m->ev, m->nEv * sizeof(MEv));
    m->ev[0] = (MEv){ 0, 0, 0, 0, 1, (uint32_t)(60000000 / (tempo * 4 > 0 ? tempo * 4 : 120)), 0 };
    m->nEv++;
    finalizeMidi(m);
    return m;
}

/* ---------------- synth ----------------
 * Each GM instrument family has a band-limited wavetable (sum of harmonics) and an envelope.
 * Drums (channel 10) are synthesized: pitch-swept sines for kick/toms, filtered noise for
 * snare/hats/cymbals. */
#define NVOICES 24
#define WT 1024
static float wavetab[16][2][WT];   /* [family][0 = rich (low notes), 1 = few harmonics (high notes)] */
static int wavesReady;

typedef struct { float attackMs, decayMs, sustain, fadeSec, releaseMs; } Env;
static const Env famEnv[16] = {
    { 2, 400, 0.55f, 3.5f, 160 },  /* piano */
    { 1, 250, 0.30f, 1.6f, 300 },  /* chromatic percussion */
    { 8, 30, 1.00f, 0, 60 },       /* organ */
    { 2, 220, 0.50f, 2.2f, 120 },  /* guitar */
    { 3, 180, 0.70f, 3.0f, 90 },   /* bass */
    { 70, 120, 0.90f, 0, 260 },    /* strings */
    { 90, 150, 0.90f, 0, 300 },    /* ensemble */
    { 25, 120, 0.80f, 0, 110 },    /* brass */
    { 18, 80, 0.85f, 0, 90 },      /* reed */
    { 25, 80, 0.85f, 0, 110 },     /* pipe */
    { 4, 120, 0.80f, 0, 90 },      /* synth lead */
    { 160, 300, 0.85f, 0, 450 },   /* synth pad */
    { 20, 300, 0.60f, 0, 300 },    /* synth fx */
    { 3, 200, 0.50f, 2.0f, 150 },  /* ethnic */
    { 1, 150, 0.20f, 0.8f, 120 },  /* percussive */
    { 5, 200, 0.50f, 1.5f, 200 },  /* sound fx */
};

static float harmonicAmp(int fam, int h) {
    float fh = (float)h;
    switch (fam) {
        case 0: return 1.0f / (fh * sqrtf(fh));                        /* piano */
        case 1: return (h == 1) ? 1.0f : (h == 4) ? 0.5f : (h == 7) ? 0.3f : (h == 10) ? 0.15f : 0; /* bells */
        case 2: { static const float d[] = { 0, 1, .8f, .6f, .5f, 0, .35f, 0, .3f }; return h <= 8 ? d[h] : 0; } /* organ */
        case 3: return 1.0f / powf(fh, 1.3f) * (h % 5 == 0 ? 0.3f : 1);  /* guitar */
        case 4: return h == 1 ? 1.0f : 0.6f / (fh * fh);                 /* bass */
        case 5: case 6: case 11: return 0.9f / fh;                       /* strings, pads: saw-like */
        case 7: return (h >= 2 && h <= 5 ? 1.2f : 1.0f) / fh;            /* brass */
        case 8: return (h & 1) ? 1.0f / fh : 0.08f / fh;                 /* reed: odd harmonics */
        case 9: return h == 1 ? 1.0f : h == 2 ? 0.25f : h == 3 ? 0.1f : 0; /* flute */
        case 10: return (h & 1) ? 1.0f / fh : 0.5f / fh;                 /* lead */
        default: return 1.0f / (fh * sqrtf(fh));
    }
}

static void buildWaves(void) {
    if (wavesReady) return;
    for (int f = 0; f < 16; f++) {
        for (int v = 0; v < 2; v++) {
            int maxH = v == 0 ? 16 : 5;
            float peak = 0;
            for (int i = 0; i < WT; i++) {
                float x = 0;
                for (int h = 1; h <= maxH; h++) {
                    float a = harmonicAmp(f, h);
                    if (a != 0) x += a * sineTab[(i * h) & (WT - 1)] * (1.0f / 32767.0f);
                }
                wavetab[f][v][i] = x;
                if (fabsf(x) > peak) peak = fabsf(x);
            }
            if (peak > 0) for (int i = 0; i < WT; i++) wavetab[f][v][i] /= peak;
        }
    }
    wavesReady = 1;
}

enum { ST_ATTACK, ST_DECAY, ST_SUSTAIN, ST_RELEASE };
enum { DR_NONE, DR_KICK, DR_SNARE, DR_HAT, DR_CYMBAL, DR_TOM, DR_CLICK };

typedef struct {
    int active;
    int ch, note;
    uint32_t phase, inc;     /* 16.16 over WT */
    const float *wave;
    float vel;
    float env, attackInc, decayMul, sustain, fadeMul, releaseMul;
    int stage;
    int drum;
    float drumFreq, drumFreqMul, noiseLp, noisePrev, toneMix;
    uint32_t noise;
    int age;
    float lgain, rgain;
} Voice;

typedef struct {
    int program, volume, expr, pan, bend;
    float bendMul;
} Chan;

typedef struct {
    Midi *midi;
    int pos;
    double songUs;     /* position in the song */
    Voice v[NVOICES];
    Chan ch[16];
    int ageCounter;
    int dirty;
    int device;        /* live MIDI device (MIDIControl): never ends */
    float pitchMul;    /* PitchControl */
} Synth;

static void synthReset(Synth *s) {
    memset(s->v, 0, sizeof s->v);
    for (int i = 0; i < 16; i++) {
        s->ch[i].program = 0; s->ch[i].volume = 100; s->ch[i].expr = 127; s->ch[i].pan = 64; s->ch[i].bend = 8192;
        s->ch[i].bendMul = 1.0f;
    }
    s->pos = 0;
    s->songUs = 0;
    s->dirty = 1;
    if (s->pitchMul <= 0) s->pitchMul = 1.0f;
}

static void voiceGains(Synth *s, Voice *v) {
    Chan *c = &s->ch[v->ch];
    float g = v->vel * (c->volume / 127.0f) * (c->expr / 127.0f);
    g = g * g * 0.9f + g * 0.1f;            /* MIDI volume is roughly a square-law curve */
    float pan = c->pan / 127.0f;
    v->lgain = g * sqrtf(1.0f - pan) * 1.41f;
    v->rgain = g * sqrtf(pan) * 1.41f;
}

static float perSample(float ms) { return ms <= 0 ? 1.0f : 1000.0f / (ms * MIX_RATE); }
static float decayTo(float ms, float ratio) { /* multiplier per sample reaching `ratio` after ms */
    if (ms <= 0) return 0;
    return powf(ratio, 1000.0f / (ms * MIX_RATE));
}

static void noteOn(Synth *s, int ch, int note, int vel) {
    int slot = -1, oldest = -1, oldAge = 0x7fffffff;
    for (int i = 0; i < NVOICES; i++) {
        if (!s->v[i].active) { slot = i; break; }
        int age = s->v[i].age - (s->v[i].stage == ST_RELEASE ? 100000 : 0);
        if (age < oldAge) { oldAge = age; oldest = i; }
    }
    if (slot < 0) slot = oldest;
    Voice *v = &s->v[slot];
    memset(v, 0, sizeof *v);
    v->active = 1; v->ch = ch; v->note = note;
    v->vel = vel / 127.0f;
    v->age = ++s->ageCounter;
    v->noise = 0x12345678u ^ (note * 2654435761u) ^ (s->ageCounter * 40503u);
    if (ch == 9) {
        int d;
        switch (note) {
            case 35: case 36: d = DR_KICK; v->drumFreq = 140; v->drumFreqMul = 0.9993f; break;
            case 38: case 40: case 37: case 39: d = DR_SNARE; v->drumFreq = 190; v->drumFreqMul = 0.9999f; v->toneMix = 0.35f; break;
            case 42: case 44: case 46: d = DR_HAT; break;
            case 49: case 51: case 52: case 55: case 57: case 59: case 53: d = DR_CYMBAL; break;
            case 41: case 43: case 45: case 47: case 48: case 50:
                d = DR_TOM; v->drumFreq = 70 + (note - 41) * 18; v->drumFreqMul = 0.99985f; break;
            default: d = DR_CLICK; break;
        }
        v->drum = d;
        v->env = 1.0f;
        v->stage = ST_SUSTAIN;
        float len = d == DR_KICK ? 260 : d == DR_SNARE ? 180 : d == DR_HAT ? (note == 46 ? 220 : 60)
                  : d == DR_CYMBAL ? 900 : d == DR_TOM ? 320 : 50;
        v->fadeMul = decayTo(len, 0.001f);
        v->releaseMul = v->fadeMul;
        v->sustain = 0;
        voiceGains(s, v);
        return;
    }
    int fam = s->ch[ch].program / 8;
    const Env *e = &famEnv[fam];
    v->wave = wavetab[fam][note >= 72 ? 1 : 0];
    v->inc = (uint32_t)(noteInc[note & 127] * s->ch[ch].bendMul * s->pitchMul);
    v->stage = ST_ATTACK;
    v->env = 0;
    v->attackInc = perSample(e->attackMs);
    v->sustain = e->sustain;
    v->decayMul = decayTo(e->decayMs, 0.05f);
    v->fadeMul = e->fadeSec > 0 ? decayTo(e->fadeSec * 1000, 0.001f) : 1.0f;
    v->releaseMul = decayTo(e->releaseMs, 0.001f);
    voiceGains(s, v);
}

static void noteOff(Synth *s, int ch, int note) {
    if (ch == 9) return; /* drums ring out */
    for (int i = 0; i < NVOICES; i++)
        if (s->v[i].active && s->v[i].ch == ch && s->v[i].note == note && s->v[i].stage != ST_RELEASE) s->v[i].stage = ST_RELEASE;
}

static void processEvent(Synth *s, MEv *e) {
    if (e->meta) return;   /* tempo changes are already in the event times */
    int ch = e->st & 15;
    Chan *c = &s->ch[ch];
    switch (e->st & 0xf0) {
        case 0x90: if (e->d2) { noteOn(s, ch, e->d1, e->d2); break; } /* fallthrough */
        case 0x80: noteOff(s, ch, e->d1); break;
        case 0xb0:
            if (e->d1 == 7) c->volume = e->d2;
            else if (e->d1 == 11) c->expr = e->d2;
            else if (e->d1 == 10) c->pan = e->d2;
            else if (e->d1 == 120 || e->d1 == 123) { for (int i = 0; i < NVOICES; i++) if (s->v[i].ch == ch) s->v[i].stage = ST_RELEASE; }
            else if (e->d1 == 121) { c->volume = 100; c->expr = 127; c->pan = 64; c->bend = 8192; c->bendMul = 1; }
            s->dirty = 1;
            break;
        case 0xc0: c->program = e->d1; break;
        case 0xe0:
            c->bend = e->d1 | (e->d2 << 7);
            c->bendMul = powf(2.0f, (c->bend - 8192) / 8192.0f * 2.0f / 12.0f);
            for (int i = 0; i < NVOICES; i++) {
                Voice *v = &s->v[i];
                if (v->active && v->ch == ch && ch != 9) v->inc = (uint32_t)(noteInc[v->note & 127] * c->bendMul * s->pitchMul);
            }
            break;
    }
}

static inline float voiceSample(Voice *v) {
    if (v->drum) {
        v->noise = v->noise * 1664525u + 1013904223u;
        float n = ((int32_t)v->noise >> 8) * (1.0f / 8388608.0f);
        float out;
        switch (v->drum) {
            case DR_KICK: case DR_TOM: {
                v->phase += (uint32_t)(v->drumFreq * (WT * 65536.0f / MIX_RATE));
                v->drumFreq *= v->drumFreqMul;
                out = sineTab[(v->phase >> 16) & 1023] * (1.0f / 32767.0f);
                if (v->drum == DR_KICK && v->env > 0.85f) out += n * 0.3f; /* attack click */
                break;
            }
            case DR_SNARE: {
                v->phase += (uint32_t)(v->drumFreq * (WT * 65536.0f / MIX_RATE));
                float tone = sineTab[(v->phase >> 16) & 1023] * (1.0f / 32767.0f);
                v->noiseLp += (n - v->noiseLp) * 0.6f;
                out = v->noiseLp * (1 - v->toneMix) + tone * v->toneMix;
                break;
            }
            case DR_HAT: case DR_CYMBAL: {
                float hp = n - v->noisePrev;   /* crude high-pass: bright metallic noise */
                v->noisePrev = n;
                out = hp * (v->drum == DR_HAT ? 0.5f : 0.4f);
                break;
            }
            default: out = n * 0.5f; break;
        }
        v->env *= v->fadeMul;
        if (v->env < 0.0005f) v->active = 0;
        return out * v->env;
    }
    switch (v->stage) {
        case ST_ATTACK:
            v->env += v->attackInc;
            if (v->env >= 1.0f) { v->env = 1.0f; v->stage = ST_DECAY; }
            break;
        case ST_DECAY:
            v->env = v->sustain + (v->env - v->sustain) * v->decayMul;
            if (v->env - v->sustain < 0.002f) v->stage = ST_SUSTAIN;
            break;
        case ST_SUSTAIN:
            v->env *= v->fadeMul;
            if (v->env < 0.0005f) { v->active = 0; return 0; }
            break;
        default:
            v->env *= v->releaseMul;
            if (v->env < 0.0005f) { v->active = 0; return 0; }
    }
    v->phase += v->inc;
    uint32_t idx = (v->phase >> 16) & (WT - 1);
    float frac = (v->phase & 0xffff) * (1.0f / 65536.0f);
    float a = v->wave[idx], b = v->wave[(idx + 1) & (WT - 1)];
    return (a + (b - a) * frac) * v->env;
}

/* renders up to `frames`; returns how many were rendered before the song (and its
 * release tails) ended, or before songUs reached stopUs (stopUs < 0: no stop time) */
static int synthRender(Synth *s, int32_t *acc, int frames, int volume, double usPerSample, double stopUs, int *stopped) {
    Midi *m = s->midi;
    float master = volume / 100.0f * 8000.0f;
    for (int f = 0; f < frames; f++) {
        if (stopUs >= 0 && s->songUs >= stopUs) { *stopped = 1; return f; }
        while (s->pos < m->nEv && m->ev[s->pos].us <= s->songUs) processEvent(s, &m->ev[s->pos++]);
        if (s->dirty) {
            for (int i = 0; i < NVOICES; i++) if (s->v[i].active) voiceGains(s, &s->v[i]);
            s->dirty = 0;
        }
        if (s->pos >= m->nEv && !s->device) {
            int any = 0;
            for (int i = 0; i < NVOICES; i++) if (s->v[i].active) { any = 1; break; }
            if (!any) return f;
        }
        s->songUs += usPerSample;
        float l = 0, r = 0;
        for (int i = 0; i < NVOICES; i++) {
            Voice *v = &s->v[i];
            if (!v->active) continue;
            float x = voiceSample(v);
            l += x * v->lgain;
            r += x * v->rgain;
        }
        /* soft limiter keeps dense chords from clipping harshly */
        l *= master; r *= master;
        if (l > 24000 || l < -24000) l = (l > 0 ? 1 : -1) * (24000 + (fabsf(l) - 24000) * 0.25f);
        if (r > 24000 || r < -24000) r = (r > 0 ? 1 : -1) * (24000 + (fabsf(r) - 24000) * 0.25f);
        acc[f * 2] += (int32_t)l;
        acc[f * 2 + 1] += (int32_t)r;
    }
    return frames;
}

/* jumps to `us`: replays controller/program changes up to there, skips the notes */
static void synthSeek(Synth *s, double us) {
    synthReset(s);
    Midi *m = s->midi;
    while (s->pos < m->nEv && m->ev[s->pos].us < us) {
        MEv *e = &m->ev[s->pos++];
        int hi = e->st & 0xf0;
        if (!e->meta && hi != 0x80 && hi != 0x90) processEvent(s, e);
    }
    memset(s->v, 0, sizeof s->v);
    s->songUs = us;
    s->dirty = 1;
}

/* ---------------- Nokia ringing tones (Smart Messaging OTA format) ----------------
 * Used by com.nokia.mid.sound.Sound with FORMAT_TONE. Bit stream, MSB first. */
typedef struct { const uint8_t *d; int len, bit; } Bits;
static int getb(Bits *b, int n) {
    int v = 0;
    while (n--) {
        if (b->bit >= b->len * 8) return -1;
        v = (v << 1) | ((b->d[b->bit >> 3] >> (7 - (b->bit & 7))) & 1);
        b->bit++;
    }
    return v;
}

static Midi *parseNokiaOta(const uint8_t *d, int len) {
    static const int bpmTab[32] = { 25, 28, 31, 35, 40, 45, 50, 56, 63, 70, 80, 90, 100, 112, 125, 140,
                                    160, 180, 200, 225, 250, 285, 320, 355, 400, 450, 500, 565, 635, 715, 800, 900 };
    Bits b = { d, len, 0 };
    if (getb(&b, 8) <= 0) return NULL;                         /* command length */
    if (getb(&b, 7) != 0x25) return NULL;                      /* ringing-tone-programming */
    if (getb(&b, 7) != 0x1d) return NULL;                      /* sound */
    int songType = getb(&b, 3);
    if (songType == 1) {                                       /* basic song: skip the title */
        int tl = getb(&b, 4);
        if (tl < 0) return NULL;
        for (int i = 0; i < tl; i++) if (getb(&b, 8) < 0) return NULL;
    } else if (songType != 2) return NULL;
    int nPatterns = getb(&b, 8);
    if (nPatterns <= 0) return NULL;
    int cap = 256;
    Midi *m = newMidi(cap);
    if (!m) return NULL;
    m->division = 48;                                          /* ticks per quarter note */
    int scale = 1, style = 0, vol = 100, bpm = 63;
    uint32_t tick = 0;
    /* tempo meta event first; updated by tempo instructions */
    for (int p = 0; p < nPatterns && p < 64; p++) {
        if (getb(&b, 3) != 0) break;                           /* pattern header id */
        getb(&b, 2);                                           /* pattern id */
        int loop = getb(&b, 4), nInstr = getb(&b, 8);
        if (loop < 0 || nInstr < 0) break;
        int startBit = b.bit;
        int reps = loop == 15 ? 3 : loop + 1;                  /* "infinite" pattern loops: a few times */
        for (int r = 0; r < reps; r++) {
            b.bit = startBit;
            for (int i = 0; i < nInstr; i++) {
                int id = getb(&b, 3);
                if (id < 0) goto done;
                if (id == 1) {                                 /* note */
                    int nv = getb(&b, 4), dur = getb(&b, 3), spec = getb(&b, 2);
                    if (spec < 0) goto done;
                    int t = 192 >> (dur > 5 ? 5 : dur);         /* whole note = 192 ticks */
                    if (spec == 1) t = t * 3 / 2; else if (spec == 2) t = t * 7 / 4; else if (spec == 3) t = t * 2 / 3;
                    if (nv >= 1 && nv <= 12) {
                        int note = 60 + (scale - 1) * 12 + 12 + (nv - 1);   /* scale-2 (A = 880 Hz) is the default */
                        if (note > 127) note = 127;
                        int on = style == 2 ? t / 2 : style == 1 ? t : t * 7 / 8;
                        if (!midiReserve(m, &cap, 2)) goto done;
                        m->ev[m->nEv++] = (MEv){ tick, 0x90, (uint8_t)note, (uint8_t)(vol * 127 / 100), 0, 0, 0 };
                        m->ev[m->nEv++] = (MEv){ tick + (on > 0 ? on : 1), 0x80, (uint8_t)note, 0, 0, 0, 0 };
                    }
                    tick += t;
                } else if (id == 2) scale = getb(&b, 2);
                else if (id == 3) style = getb(&b, 2);
                else if (id == 4) {
                    int bi = getb(&b, 5);
                    if (bi < 0) goto done;
                    bpm = bpmTab[bi];
                    if (!midiReserve(m, &cap, 1)) goto done;
                    m->ev[m->nEv++] = (MEv){ tick, 0, 0, 0, 1, (uint32_t)(60000000 / bpm), 0 };
                } else if (id == 5) { int v = getb(&b, 4); if (v < 0) goto done; vol = v * 100 / 15; }
                else goto done;
                if (scale < 0 || style < 0) goto done;
            }
        }
    }
done:
    if (!midiReserve(m, &cap, 1)) { free(m->ev); free(m); return NULL; }
    m->ev[m->nEv++] = (MEv){ 0, 0, 0, 0, 1, (uint32_t)(60000000 / bpm), 0 };   /* initial tempo */
    finalizeMidi(m);
    if (m->nEv <= 1) { free(m->ev); free(m); return NULL; }
    return m;
}

/* ---------------- channels ----------------
 * A channel is one loaded sound (a Player): it keeps its position while paused.
 * Channels that are not playing may be evicted when slots or memory run out; the Java
 * side then simply loads the sound again. */
#define NCHAN 48
#define MIX_BYTES_BUDGET (6 * 1024 * 1024)
enum { CH_FREE, CH_PCM, CH_SYNTH, CH_TONE };
typedef struct {
    int handle;
    int type;
    int playing;
    int atEnd;           /* reached the end: start() rewinds */
    int autoFree;        /* fire-and-forget (tones, mix_play): freed when finished */
    int loops;           /* plays left including the current one; -1 = forever */
    int loopsDone;       /* how often the end was reached (END_OF_MEDIA events) */
    int stoppedAtTime;
    double stopAtUs;     /* StopTimeControl; < 0 = none */
    int volume;
    int rateMilli;       /* RateControl: 100000 = normal speed */
    uint32_t lastUse;
    size_t bytes;
    /* CH_PCM */
    int16_t *pcm;
    int frames, chans, rate;
    uint64_t pos;        /* 48.16 fixed-point frame position */
    /* CH_SYNTH */
    Synth *synth;
    /* CH_TONE */
    int toneLeft;
    uint32_t tonePhase, toneInc;
} Chn;

static Chn chans[NCHAN];
static int nextHandle = 1;
static uint32_t useCounter;

void mix_init(void (*lock)(void), void (*unlock)(void)) {
    if (lock) lockFn = lock;
    if (unlock) unlockFn = unlock;
    initTables();
}

static void freeChn(Chn *c) {
    free(c->pcm);
    if (c->synth) { free(c->synth->midi->ev); free(c->synth->midi); free(c->synth); }
    memset(c, 0, sizeof *c);
}

static Chn *findChn(int h) {
    if (h <= 0) return NULL;
    for (int i = 0; i < NCHAN; i++) if (chans[i].handle == h) return &chans[i];
    return NULL;
}

static size_t bytesInUse(void) {
    size_t n = 0;
    for (int i = 0; i < NCHAN; i++) if (chans[i].handle) n += chans[i].bytes;
    return n;
}

/* least recently used channel that isn't playing (NULL if all play) */
static Chn *idleLru(void) {
    Chn *best = NULL;
    for (int i = 0; i < NCHAN; i++) {
        Chn *c = &chans[i];
        if (c->handle && !c->playing && (!best || (int32_t)(c->lastUse - best->lastUse) < 0)) best = c;
    }
    return best;
}

static Chn *allocChn(size_t bytes) {
    while (bytesInUse() + bytes > MIX_BYTES_BUDGET) {
        Chn *v = idleLru();
        if (!v) break;
        freeChn(v);
    }
    for (int i = 0; i < NCHAN; i++) if (!chans[i].handle) return &chans[i];
    Chn *v = idleLru();
    if (!v) {   /* everything is playing: replace the oldest fire-and-forget sound */
        for (int i = 0; i < NCHAN; i++) if (chans[i].autoFree && (!v || (int32_t)(chans[i].lastUse - v->lastUse) < 0)) v = &chans[i];
    }
    if (!v) return NULL;
    freeChn(v);
    return v;
}

static Chn *newChn(int type, size_t bytes) {
    Chn *c = allocChn(bytes);
    if (!c) return NULL;
    memset(c, 0, sizeof *c);
    c->handle = nextHandle++;
    if (nextHandle <= 0) nextHandle = 1;
    c->type = type;
    c->loops = 1;
    c->volume = 100;
    c->rateMilli = 100000;
    c->stopAtUs = -1;
    c->bytes = bytes;
    c->lastUse = ++useCounter;
    return c;
}

static Synth *newSynth(Midi *midi, int device) {
    Synth *syn = calloc(1, sizeof(Synth));
    if (!syn) return NULL;
    syn->midi = midi;
    syn->device = device;
    syn->pitchMul = 1.0f;
    synthReset(syn);
    return syn;
}

/* decodes or parses a sound; returns a stopped channel's handle or -1 */
static int loadLocked(const uint8_t *data, int len, int nokiaTone) {
    int frames, ch, rate;
    int16_t *pcm = nokiaTone ? NULL : decodeWav(data, len, &frames, &ch, &rate);
    if (pcm) {
        Chn *c = newChn(CH_PCM, (size_t)frames * ch * 2);
        if (!c) { free(pcm); return -1; }
        c->pcm = pcm; c->frames = frames; c->chans = ch; c->rate = rate;
        return c->handle;
    }
    Midi *midi = nokiaTone ? parseNokiaOta(data, len) : parseMidi(data, len);
    if (!midi && !nokiaTone) midi = parseToneSeq(data, len);
    if (!midi) return -1;
    Synth *syn = newSynth(midi, 0);
    Chn *c = syn ? newChn(CH_SYNTH, sizeof(Synth) + (size_t)midi->nEv * sizeof(MEv)) : NULL;
    if (!c) { free(syn); free(midi->ev); free(midi); return -1; }
    c->synth = syn;
    return c->handle;
}

int mix_load(const uint8_t *data, int len, int nokiaTone) {
    initTables();
    lockFn();
    int h = loadLocked(data, len, nokiaTone);
    unlockFn();
    return h;
}

/* a beep at freqHz (com.nokia.mid.sound.Sound(freq, duration)) */
int mix_load_beep(int freqHz, int durationMs) {
    if (freqHz < 20) freqHz = 20;
    if (freqHz > 20000) freqHz = 20000;
    if (durationMs < 1) durationMs = 1;
    if (durationMs > 10000) durationMs = 10000;
    int rate = 22050, frames = (int)((int64_t)durationMs * rate / 1000);
    int16_t *pcm = malloc((size_t)frames * 2 + 2);
    if (!pcm) return -1;
    for (int i = 0; i < frames; i++) {
        double t = (double)i / rate;
        double env = i < 200 ? i / 200.0 : (frames - i < 400 ? (frames - i) / 400.0 : 1.0);   /* no clicks */
        pcm[i] = (int16_t)((fmod(t * freqHz, 1.0) < 0.5 ? 7000 : -7000) * env);
    }
    lockFn();
    Chn *c = newChn(CH_PCM, (size_t)frames * 2);
    int h = -1;
    if (c) { c->pcm = pcm; c->frames = frames; c->chans = 1; c->rate = rate; h = c->handle; }
    else free(pcm);
    unlockFn();
    return h;
}

/* an empty, never-ending synth for "device://midi" players driven by MIDIControl */
int mix_load_midi_device(void) {
    initTables();
    Midi *m = newMidi(1);
    if (!m) return -1;
    m->division = 480;
    lockFn();
    Synth *syn = newSynth(m, 1);
    Chn *c = syn ? newChn(CH_SYNTH, sizeof(Synth)) : NULL;
    int h = -1;
    if (c) { c->synth = syn; h = c->handle; }
    else { free(syn); free(m->ev); free(m); }
    unlockFn();
    return h;
}

static void rewindChn(Chn *c) {
    c->pos = 0;
    if (c->synth) synthReset(c->synth);
    c->atEnd = 0;
}

void mix_start(int h) {
    lockFn();
    Chn *c = findChn(h);
    if (c) {
        if (c->atEnd) rewindChn(c);
        c->playing = 1;
        c->stoppedAtTime = 0;
        c->lastUse = ++useCounter;
    }
    unlockFn();
}

void mix_pause(int h) {
    lockFn();
    Chn *c = findChn(h);
    if (c) {
        c->playing = 0;
        if (c->synth) for (int i = 0; i < NVOICES; i++) c->synth->v[i].stage = ST_RELEASE;
    }
    unlockFn();
}

void mix_free(int h) {
    lockFn();
    Chn *c = findChn(h);
    if (c) freeChn(c);
    unlockFn();
}

int mix_valid(int h) {
    lockFn();
    int r = findChn(h) != NULL;
    unlockFn();
    return r;
}

/* bit 0: playing, bit 1: stopped at the StopTimeControl time, bit 2: at the end */
int mix_state(int h) {
    lockFn();
    Chn *c = findChn(h);
    int r = c ? (c->playing ? 1 : 0) | (c->stoppedAtTime ? 2 : 0) | (c->atEnd ? 4 : 0) : 0;
    unlockFn();
    return r;
}

void mix_set_loops(int h, int loops) {
    lockFn();
    Chn *c = findChn(h);
    if (c) c->loops = loops == 0 ? 1 : loops < -1 ? -1 : loops;
    unlockFn();
}

int mix_loops_done(int h) {
    lockFn();
    Chn *c = findChn(h);
    int r = c ? c->loopsDone : 0;
    unlockFn();
    return r;
}

static int64_t timeUs(Chn *c) {
    if (c->type == CH_PCM) return (int64_t)((c->pos >> 16) * 1000000ULL / (uint64_t)c->rate);
    if (c->type == CH_SYNTH) {
        double t = c->synth->songUs;
        if (!c->synth->device && t > c->synth->midi->durationUs) t = c->synth->midi->durationUs;
        return (int64_t)t;
    }
    return 0;
}

int64_t mix_time_us(int h) {
    lockFn();
    Chn *c = findChn(h);
    int64_t r = c ? timeUs(c) : 0;
    unlockFn();
    return r;
}

int64_t mix_duration_us(int h) {
    lockFn();
    Chn *c = findChn(h);
    int64_t r = -1;
    if (c && c->type == CH_PCM) r = (int64_t)c->frames * 1000000 / c->rate;
    else if (c && c->type == CH_SYNTH && !c->synth->device) r = c->synth->midi->durationUs;
    unlockFn();
    return r;
}

int64_t mix_seek_us(int h, int64_t us) {
    lockFn();
    Chn *c = findChn(h);
    int64_t r = 0;
    if (c) {
        if (us < 0) us = 0;
        if (c->type == CH_PCM) {
            uint64_t f = (uint64_t)us * (uint64_t)c->rate / 1000000ULL;
            if (f > (uint64_t)c->frames) f = c->frames;
            c->pos = f << 16;
        } else if (c->type == CH_SYNTH && !c->synth->device) {
            double d = c->synth->midi->durationUs;
            synthSeek(c->synth, us > d ? d : (double)us);
        }
        c->atEnd = 0;
        r = timeUs(c);
    }
    unlockFn();
    return r;
}

void mix_stop_at(int h, int64_t us) {
    lockFn();
    Chn *c = findChn(h);
    if (c) c->stopAtUs = us < 0 ? -1 : (double)us;
    unlockFn();
}

void mix_volume(int h, int volume) {
    lockFn();
    Chn *c = findChn(h);
    if (c) c->volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
    unlockFn();
}

void mix_rate(int h, int milli) {
    lockFn();
    Chn *c = findChn(h);
    if (c) c->rateMilli = milli < 10000 ? 10000 : milli > 400000 ? 400000 : milli;
    unlockFn();
}

void mix_pitch(int h, int milliSemitones) {
    lockFn();
    Chn *c = findChn(h);
    if (c && c->synth) {
        Synth *s = c->synth;
        s->pitchMul = powf(2.0f, milliSemitones / 12000.0f);
        for (int i = 0; i < NVOICES; i++) {
            Voice *v = &s->v[i];
            if (v->active && !v->drum) v->inc = (uint32_t)(noteInc[v->note & 127] * s->ch[v->ch].bendMul * s->pitchMul);
        }
    }
    unlockFn();
}

/* MIDIControl.shortMidiEvent (and setProgram / setChannelVolume) */
void mix_midi_event(int h, int status, int d1, int d2) {
    lockFn();
    Chn *c = findChn(h);
    if (c && c->synth && (status & 0x80) && status < 0xf0) {
        MEv e = { 0, (uint8_t)status, (uint8_t)(d1 & 0x7f), (uint8_t)(d2 & 0x7f), 0, 0, 0 };
        processEvent(c->synth, &e);
        if (c->synth->device) c->playing = 1;
    }
    unlockFn();
}

/* what: 0 = program of channel ch, 1 = channel volume; -1 if not a MIDI player */
int mix_midi_query(int h, int what, int ch) {
    lockFn();
    Chn *c = findChn(h);
    int r = -1;
    if (c && c->synth && ch >= 0 && ch < 16) r = what == 0 ? c->synth->ch[ch].program : c->synth->ch[ch].volume;
    unlockFn();
    return r;
}

/* fire-and-forget playback (used by the tests and tools) */
int mix_play(const uint8_t *data, int len, int loops, int volume) {
    int h = mix_load(data, len, 0);
    if (h <= 0) return -1;
    lockFn();
    Chn *c = findChn(h);
    if (c) { c->loops = loops == 0 ? 1 : loops; c->volume = volume; c->autoFree = 1; c->playing = 1; }
    unlockFn();
    return h;
}

void mix_stop(int handle) { mix_free(handle); }

int mix_playing(int handle) { return mix_state(handle) & 1; }

void mix_stop_all(void) {
    lockFn();
    for (int i = 0; i < NCHAN; i++) if (chans[i].handle) freeChn(&chans[i]);
    unlockFn();
}

void mix_tone(int note, int durationMs, int volume) {
    initTables();
    if (durationMs < 0) durationMs = 0;
    if (durationMs > 60000) durationMs = 60000;
    lockFn();
    Chn *c = newChn(CH_TONE, 0);
    if (c) {
        c->autoFree = 1;
        c->playing = 1;
        c->toneLeft = durationMs * MIX_RATE / 1000;
        c->toneInc = noteInc[note & 127];
        c->volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
    }
    unlockFn();
}

/* the end of the media was reached: loop, or stop at the end */
static int endReached(Chn *c) {
    c->loopsDone++;
    if (c->loops == -1 || c->loops > 1) {
        if (c->loops > 1) c->loops--;
        rewindChn(c);
        return 1;
    }
    c->playing = 0;
    c->atEnd = 1;
    if (c->autoFree) freeChn(c);
    return 0;
}

static void renderPcm(Chn *c, int32_t *acc, int frames) {
    uint64_t step = ((uint64_t)c->rate << 16) * (uint64_t)c->rateMilli / ((uint64_t)MIX_RATE * 100000);
    if (step == 0) step = 1;
    uint64_t stopFrame = c->stopAtUs >= 0 ? (uint64_t)(c->stopAtUs * c->rate / 1000000.0) : UINT64_MAX;
    int vol = c->volume;
    for (int f = 0; f < frames; f++) {
        uint64_t fi = c->pos >> 16;
        if (fi >= stopFrame) { c->playing = 0; c->stoppedAtTime = 1; c->stopAtUs = -1; return; }
        if (fi >= (uint64_t)c->frames) {
            if (!endReached(c)) return;
            fi = 0;
        }
        /* linear interpolation between neighbouring samples */
        int frac = (int)(c->pos & 0xffff) >> 1;   /* 15 bits: (diff * frac) fits in an int */
        uint64_t fn = fi + 1 < (uint64_t)c->frames ? fi + 1 : fi;
        int l, r;
        if (c->chans == 2) {
            l = c->pcm[fi * 2] + ((c->pcm[fn * 2] - c->pcm[fi * 2]) * frac >> 15);
            r = c->pcm[fi * 2 + 1] + ((c->pcm[fn * 2 + 1] - c->pcm[fi * 2 + 1]) * frac >> 15);
        } else l = r = c->pcm[fi] + ((c->pcm[fn] - c->pcm[fi]) * frac >> 15);
        acc[f * 2] += l * vol / 100;
        acc[f * 2 + 1] += r * vol / 100;
        c->pos += step;
    }
}

void mix_render(int16_t *out, int frames) {
    static int32_t *acc;
    static int accCap;
    if (frames > accCap) {
        free(acc);
        acc = malloc(frames * 2 * sizeof(int32_t));
        accCap = acc ? frames : 0;
        if (!acc) { memset(out, 0, (size_t)frames * 2 * sizeof(int16_t)); return; }
    }
    memset(acc, 0, frames * 2 * sizeof(int32_t));
    lockFn();
    for (int i = 0; i < NCHAN; i++) {
        Chn *c = &chans[i];
        if (!c->handle) continue;
        if (c->type == CH_SYNTH) {
            /* a paused synth still renders its release tails, then goes quiet */
            double usPerSample = 1000000.0 / MIX_RATE * c->rateMilli / 100000.0;
            int done = 0;
            if (!c->playing) {
                int any = 0;
                for (int v = 0; v < NVOICES; v++) if (c->synth->v[v].active) { any = 1; break; }
                if (any) {
                    /* render the tails without advancing the song */
                    double keep = c->synth->songUs;
                    int pos = c->synth->pos;
                    c->synth->pos = c->synth->midi->nEv;
                    int dev = c->synth->device;
                    c->synth->device = 1;
                    synthRender(c->synth, acc, frames, c->volume, 0, -1, &done);
                    c->synth->device = dev;
                    c->synth->pos = pos;
                    c->synth->songUs = keep;
                }
                continue;
            }
            int f = 0;
            while (f < frames && c->handle && c->playing) {
                int stopped = 0;
                int n = synthRender(c->synth, acc + f * 2, frames - f, c->volume, usPerSample, c->stopAtUs, &stopped);
                f += n;
                if (stopped) { c->playing = 0; c->stoppedAtTime = 1; c->stopAtUs = -1; break; }
                if (f < frames && !endReached(c)) break;
            }
        } else if (!c->playing) {
            continue;
        } else if (c->type == CH_PCM) {
            renderPcm(c, acc, frames);
        } else if (c->type == CH_TONE) {
            int n = frames < c->toneLeft ? frames : c->toneLeft;
            for (int f = 0; f < n; f++) {
                c->tonePhase += c->toneInc;
                int s = ((c->tonePhase >> 16) & 1023) < 512 ? 8000 : -8000;
                s = s * c->volume / 100;
                acc[f * 2] += s; acc[f * 2 + 1] += s;
            }
            c->toneLeft -= n;
            if (c->toneLeft <= 0) freeChn(c);
        }
    }
    unlockFn();
    for (int i = 0; i < frames * 2; i++) {
        int32_t v = acc[i];
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        out[i] = (int16_t)v;
    }
}
