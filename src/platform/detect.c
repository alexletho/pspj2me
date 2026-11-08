/* Game build detection: screen size and phone key-code profile (see detect.h). */
#include "detect.h"
#include "../vm/zip.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char *const kp_names[KP_COUNT] = { "Auto", "Nokia / Sony Ericsson / Samsung", "Siemens", "Motorola", "LG" };

/* screen sizes of keypad phones */
static const int phoneSizes[][2] = { { 128, 128 }, { 128, 160 }, { 132, 176 }, { 176, 208 }, { 176, 220 }, { 208, 208 },
                                     { 240, 320 }, { 320, 240 }, { 240, 400 }, { 352, 416 }, { 240, 260 }, { 220, 176 } };
#define NPHONESIZES ((int)(sizeof phoneSizes / sizeof phoneSizes[0]))

static int plausible(int w, int h) { return w >= 96 && h >= 96 && w <= 640 && h <= 640; }

/* popular phones (file names often carry the model a build was made for) */
static const struct { const char *model; int w, h, keys; } phones[] = {
    /* Nokia Series 40 / 60 */
    { "n73", 240, 320, KP_NOKIA }, { "n95", 240, 320, KP_NOKIA }, { "n82", 240, 320, KP_NOKIA }, { "n78", 240, 320, KP_NOKIA },
    { "n79", 240, 320, KP_NOKIA }, { "n85", 240, 320, KP_NOKIA }, { "n96", 240, 320, KP_NOKIA }, { "n81", 240, 320, KP_NOKIA },
    { "n77", 240, 320, KP_NOKIA }, { "6300", 240, 320, KP_NOKIA }, { "5310", 240, 320, KP_NOKIA }, { "6500", 240, 320, KP_NOKIA },
    { "6120", 240, 320, KP_NOKIA }, { "6210", 240, 320, KP_NOKIA }, { "6220", 240, 320, KP_NOKIA }, { "5610", 240, 320, KP_NOKIA },
    { "6700", 240, 320, KP_NOKIA }, { "e51", 240, 320, KP_NOKIA }, { "e71", 320, 240, KP_NOKIA }, { "e61", 320, 240, KP_NOKIA },
    { "e63", 320, 240, KP_NOKIA }, { "e72", 320, 240, KP_NOKIA }, { "n70", 176, 208, KP_NOKIA }, { "n72", 176, 208, KP_NOKIA },
    { "6600", 176, 208, KP_NOKIA }, { "6630", 176, 208, KP_NOKIA }, { "6680", 176, 208, KP_NOKIA }, { "6681", 176, 208, KP_NOKIA },
    { "7610", 176, 208, KP_NOKIA }, { "3230", 176, 208, KP_NOKIA }, { "6670", 176, 208, KP_NOKIA }, { "3250", 176, 208, KP_NOKIA },
    { "6280", 240, 320, KP_NOKIA }, { "6288", 240, 320, KP_NOKIA }, { "6233", 240, 320, KP_NOKIA }, { "6234", 240, 320, KP_NOKIA },
    { "6131", 240, 320, KP_NOKIA }, { "6086", 240, 320, KP_NOKIA }, { "5300", 240, 320, KP_NOKIA }, { "7373", 240, 320, KP_NOKIA },
    { "6230i", 208, 208, KP_NOKIA }, { "6230", 128, 128, KP_NOKIA }, { "6020", 128, 128, KP_NOKIA }, { "6021", 128, 128, KP_NOKIA },
    { "3100", 128, 128, KP_NOKIA }, { "7210", 128, 128, KP_NOKIA }, { "6060", 128, 160, KP_NOKIA }, { "6070", 128, 160, KP_NOKIA },
    { "2600c", 128, 160, KP_NOKIA }, { "2630", 128, 160, KP_NOKIA }, { "6111", 128, 160, KP_NOKIA }, { "7360", 128, 160, KP_NOKIA },
    { "6101", 128, 160, KP_NOKIA }, { "6103", 128, 160, KP_NOKIA }, { "6125", 128, 160, KP_NOKIA }, { "7260", 128, 128, KP_NOKIA },
    /* Sony Ericsson */
    { "k800", 240, 320, KP_NOKIA }, { "k800i", 240, 320, KP_NOKIA }, { "k810", 240, 320, KP_NOKIA }, { "k850", 240, 320, KP_NOKIA },
    { "w910", 240, 320, KP_NOKIA }, { "c905", 240, 320, KP_NOKIA }, { "c902", 240, 320, KP_NOKIA }, { "w880", 240, 320, KP_NOKIA },
    { "k790", 240, 320, KP_NOKIA }, { "w950", 240, 320, KP_NOKIA }, { "k660", 240, 320, KP_NOKIA }, { "w595", 240, 320, KP_NOKIA },
    { "k750", 176, 220, KP_NOKIA }, { "k750i", 176, 220, KP_NOKIA }, { "w800", 176, 220, KP_NOKIA }, { "w810", 176, 220, KP_NOKIA },
    { "k550", 176, 220, KP_NOKIA }, { "w580", 176, 220, KP_NOKIA }, { "w550", 176, 220, KP_NOKIA }, { "k610", 176, 220, KP_NOKIA },
    { "k510", 128, 160, KP_NOKIA }, { "k300", 128, 128, KP_NOKIA }, { "k700", 176, 220, KP_NOKIA }, { "s700", 240, 320, KP_NOKIA },
    { "z520", 128, 160, KP_NOKIA }, { "w200", 128, 160, KP_NOKIA }, { "k310", 128, 160, KP_NOKIA }, { "t610", 128, 160, KP_NOKIA },
    /* Samsung (Nokia-style key codes) */
    { "d900", 240, 320, KP_NOKIA }, { "e250", 176, 220, KP_NOKIA }, { "d600", 240, 320, KP_NOKIA }, { "x680", 128, 160, KP_NOKIA },
    { "e900", 240, 320, KP_NOKIA }, { "j700", 176, 220, KP_NOKIA }, { "e370", 176, 220, KP_NOKIA }, { "c3050", 176, 220, KP_NOKIA },
    /* Siemens */
    { "s65", 132, 176, KP_SIEMENS }, { "c65", 132, 176, KP_SIEMENS }, { "cx65", 132, 176, KP_SIEMENS }, { "m65", 132, 176, KP_SIEMENS },
    { "sk65", 132, 176, KP_SIEMENS }, { "c72", 132, 176, KP_SIEMENS }, { "c75", 132, 176, KP_SIEMENS }, { "cx75", 132, 176, KP_SIEMENS },
    { "me75", 132, 176, KP_SIEMENS }, { "m75", 132, 176, KP_SIEMENS }, { "s75", 132, 176, KP_SIEMENS }, { "sl65", 132, 176, KP_SIEMENS },
    { "sl75", 132, 176, KP_SIEMENS }, { "el71", 240, 320, KP_SIEMENS }, { "c81", 132, 176, KP_SIEMENS }, { "s68", 240, 320, KP_SIEMENS },
    { "c60", 101, 80, KP_SIEMENS }, { "cf62", 130, 130, KP_SIEMENS },
    /* Motorola */
    { "razr", 176, 220, KP_MOTOROLA }, { "v3i", 176, 220, KP_MOTOROLA }, { "v3x", 176, 220, KP_MOTOROLA },
    { "e398", 176, 220, KP_MOTOROLA }, { "l6", 128, 160, KP_MOTOROLA }, { "l7", 176, 220, KP_MOTOROLA }, 
    { "v600", 176, 220, KP_MOTOROLA }, { "v635", 176, 220, KP_MOTOROLA }, { "rokr", 176, 220, KP_MOTOROLA },
    { "slvr", 176, 220, KP_MOTOROLA }, { "z3", 240, 320, KP_MOTOROLA }, { "krzr", 176, 220, KP_MOTOROLA }, 
    { "e770", 176, 220, KP_MOTOROLA }, { "e1000", 176, 220, KP_MOTOROLA }, { "c650", 128, 128, KP_MOTOROLA }, { "v220", 128, 160, KP_MOTOROLA },
    /* LG */
    { "kg800", 128, 160, KP_LG }, { "ku800", 240, 320, KP_LG }, { "ku990", 240, 400, KP_LG }, { "kg320", 176, 220, KP_LG },
    { "ku380", 176, 220, KP_LG }, { "kp100", 128, 160, KP_LG }, { "kg290", 176, 220, KP_LG },
};
#define NPHONES ((int)(sizeof phones / sizeof phones[0]))

/* maker names / abbreviations used in file names */
static const struct { const char *token; int keys; } makers[] = {
    { "siemens", KP_SIEMENS }, { "sie", KP_SIEMENS }, { "motorola", KP_MOTOROLA }, { "moto", KP_MOTOROLA }, { "mot", KP_MOTOROLA },
    { "lg", KP_LG }, { "lge", KP_LG }, { "nokia", KP_NOKIA }, { "sonyericsson", KP_NOKIA }, { "se", KP_NOKIA },
    { "samsung", KP_NOKIA }, { "sam", KP_NOKIA }, { "s40", KP_NOKIA }, { "s60", KP_NOKIA },
};

/* splits a file name (without directory and extension) into lowercase alphanumeric tokens */
static int tokens(const char *name, char tok[][24], int max) {
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    int n = 0, len = 0;
    const char *end = strrchr(base, '.');
    if (!end) end = base + strlen(base);
    for (const char *p = base; p <= end && n < max; p++) {
        int c = p < end ? (unsigned char)*p : 0;
        if (c && isalnum(c) && len < 23) { tok[n][len++] = (char)tolower(c); continue; }
        if (len) { tok[n][len] = 0; n++; len = 0; }
    }
    return n;
}

int detect_size_from_value(const char *v, int *w, int *h) {
    int a = 0, b = 0;
    while (*v && !isdigit((unsigned char)*v)) v++;
    if (sscanf(v, "%d%*[ ,xX*_]%d", &a, &b) == 2 && plausible(a, b)) { *w = a; *h = b; return 1; }
    return 0;
}

int detect_size_from_name(const char *name, int *w, int *h, char *from, int fromLen) {
    /* "240x320", "240X320", "240*320", "240_320" anywhere in the name */
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    for (const char *p = base; *p; p++) {
        if (!isdigit((unsigned char)*p) || (p > base && isdigit((unsigned char)p[-1]))) continue;
        int a = 0, b = 0, n = 0;
        if (sscanf(p, "%3d%*1[xX*_]%3d%n", &a, &b, &n) == 2 && n >= 5 && plausible(a, b)) {
            *w = a; *h = b;
            snprintf(from, fromLen, "file name");
            return 1;
        }
    }
    char tok[32][24];
    int nt = tokens(name, tok, 32);
    for (int i = 0; i < nt; i++)
        for (int k = 0; k < NPHONES; k++)
            if (!strcmp(tok[i], phones[k].model) && plausible(phones[k].w, phones[k].h)) {
                *w = phones[k].w; *h = phones[k].h;
                snprintf(from, fromLen, "phone model %s in the file name", phones[k].model);
                return 1;
            }
    return 0;
}

int detect_keys_from_name(const char *name, char *from, int fromLen) {
    char tok[32][24];
    int nt = tokens(name, tok, 32);
    for (int i = 0; i < nt; i++) {
        for (int k = 0; k < (int)(sizeof makers / sizeof makers[0]); k++)
            if (!strcmp(tok[i], makers[k].token)) { snprintf(from, fromLen, "\"%s\" in the file name", tok[i]); return makers[k].keys; }
        for (int k = 0; k < NPHONES; k++)
            if (!strcmp(tok[i], phones[k].model)) { snprintf(from, fromLen, "phone model %s in the file name", phones[k].model); return phones[k].keys; }
    }
    return KP_AUTO;
}

/* value of `key` in a manifest/JAD text ("Key: value" lines); NULL if absent */
static int propValue(const char *text, const char *key, char *out, int outLen) {
    size_t kl = strlen(key);
    for (const char *p = text; p && *p;) {
        if (!strncasecmp(p, key, kl) && p[kl] == ':') {
            const char *v = p + kl + 1;
            while (*v == ' ' || *v == '\t') v++;
            int n = 0;
            while (v[n] && v[n] != '\r' && v[n] != '\n' && n < outLen - 1) { out[n] = v[n]; n++; }
            out[n] = 0;
            return 1;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
    return 0;
}

/* screen size declared by the manifest or JAD */
static int sizeFromProps(const char *text, int *w, int *h) {
    static const char *keys[] = { "Nokia-MIDlet-Original-Display-Size", "Nokia-MIDlet-Target-Display-Size",
                                  "MIDlet-Display-Size", "MIDlet-Screen-Size", "Display-Size", "ScreenSize" };
    char v[64];
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
        if (propValue(text, keys[i], v, sizeof v) && detect_size_from_value(v, w, h)) return 1;
    char a[32], b[32];
    if (propValue(text, "MIDlet-Screen-Width", a, sizeof a) && propValue(text, "MIDlet-Screen-Height", b, sizeof b)) {
        int x = atoi(a), y = atoi(b);
        if (plausible(x, y)) { *w = x; *h = y; return 1; }
    }
    return 0;
}

static char *readText(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 65536) { fclose(f); return NULL; }
    char *b = malloc(n + 1);
    if (b && fread(b, 1, n, f) != (size_t)n) { free(b); b = NULL; }
    if (b) b[n] = 0;
    fclose(f);
    return b;
}

static int memfind(const uint8_t *h, size_t hl, const char *needle) {
    size_t nl = strlen(needle);
    if (hl < nl) return 0;
    for (size_t i = 0; i + nl <= hl; i++) if (h[i] == (uint8_t)needle[0] && !memcmp(h + i, needle, nl)) return 1;
    return 0;
}

void detect_game(const char *jarPath, GameDetect *out) {
    memset(out, 0, sizeof *out);
    out->keys = KP_NOKIA;
    snprintf(out->keysFrom, sizeof out->keysFrom, "default");
    int keys = detect_keys_from_name(jarPath, out->keysFrom, sizeof out->keysFrom);
    int haveSize = detect_size_from_name(jarPath, &out->w, &out->h, out->sizeFrom, sizeof out->sizeFrom);

    /* the JAD next to the jar, then the manifest */
    char jad[600];
    snprintf(jad, sizeof jad, "%s", jarPath);
    char *dot = strrchr(jad, '.');
    if (dot && !haveSize) {
        strcpy(dot, ".jad");
        char *t = readText(jad);
        if (!t) { strcpy(dot, ".JAD"); t = readText(jad); }
        if (t && sizeFromProps(t, &out->w, &out->h)) { haveSize = 1; snprintf(out->sizeFrom, sizeof out->sizeFrom, "JAD file"); }
        free(t);
    }
    Zip *z = zip_open_file_lazy(jarPath);
    if (!z) { if (keys) out->keys = keys; return; }
    if (!haveSize) {
        ZipEntry *e = zip_find(z, "META-INF/MANIFEST.MF");
        uint32_t len;
        char *mf = e ? (char *)zip_read(z, e, &len) : NULL;
        if (mf && sizeFromProps(mf, &out->w, &out->h)) { haveSize = 1; snprintf(out->sizeFrom, sizeof out->sizeFrom, "manifest"); }
        free(mf);
    }
    /* images exactly the size of a phone screen (splash/menu backgrounds) */
    int votes[NPHONESIZES] = { 0 };
    int vendor[KP_COUNT] = { 0 }, nokiaRefs = 0;
    size_t scanned = 0;
    for (int i = 0; i < z->nEntries; i++) {
        ZipEntry *e = &z->entries[i];
        size_t nl = strlen(e->name);
        int isPng = nl > 4 && !strcasecmp(e->name + nl - 4, ".png");
        int isClass = nl > 6 && !strcmp(e->name + nl - 6, ".class");
        if (isPng && !haveSize && e->size >= 24 && e->size < 512 * 1024) {
            uint32_t l;
            uint8_t *d = zip_read(z, e, &l);
            if (d && l >= 24 && !memcmp(d + 12, "IHDR", 4)) {
                int w = (d[16] << 24) | (d[17] << 16) | (d[18] << 8) | d[19], h = (d[20] << 24) | (d[21] << 16) | (d[22] << 8) | d[23];
                for (int k = 0; k < NPHONESIZES; k++) if (phoneSizes[k][0] == w && phoneSizes[k][1] == h) votes[k]++;
            }
            free(d);
        } else if (isClass && !keys && scanned < 4 * 1024 * 1024 && e->size < 512 * 1024) {
            /* phone maker APIs the game's code refers to */
            uint32_t l;
            uint8_t *d = zip_read(z, e, &l);
            if (d) {
                scanned += l;
                if (memfind(d, l, "com/siemens/mp/")) vendor[KP_SIEMENS]++;
                if (memfind(d, l, "com/motorola/")) vendor[KP_MOTOROLA]++;
                if (memfind(d, l, "mmpp/") || memfind(d, l, "com/lge/")) vendor[KP_LG]++;
                if (memfind(d, l, "com/nokia/") || memfind(d, l, "com/samsung/") || memfind(d, l, "com/sonyericsson/")) nokiaRefs++;
            }
            free(d);
        }
    }
    zip_close(z);
    if (!haveSize) {
        int best = -1;
        for (int k = 0; k < NPHONESIZES; k++)
            if (votes[k] && (best < 0 || votes[k] > votes[best] ||
                             (votes[k] == votes[best] && phoneSizes[k][0] * phoneSizes[k][1] > phoneSizes[best][0] * phoneSizes[best][1]))) best = k;
        if (best >= 0) {
            out->w = phoneSizes[best][0];
            out->h = phoneSizes[best][1];
            snprintf(out->sizeFrom, sizeof out->sizeFrom, "the game's full-screen images");
        }
    }
    if (keys) out->keys = keys;
    else if (!nokiaRefs) {
        /* only when the game refers to exactly one other maker (multi-phone builds pick at run time) */
        int found = 0, which = 0;
        for (int k = KP_SIEMENS; k < KP_COUNT; k++) if (vendor[k]) { found++; which = k; }
        if (found == 1) { out->keys = which; snprintf(out->keysFrom, sizeof out->keysFrom, "the game uses %s phone APIs", kp_names[which]); }
    }
}
