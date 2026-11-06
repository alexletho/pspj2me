package java.lang;

public class Throwable {
    private String detailMessage;
    private Object backtrace;

    public Throwable() { fillInStackTrace0(); }
    public Throwable(String message) { detailMessage = message; fillInStackTrace0(); }
    public Throwable(String message, Throwable cause) { this(message); }
    public Throwable(Throwable cause) { this(cause == null ? null : cause.toString()); }

    private native void fillInStackTrace0();
    public Throwable fillInStackTrace() { return this; }

    public String getMessage() { return detailMessage; }
    public String getLocalizedMessage() { return detailMessage; }
    public Throwable getCause() { return null; }

    public String toString() {
        String n = getClass().getName();
        return detailMessage != null ? n + ": " + detailMessage : n;
    }
    public native void printStackTrace();
}
