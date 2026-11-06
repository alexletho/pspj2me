package java.io;
public class InputStreamReader extends Reader {
    private InputStream in;
    private int enc;
    private int pending = -1;
    public InputStreamReader(InputStream is) { in = is; enc = java.lang.Charsets.DEFAULT; }
    public InputStreamReader(InputStream is, String encName) throws UnsupportedEncodingException {
        in = is; enc = java.lang.Charsets.lookup(encName);
    }
    private int readByte() throws IOException { return in.read(); }
    public int read() throws IOException {
        if (pending >= 0) { int p = pending; pending = -1; return p; }
        int c = readByte();
        if (c < 0) return -1;
        switch (enc) {
            case java.lang.Charsets.LATIN1: case java.lang.Charsets.ASCII: return c;
            case java.lang.Charsets.CP1251: {
                byte[] b = { (byte) c };
                return java.lang.Charsets.decode(b, 0, 1, enc)[0];
            }
            case java.lang.Charsets.UTF16BE: { int d = readByte(); return d < 0 ? -1 : ((c << 8) | d); }
            case java.lang.Charsets.UTF16LE: { int d = readByte(); return d < 0 ? -1 : ((d << 8) | c); }
            default:
                if (c < 0x80) return c;
                if ((c & 0xe0) == 0xc0) { int d = readByte(); return ((c & 0x1f) << 6) | (d & 0x3f); }
                if ((c & 0xf0) == 0xe0) { int d = readByte(), e = readByte(); return ((c & 0x0f) << 12) | ((d & 0x3f) << 6) | (e & 0x3f); }
                if ((c & 0xf8) == 0xf0) {
                    int d = readByte(), e = readByte(), f = readByte();
                    int cp = (((c & 7) << 18) | ((d & 0x3f) << 12) | ((e & 0x3f) << 6) | (f & 0x3f)) - 0x10000;
                    pending = 0xdc00 | (cp & 0x3ff);
                    return 0xd800 | (cp >> 10);
                }
                return c;
        }
    }
    public int read(char[] cbuf, int off, int len) throws IOException {
        if (len == 0) return 0;
        int n = 0;
        while (n < len) {
            int c = read();
            if (c < 0) break;
            cbuf[off + n++] = (char) c;
            if (in.available() <= 0 && pending < 0 && n > 0) break;
        }
        return n == 0 ? -1 : n;
    }
    public long skip(long n) throws IOException { long i = 0; while (i < n && read() >= 0) i++; return i; }
    public boolean ready() throws IOException { return pending >= 0 || in.available() > 0; }
    public void close() throws IOException { in.close(); }
}
