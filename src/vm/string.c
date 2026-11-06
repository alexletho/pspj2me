#include "vm.h"
#include <stdlib.h>
#include <stdio.h>

uint32_t g_strValueOff, g_strOffsetOff, g_strCountOff;
ref_t *g_internTable;
int g_internCap;
static int internCount;
static ref_t tmpRoot1, tmpRoot2;
static int rootsAdded;

static void ensureRoots(void) {
    if (!rootsAdded) { gc_add_root(&tmpRoot1); gc_add_root(&tmpRoot2); rootsAdded = 1; }
}

/* decode (modified) UTF-8 into UTF-16; returns count. out may be NULL to count. */
static int decodeUtf8(const uint8_t *s, int len, uint16_t *out) {
    int n = 0, i = 0;
    while (i < len) {
        uint8_t c = s[i];
        uint32_t ch;
        if (c < 0x80) { ch = c; i++; }
        else if ((c & 0xe0) == 0xc0 && i + 1 < len && (s[i + 1] & 0xc0) == 0x80) {
            ch = ((c & 0x1f) << 6) | (s[i + 1] & 0x3f); i += 2;
        } else if ((c & 0xf0) == 0xe0 && i + 2 < len && (s[i + 1] & 0xc0) == 0x80 && (s[i + 2] & 0xc0) == 0x80) {
            ch = ((c & 0x0f) << 12) | ((s[i + 1] & 0x3f) << 6) | (s[i + 2] & 0x3f); i += 3;
        } else if ((c & 0xf8) == 0xf0 && i + 3 < len) {
            ch = ((c & 0x07) << 18) | ((s[i + 1] & 0x3f) << 12) | ((s[i + 2] & 0x3f) << 6) | (s[i + 3] & 0x3f);
            i += 4;
            ch -= 0x10000;
            if (out) { out[n] = 0xd800 | (ch >> 10); out[n + 1] = 0xdc00 | (ch & 0x3ff); }
            n += 2;
            continue;
        } else { ch = c; i++; } /* invalid: latin-1 fallback */
        if (out) out[n] = (uint16_t)ch;
        n++;
    }
    return n;
}

ref_t new_string_chars(const uint16_t *chars, int len) {
    ensureRoots();
    ref_t arr = new_prim_array('C', len);
    if (!arr) return 0;
    if (len) memcpy(A_CHAR(arr), chars, len * 2);
    tmpRoot1 = arr;
    ref_t s = new_object(C_String);
    tmpRoot1 = 0;
    if (!s) return 0;
    F_REF(s, g_strValueOff) = arr;
    F_INT(s, g_strOffsetOff) = 0;
    F_INT(s, g_strCountOff) = len;
    return s;
}

ref_t new_string_utf8(const char *str) {
    ensureRoots();
    int len = (int)strlen(str);
    int n = decodeUtf8((const uint8_t *)str, len, NULL);
    ref_t arr = new_prim_array('C', n);
    if (!arr) return 0;
    decodeUtf8((const uint8_t *)str, len, A_CHAR(arr));
    tmpRoot1 = arr;
    ref_t s = new_object(C_String);
    tmpRoot1 = 0;
    if (!s) return 0;
    F_REF(s, g_strValueOff) = arr;
    F_INT(s, g_strOffsetOff) = 0;
    F_INT(s, g_strCountOff) = n;
    return s;
}

uint16_t *string_chars(ref_t s) {
    ref_t v = F_REF(s, g_strValueOff);
    return A_CHAR(v) + F_INT(s, g_strOffsetOff);
}

int string_length(ref_t s) { return F_INT(s, g_strCountOff); }

int string_to_utf8(ref_t s, char *buf, int bufLen) {
    if (!s) { if (bufLen) buf[0] = 0; return 0; }
    uint16_t *c = string_chars(s);
    int n = string_length(s), o = 0;
    for (int i = 0; i < n; i++) {
        uint32_t ch = c[i];
        if (ch >= 0xd800 && ch < 0xdc00 && i + 1 < n && c[i + 1] >= 0xdc00 && c[i + 1] < 0xe000) {
            ch = 0x10000 + ((ch - 0xd800) << 10) + (c[i + 1] - 0xdc00);
            i++;
        }
        char tmp[4]; int k;
        if (ch < 0x80) { tmp[0] = (char)ch; k = 1; }
        else if (ch < 0x800) { tmp[0] = 0xc0 | (ch >> 6); tmp[1] = 0x80 | (ch & 0x3f); k = 2; }
        else if (ch < 0x10000) { tmp[0] = 0xe0 | (ch >> 12); tmp[1] = 0x80 | ((ch >> 6) & 0x3f); tmp[2] = 0x80 | (ch & 0x3f); k = 3; }
        else { tmp[0] = 0xf0 | (ch >> 18); tmp[1] = 0x80 | ((ch >> 12) & 0x3f); tmp[2] = 0x80 | ((ch >> 6) & 0x3f); tmp[3] = 0x80 | (ch & 0x3f); k = 4; }
        if (o + k >= bufLen) break;
        memcpy(buf + o, tmp, k);
        o += k;
    }
    if (bufLen) buf[o] = 0;
    return o;
}

char *string_to_cstr(ref_t s) {
    int n = s ? string_length(s) : 0;
    int cap = n * 4 + 1;
    char *b = malloc(cap);
    string_to_utf8(s, b, cap);
    return b;
}

int string_equals_c(ref_t s, const char *c) {
    char buf[512];
    string_to_utf8(s, buf, sizeof buf);
    return strcmp(buf, c) == 0;
}

static uint32_t hashChars(const uint16_t *c, int n) {
    uint32_t h = 0;
    for (int i = 0; i < n; i++) h = 31 * h + c[i];
    return h;
}

static void internInsert(ref_t s) {
    uint32_t h = hashChars(string_chars(s), string_length(s));
    int mask = g_internCap - 1;
    int i = h & mask;
    while (g_internTable[i]) i = (i + 1) & mask;
    g_internTable[i] = s;
}

static void internGrow(void) {
    int oldCap = g_internCap;
    ref_t *old = g_internTable;
    g_internCap = oldCap ? oldCap * 2 : 1024;
    g_internTable = calloc(g_internCap, sizeof(ref_t));
    for (int i = 0; i < oldCap; i++) if (old[i]) internInsert(old[i]);
    free(old);
}

static ref_t internLookup(const uint16_t *c, int n) {
    if (!g_internCap) return 0;
    uint32_t h = hashChars(c, n);
    int mask = g_internCap - 1;
    int i = h & mask;
    while (g_internTable[i]) {
        ref_t s = g_internTable[i];
        if (string_length(s) == n && memcmp(string_chars(s), c, n * 2) == 0) return s;
        i = (i + 1) & mask;
    }
    return 0;
}

ref_t intern_string(ref_t s) {
    ref_t e = internLookup(string_chars(s), string_length(s));
    if (e) return e;
    if ((internCount + 1) * 2 > g_internCap) {
        ensureRoots();
        tmpRoot2 = s;
        internGrow();
        tmpRoot2 = 0;
    }
    internInsert(s);
    internCount++;
    return s;
}

ref_t intern_utf8(const char *mutf) {
    int len = (int)strlen(mutf);
    uint16_t stackBuf[256];
    int n = decodeUtf8((const uint8_t *)mutf, len, NULL);
    uint16_t *buf = n <= 256 ? stackBuf : malloc(n * 2);
    decodeUtf8((const uint8_t *)mutf, len, buf);
    ref_t s = internLookup(buf, n);
    if (!s) {
        s = new_string_chars(buf, n);
        if (s) s = intern_string(s);
    }
    if (buf != stackBuf) free(buf);
    return s;
}

void strings_reset(void) { free(g_internTable); g_internTable = NULL; g_internCap = 0; internCount = 0; rootsAdded = 0; tmpRoot1 = tmpRoot2 = 0; }
