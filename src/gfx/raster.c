/* Software rasterizer for MIDP Graphics. All surfaces are 0xAARRGGBB. */
#include "gfx.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static inline uint32_t blend(uint32_t d, uint32_t s) {
    uint32_t a = s >> 24;
    if (a == 255) return s;
    if (a == 0) return d;
    uint32_t ia = 255 - a;
    uint32_t rb = ((s & 0xff00ff) * a + (d & 0xff00ff) * ia + 0x800080) >> 8 & 0xff00ff;
    uint32_t g = ((s & 0x00ff00) * a + (d & 0x00ff00) * ia + 0x008000) >> 8 & 0x00ff00;
    uint32_t da = d >> 24;
    uint32_t oa = a + ((da * ia + 127) / 255);
    return (oa << 24) | rb | g;
}

static inline void plot(Surface *s, int x, int y, uint32_t c) {
    if (x < s->cx1 || y < s->cy1 || x >= s->cx2 || y >= s->cy2) return;
    uint32_t *p = &s->px[y * s->stride + x];
    *p = (c >> 24) == 255 ? c : blend(*p, c);
}

static void hspan(Surface *s, int x1, int x2, int y, uint32_t c) { /* inclusive x1..x2 */
    if (y < s->cy1 || y >= s->cy2) return;
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (x1 < s->cx1) x1 = s->cx1;
    if (x2 >= s->cx2) x2 = s->cx2 - 1;
    if (x1 > x2) return;
    uint32_t *p = &s->px[y * s->stride + x1];
    int n = x2 - x1 + 1;
    if ((c >> 24) == 255) { while (n--) *p++ = c; }
    else { while (n--) { *p = blend(*p, c); p++; } }
}

void gfx_fill_rect(Surface *s, int x, int y, int w, int h, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    int x1 = x, y1 = y, x2 = x + w, y2 = y + h;
    if (x1 < s->cx1) x1 = s->cx1;
    if (y1 < s->cy1) y1 = s->cy1;
    if (x2 > s->cx2) x2 = s->cx2;
    if (y2 > s->cy2) y2 = s->cy2;
    if (x1 >= x2 || y1 >= y2) return;
    for (int yy = y1; yy < y2; yy++) {
        uint32_t *p = &s->px[yy * s->stride + x1];
        int n = x2 - x1;
        if ((c >> 24) == 255) { while (n--) *p++ = c; }
        else { while (n--) { *p = blend(*p, c); p++; } }
    }
}

void gfx_line(Surface *s, int x1, int y1, int x2, int y2, uint32_t c, int dotted) {
    if (y1 == y2 && !dotted) { hspan(s, x1, x2, y1, c); return; }
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy, n = 0;
    /* quick reject */
    int minx = x1 < x2 ? x1 : x2, maxx = x1 < x2 ? x2 : x1, miny = y1 < y2 ? y1 : y2, maxy = y1 < y2 ? y2 : y1;
    if (maxx < s->cx1 || minx >= s->cx2 || maxy < s->cy1 || miny >= s->cy2) return;
    for (;;) {
        if (!dotted || ((n >> 1) & 1) == 0) plot(s, x1, y1, c);
        n++;
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void gfx_rect(Surface *s, int x, int y, int w, int h, uint32_t c, int dotted) {
    if (w < 0 || h < 0) return;
    if (w == 0 || h == 0) { gfx_line(s, x, y, x + w, y + h, c, dotted); return; }
    gfx_line(s, x, y, x + w, y, c, dotted);
    gfx_line(s, x, y + h, x + w, y + h, c, dotted);
    if (h > 1) {
        gfx_line(s, x, y + 1, x, y + h - 1, c, dotted);
        gfx_line(s, x + w, y + 1, x + w, y + h - 1, c, dotted);
    }
}

void gfx_fill_polygon(Surface *s, const int *xs, const int *ys, int n, uint32_t c) {
    if (n < 3) return;
    int miny = ys[0], maxy = ys[0];
    for (int i = 1; i < n; i++) { if (ys[i] < miny) miny = ys[i]; if (ys[i] > maxy) maxy = ys[i]; }
    if (miny < s->cy1) miny = s->cy1;
    if (maxy > s->cy2 - 1) maxy = s->cy2 - 1;
    int stackX[64];
    int *nodes = n <= 64 ? stackX : malloc(n * sizeof(int));
    for (int y = miny; y <= maxy; y++) {
        int cnt = 0;
        /* sample at pixel centre */
        int yc2 = y * 2 + 1;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            int yi2 = ys[i] * 2, yj2 = ys[j] * 2;
            if ((yi2 < yc2 && yj2 >= yc2) || (yj2 < yc2 && yi2 >= yc2)) {
                /* x at yc */
                int64_t num = (int64_t)(yc2 - yi2) * (xs[j] - xs[i]) * 2;
                int x2 = xs[i] * 2 + (int)(num / (yj2 - yi2));
                nodes[cnt++] = x2;
            }
        }
        for (int i = 1; i < cnt; i++) { int v = nodes[i], k = i - 1; while (k >= 0 && nodes[k] > v) { nodes[k + 1] = nodes[k]; k--; } nodes[k + 1] = v; }
        for (int i = 0; i + 1 < cnt; i += 2) {
            int xa = (nodes[i] + 1) >> 1, xb = (nodes[i + 1] - 1) >> 1;
            if (xb >= xa) hspan(s, xa, xb, y, c);
            else if (nodes[i + 1] - nodes[i] > 0) hspan(s, xa, xa, y, c);
        }
    }
    if (nodes != stackX) free(nodes);
}

void gfx_fill_triangle(Surface *s, int x1, int y1, int x2, int y2, int x3, int y3, uint32_t c) {
    int xs[3] = { x1, x2, x3 }, ys[3] = { y1, y2, y3 };
    gfx_fill_polygon(s, xs, ys, 3, c);
    /* make degenerate/thin triangles visible like real devices: draw edges */
    gfx_line(s, x1, y1, x2, y2, c, 0);
    gfx_line(s, x2, y2, x3, y3, c, 0);
    gfx_line(s, x3, y3, x1, y1, c, 0);
}

/* angle test: does angle (deg, 0..360) of point lie in [start, start+arc)? */
static int angleIn(double ang, int start, int arc) {
    if (arc >= 360 || arc <= -360) return 1;
    if (arc < 0) { start += arc; arc = -arc; }
    start %= 360; if (start < 0) start += 360;
    double rel = ang - start;
    while (rel < 0) rel += 360;
    while (rel >= 360) rel -= 360;
    return rel <= arc;
}

void gfx_fill_arc(Surface *s, int x, int y, int w, int h, int start, int arc, uint32_t c) {
    if (w <= 0 || h <= 0 || arc == 0) return;
    double rx = w / 2.0, ry = h / 2.0, cx = x + rx, cy = y + ry;
    int full = arc >= 360 || arc <= -360;
    for (int py = y; py < y + h; py++) {
        if (py < s->cy1 || py >= s->cy2) continue;
        double dy = (py + 0.5 - cy) / ry;
        if (dy * dy > 1) continue;
        double half = sqrt(1 - dy * dy) * rx;
        int xa = (int)ceil(cx - half - 0.5), xb = (int)floor(cx + half - 0.5);
        if (full) { hspan(s, xa, xb, py, c); continue; }
        for (int px = xa; px <= xb; px++) {
            double dx = (px + 0.5 - cx) / rx;
            double ang = atan2(-dy, dx) * 180.0 / M_PI;
            if (ang < 0) ang += 360;
            if (angleIn(ang, start, arc)) plot(s, px, py, c);
        }
    }
}

void gfx_draw_arc(Surface *s, int x, int y, int w, int h, int start, int arc, uint32_t c, int dotted) {
    if (w < 0 || h < 0 || arc == 0) return;
    if (w == 0 || h == 0) { gfx_line(s, x, y, x + w, y + h, c, dotted); return; }
    double rx = w / 2.0, ry = h / 2.0, cx = x + rx, cy = y + ry;
    if (arc > 360) arc = 360;
    if (arc < -360) arc = -360;
    int steps = (int)((fabs((double)arc) / 360.0) * (w + h) * 2) + 4;
    int px = 0, py = 0;
    for (int i = 0; i <= steps; i++) {
        double a = (start + (double)arc * i / steps) * M_PI / 180.0;
        int nx = (int)floor(cx + cos(a) * rx + 0.5), ny = (int)floor(cy - sin(a) * ry + 0.5);
        if (nx >= x + w) nx = x + w;
        if (ny >= y + h) ny = y + h;
        if (i > 0) gfx_line(s, px, py, nx, ny, c, dotted);
        px = nx; py = ny;
    }
}

void gfx_fill_round_rect(Surface *s, int x, int y, int w, int h, int aw, int ah, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    if (aw < 0) aw = -aw;
    if (ah < 0) ah = -ah;
    if (aw > w) aw = w;
    if (ah > h) ah = h;
    if (aw == 0 || ah == 0) { gfx_fill_rect(s, x, y, w, h, c); return; }
    double rx = aw / 2.0, ry = ah / 2.0;
    for (int py = y; py < y + h; py++) {
        double inset = 0;
        double fy = py + 0.5;
        double d = 0;
        if (fy < y + ry) d = (y + ry - fy) / ry;
        else if (fy > y + h - ry) d = (fy - (y + h - ry)) / ry;
        if (d > 0) inset = rx - sqrt(1 - (d > 1 ? 1 : d * d)) * rx;
        int xa = x + (int)(inset + 0.5), xb = x + w - 1 - (int)(inset + 0.5);
        if (xa <= xb) hspan(s, xa, xb, py, c);
    }
}

void gfx_draw_round_rect(Surface *s, int x, int y, int w, int h, int aw, int ah, uint32_t c, int dotted) {
    if (w < 0 || h < 0) return;
    if (aw < 0) aw = -aw;
    if (ah < 0) ah = -ah;
    if (aw > w) aw = w;
    if (ah > h) ah = h;
    if (aw == 0 || ah == 0) { gfx_rect(s, x, y, w, h, c, dotted); return; }
    int rx = aw / 2, ry = ah / 2;
    gfx_line(s, x + rx, y, x + w - rx, y, c, dotted);
    gfx_line(s, x + rx, y + h, x + w - rx, y + h, c, dotted);
    gfx_line(s, x, y + ry, x, y + h - ry, c, dotted);
    gfx_line(s, x + w, y + ry, x + w, y + h - ry, c, dotted);
    gfx_draw_arc(s, x, y, aw, ah, 90, 90, c, dotted);
    gfx_draw_arc(s, x + w - aw, y, aw, ah, 0, 90, c, dotted);
    gfx_draw_arc(s, x, y + h - ah, aw, ah, 180, 90, c, dotted);
    gfx_draw_arc(s, x + w - aw, y + h - ah, aw, ah, 270, 90, c, dotted);
}

/* ---------- blitting ---------- */
void gfx_blit(Surface *d, const uint32_t *src, int srcStride, int sx, int sy, int sw, int sh,
              int transform, int dx, int dy, int hasAlpha) {
    if (sw <= 0 || sh <= 0) return;
    int swap = transform >= 4;
    int dw = swap ? sh : sw, dh = swap ? sw : sh;
    int x1 = dx, y1 = dy, x2 = dx + dw, y2 = dy + dh;
    if (x1 < d->cx1) x1 = d->cx1;
    if (y1 < d->cy1) y1 = d->cy1;
    if (x2 > d->cx2) x2 = d->cx2;
    if (y2 > d->cy2) y2 = d->cy2;
    if (x1 >= x2 || y1 >= y2) return;
    /* source coordinate for dest local (u,v): sxl = a*u + b*v + c, syl = e*u + f*v + g */
    int a, b, cc, e, f, g;
    int W = sw, H = sh;
    switch (transform) {
        default:
        case 0: a = 1; b = 0; cc = 0; e = 0; f = 1; g = 0; break;           /* NONE */
        case 5: a = 0; b = 1; cc = 0; e = -1; f = 0; g = H - 1; break;      /* ROT90 */
        case 3: a = -1; b = 0; cc = W - 1; e = 0; f = -1; g = H - 1; break; /* ROT180 */
        case 6: a = 0; b = -1; cc = W - 1; e = 1; f = 0; g = 0; break;      /* ROT270 */
        case 2: a = -1; b = 0; cc = W - 1; e = 0; f = 1; g = 0; break;      /* MIRROR */
        case 7: a = 0; b = -1; cc = W - 1; e = -1; f = 0; g = H - 1; break; /* MIRROR_ROT90 */
        case 1: a = 1; b = 0; cc = 0; e = 0; f = -1; g = H - 1; break;      /* MIRROR_ROT180 */
        case 4: a = 0; b = 1; cc = 0; e = 1; f = 0; g = 0; break;           /* MIRROR_ROT270 */
    }
    int stepX = e * srcStride + a;
    int stepY = f * srcStride + b;
    for (int y = y1; y < y2; y++) {
        int u = x1 - dx, v = y - dy;
        int sxl = a * u + b * v + cc, syl = e * u + f * v + g;
        const uint32_t *sp = src + (sy + syl) * srcStride + (sx + sxl);
        uint32_t *dp = &d->px[y * d->stride + x1];
        int n = x2 - x1;
        if (!hasAlpha) {
            if (stepX == 1) memcpy(dp, sp, n * 4);
            else for (int i = 0; i < n; i++) { *dp++ = *sp; sp += stepX; }
        } else {
            for (int i = 0; i < n; i++) {
                uint32_t p = *sp;
                uint32_t al = p >> 24;
                if (al == 255) *dp = p;
                else if (al) *dp = blend(*dp, p);
                dp++;
                sp += stepX;
            }
        }
        (void)stepY;
    }
}

void gfx_draw_rgb(Surface *d, const int32_t *rgb, int offset, int scan, int x, int y, int w, int h, int processAlpha) {
    if (w <= 0 || h <= 0) return;
    int x1 = x, y1 = y, x2 = x + w, y2 = y + h;
    if (x1 < d->cx1) x1 = d->cx1;
    if (y1 < d->cy1) y1 = d->cy1;
    if (x2 > d->cx2) x2 = d->cx2;
    if (y2 > d->cy2) y2 = d->cy2;
    for (int yy = y1; yy < y2; yy++) {
        const int32_t *sp = rgb + offset + (yy - y) * scan + (x1 - x);
        uint32_t *dp = &d->px[yy * d->stride + x1];
        for (int xx = x1; xx < x2; xx++) {
            uint32_t p = (uint32_t)*sp++;
            if (!processAlpha) *dp = p | 0xff000000u;
            else {
                uint32_t al = p >> 24;
                if (al == 255) *dp = p;
                else if (al) *dp = blend(*dp, p);
            }
            dp++;
        }
    }
}

/* ---------- text ---------- */
static int glyphIndex(const BitmapFont *f, uint16_t ch) {
    int lo = 0, hi = f->nGlyphs - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (f->codes[mid] == ch) return mid;
        if (f->codes[mid] < ch) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

static int fallbackIndex(const BitmapFont *f, uint16_t ch) {
    int i = glyphIndex(f, ch);
    if (i >= 0) return i;
    /* fold some common characters */
    if (ch == 0x2018 || ch == 0x2019) i = glyphIndex(f, '\'');
    else if (ch == 0x201c || ch == 0x201d) i = glyphIndex(f, '"');
    else if (ch == 0x2013 || ch == 0x2014) i = glyphIndex(f, '-');
    if (i >= 0) return i;
    return glyphIndex(f, '?');
}

int gfx_char_width(const BitmapFont *f, uint16_t ch) {
    if (ch == '\n' || ch == '\r') return 0;
    int i = fallbackIndex(f, ch);
    return i >= 0 ? f->widths[i] : 0;
}

int gfx_text_width(const BitmapFont *f, const uint16_t *s, int n) {
    int w = 0;
    for (int i = 0; i < n; i++) w += gfx_char_width(f, s[i]);
    return w;
}

void gfx_draw_text(Surface *d, const BitmapFont *f, const uint16_t *s, int n, int x, int y, uint32_t argb, int style) {
    int startX = x;
    uint32_t rgb = argb & 0xffffff;
    uint32_t baseA = argb >> 24;
    int italic = style & FSTYLE_ITALIC;
    /* rows of the font that are inside the clip */
    int r0 = d->cy1 - y; if (r0 < 0) r0 = 0;
    int r1 = d->cy2 - y; if (r1 > f->height) r1 = f->height;
    for (int k = 0; k < n; k++) {
        int i = fallbackIndex(f, s[k]);
        if (i < 0) continue;
        int gw = f->widths[i];
        if (x >= d->cx2) break;
        if (r0 < r1 && x + gw + 1 > d->cx1) {
            const uint8_t *bm = f->bitmaps + f->offsets[i];
            if (italic) {
                for (int row = r0; row < r1; row++) {
                    int shear = (f->ascent - row) / 4;
                    for (int col = 0; col < gw; col++) {
                        uint32_t cov = bm[row * gw + col];
                        if (!cov) continue;
                        uint32_t al = baseA == 255 ? cov : (cov * baseA) / 255;
                        plot(d, x + col + shear, y + row, (al << 24) | rgb);
                    }
                }
            } else {
                int c0 = d->cx1 - x; if (c0 < 0) c0 = 0;
                int c1 = d->cx2 - x; if (c1 > gw) c1 = gw;
                for (int row = r0; row < r1; row++) {
                    const uint8_t *src = bm + row * gw;
                    uint32_t *dst = &d->px[(y + row) * d->stride + x];
                    for (int col = c0; col < c1; col++) {
                        uint32_t cov = src[col];
                        if (!cov) continue;
                        if (cov == 255 && baseA == 255) dst[col] = argb;
                        else {
                            uint32_t al = baseA == 255 ? cov : (cov * baseA) / 255;
                            dst[col] = blend(dst[col], (al << 24) | rgb);
                        }
                    }
                }
            }
        }
        x += gw;
    }
    if (style & FSTYLE_UNDERLINED) hspan(d, startX, x - 1, y + f->ascent + 1, argb);
}
