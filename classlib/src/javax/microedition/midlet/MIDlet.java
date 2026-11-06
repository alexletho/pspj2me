package javax.microedition.midlet;

import pspj2me.Native;

public abstract class MIDlet {
    protected MIDlet() {}
    protected abstract void startApp() throws MIDletStateChangeException;
    protected abstract void pauseApp();
    protected abstract void destroyApp(boolean unconditional) throws MIDletStateChangeException;

    public final void notifyDestroyed() { Native.exit(); }
    public final void notifyPaused() {}
    public final String getAppProperty(String key) { return Native.getAppProperty(key); }
    public final void resumeRequest() {}
    public final boolean platformRequest(String url) { return false; }
    public final int checkPermission(String permission) { return 1; }

    /* called by the runtime */
    public static void callStartApp(MIDlet m) throws MIDletStateChangeException { m.startApp(); }
    public static void callPauseApp(MIDlet m) { m.pauseApp(); }
    public static void callDestroyApp(MIDlet m, boolean u) throws MIDletStateChangeException { m.destroyApp(u); }
}
