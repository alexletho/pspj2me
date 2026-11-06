/* Minimal ZIP reader + DEFLATE decoder (canonical-Huffman, puff-style). */
#include "zip.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------------- inflate ---------------- */
#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define FIXLCODES 288

typedef struct {
    const uint8_t *in; size_t inLen, inPos;
    uint8_t *out; size_t outLen, outPos;
    uint32_t bitbuf; int bitcnt;
    int err;
} IState;

typedef struct { short count[MAXBITS + 1]; short symbol[FIXLCODES]; } Huff;

static int getbits(IState *s, int need) {
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->inPos >= s->inLen) { s->err = 1; return 0; }
        val |= (uint32_t)s->in[s->inPos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}

static int stored(IState *s) {
    s->bitbuf = 0; s->bitcnt = 0;
    if (s->inPos + 4 > s->inLen) return -1;
    unsigned len = s->in[s->inPos] | (s->in[s->inPos + 1] << 8);
    unsigned nlen = s->in[s->inPos + 2] | (s->in[s->inPos + 3] << 8);
    s->inPos += 4;
    if (len != (~nlen & 0xffff)) return -2;
    if (s->inPos + len > s->inLen) return -1;
    if (s->outPos + len > s->outLen) return -3;
    memcpy(s->out + s->outPos, s->in + s->inPos, len);
    s->outPos += len; s->inPos += len;
    return 0;
}

static int decode(IState *s, const Huff *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAXBITS; len++) {
        code |= getbits(s, 1);
        if (s->err) return -10;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
    }
    return -10;
}

static int construct(Huff *h, const short *length, int n) {
    short offs[MAXBITS + 1];
    for (int len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) {
        left <<= 1; left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++) offs[len + 1] = offs[len] + h->count[len];
    for (int sym = 0; sym < n; sym++) if (length[sym] != 0) h->symbol[offs[length[sym]]++] = sym;
    return left;
}

static const short lbase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const short lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const short dbase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const short dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static int codes(IState *s, const Huff *lencode, const Huff *distcode) {
    int symbol;
    do {
        symbol = decode(s, lencode);
        if (symbol < 0) return symbol;
        if (symbol < 256) {
            if (s->outPos >= s->outLen) return -3;
            s->out[s->outPos++] = (uint8_t)symbol;
        } else if (symbol > 256) {
            symbol -= 257;
            if (symbol >= 29) return -10;
            int len = lbase[symbol] + getbits(s, lext[symbol]);
            symbol = decode(s, distcode);
            if (symbol < 0) return symbol;
            if (symbol >= 30) return -10;
            unsigned dist = dbase[symbol] + getbits(s, dext[symbol]);
            if (s->err) return -10;
            if (dist > s->outPos) return -11;
            if (s->outPos + len > s->outLen) return -3;
            uint8_t *o = s->out + s->outPos;
            const uint8_t *from = o - dist;
            for (int i = 0; i < len; i++) o[i] = from[i];
            s->outPos += len;
        }
    } while (symbol != 256);
    return 0;
}

static Huff fixedLen, fixedDist;
static int fixedBuilt = 0;

static int fixed(IState *s) {
    if (!fixedBuilt) {
        short lengths[FIXLCODES];
        int sym;
        for (sym = 0; sym < 144; sym++) lengths[sym] = 8;
        for (; sym < 256; sym++) lengths[sym] = 9;
        for (; sym < 280; sym++) lengths[sym] = 7;
        for (; sym < FIXLCODES; sym++) lengths[sym] = 8;
        construct(&fixedLen, lengths, FIXLCODES);
        for (sym = 0; sym < MAXDCODES; sym++) lengths[sym] = 5;
        construct(&fixedDist, lengths, MAXDCODES);
        fixedBuilt = 1;
    }
    return codes(s, &fixedLen, &fixedDist);
}

static int dynamic(IState *s) {
    short lengths[MAXLCODES + MAXDCODES];
    Huff lencode, distcode;
    static const short order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    int nlen = getbits(s, 5) + 257;
    int ndist = getbits(s, 5) + 1;
    int ncode = getbits(s, 4) + 4;
    if (s->err) return -10;
    if (nlen > MAXLCODES || ndist > MAXDCODES) return -3;
    int index;
    for (index = 0; index < ncode; index++) lengths[order[index]] = getbits(s, 3);
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (s->err) return -10;
    if (construct(&lencode, lengths, 19) != 0) return -4;
    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(s, &lencode);
        if (symbol < 0) return symbol;
        if (symbol < 16) lengths[index++] = symbol;
        else {
            int len = 0;
            if (symbol == 16) {
                if (index == 0) return -5;
                len = lengths[index - 1];
                symbol = 3 + getbits(s, 2);
            } else if (symbol == 17) symbol = 3 + getbits(s, 3);
            else symbol = 11 + getbits(s, 7);
            if (s->err) return -10;
            if (index + symbol > nlen + ndist) return -6;
            while (symbol--) lengths[index++] = len;
        }
    }
    if (lengths[256] == 0) return -9;
    int err = construct(&lencode, lengths, nlen);
    if (err && (err < 0 || nlen != lencode.count[0] + lencode.count[1])) return -7;
    err = construct(&distcode, lengths + nlen, ndist);
    if (err && (err < 0 || ndist != distcode.count[0] + distcode.count[1])) return -8;
    return codes(s, &lencode, &distcode);
}

int inflate_raw(const uint8_t *src, size_t srcLen, uint8_t *dst, size_t dstLen, size_t *outLen) {
    IState s;
    memset(&s, 0, sizeof s);
    s.in = src; s.inLen = srcLen; s.out = dst; s.outLen = dstLen;
    int last, err;
    do {
        last = getbits(&s, 1);
        int type = getbits(&s, 2);
        if (s.err) { err = -10; break; }
        if (type == 0) err = stored(&s);
        else if (type == 1) err = fixed(&s);
        else if (type == 2) err = dynamic(&s);
        else err = -1;
        if (err) break;
    } while (!last);
    if (outLen) *outLen = s.outPos;
    return err;
}

int inflate_zlib(const uint8_t *src, size_t srcLen, uint8_t *dst, size_t dstLen, size_t *outLen) {
    if (srcLen < 2) return -1;
    if ((src[0] & 0x0f) != 8) return -1;
    if (src[1] & 0x20) return -1; /* preset dict not supported */
    return inflate_raw(src + 2, srcLen - 2, dst, dstLen, outLen);
}

/* ---------------- zip ---------------- */
static uint32_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static unsigned hashName(const char *s) {
    unsigned h = 5381;
    while (*s) h = h * 33 + (unsigned char)*s++;
    return h;
}

Zip *zip_open_mem(uint8_t *data, size_t len, int own) {
    if (len < 22) return NULL;
    /* locate end of central directory */
    long eocd = -1;
    long minPos = (long)len - 22 - 65535;
    if (minPos < 0) minPos = 0;
    for (long i = (long)len - 22; i >= minPos; i--) {
        if (rd32(data + i) == 0x06054b50) { eocd = i; break; }
    }
    if (eocd < 0) return NULL;
    uint32_t count = rd16(data + eocd + 10);
    uint32_t cdOff = rd32(data + eocd + 16);
    if (cdOff >= len) return NULL;
    Zip *z = calloc(1, sizeof(Zip));
    z->data = data; z->len = len; z->ownsData = own;
    z->entries = calloc(count ? count : 1, sizeof(ZipEntry));
    z->nBuckets = 1;
    while (z->nBuckets < (int)count * 2) z->nBuckets <<= 1;
    z->buckets = calloc(z->nBuckets, sizeof(ZipEntry *));
    size_t p = cdOff;
    int n = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (p + 46 > len || rd32(data + p) != 0x02014b50) break;
        ZipEntry *e = &z->entries[n];
        e->method = rd16(data + p + 10);
        e->compSize = rd32(data + p + 20);
        e->size = rd32(data + p + 24);
        uint32_t nameLen = rd16(data + p + 28);
        uint32_t extraLen = rd16(data + p + 30);
        uint32_t commentLen = rd16(data + p + 32);
        e->localOffset = rd32(data + p + 42);
        if (p + 46 + nameLen > len) break;
        e->name = malloc(nameLen + 1);
        memcpy(e->name, data + p + 46, nameLen);
        e->name[nameLen] = 0;
        unsigned h = hashName(e->name) & (z->nBuckets - 1);
        e->next = z->buckets[h];
        z->buckets[h] = e;
        n++;
        p += 46 + nameLen + extraLen + commentLen;
    }
    z->nEntries = n;
    return z;
}

Zip *zip_open_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return NULL; }
    uint8_t *buf = malloc(len);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, len, f) != (size_t)len) { free(buf); fclose(f); return NULL; }
    fclose(f);
    Zip *z = zip_open_mem(buf, len, 1);
    if (!z) free(buf);
    return z;
}

void zip_close(Zip *z) {
    if (!z) return;
    for (int i = 0; i < z->nEntries; i++) free(z->entries[i].name);
    free(z->entries);
    free(z->buckets);
    if (z->ownsData) free(z->data);
    free(z);
}

ZipEntry *zip_find(Zip *z, const char *name) {
    if (!z) return NULL;
    while (*name == '/') name++;
    unsigned h = hashName(name) & (z->nBuckets - 1);
    for (ZipEntry *e = z->buckets[h]; e; e = e->next)
        if (strcmp(e->name, name) == 0) return e;
    return NULL;
}

uint8_t *zip_read(Zip *z, ZipEntry *e, uint32_t *outLen) {
    if (e->localOffset + 30 > z->len) return NULL;
    const uint8_t *lh = z->data + e->localOffset;
    if (rd32(lh) != 0x04034b50) return NULL;
    size_t dataOff = e->localOffset + 30 + rd16(lh + 26) + rd16(lh + 28);
    if (dataOff + e->compSize > z->len) return NULL;
    uint8_t *out = malloc(e->size + 1);
    if (!out) return NULL;
    if (e->method == 0) {
        memcpy(out, z->data + dataOff, e->size);
    } else if (e->method == 8) {
        size_t got = 0;
        int r = inflate_raw(z->data + dataOff, e->compSize, out, e->size, &got);
        if (r != 0 || got != e->size) { free(out); return NULL; }
    } else {
        free(out);
        return NULL;
    }
    out[e->size] = 0;
    if (outLen) *outLen = e->size;
    return out;
}
