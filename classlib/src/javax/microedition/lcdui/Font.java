package javax.microedition.lcdui;

public final class Font {
    public static final int STYLE_PLAIN = 0, STYLE_BOLD = 1, STYLE_ITALIC = 2, STYLE_UNDERLINED = 4;
    public static final int SIZE_SMALL = 8, SIZE_MEDIUM = 0, SIZE_LARGE = 16;
    public static final int FACE_SYSTEM = 0, FACE_MONOSPACE = 32, FACE_PROPORTIONAL = 64;
    public static final int FONT_STATIC_TEXT = 0, FONT_INPUT_TEXT = 1;

    int face, style, size;
    int sizeIdx;
    private int height = -1, baseline;
    private static final Font[] cache = new Font[3 * 8 * 3];
    private static Font defaultFont;

    private Font(int face, int style, int size) {
        this.face = face; this.style = style; this.size = size;
        sizeIdx = size == SIZE_SMALL ? 0 : size == SIZE_LARGE ? 2 : 1;
    }

    public static Font getDefaultFont() {
        if (defaultFont == null) defaultFont = getFont(FACE_SYSTEM, STYLE_PLAIN, SIZE_MEDIUM);
        return defaultFont;
    }
    public static Font getFont(int fontSpecifier) {
        if (fontSpecifier != FONT_STATIC_TEXT && fontSpecifier != FONT_INPUT_TEXT) throw new IllegalArgumentException();
        return getDefaultFont();
    }
    public static Font getFont(int face, int style, int size) {
        if ((face != FACE_SYSTEM && face != FACE_MONOSPACE && face != FACE_PROPORTIONAL)
            || (size != SIZE_SMALL && size != SIZE_MEDIUM && size != SIZE_LARGE) || (style & ~7) != 0)
            throw new IllegalArgumentException("Unsupported font");
        int fi = face == FACE_SYSTEM ? 0 : face == FACE_MONOSPACE ? 1 : 2;
        int si = size == SIZE_SMALL ? 0 : size == SIZE_LARGE ? 2 : 1;
        int idx = (fi * 8 + style) * 3 + si;
        synchronized (cache) {
            if (cache[idx] == null) cache[idx] = new Font(face, style, size);
            return cache[idx];
        }
    }

    public int getStyle() { return style; }
    public int getSize() { return size; }
    public int getFace() { return face; }
    public boolean isPlain() { return style == STYLE_PLAIN; }
    public boolean isBold() { return (style & STYLE_BOLD) != 0; }
    public boolean isItalic() { return (style & STYLE_ITALIC) != 0; }
    public boolean isUnderlined() { return (style & STYLE_UNDERLINED) != 0; }

    public int getHeight() { if (height < 0) { height = height0(sizeIdx); baseline = baseline0(sizeIdx); } return height; }
    public int getBaselinePosition() { getHeight(); return baseline; }
    public int charWidth(char ch) { return charWidth0(sizeIdx, style, ch); }
    public int charsWidth(char[] ch, int offset, int length) {
        if (offset < 0 || length < 0 || offset + length > ch.length) throw new ArrayIndexOutOfBoundsException();
        return charsWidth0(sizeIdx, style, ch, offset, length);
    }
    public int stringWidth(String str) { return stringWidth0(sizeIdx, style, str, 0, str.length()); }
    public int substringWidth(String str, int offset, int len) {
        if (offset < 0 || len < 0 || offset + len > str.length()) throw new StringIndexOutOfBoundsException();
        return stringWidth0(sizeIdx, style, str, offset, len);
    }

    private static native int height0(int sizeIdx);
    private static native int baseline0(int sizeIdx);
    private static native int charWidth0(int sizeIdx, int style, char ch);
    private static native int charsWidth0(int sizeIdx, int style, char[] ch, int off, int len);
    private static native int stringWidth0(int sizeIdx, int style, String s, int off, int len);
}
