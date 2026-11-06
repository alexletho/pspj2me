package com.nokia.mid.ui;

import javax.microedition.lcdui.*;

final class DirectGraphicsImpl implements DirectGraphics {
    private final Graphics g;
    DirectGraphicsImpl(Graphics g) { this.g = g; }

    static int toTransform(int manipulation) {
        boolean fh = (manipulation & FLIP_HORIZONTAL) != 0, fv = (manipulation & FLIP_VERTICAL) != 0;
        int ccw = (manipulation & 0x0fff) % 360;
        int mirror = (fh ^ fv) ? 1 : 0;
        int rot = (fv ? 2 : 0) + (4 - ccw / 90) % 4;
        rot %= 4;
        int[][] map = { { 0, 5, 3, 6 }, { 2, 7, 1, 4 } };
        return map[mirror][rot];
    }

    public void setARGBColor(int argb) { Internal.setARGBColor(g, argb); }
    public int getAlphaComponent() { return (Internal.getARGBColor(g) >>> 24); }
    public int getNativePixelFormat() { return TYPE_INT_8888_ARGB; }

    public void drawImage(Image img, int x, int y, int anchor, int manipulation) {
        if (img == null) throw new NullPointerException();
        Internal.drawRegion(g, img, 0, 0, img.getWidth(), img.getHeight(), toTransform(manipulation), x, y, anchor);
    }

    private void withColor(int argb, Runnable r) {
        int old = Internal.getARGBColor(g);
        Internal.setARGBColor(g, argb);
        r.run();
        Internal.setARGBColor(g, old);
    }

    public void drawTriangle(int x1, int y1, int x2, int y2, int x3, int y3, int argb) {
        int old = Internal.getARGBColor(g);
        Internal.setARGBColor(g, argb);
        g.drawLine(x1, y1, x2, y2); g.drawLine(x2, y2, x3, y3); g.drawLine(x3, y3, x1, y1);
        Internal.setARGBColor(g, old);
    }
    public void fillTriangle(int x1, int y1, int x2, int y2, int x3, int y3, int argb) {
        int old = Internal.getARGBColor(g);
        Internal.setARGBColor(g, argb);
        g.fillTriangle(x1, y1, x2, y2, x3, y3);
        Internal.setARGBColor(g, old);
    }
    public void drawPolygon(int[] xp, int xo, int[] yp, int yo, int n, int argb) {
        int old = Internal.getARGBColor(g);
        Internal.setARGBColor(g, argb);
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            g.drawLine(xp[xo + i], yp[yo + i], xp[xo + j], yp[yo + j]);
        }
        Internal.setARGBColor(g, old);
    }
    public void fillPolygon(int[] xp, int xo, int[] yp, int yo, int n, int argb) {
        int old = Internal.getARGBColor(g);
        Internal.setARGBColor(g, argb);
        Internal.fillPolygon(g, xp, xo, yp, yo, n);
        Internal.setARGBColor(g, old);
    }

    private void blitArgb(int[] argb, int w, int h, int x, int y, int manipulation, boolean alpha) {
        if (w <= 0 || h <= 0) return;
        Image img = Image.createRGBImage(argb, w, h, alpha);
        Internal.drawRegion(g, img, 0, 0, w, h, toTransform(manipulation), x, y, Graphics.TOP | Graphics.LEFT);
    }

    public void drawPixels(int[] pixels, boolean transparency, int offset, int scan, int x, int y, int w, int h, int manipulation, int format) {
        if (pixels == null) throw new NullPointerException();
        int[] buf = new int[w * h];
        for (int r = 0; r < h; r++) {
            for (int c = 0; c < w; c++) {
                int p = pixels[offset + r * scan + c];
                if (format == TYPE_INT_888_RGB || !transparency) p |= 0xff000000;
                buf[r * w + c] = p;
            }
        }
        blitArgb(buf, w, h, x, y, manipulation, transparency);
    }

    static int shortToArgb(int s, int format) {
        switch (format) {
            case TYPE_USHORT_4444_ARGB: {
                int a = (s >> 12) & 15, r = (s >> 8) & 15, gg = (s >> 4) & 15, b = s & 15;
                return (a * 17 << 24) | (r * 17 << 16) | (gg * 17 << 8) | (b * 17);
            }
            case TYPE_USHORT_444_RGB: {
                int r = (s >> 8) & 15, gg = (s >> 4) & 15, b = s & 15;
                return 0xff000000 | (r * 17 << 16) | (gg * 17 << 8) | (b * 17);
            }
            case TYPE_USHORT_555_RGB: case TYPE_USHORT_1555_ARGB: {
                int r = (s >> 10) & 31, gg = (s >> 5) & 31, b = s & 31;
                int a = format == TYPE_USHORT_1555_ARGB ? ((s & 0x8000) != 0 ? 0xff : 0) : 0xff;
                return (a << 24) | ((r * 255 / 31) << 16) | ((gg * 255 / 31) << 8) | (b * 255 / 31);
            }
            default: { /* 565 */
                int r = (s >> 11) & 31, gg = (s >> 5) & 63, b = s & 31;
                return 0xff000000 | ((r * 255 / 31) << 16) | ((gg * 255 / 63) << 8) | (b * 255 / 31);
            }
        }
    }

    static int argbToShort(int p, int format) {
        int a = p >>> 24, r = (p >> 16) & 255, gg = (p >> 8) & 255, b = p & 255;
        switch (format) {
            case TYPE_USHORT_4444_ARGB: return ((a >> 4) << 12) | ((r >> 4) << 8) | ((gg >> 4) << 4) | (b >> 4);
            case TYPE_USHORT_444_RGB: return ((r >> 4) << 8) | ((gg >> 4) << 4) | (b >> 4);
            case TYPE_USHORT_555_RGB: return ((r >> 3) << 10) | ((gg >> 3) << 5) | (b >> 3);
            case TYPE_USHORT_1555_ARGB: return (a >= 128 ? 0x8000 : 0) | ((r >> 3) << 10) | ((gg >> 3) << 5) | (b >> 3);
            default: return ((r >> 3) << 11) | ((gg >> 2) << 5) | (b >> 3);
        }
    }

    public void drawPixels(short[] pixels, boolean transparency, int offset, int scan, int x, int y, int w, int h, int manipulation, int format) {
        if (pixels == null) throw new NullPointerException();
        int[] buf = new int[w * h];
        for (int r = 0; r < h; r++)
            for (int c = 0; c < w; c++) {
                int p = shortToArgb(pixels[offset + r * scan + c] & 0xffff, format);
                if (!transparency) p |= 0xff000000;
                buf[r * w + c] = p;
            }
        blitArgb(buf, w, h, x, y, manipulation, transparency);
    }

    public void drawPixels(byte[] pixels, byte[] mask, int offset, int scan, int x, int y, int w, int h, int manipulation, int format) {
        if (pixels == null) throw new NullPointerException();
        int[] buf = new int[w * h];
        for (int r = 0; r < h; r++)
            for (int c = 0; c < w; c++) {
                int p;
                if (format == TYPE_BYTE_1_GRAY) {
                    int bit = offset * 8 + r * scan + c;
                    int on = (pixels[bit >> 3] >> (7 - (bit & 7))) & 1;
                    p = on != 0 ? 0xff000000 : 0xffffffff;
                    if (mask != null) { int mb = (mask[bit >> 3] >> (7 - (bit & 7))) & 1; if (mb == 0) p &= 0x00ffffff; }
                } else if (format == TYPE_BYTE_332_RGB) {
                    int v = pixels[offset + r * scan + c] & 255;
                    p = 0xff000000 | (((v >> 5) * 255 / 7) << 16) | ((((v >> 2) & 7) * 255 / 7) << 8) | ((v & 3) * 85);
                } else {
                    int v = pixels[offset + r * scan + c] & 255;
                    p = 0xff000000 | (v << 16) | (v << 8) | v;
                }
                buf[r * w + c] = p;
            }
        blitArgb(buf, w, h, x, y, manipulation, mask != null);
    }

    public void getPixels(int[] pixels, int offset, int scan, int x, int y, int w, int h, int format) {
        throw new IllegalArgumentException("getPixels unsupported on this Graphics");
    }
    public void getPixels(byte[] pixels, byte[] mask, int offset, int scan, int x, int y, int w, int h, int format) {
        throw new IllegalArgumentException("getPixels unsupported on this Graphics");
    }
    public void getPixels(short[] pixels, int offset, int scan, int x, int y, int w, int h, int format) {
        throw new IllegalArgumentException("getPixels unsupported on this Graphics");
    }
}
