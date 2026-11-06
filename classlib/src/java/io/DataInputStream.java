package java.io;
public class DataInputStream extends InputStream implements DataInput {
    protected InputStream in;
    public DataInputStream(InputStream in) { this.in = in; }
    public int read() throws IOException { return in.read(); }
    public final int read(byte[] b) throws IOException { return in.read(b, 0, b.length); }
    public final int read(byte[] b, int off, int len) throws IOException { return in.read(b, off, len); }
    public final void readFully(byte[] b) throws IOException { readFully(b, 0, b.length); }
    public final void readFully(byte[] b, int off, int len) throws IOException {
        if (len < 0) throw new IndexOutOfBoundsException();
        int n = 0;
        while (n < len) {
            int c = in.read(b, off + n, len - n);
            if (c < 0) throw new EOFException();
            n += c;
        }
    }
    public final int skipBytes(int n) throws IOException {
        int total = 0, cur;
        while (total < n && (cur = (int) in.skip(n - total)) > 0) total += cur;
        while (total < n && in.read() >= 0) total++;
        return total;
    }
    public final boolean readBoolean() throws IOException { return readUnsignedByte() != 0; }
    public final byte readByte() throws IOException { return (byte) readUnsignedByte(); }
    public final int readUnsignedByte() throws IOException {
        int c = in.read();
        if (c < 0) throw new EOFException();
        return c;
    }
    public final short readShort() throws IOException { return (short) readUnsignedShort(); }
    public final int readUnsignedShort() throws IOException {
        int a = in.read(), b = in.read();
        if ((a | b) < 0) throw new EOFException();
        return (a << 8) | b;
    }
    public final char readChar() throws IOException { return (char) readUnsignedShort(); }
    public final int readInt() throws IOException {
        int a = in.read(), b = in.read(), c = in.read(), d = in.read();
        if ((a | b | c | d) < 0) throw new EOFException();
        return (a << 24) | (b << 16) | (c << 8) | d;
    }
    public final long readLong() throws IOException { return ((long) readInt() << 32) | (readInt() & 0xffffffffL); }
    public final float readFloat() throws IOException { return Float.intBitsToFloat(readInt()); }
    public final double readDouble() throws IOException { return Double.longBitsToDouble(readLong()); }
    public final String readUTF() throws IOException { return readUTF(this); }
    public static final String readUTF(DataInput in) throws IOException {
        int len = in.readUnsignedShort();
        byte[] b = new byte[len];
        in.readFully(b, 0, len);
        char[] out = new char[len];
        int n = 0, i = 0;
        while (i < len) {
            int c = b[i] & 0xff;
            if (c < 0x80) { out[n++] = (char) c; i++; }
            else if ((c & 0xe0) == 0xc0) {
                if (i + 1 >= len) throw new UTFDataFormatException();
                out[n++] = (char) (((c & 0x1f) << 6) | (b[i + 1] & 0x3f)); i += 2;
            } else if ((c & 0xf0) == 0xe0) {
                if (i + 2 >= len) throw new UTFDataFormatException();
                out[n++] = (char) (((c & 0x0f) << 12) | ((b[i + 1] & 0x3f) << 6) | (b[i + 2] & 0x3f)); i += 3;
            } else throw new UTFDataFormatException();
        }
        return new String(out, 0, n);
    }
    public long skip(long n) throws IOException { return in.skip(n); }
    public int available() throws IOException { return in.available(); }
    public void close() throws IOException { in.close(); }
    public synchronized void mark(int limit) { in.mark(limit); }
    public synchronized void reset() throws IOException { in.reset(); }
    public boolean markSupported() { return in.markSupported(); }
}
