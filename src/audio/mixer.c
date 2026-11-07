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
typedef struct { uint32_t tick; uint8_t st, d1, d2; uint8_t meta; uint32_t tempo; } MEv;

typedef struct {
    MEv *ev;
    int nEv;
    int division;
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
    qsort(m->ev, m->nEv, sizeof(MEv), cmpEv);
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
                        m->ev[m->nEv++] = (MEv){ tick, 0x90, (uint8_t)note, (uint8_t)(vol * 127 / 100), 0, 0 };
                        m->ev[m->nEv++] = (MEv){ tick + dur, 0x80, (uint8_t)note, 0, 0, 0 };
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
            m->ev[m->nEv++] = (MEv){ tick, 0x90, (uint8_t)c, (uint8_t)(vol * 127 / 100), 0, 0 };
            m->ev[m->nEv++] = (MEv){ tick + v, 0x80, (uint8_t)c, 0, 0, 0 };
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
    m->ev[0] = (MEv){ 0, 0, 0, 0, 1, (uint32_t)(60000000 / (tempo * 4 > 0 ? tempo * 4 : 120)) };
    m->nEv++;
    qsort(m->ev, m->nEv, sizeof(MEv), cmpEv);
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
    double now;
    double anchor;
    double tickSamples;
    uint32_t curTick;
    Voice v[NVOICES];
    Chan ch[16];
    int loopsLeft;
    int ageCounter;
    int dirty;
} Synth;

static void synthReset(Synth *s) {
    memset(s->v, 0, sizeof s->v);
    for (int i = 0; i < 16; i++) {
        s->ch[i].program = 0; s->ch[i].volume = 100; s->ch[i].expr = 127; s->ch[i].pan = 64; s->ch[i].bend = 8192;
        s->ch[i].bendMul = 1.0f;
    }
    s->pos = 0;
    s->curTick = 0;
    s->now = 0;
    s->anchor = 0;
    s->tickSamples = (500000.0 / s->midi->division) * MIX_RATE / 1000000.0;
    s->dirty = 1;
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
    v->inc = (uint32_t)(noteInc[note & 127] * s->ch[ch].bendMul);
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
    if (e->meta) { s->tickSamples = ((double)e->tempo / s->midi->division) * MIX_RATE / 1000000.0; return; }
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
                if (v->active && v->ch == ch && ch != 9) v->inc = (uint32_t)(noteInc[v->note & 127] * c->bendMul);
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

/* returns 0 when finished */
static int synthRender(Synth *s, int32_t *acc, int frames, int volume) {
    Midi *m = s->midi;
    float master = volume / 100.0f * 8000.0f;
    for (int f = 0; f < frames; f++) {
        while (s->pos < m->nEv) {
            MEv *e = &m->ev[s->pos];
            double due = s->anchor + (double)(e->tick - s->curTick) * s->tickSamples;
            if (due > s->now) break;
            s->anchor = due;
            s->curTick = e->tick;
            processEvent(s, e);
            s->pos++;
        }
        if (s->dirty) {
            for (int i = 0; i < NVOICES; i++) if (s->v[i].active) voiceGains(s, &s->v[i]);
            s->dirty = 0;
        }
        if (s->pos >= m->nEv) {
            int any = 0;
            for (int i = 0; i < NVOICES; i++) if (s->v[i].active) { any = 1; break; }
            if (!any) {
                if (s->loopsLeft == -1 || --s->loopsLeft > 0) synthReset(s);
                else return 0;
            }
        }
        s->now += 1.0;
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
    return 1;
}

/* ---------------- channels ---------------- */
#define NCHAN 8
typedef struct {
    int handle;
    int type;          /* 1 pcm, 2 synth, 3 tone */
    int16_t *pcm;
    int frames, chans;
    uint32_t pos;      /* 16.16 frame position */
    uint32_t step;
    int loops;
    int volume;
    Synth *synth;
    int toneLeft;
    uint32_t tonePhase, toneInc;
} Chn;

static Chn chans[NCHAN];
static int nextHandle = 1;

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

static Chn *allocChn(void) {
    for (int i = 0; i < NCHAN; i++) if (!chans[i].handle) return &chans[i];
    /* steal the first PCM channel */
    for (int i = 0; i < NCHAN; i++) if (chans[i].type == 1) { freeChn(&chans[i]); return &chans[i]; }
    freeChn(&chans[0]);
    return &chans[0];
}

int mix_play(const uint8_t *data, int len, int loops, int volume) {
    initTables();
    int frames, ch, rate;
    int16_t *pcm = decodeWav(data, len, &frames, &ch, &rate);
    Midi *midi = NULL;
    if (!pcm) {
        midi = parseMidi(data, len);
        if (!midi) midi = parseToneSeq(data, len);
        if (!midi) return -1;
    }
    Synth *syn = NULL;
    if (midi) {
        syn = calloc(1, sizeof(Synth));
        if (!syn) { free(midi->ev); free(midi); return -1; }
    }
    lockFn();
    Chn *c = allocChn();
    c->handle = nextHandle++;
    c->loops = loops == 0 ? 1 : loops;
    c->volume = volume;
    if (pcm) {
        c->type = 1; c->pcm = pcm; c->frames = frames; c->chans = ch;
        c->step = (uint32_t)(((uint64_t)rate << 16) / MIX_RATE);
        c->pos = 0;
    } else {
        c->type = 2;
        c->synth = syn;
        c->synth->midi = midi;
        c->synth->loopsLeft = c->loops;
        synthReset(c->synth);
    }
    int h = c->handle;
    unlockFn();
    return h;
}

void mix_stop(int handle) {
    lockFn();
    for (int i = 0; i < NCHAN; i++) if (chans[i].handle == handle) freeChn(&chans[i]);
    unlockFn();
}

void mix_stop_all(void) {
    lockFn();
    for (int i = 0; i < NCHAN; i++) if (chans[i].handle) freeChn(&chans[i]);
    unlockFn();
}

int mix_playing(int handle) {
    int r = 0;
    lockFn();
    for (int i = 0; i < NCHAN; i++) if (chans[i].handle == handle) r = 1;
    unlockFn();
    return r;
}

void mix_volume(int handle, int volume) {
    lockFn();
    for (int i = 0; i < NCHAN; i++) if (chans[i].handle == handle) chans[i].volume = volume;
    unlockFn();
}

void mix_tone(int note, int durationMs, int volume) {
    initTables();
    lockFn();
    Chn *c = allocChn();
    c->handle = nextHandle++;
    c->type = 3;
    if (durationMs < 0) durationMs = 0;
    if (durationMs > 60000) durationMs = 60000;
    c->toneLeft = durationMs * MIX_RATE / 1000;
    c->toneInc = noteInc[note & 127];
    c->volume = volume;
    unlockFn();
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
        if (c->type == 1) {
            for (int f = 0; f < frames; f++) {
                uint32_t fi = c->pos >> 16;
                if ((int)fi >= c->frames) {
                    if (c->loops == -1 || --c->loops > 0) { c->pos = 0; fi = 0; }
                    else { freeChn(c); break; }
                }
                int l, r;
                if (c->chans == 2) { l = c->pcm[fi * 2]; r = c->pcm[fi * 2 + 1]; }
                else l = r = c->pcm[fi];
                acc[f * 2] += l * c->volume / 100;
                acc[f * 2 + 1] += r * c->volume / 100;
                c->pos += c->step;
            }
        } else if (c->type == 2) {
            if (!synthRender(c->synth, acc, frames, c->volume)) freeChn(c);
        } else if (c->type == 3) {
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
