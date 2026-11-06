package pspj2me;

import java.io.OutputStream;

public final class LogStream extends OutputStream {
    public native void write(int b);
    public native void write(byte[] b, int off, int len);
}
