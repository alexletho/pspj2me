package java.lang;

import java.io.UnsupportedEncodingException;

/* Encoding helpers shared by String and java.io readers/writers. */
public final class Charsets {
    public static final int UTF8 = 0, LATIN1 = 1, ASCII = 2, CP1251 = 3, UTF16BE = 4, UTF16LE = 5;
    public static int DEFAULT = UTF8;

    private static char[] cp1251;

    public static int lookup(String enc) throws UnsupportedEncodingException {
        if (enc == null) throw new NullPointerException();
        String e = enc.toUpperCase().replace('_', '-');
        if (e.equals("UTF-8") || e.equals("UTF8")) return UTF8;
        if (e.equals("ISO-8859-1") || e.equals("ISO8859-1") || e.equals("ISO-8859-1") || e.equals("LATIN1") || e.equals("8859-1")) return LATIN1;
        if (e.equals("US-ASCII") || e.equals("ASCII")) return ASCII;
        if (e.equals("WINDOWS-1251") || e.equals("CP1251") || e.equals("WIN1251")) return CP1251;
        if (e.equals("UTF-16BE") || e.equals("UTF-16") || e.equals("UNICODEBIG") || e.equals("UCS-2")) return UTF16BE;
        if (e.equals("UTF-16LE") || e.equals("UNICODELITTLE")) return UTF16LE;
        throw new UnsupportedEncodingException(enc);
    }

    private static char[] cp1251Table() {
        if (cp1251 == null) {
            String hi = "ЂЃ‚ѓ„…†‡€‰Љ‹ЊЌЋЏ"
                      + "ђ‘’“”•–—�™љ›њќћџ"
                      + " ЎўЈ¤Ґ¦§Ё©Є«¬­®Ї"
                      + "°±Ііґµ¶·ё№є»јЅѕї";
            char[] t = new char[128];
            for (int i = 0; i < 64; i++) t[i] = hi.charAt(i);
            for (int i = 64; i < 128; i++) t[i] = (char) (0x0410 + (i - 64));
            cp1251 = t;
        }
        return cp1251;
    }

    public static char[] decode(byte[] b, int off, int len, int enc) {
        char[] out;
        int n = 0;
        switch (enc) {
            case LATIN1: case ASCII:
                out = new char[len];
                for (int i = 0; i < len; i++) out[i] = (char) (b[off + i] & 0xff);
                return out;
            case CP1251: {
                char[] t = cp1251Table();
                out = new char[len];
                for (int i = 0; i < len; i++) {
                    int c = b[off + i] & 0xff;
                    out[i] = c < 128 ? (char) c : t[c - 128];
                }
                return out;
            }
            case UTF16BE: case UTF16LE: {
                out = new char[len / 2];
                for (int i = 0; i + 1 < len; i += 2) {
                    int a = b[off + i] & 0xff, c = b[off + i + 1] & 0xff;
                    out[n++] = enc == UTF16BE ? (char) ((a << 8) | c) : (char) ((c << 8) | a);
                }
                return out;
            }
            default: {
                out = new char[len];
                int i = off, end = off + len;
                while (i < end) {
                    int c = b[i] & 0xff;
                    if (c < 0x80) { out[n++] = (char) c; i++; }
                    else if ((c & 0xe0) == 0xc0 && i + 1 < end && (b[i + 1] & 0xc0) == 0x80) {
                        out[n++] = (char) (((c & 0x1f) << 6) | (b[i + 1] & 0x3f)); i += 2;
                    } else if ((c & 0xf0) == 0xe0 && i + 2 < end && (b[i + 1] & 0xc0) == 0x80 && (b[i + 2] & 0xc0) == 0x80) {
                        out[n++] = (char) (((c & 0x0f) << 12) | ((b[i + 1] & 0x3f) << 6) | (b[i + 2] & 0x3f)); i += 3;
                    } else if ((c & 0xf8) == 0xf0 && i + 3 < end) {
                        int cp = ((c & 0x07) << 18) | ((b[i + 1] & 0x3f) << 12) | ((b[i + 2] & 0x3f) << 6) | (b[i + 3] & 0x3f);
                        cp -= 0x10000;
                        if (n + 2 > out.length) { char[] nb = new char[out.length + 4]; System.arraycopy(out, 0, nb, 0, n); out = nb; }
                        out[n++] = (char) (0xd800 | (cp >> 10));
                        out[n++] = (char) (0xdc00 | (cp & 0x3ff));
                        i += 4;
                    } else { out[n++] = (char) c; i++; }
                }
                break;
            }
        }
        if (n != out.length) {
            char[] r = new char[n];
            System.arraycopy(out, 0, r, 0, n);
            out = r;
        }
        return out;
    }

    public static byte[] encode(char[] v, int off, int len, int enc) {
        byte[] out;
        int n = 0;
        switch (enc) {
            case LATIN1: case ASCII:
                out = new byte[len];
                for (int i = 0; i < len; i++) { char c = v[off + i]; out[i] = (byte) (c < 256 ? c : '?'); }
                return out;
            case CP1251: {
                char[] t = cp1251Table();
                out = new byte[len];
                for (int i = 0; i < len; i++) {
                    char c = v[off + i];
                    if (c < 128) out[i] = (byte) c;
                    else {
                        byte r = (byte) '?';
                        for (int k = 0; k < 128; k++) if (t[k] == c) { r = (byte) (k + 128); break; }
                        out[i] = r;
                    }
                }
                return out;
            }
            case UTF16BE: case UTF16LE:
                out = new byte[len * 2];
                for (int i = 0; i < len; i++) {
                    char c = v[off + i];
                    if (enc == UTF16BE) { out[i * 2] = (byte) (c >> 8); out[i * 2 + 1] = (byte) c; }
                    else { out[i * 2] = (byte) c; out[i * 2 + 1] = (byte) (c >> 8); }
                }
                return out;
            default: {
                int size = 0;
                for (int i = 0; i < len; i++) {
                    char c = v[off + i];
                    size += c < 0x80 ? 1 : c < 0x800 ? 2 : 3;
                }
                out = new byte[size];
                for (int i = 0; i < len; i++) {
                    char c = v[off + i];
                    if (c < 0x80) out[n++] = (byte) c;
                    else if (c < 0x800) { out[n++] = (byte) (0xc0 | (c >> 6)); out[n++] = (byte) (0x80 | (c & 0x3f)); }
                    else { out[n++] = (byte) (0xe0 | (c >> 12)); out[n++] = (byte) (0x80 | ((c >> 6) & 0x3f)); out[n++] = (byte) (0x80 | (c & 0x3f)); }
                }
                return out;
            }
        }
    }
}
