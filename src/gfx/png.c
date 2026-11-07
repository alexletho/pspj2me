/* PNG decoder -> ARGB8888. Supports all color types/bit depths, tRNS, Adam7. */
#include "gfx.h"
#include "../vm/zip.h"
#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static int unfilter(uint8_t *data, int rows, int rowBytes, int bpp) {
    uint8_t *prev = NULL;
    for (int y = 0; y < rows; y++) {
        uint8_t *line = data + y * (rowBytes + 1);
        int ft = line[0];
        uint8_t *cur = line + 1;
        switch (ft) {
            case 0: break;
            case 1: for (int i = bpp; i < rowBytes; i++) cur[i] += cur[i - bpp]; break;
            case 2: if (prev) for (int i = 0; i < rowBytes; i++) cur[i] += prev[i]; break;
            case 3:
                for (int i = 0; i < rowBytes; i++) {
                    int a = i >= bpp ? cur[i - bpp] : 0, b = prev ? prev[i] : 0;
                    cur[i] += (uint8_t)((a + b) >> 1);
                }
                break;
            case 4:
                for (int i = 0; i < rowBytes; i++) {
                    int a = i >= bpp ? cur[i - bpp] : 0, b = prev ? prev[i] : 0, c = (prev && i >= bpp) ? prev[i - bpp] : 0;
                    cur[i] += (uint8_t)paeth(a, b, c);
                }
                break;
            default: return -1;
        }
        prev = cur;
    }
    return 0;
}

typedef struct {
    int colorType, depth, channels;
    uint32_t palette[256];
    int hasTrns;
    uint16_t trnsGray, trnsR, trnsG, trnsB;
    int usesAlpha;
} PngInfo;

static uint32_t samplePixel(PngInfo *pi, const uint8_t *row, int x) {
    int d = pi->depth;
    uint32_t r, g, b, a = 255;
    if (pi->colorType == 3 || (pi->colorType == 0 && d < 8)) {
        int perByte = 8 / d;
        int v = (row[x / perByte] >> ((perByte - 1 - x % perByte) * d)) & ((1 << d) - 1);
        if (pi->colorType == 3) return pi->palette[v];
        int gray = v * 255 / ((1 << d) - 1);
        if (pi->hasTrns && v == pi->trnsGray) a = 0;
        return (a << 24) | (gray << 16) | (gray << 8) | gray;
    }
    int bps = d / 8; /* bytes per sample: 1 or 2 */
    const uint8_t *p = row + x * pi->channels * bps;
    #define S(i) (bps == 1 ? p[(i)] : p[(i) * 2])
    #define S16(i) (bps == 1 ? p[(i)] : ((p[(i) * 2] << 8) | p[(i) * 2 + 1]))
    switch (pi->colorType) {
        case 0:
            r = g = b = S(0);
            if (pi->hasTrns && S16(0) == pi->trnsGray) a = 0;
            break;
        case 2:
            r = S(0); g = S(1); b = S(2);
            if (pi->hasTrns && S16(0) == pi->trnsR && S16(1) == pi->trnsG && S16(2) == pi->trnsB) a = 0;
            break;
        case 4: r = g = b = S(0); a = S(1); break;
        case 6: r = S(0); g = S(1); b = S(2); a = S(3); break;
        default: r = g = b = 0;
    }
    #undef S
    #undef S16
    return (a << 24) | (r << 16) | (g << 8) | b;
}

uint32_t *png_decode(const uint8_t *data, int len, int *outW, int *outH, int *hasAlpha) {
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (len < 8 || memcmp(data, sig, 8) != 0) return NULL;
    int pos = 8;
    int w = 0, h = 0, interlace = 0;
    PngInfo pi;
    memset(&pi, 0, sizeof pi);
    uint8_t *idat = NULL;
    size_t idatLen = 0, idatCap = 0;
    int nPal = 0;
    uint8_t trnsPal[256];
    int nTrnsPal = 0;
    while (pos + 8 <= len) {
        uint32_t clen = be32(data + pos);
        const uint8_t *type = data + pos + 4;
        const uint8_t *cd = data + pos + 8;
        if (clen > (uint32_t)(len - pos - 8)) break;
        if (!memcmp(type, "IHDR", 4) && clen >= 13) {
            w = be32(cd); h = be32(cd + 4);
            pi.depth = cd[8]; pi.colorType = cd[9]; interlace = cd[12];
        } else if (!memcmp(type, "PLTE", 4)) {
            nPal = clen / 3;
            if (nPal > 256) nPal = 256;
            for (int i = 0; i < nPal; i++) pi.palette[i] = 0xff000000u | (cd[i * 3] << 16) | (cd[i * 3 + 1] << 8) | cd[i * 3 + 2];
        } else if (!memcmp(type, "tRNS", 4)) {
            pi.hasTrns = 1;
            if (pi.colorType == 3) { nTrnsPal = clen > 256 ? 256 : clen; memcpy(trnsPal, cd, nTrnsPal); }
            else if (pi.colorType == 0 && clen >= 2) pi.trnsGray = (cd[0] << 8) | cd[1];
            else if (pi.colorType == 2 && clen >= 6) { pi.trnsR = (cd[0] << 8) | cd[1]; pi.trnsG = (cd[2] << 8) | cd[3]; pi.trnsB = (cd[4] << 8) | cd[5]; }
        } else if (!memcmp(type, "IDAT", 4) && clen > 0) {
            if (idatLen + clen > idatCap) {
                size_t ncap = (idatLen + clen) * 2;
                uint8_t *n = realloc(idat, ncap);
                if (!n) { free(idat); return NULL; }   /* out of memory: give up cleanly */
                idat = n;
                idatCap = ncap;
            }
            memcpy(idat + idatLen, cd, clen);
            idatLen += clen;
        } else if (!memcmp(type, "IEND", 4)) break;
        pos += 12 + clen;
    }
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || !idat) { free(idat); return NULL; }
    /* only the bit depths the PNG spec allows for each color type */
    {
        int d = pi.depth, ok;
        switch (pi.colorType) {
            case 0: ok = d == 1 || d == 2 || d == 4 || d == 8 || d == 16; break;
            case 3: ok = d == 1 || d == 2 || d == 4 || d == 8; break;
            case 2: case 4: case 6: ok = d == 8 || d == 16; break;
            default: ok = 0;
        }
        if (!ok || interlace > 1) { free(idat); return NULL; }
    }
    switch (pi.colorType) {
        case 0: pi.channels = 1; break;
        case 2: pi.channels = 3; break;
        case 3: pi.channels = 1; break;
        case 4: pi.channels = 2; break;
        case 6: pi.channels = 4; break;
        default: free(idat); return NULL;
    }
    if (pi.colorType == 3) {
        for (int i = 0; i < nTrnsPal; i++) pi.palette[i] = (pi.palette[i] & 0xffffff) | ((uint32_t)trnsPal[i] << 24);
        for (int i = nPal; i < 256; i++) pi.palette[i] = 0xff000000u;
    }
    int bitsPP = pi.depth * pi.channels;
    int bpp = (bitsPP + 7) / 8;
    /* compute raw size */
    static const int ax[7] = { 0, 4, 0, 2, 0, 1, 0 }, ay[7] = { 0, 0, 4, 0, 2, 0, 1 };
    static const int dxs[7] = { 8, 8, 4, 4, 2, 2, 1 }, dys[7] = { 8, 8, 8, 4, 4, 2, 2 };
    size_t rawSize = 0;
    if (!interlace) rawSize = (size_t)h * (1 + ((size_t)w * bitsPP + 7) / 8);
    else {
        for (int p = 0; p < 7; p++) {
            int pw = (w - ax[p] + dxs[p] - 1) / dxs[p], ph = (h - ay[p] + dys[p] - 1) / dys[p];
            if (pw > 0 && ph > 0) rawSize += (size_t)ph * (1 + ((size_t)pw * bitsPP + 7) / 8);
        }
    }
    uint8_t *raw = malloc(rawSize + 1);
    uint32_t *out = malloc((size_t)w * h * 4);
    if (!raw || !out) { free(raw); free(out); free(idat); return NULL; }
    size_t got = 0;
    int r = inflate_zlib(idat, idatLen, raw, rawSize, &got);
    free(idat);
    if (r != 0 && got < rawSize) {
        /* tolerate truncated streams: zero the rest */
        memset(raw + got, 0, rawSize - got);
    }
    int alpha = 0;
    if (!interlace) {
        int rowBytes = (w * bitsPP + 7) / 8;
        unfilter(raw, h, rowBytes, bpp);
        for (int y = 0; y < h; y++) {
            const uint8_t *row = raw + y * (rowBytes + 1) + 1;
            for (int x = 0; x < w; x++) {
                uint32_t px = samplePixel(&pi, row, x);
                if ((px >> 24) != 255) alpha = 1;
                out[y * w + x] = px;
            }
        }
    } else {
        uint8_t *p = raw;
        for (int pass = 0; pass < 7; pass++) {
            int pw = (w - ax[pass] + dxs[pass] - 1) / dxs[pass], ph = (h - ay[pass] + dys[pass] - 1) / dys[pass];
            if (pw <= 0 || ph <= 0) continue;
            int rowBytes = (pw * bitsPP + 7) / 8;
            unfilter(p, ph, rowBytes, bpp);
            for (int y = 0; y < ph; y++) {
                const uint8_t *row = p + y * (rowBytes + 1) + 1;
                for (int x = 0; x < pw; x++) {
                    uint32_t px = samplePixel(&pi, row, x);
                    if ((px >> 24) != 255) alpha = 1;
                    out[(ay[pass] + y * dys[pass]) * w + ax[pass] + x * dxs[pass]] = px;
                }
            }
            p += (size_t)ph * (rowBytes + 1);
        }
    }
    free(raw);
    *outW = w; *outH = h;
    if (hasAlpha) *hasAlpha = alpha;
    return out;
}
