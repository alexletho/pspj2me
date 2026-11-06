#ifndef GFX_H
#define GFX_H
#include <stdint.h>

typedef struct {
    uint32_t *px;
    int w, h, stride;
    int cx1, cy1, cx2, cy2;   /* clip, x2/y2 exclusive */
} Surface;

/* stroke */
#define STROKE_SOLID 0
#define STROKE_DOTTED 1

void gfx_fill_rect(Surface *s, int x, int y, int w, int h, uint32_t argb);
void gfx_line(Surface *s, int x1, int y1, int x2, int y2, uint32_t argb, int dotted);
void gfx_rect(Surface *s, int x, int y, int w, int h, uint32_t argb, int dotted);
void gfx_fill_triangle(Surface *s, int x1, int y1, int x2, int y2, int x3, int y3, uint32_t argb);
void gfx_fill_arc(Surface *s, int x, int y, int w, int h, int start, int arc, uint32_t argb);
void gfx_draw_arc(Surface *s, int x, int y, int w, int h, int start, int arc, uint32_t argb, int dotted);
void gfx_fill_round_rect(Surface *s, int x, int y, int w, int h, int aw, int ah, uint32_t argb);
void gfx_draw_round_rect(Surface *s, int x, int y, int w, int h, int aw, int ah, uint32_t argb, int dotted);
void gfx_fill_polygon(Surface *s, const int *xs, const int *ys, int n, uint32_t argb);

/* blit a region of an ARGB source with an MIDP transform; hasAlpha: 0 = opaque source */
void gfx_blit(Surface *d, const uint32_t *src, int srcStride, int sx, int sy, int sw, int sh,
              int transform, int dx, int dy, int hasAlpha);
void gfx_draw_rgb(Surface *d, const int32_t *rgb, int offset, int scan, int x, int y, int w, int h, int processAlpha);

/* fonts */
typedef struct {
    int height;      /* line height */
    int ascent;      /* baseline offset from top */
    int nGlyphs;
    const uint16_t *codes;     /* sorted code points */
    const uint8_t *widths;     /* advance widths */
    const uint32_t *offsets;   /* offset into bitmap data per glyph */
    const uint8_t *bitmaps;    /* 8-bit alpha rows, width=advance, rows=height */
} BitmapFont;

const BitmapFont *gfx_font(int sizeIdx, int bold);
int gfx_char_width(const BitmapFont *f, uint16_t ch);
int gfx_text_width(const BitmapFont *f, const uint16_t *s, int n);
void gfx_draw_text(Surface *d, const BitmapFont *f, const uint16_t *s, int n, int x, int y, uint32_t argb, int style);
#define FSTYLE_BOLD 1
#define FSTYLE_ITALIC 2
#define FSTYLE_UNDERLINED 4

/* png.c */
uint32_t *png_decode(const uint8_t *data, int len, int *w, int *h, int *hasAlpha);
/* jpeg.c */
uint32_t *jpeg_decode(const uint8_t *data, int len, int *w, int *h);

#endif
