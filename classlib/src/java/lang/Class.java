package java.lang;

import java.io.InputStream;
import java.io.ByteArrayInputStream;

public final class Class {
    private int vmClass;
    private Class() {}

    public static native Class forName(String className) throws ClassNotFoundException;
    public native Object newInstance() throws InstantiationException, IllegalAccessException;
    public native boolean isInstance(Object obj);
    public native boolean isAssignableFrom(Class cls);
    public native boolean isInterface();
    public native boolean isArray();
    public native String getName();
    public native Class getSuperclass();

    public String toString() { return (isInterface() ? "interface " : "class ") + getName(); }

    private static native byte[] getResourceBytes(String absName);

    public InputStream getResourceAsStream(String name) {
        if (name == null) return null;
        String abs;
        if (name.startsWith("/")) abs = name.substring(1);
        else {
            String cn = getName();
            int i = cn.lastIndexOf('.');
            abs = (i >= 0) ? cn.substring(0, i).replace('.', '/') + "/" + name : name;
        }
        byte[] b = getResourceBytes(abs);
        if (b == null) return null;
        return new ByteArrayInputStream(b);
    }
}
