#include "../../src/audio/mixer.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
int main(int c, char **v) {
  FILE *f = fopen(v[1], "rb"); fseek(f, 0, 2); int n = ftell(f); fseek(f, 0, 0); unsigned char *b = malloc(n); if (fread(b, 1, n, f) != (size_t)n) return 1; fclose(f);
  mix_init(NULL, NULL);
  int h = mix_play(b, n, -1, 100);
  int16_t buf[2048]; struct timespec a, z; clock_gettime(CLOCK_MONOTONIC, &a);
  for (int i = 0; i < 44100 * 20 / 1024; i++) mix_render(buf, 1024);
  clock_gettime(CLOCK_MONOTONIC, &z);
  double ms = (z.tv_sec - a.tv_sec) * 1e3 + (z.tv_nsec - a.tv_nsec) / 1e6;
  printf("20 s of dense MIDI rendered in %.1f ms on host (%.2f%% of one core)\n", ms, ms / 200.0);
  return h < 0;
}
