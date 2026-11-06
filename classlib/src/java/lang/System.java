package java.lang;

import java.io.PrintStream;

public final class System {
    public static final PrintStream out = new PrintStream(new pspj2me.LogStream());
    public static final PrintStream err = out;
    private System() {}
    public static native long currentTimeMillis();
    public static native void arraycopy(Object src, int srcPos, Object dst, int dstPos, int length);
    public static native int identityHashCode(Object x);
    public static String getProperty(String key) {
        if (key == null) throw new NullPointerException();
        return getProperty0(key);
    }
    public static String getProperty(String key, String def) {
        String v = getProperty(key);
        return v == null ? def : v;
    }
    private static native String getProperty0(String key);
    public static void exit(int status) { Runtime.getRuntime().exit(status); }
    public static void gc() { Runtime.getRuntime().gc(); }
    public static long nanoTime() { return currentTimeMillis() * 1000000L; }
}
