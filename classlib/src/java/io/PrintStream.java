package java.io;
public class PrintStream extends OutputStream {
    private OutputStream out;
    private boolean error;
    public PrintStream(OutputStream out) { this.out = out; }
    public void flush() { try { out.flush(); } catch (IOException e) { error = true; } }
    public void close() { try { out.close(); } catch (IOException e) { error = true; } }
    public boolean checkError() { return error; }
    protected void setError() { error = true; }
    public void write(int b) { try { out.write(b); } catch (IOException e) { error = true; } }
    public void write(byte[] buf, int off, int len) { try { out.write(buf, off, len); } catch (IOException e) { error = true; } }
    private void str(String s) { if (s == null) s = "null"; byte[] b = s.getBytes(); write(b, 0, b.length); }
    public void print(boolean b) { str(b ? "true" : "false"); }
    public void print(char c) { str(String.valueOf(c)); }
    public void print(int i) { str(String.valueOf(i)); }
    public void print(long l) { str(String.valueOf(l)); }
    public void print(float f) { str(String.valueOf(f)); }
    public void print(double d) { str(String.valueOf(d)); }
    public void print(char[] s) { str(new String(s)); }
    public void print(String s) { str(s); }
    public void print(Object o) { str(String.valueOf(o)); }
    public void println() { str("\n"); }
    public void println(boolean x) { str((x ? "true" : "false") + "\n"); }
    public void println(char x) { str(String.valueOf(x) + "\n"); }
    public void println(int x) { str(String.valueOf(x) + "\n"); }
    public void println(long x) { str(String.valueOf(x) + "\n"); }
    public void println(float x) { str(String.valueOf(x) + "\n"); }
    public void println(double x) { str(String.valueOf(x) + "\n"); }
    public void println(char[] x) { str(new String(x) + "\n"); }
    public void println(String x) { str(x + "\n"); }
    public void println(Object x) { str(String.valueOf(x) + "\n"); }
}
