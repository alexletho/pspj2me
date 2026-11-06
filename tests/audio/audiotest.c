#include "../../src/audio/mixer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char *rd(const char *p, int *n) { FILE *f = fopen(p, "rb"); fseek(f, 0, 2); *n = ftell(f); fseek(f, 0, 0); unsigned char *b = malloc(*n); fread(b, 1, *n, f); fclose(f); return b; }
int main(int argc, char **argv) {
    mix_init(NULL, NULL);
    int n; unsigned char *d = rd(argv[1], &n);
    int h = mix_play(d, n, 1, 100);
    printf("handle %d\n", h);
    if (h < 0) return 1;
    FILE *o = fopen(argv[2], "wb");
    int total = 0; long long energy = 0; int peak = 0;
    int16_t buf[2048];
    fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, o);
    int hdr[] = { 16 }; fwrite(hdr, 4, 1, o);
    short f1[] = { 1, 2 }; fwrite(f1, 2, 2, o);
    int f2[] = { 44100, 44100 * 4 }; fwrite(f2, 4, 2, o);
    short f3[] = { 4, 16 }; fwrite(f3, 2, 2, o);
    fwrite("data\0\0\0\0", 1, 8, o);
    while (mix_playing(h) && total < 44100 * 30) {
        mix_render(buf, 1024);
        for (int i = 0; i < 2048; i++) { energy += buf[i] * buf[i]; if (abs(buf[i]) > peak) peak = abs(buf[i]); }
        fwrite(buf, 4, 1024, o);
        total += 1024;
    }
    fclose(o);
    printf("rendered %.2f s, rms %.0f, peak %d\n", total / 44100.0, total ? __builtin_sqrt((double)energy / (total * 2)) : 0, peak);
    return 0;
}
