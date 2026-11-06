package java.io;
public class ByteArrayOutputStream extends OutputStream {
    protected byte[] buf;
    protected int count;
    public ByteArrayOutputStream() { this(32); }
    public ByteArrayOutputStream(int size) {
        if (size < 0) throw new IllegalArgumentException();
        buf = new byte[size];
    }
    private void grow(int n) {
        if (n > buf.length) {
            byte[] nb = new byte[Math.max(buf.length << 1, n)];
            System.arraycopy(buf, 0, nb, 0, count);
            buf = nb;
        }
    }
    public synchronized void write(int b) { grow(count + 1); buf[count++] = (byte) b; }
    public synchronized void write(byte[] b, int off, int len) {
        if (off < 0 || len < 0 || off + len > b.length) throw new IndexOutOfBoundsException();
        grow(count + len);
        System.arraycopy(b, off, buf, count, len);
        count += len;
    }
    public synchronized void writeTo(OutputStream out) throws IOException { out.write(buf, 0, count); }
    public synchronized void reset() { count = 0; }
    public synchronized byte[] toByteArray() {
        byte[] r = new byte[count];
        System.arraycopy(buf, 0, r, 0, count);
        return r;
    }
    public int size() { return count; }
    public String toString() { return new String(buf, 0, count); }
    public void close() throws IOException {}
}
