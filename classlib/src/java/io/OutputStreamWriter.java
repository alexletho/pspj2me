package java.io;
public class OutputStreamWriter extends Writer {
    private OutputStream out;
    private int enc;
    public OutputStreamWriter(OutputStream os) { out = os; enc = java.lang.Charsets.DEFAULT; }
    public OutputStreamWriter(OutputStream os, String encName) throws UnsupportedEncodingException {
        out = os; enc = java.lang.Charsets.lookup(encName);
    }
    public void write(char[] cbuf, int off, int len) throws IOException { out.write(java.lang.Charsets.encode(cbuf, off, len, enc)); }
    public void flush() throws IOException { out.flush(); }
    public void close() throws IOException { out.close(); }
}
