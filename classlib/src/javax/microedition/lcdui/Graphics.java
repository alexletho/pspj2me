package javax.microedition.lcdui;

public class Graphics {
    public static final int HCENTER = 1, VCENTER = 2, LEFT = 4, RIGHT = 8, TOP = 16, BOTTOM = 32, BASELINE = 64;
    public static final int SOLID = 0, DOTTED = 1;

    Image img;
    int[] buf;
    int bw, bh;
    int tx, ty;
    int clipX1, clipY1, clipX2, clipY2;
    int color = 0xff000000;
    Font font;
    int stroke;
    /* bounds the clip may never exceed (used for the canvas area) */
    int maxX1, maxY1, maxX2, maxY2;

    Graphics(Image target) {
        img = target;
        buf = target.pixels;
        bw = target.width;
        bh = target.height;
        maxX2 = bw; maxY2 = bh;
        reset();
    }

    void reset() {
        tx = 0; ty = 0;
        clipX1 = maxX1; clipY1 = maxY1; clipX2 = maxX2; clipY2 = maxY2;
        color = 0xff000000;
        font = Font.getDefaultFont();
        stroke = SOLID;
    }

    void setBounds(int x1, int y1, int x2, int y2) {
        maxX1 = x1; maxY1 = y1; maxX2 = x2; maxY2 = y2;
        reset();
    }

    public void translate(int x, int y) { tx += x; ty += y; }
    public int getTranslateX() { return tx; }
    public int getTranslateY() { return ty; }

    public int getColor() { return color & 0xffffff; }
    public int getRedComponent() { return (color >> 16) & 0xff; }
    public int getGreenComponent() { return (color >> 8) & 0xff; }
    public int getBlueComponent() { return color & 0xff; }
    public int getGrayScale() { return (getRedComponent() * 76 + getGreenComponent() * 150 + getBlueComponent() * 29) >> 8; }
    public void setColor(int red, int green, int blue) {
        if ((red | green | blue) < 0 || red > 255 || green > 255 || blue > 255) throw new IllegalArgumentException();
        color = 0xff000000 | (red << 16) | (green << 8) | blue;
    }
    public void setColor(int rgb) { color = 0xff000000 | (rgb & 0xffffff); }
    public void setGrayScale(int v) {
        if (v < 0 || v > 255) throw new IllegalArgumentException();
        setColor(v, v, v);
    }
    /* Nokia DirectGraphics support */
    void setARGB(int argb) { color = argb; }

    public Font getFont() { return font; }
    public void setFont(Font f) { font = f == null ? Font.getDefaultFont() : f; }
    public void setStrokeStyle(int style) {
        if (style != SOLID && style != DOTTED) throw new IllegalArgumentException();
        stroke = style;
    }
    public int getStrokeStyle() { return stroke; }

    public int getClipX() { return clipX1 - tx; }
    public int getClipY() { return clipY1 - ty; }
    public int getClipWidth() { return clipX2 - clipX1; }
    public int getClipHeight() { return clipY2 - clipY1; }

    public void clipRect(int x, int y, int width, int height) {
        int x1 = x + tx, y1 = y + ty, x2 = x1 + width, y2 = y1 + height;
        if (x1 > clipX1) clipX1 = x1;
        if (y1 > clipY1) clipY1 = y1;
        if (x2 < clipX2) clipX2 = x2;
        if (y2 < clipY2) clipY2 = y2;
        if (clipX2 < clipX1) clipX2 = clipX1;
        if (clipY2 < clipY1) clipY2 = clipY1;
    }

    public void setClip(int x, int y, int width, int height) {
        int x1 = x + tx, y1 = y + ty, x2 = x1 + width, y2 = y1 + height;
        if (x1 < maxX1) x1 = maxX1;
        if (y1 < maxY1) y1 = maxY1;
        if (x2 > maxX2) x2 = maxX2;
        if (y2 > maxY2) y2 = maxY2;
        if (x2 < x1) x2 = x1;
        if (y2 < y1) y2 = y1;
        clipX1 = x1; clipY1 = y1; clipX2 = x2; clipY2 = y2;
    }

    public native void drawLine(int x1, int y1, int x2, int y2);
    public native void fillRect(int x, int y, int width, int height);
    public native void drawRect(int x, int y, int width, int height);
    public native void drawRoundRect(int x, int y, int width, int height, int arcWidth, int arcHeight);
    public native void fillRoundRect(int x, int y, int width, int height, int arcWidth, int arcHeight);
    public native void fillArc(int x, int y, int width, int height, int startAngle, int arcAngle);
    public native void drawArc(int x, int y, int width, int height, int startAngle, int arcAngle);
    public native void fillTriangle(int x1, int y1, int x2, int y2, int x3, int y3);

    public void drawString(String str, int x, int y, int anchor) {
        if (str == null) throw new NullPointerException();
        drawText0(str, null, 0, str.length(), x, y, anchor);
    }
    public void drawSubstring(String str, int offset, int len, int x, int y, int anchor) {
        if (str == null) throw new NullPointerException();
        if (offset < 0 || len < 0 || offset + len > str.length()) throw new StringIndexOutOfBoundsException();
        drawText0(str, null, offset, len, x, y, anchor);
    }
    public void drawChar(char character, int x, int y, int anchor) {
        char[] c = { character };
        drawText0(null, c, 0, 1, x, y, anchor);
    }
    public void drawChars(char[] data, int offset, int length, int x, int y, int anchor) {
        if (data == null) throw new NullPointerException();
        if (offset < 0 || length < 0 || offset + length > data.length) throw new ArrayIndexOutOfBoundsException();
        drawText0(null, data, offset, length, x, y, anchor);
    }
    private native void drawText0(String s, char[] c, int off, int len, int x, int y, int anchor);

    public void drawImage(Image image, int x, int y, int anchor) {
        if (image == null) throw new NullPointerException();
        drawRegion0(image, 0, 0, image.width, image.height, 0, x, y, anchor);
    }
    public void drawRegion(Image src, int x_src, int y_src, int width, int height, int transform, int x_dest, int y_dest, int anchor) {
        if (src == null) throw new NullPointerException();
        if (src == img) throw new IllegalArgumentException();
        if (x_src < 0 || y_src < 0 || width < 0 || height < 0 || x_src + width > src.width || y_src + height > src.height || transform < 0 || transform > 7)
            throw new IllegalArgumentException();
        drawRegion0(src, x_src, y_src, width, height, transform, x_dest, y_dest, anchor);
    }
    native void drawRegion0(Image src, int x_src, int y_src, int width, int height, int transform, int x_dest, int y_dest, int anchor);

    public void copyArea(int x_src, int y_src, int width, int height, int x_dest, int y_dest, int anchor) {
        if (img == null || !img.mutable) throw new IllegalStateException();
        int ax = x_src + tx, ay = y_src + ty;
        if (ax < 0 || ay < 0 || ax + width > bw || ay + height > bh) throw new IllegalArgumentException();
        if (width <= 0 || height <= 0) return;
        Image tmp = new Image(width, height, false);
        for (int r = 0; r < height; r++) System.arraycopy(buf, (ay + r) * bw + ax, tmp.pixels, r * width, width);
        drawRegion0(tmp, 0, 0, width, height, 0, x_dest, y_dest, anchor);
    }

    public native void drawRGB(int[] rgbData, int offset, int scanlength, int x, int y, int width, int height, boolean processAlpha);

    native void fillPolygon0(int[] xs, int xOff, int[] ys, int yOff, int n);

    public int getDisplayColor(int color) { return color & 0xffffff; }
}
