/* Mutation fuzzer for the decoders that read game data (PNG, JPEG, ZIP, WAV, MIDI, tone sequences).
 * Build with -fsanitize=address,undefined; any memory error aborts with a report. */
#include "../../src/gfx/gfx.h"
#include "../../src/vm/zip.h"
#include "../../src/audio/mixer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TMPZIP "build/fuzz_tmp.zip"

static uint8_t *readf(const char *p, int *n) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, 2); *n = ftell(f); fseek(f, 0, 0);
    uint8_t *b = malloc(*n + 1); if (fread(b, 1, *n, f) != (size_t)*n) { fclose(f); free(b); return NULL; }
    fclose(f); return b;
}
static uint32_t rs = 12345;
static uint32_t rnd(void) { rs = rs * 1103515245u + 12345u; return rs >> 8; }

static void mutate(uint8_t *d, int *n, int cap) {
    int k = 1 + rnd() % 8;
    while (k--) {
        int op = rnd() % 5, len = *n;
        if (len == 0) return;
        switch (op) {
            case 0: d[rnd() % len] = rnd(); break;                         /* random byte */
            case 1: d[rnd() % len] ^= 1 << (rnd() % 8); break;            /* bit flip */
            case 2: *n = 1 + rnd() % len; break;                           /* truncate */
            case 3: { int p = rnd() % len; static const uint8_t v[] = { 0, 0xff, 0x7f, 0x80 }; d[p] = v[rnd() % 4]; break; }
            case 4: if (*n + 16 < cap) { int p = rnd() % len; memmove(d + p + 4, d + p, len - p); memset(d + p, 0xff, 4); *n += 4; } break;
        }
    }
}

int main(int argc, char **argv) {
    int iters = argc > 1 ? atoi(argv[1]) : 2000;
    const char *files[] = { "tests/midlets/res/sprites.png", "tests/midlets/res/pal.png", "tests/midlets/res/arrow.png",
                            "tests/midlets/res/tiles.png", "tests/jpeg/a420.jpg", "tests/jpeg/a444.jpg", "tests/jpeg/gray.jpg",
                            "tests/midlets/jars/GameTest.jar", "tests/audio/beep.wav", "tests/audio/rich.mid",
                            "tests/fuzz/seeds/tone.seq", "tests/fuzz/seeds/adpcm.wav", NULL };
    mix_init(NULL, NULL);
    int16_t abuf[2048];
    for (int fi = 0; files[fi]; fi++) {
        int n0; uint8_t *orig = readf(files[fi], &n0);
        if (!orig) { printf("missing %s\n", files[fi]); continue; }
        int cap = n0 + 4096;
        uint8_t *d = malloc(cap);
        for (int it = 0; it < iters; it++) {
            int n = n0; memcpy(d, orig, n0);
            mutate(d, &n, cap);
            int w, h, a;
            uint32_t *px = png_decode(d, n, &w, &h, &a); free(px);
            px = jpeg_decode(d, n, &w, &h); free(px);
            uint8_t *copy = malloc(n); memcpy(copy, d, n);
            Zip *z = zip_open_mem(copy, n, 1);
            /* the same bytes through the lazy (file-backed) reader must give identical results */
            FILE *tf = fopen(TMPZIP, "wb"); fwrite(d, 1, n, tf); fclose(tf);
            Zip *lz = zip_open_file_lazy(TMPZIP);
            if (z) {
                for (int e = 0; e < z->nEntries && e < 20; e++) {
                    uint32_t l = 0, ll = 0;
                    uint8_t *c = zip_read(z, &z->entries[e], &l);
                    uint8_t *lc = lz && e < lz->nEntries ? zip_read(lz, &lz->entries[e], &ll) : NULL;
                    if (lc && (!c || l != ll || memcmp(c, lc, l))) { printf("lazy/mem mismatch in %s entry %d\n", files[fi], e); abort(); }
                    free(c); free(lc);
                }
                zip_close(z);
            } else free(copy);
            if (lz) {
                for (int e = 0; e < lz->nEntries && e < 20; e++) { uint32_t l; free(zip_read(lz, &lz->entries[e], &l)); }
                zip_close(lz);
            }
            int hnd = mix_play(d, n, 1, 100);
            if (hnd > 0) { for (int r = 0; r < 20 && mix_playing(hnd); r++) mix_render(abuf, 1024); mix_stop(hnd); }
        }
        printf("fuzzed %-36s %d iterations\n", files[fi], iters);
        free(d); free(orig);
    }
    printf("fuzz done: no crashes\n");
    return 0;
}
