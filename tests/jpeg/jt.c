#include "../../src/gfx/gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
int main(int c, char **v) {
  FILE *f=fopen(v[1],"rb"); fseek(f,0,2); int n=ftell(f); fseek(f,0,0); unsigned char*b=malloc(n); if(fread(b,1,n,f)!=(size_t)n) return 2; fclose(f);
  int w,h; uint32_t *p=jpeg_decode(b,n,&w,&h); if(!p){printf("%s: decode failed\n",v[1]);return 1;}
  FILE*o=fopen(v[2],"wb"); fprintf(o,"P6\n%d %d\n255\n",w,h); for(int i=0;i<w*h;i++){fputc(p[i]>>16&255,o);fputc(p[i]>>8&255,o);fputc(p[i]&255,o);} fclose(o);
  printf("%s: %dx%d\n",v[1],w,h); return 0; }
