package pspj2me;

/* Bridge to the C runtime. */
public final class Native {
    public static final int EV_KEY_PRESS = 1, EV_KEY_RELEASE = 2, EV_KEY_REPEAT = 3;
    public static final int EV_PTR_PRESS = 4, EV_PTR_RELEASE = 5, EV_PTR_DRAG = 6;
    public static final int EV_EXIT = 11, EV_PAUSE = 12, EV_RESUME = 13;

    public static native boolean pollEvent(int[] out);
    public static native void waitEvent(int timeoutMs);
    public static native void wakeEventLoop();
    public static native void present(int[] pixels, int w, int h);
    public static native int screenWidth();
    public static native int screenHeight();
    public static native boolean forceFullScreen();
    public static native void exit();
    public static native String getAppProperty(String key);
    public static native byte[] rmsRead(String name);
    public static native boolean rmsWrite(String name, byte[] data);
    public static native boolean rmsDelete(String name);
    public static native String[] rmsList();
    public static native String editText(String title, String text, int maxSize, int constraints);
    public static native void vibrate(int ms);
    public static native void setOverlayActive(boolean active);
    public static native int soundPlay(byte[] data, String type, int loops, int volume);
    public static native void soundStop(int handle);
    public static native boolean soundIsPlaying(int handle);
    public static native void soundVolume(int handle, int volume);
    public static native void tone(int note, int duration, int volume);
}
