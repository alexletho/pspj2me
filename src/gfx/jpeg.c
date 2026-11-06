/* Baseline (sequential, Huffman) JPEG decoder -> ARGB8888.
 * Supports 8-bit greyscale and YCbCr with any sampling factors, restart markers.
 * Progressive JPEGs are not supported (returns NULL). */
#include "gfx.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t bits[17];
    uint8_t vals[256];
    /* fast lookup: code -> (length<<8 | value), built on demand */
    int maxcode[18];
    int valptr[17];
    int mincode[17];
} Huff;

typedef struct {
    int id, h, v, tq;
    int td, ta;
    int pred;
    uint8_t *pix;     /* component plane */
    int pw, ph;       /* plane size (blocks*8) */
} Comp;

typedef struct {
    const uint8_t *d;
    int len, pos;
    uint32_t bitbuf;
    int bitcnt;
    int marker;
    uint16_t qt[4][64];
    Huff hdc[4], hac[4];
    Comp comp[3];
    int ncomp;
    int w, h;
    int hmax, vmax;
    int restart;
    int progressive;
} Jpg;

static const uint8_t zigzag[64] = { 0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21,
    28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

static void buildHuff(Huff *h) {
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
        h->valptr[l] = k;
        h->mincode[l] = code;
        code += h->bits[l];
        k += h->bits[l];
        h->maxcode[l] = h->bits[l] ? code - 1 : -1;
        code <<= 1;
    }
    h->maxcode[17] = 0x7fffffff;
}

static int getbit(Jpg *j) {
    if (j->bitcnt == 0) {
        if (j->marker || j->pos >= j->len) { j->bitbuf = 0; j->bitcnt = 8; }
        else {
            int b = j->d[j->pos++];
            if (b == 0xff) {
                int b2 = j->pos < j->len ? j->d[j->pos] : 0;
                if (b2 == 0) j->pos++;
                else { j->marker = b2; b = 0; }
            }
            j->bitbuf = b;
            j->bitcnt = 8;
        }
    }
    j->bitcnt--;
    return (j->bitbuf >> j->bitcnt) & 1;
}

static int getbits(Jpg *j, int n) {
    int v = 0;
    while (n--) v = (v << 1) | getbit(j);
    return v;
}

static int decodeHuff(Jpg *j, Huff *h) {
    int code = 0;
    for (int l = 1; l <= 16; l++) {
        code = (code << 1) | getbit(j);
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l]) return h->vals[h->valptr[l] + code - h->mincode[l]];
    }
    return 0;
}

static int extend(int v, int t) { return v < (1 << (t - 1)) ? v - (1 << t) + 1 : v; }

/* simple separable float-free IDCT (AAN-free, integer) */
static void idct(const int *in, uint8_t *out, int stride) {
    static int c[8][8];
    static int ready;
    if (!ready) {
        /* cos table scaled by 4096 */
        static const int cs[8] = { 4096, 4017, 3784, 3406, 2896, 2276, 1567, 799 };
        for (int x = 0; x < 8; x++)
            for (int u = 0; u < 8; u++) {
                int k = ((2 * x + 1) * u) % 32;
                int sign = 1;
                if (k > 16) { k = 32 - k; }
                if (k > 8) { k = 16 - k; sign = -1; }
                int v = (k == 8) ? 0 : cs[k];
                if (u == 0) v = 2896; /* 1/sqrt2 */
                c[x][u] = sign * v;
            }
        ready = 1;
    }
    int tmp[64];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int s = 0;
            for (int u = 0; u < 8; u++) s += c[x][u] * in[y * 8 + u];
            tmp[y * 8 + x] = s >> 10;
        }
    for (int x = 0; x < 8; x++)
        for (int y = 0; y < 8; y++) {
            int s = 0;
            for (int v = 0; v < 8; v++) s += c[y][v] * tmp[v * 8 + x];
            s = (s >> 16) + 128;
            if (s < 0) s = 0;
            if (s > 255) s = 255;
            out[y * stride + x] = (uint8_t)s;
        }
}

static void decodeBlock(Jpg *j, Comp *c, uint8_t *dst, int stride) {
    int coef[64];
    memset(coef, 0, sizeof coef);
    int t = decodeHuff(j, &j->hdc[c->td]);
    int diff = t ? extend(getbits(j, t), t) : 0;
    c->pred += diff;
    uint16_t *q = j->qt[c->tq];
    coef[0] = c->pred * q[0];
    for (int k = 1; k < 64;) {
        int rs = decodeHuff(j, &j->hac[c->ta]);
        int r = rs >> 4, s = rs & 15;
        if (s == 0) {
            if (r == 15) { k += 16; continue; }
            break;
        }
        k += r;
        if (k > 63) break;
        coef[zigzag[k]] = extend(getbits(j, s), s) * q[k];
        k++;
    }
    idct(coef, dst, stride);
}

static int rd16(Jpg *j) { int v = (j->d[j->pos] << 8) | j->d[j->pos + 1]; j->pos += 2; return v; }

uint32_t *jpeg_decode(const uint8_t *data, int len, int *outW, int *outH) {
    if (len < 4 || data[0] != 0xff || data[1] != 0xd8) return NULL;
    Jpg *j = calloc(1, sizeof(Jpg));
    j->d = data; j->len = len; j->pos = 2;
    uint32_t *out = NULL;
    while (j->pos + 4 <= len) {
        if (j->d[j->pos] != 0xff) { j->pos++; continue; }
        int m = j->d[j->pos + 1];
        j->pos += 2;
        if (m == 0xd8 || (m >= 0xd0 && m <= 0xd7) || m == 0x01 || m == 0xff) continue;
        if (m == 0xd9) break;
        int segLen = rd16(j);
        int segEnd = j->pos + segLen - 2;
        if (segEnd > len) break;
        if (m == 0xdb) {
            while (j->pos < segEnd) {
                int pq = j->d[j->pos] >> 4, tq = j->d[j->pos] & 3;
                j->pos++;
                for (int i = 0; i < 64; i++) j->qt[tq][i] = pq ? rd16(j) : j->d[j->pos++];
            }
        } else if (m == 0xc4) {
            while (j->pos < segEnd) {
                int tc = j->d[j->pos] >> 4, th = j->d[j->pos] & 3;
                j->pos++;
                Huff *h = tc ? &j->hac[th] : &j->hdc[th];
                int total = 0;
                h->bits[0] = 0;
                for (int i = 1; i <= 16; i++) { h->bits[i] = j->d[j->pos++]; total += h->bits[i]; }
                if (total > 256) goto fail;
                memcpy(h->vals, j->d + j->pos, total);
                j->pos += total;
                buildHuff(h);
            }
        } else if (m == 0xc0 || m == 0xc1) {
            j->pos++; /* precision */
            j->h = rd16(j); j->w = rd16(j);
            j->ncomp = j->d[j->pos++];
            if (j->ncomp != 1 && j->ncomp != 3) goto fail;
            if (j->w <= 0 || j->h <= 0 || j->w > 4096 || j->h > 4096) goto fail;
            for (int i = 0; i < j->ncomp; i++) {
                Comp *c = &j->comp[i];
                c->id = j->d[j->pos]; c->h = j->d[j->pos + 1] >> 4; c->v = j->d[j->pos + 1] & 15; c->tq = j->d[j->pos + 2] & 3;
                if (c->h < 1 || c->h > 4 || c->v < 1 || c->v > 4) goto fail;
                j->pos += 3;
                if (c->h > j->hmax) j->hmax = c->h;
                if (c->v > j->vmax) j->vmax = c->v;
            }
        } else if (m == 0xc2 || m == 0xc3 || (m >= 0xc5 && m <= 0xcf && m != 0xc8 && m != 0xcc)) {
            j->progressive = 1;
            goto fail;
        } else if (m == 0xdd) {
            j->restart = rd16(j);
        } else if (m == 0xda) {
            if (!j->ncomp) goto fail;
            int ns = j->d[j->pos++];
            for (int i = 0; i < ns; i++) {
                int cid = j->d[j->pos], tbl = j->d[j->pos + 1];
                j->pos += 2;
                for (int k = 0; k < j->ncomp; k++) if (j->comp[k].id == cid) { j->comp[k].td = tbl >> 4 & 3; j->comp[k].ta = tbl & 3; }
            }
            j->pos += 3;
            int mcuW = 8 * j->hmax, mcuH = 8 * j->vmax;
            int mcusX = (j->w + mcuW - 1) / mcuW, mcusY = (j->h + mcuH - 1) / mcuH;
            for (int i = 0; i < j->ncomp; i++) {
                Comp *c = &j->comp[i];
                c->pw = mcusX * c->h * 8; c->ph = mcusY * c->v * 8;
                c->pix = malloc(c->pw * c->ph);
                c->pred = 0;
            }
            int single = (j->ncomp == 1);
            if (single) { /* non-interleaved: blocks cover the component directly */
                Comp *c = &j->comp[0];
                int bx = (j->w + 7) / 8, by = (j->h + 7) / 8, n = 0;
                for (int y = 0; y < by; y++)
                    for (int x = 0; x < bx; x++) {
                        if (j->restart && n && n % j->restart == 0) { j->bitcnt = 0; j->marker = 0; c->pred = 0; while (j->pos + 1 < len && !(j->d[j->pos] == 0xff && j->d[j->pos + 1] >= 0xd0 && j->d[j->pos + 1] <= 0xd7)) j->pos++; j->pos += 2; }
                        decodeBlock(j, c, c->pix + y * 8 * c->pw + x * 8, c->pw);
                        n++;
                    }
                j->hmax = j->vmax = c->h = c->v = 1;
            } else {
                int n = 0;
                for (int my = 0; my < mcusY; my++)
                    for (int mx = 0; mx < mcusX; mx++) {
                        if (j->restart && n && n % j->restart == 0) {
                            j->bitcnt = 0; j->marker = 0;
                            for (int i = 0; i < j->ncomp; i++) j->comp[i].pred = 0;
                            while (j->pos + 1 < len && !(j->d[j->pos] == 0xff && j->d[j->pos + 1] >= 0xd0 && j->d[j->pos + 1] <= 0xd7)) j->pos++;
                            j->pos += 2;
                        }
                        for (int i = 0; i < j->ncomp; i++) {
                            Comp *c = &j->comp[i];
                            for (int v = 0; v < c->v; v++)
                                for (int h = 0; h < c->h; h++) {
                                    int px = (mx * c->h + h) * 8, py = (my * c->v + v) * 8;
                                    decodeBlock(j, c, c->pix + py * c->pw + px, c->pw);
                                }
                        }
                        n++;
                    }
            }
            /* color convert */
            out = malloc((size_t)j->w * j->h * 4);
            for (int y = 0; y < j->h; y++)
                for (int x = 0; x < j->w; x++) {
                    int Y, cb = 128, cr = 128;
                    Comp *c0 = &j->comp[0];
                    Y = c0->pix[(y * c0->v / j->vmax) * c0->pw + (x * c0->h / j->hmax)];
                    if (j->ncomp == 3) {
                        Comp *c1 = &j->comp[1], *c2 = &j->comp[2];
                        cb = c1->pix[(y * c1->v / j->vmax) * c1->pw + (x * c1->h / j->hmax)];
                        cr = c2->pix[(y * c2->v / j->vmax) * c2->pw + (x * c2->h / j->hmax)];
                    }
                    int r = Y + ((91881 * (cr - 128)) >> 16);
                    int g = Y - ((22554 * (cb - 128) + 46802 * (cr - 128)) >> 16);
                    int b = Y + ((116130 * (cb - 128)) >> 16);
                    r = r < 0 ? 0 : r > 255 ? 255 : r;
                    g = g < 0 ? 0 : g > 255 ? 255 : g;
                    b = b < 0 ? 0 : b > 255 ? 255 : b;
                    out[y * j->w + x] = 0xff000000u | (r << 16) | (g << 8) | b;
                }
            *outW = j->w; *outH = j->h;
            break;
        }
        j->pos = segEnd;
    }
fail:
    for (int i = 0; i < 3; i++) free(j->comp[i].pix);
    free(j);
    return out;
}
