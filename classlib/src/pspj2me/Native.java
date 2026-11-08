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
    /** phone key-code profile of the running game (1 Nokia, 2 Siemens, 3 Motorola, 4 LG) */
    public static native int keyProfile();
    public static native void exit();
    public static native String getAppProperty(String key);
    public static native byte[] rmsRead(String name);
    public static native boolean rmsWrite(String name, byte[] data);
    public static native boolean rmsDelete(String name);
    public static native String[] rmsList();
    public static native String editText(String title, String text, int maxSize, int constraints);
    public static native void vibrate(int ms);
    public static native void setOverlayActive(boolean active);
    /* sound: handles of the native mixer (src/audio/mixer.h); times in microseconds */
    public static native int soundLoad(byte[] data, int kind);   /* kind 0: by content, 1: Nokia ringtone */
    public static native int soundBeep(int freqHz, int durationMs);
    public static native int soundMidiDevice();
    public static native void soundStart(int h);
    public static native void soundPause(int h);
    public static native void soundFree(int h);
    public static native int soundState(int h);                  /* -1 evicted; bit 0 playing, 1 stopped at time, 2 at end */
    public static native void soundLoops(int h, int loops);
    public static native int soundLoopsDone(int h);
    public static native long soundTime(int h);
    public static native long soundDuration(int h);
    public static native long soundSeek(int h, long us);
    public static native void soundStopAt(int h, long us);
    public static native void soundVolume(int h, int volume);
    public static native void soundRate(int h, int milli);
    public static native void soundPitch(int h, int milliSemitones);
    public static native void soundMidi(int h, int status, int d1, int d2);
    public static native int soundMidiQuery(int h, int what, int channel);
    public static native void tone(int note, int duration, int volume);
    /** phase: 0 = while running, 1 = creating the MIDlet, 2 = in startApp */
    public static native void reportProblem(Throwable t, int phase);
    /** an exception thrown by game code that the system caught (paint, key handler, ...) */
    public static void gameError(Throwable t) { t.printStackTrace(); reportProblem(t, 0); }
}
