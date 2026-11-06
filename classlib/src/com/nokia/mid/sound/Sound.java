package com.nokia.mid.sound;

public class Sound {
    public static final int FORMAT_TONE = 1, FORMAT_WAV = 5, SOUND_PLAYING = 0, SOUND_STOPPED = 1, SOUND_UNINITIALIZED = 3;
    private int state = SOUND_STOPPED;
    private int gain = 255;
    private byte[] data;
    private int type;
    private SoundListener listener;
    private int handle;

    public Sound(byte[] data, int type) { init(data, type); }
    public Sound(int freq, long duration) { init(freq, duration); }
    public void init(byte[] data, int type) { this.data = data; this.type = type; state = SOUND_STOPPED; }
    public void init(int freq, long duration) { data = null; state = SOUND_STOPPED; }
    public static int getConcurrentSoundCount(int type) { return 1; }
    public static int[] getSupportedFormats() { return new int[] { FORMAT_TONE, FORMAT_WAV }; }
    public void play(int loop) {
        if (data != null && type == FORMAT_WAV) handle = pspj2me.Native.soundPlay(data, "audio/x-wav", loop == 0 ? -1 : loop, gain * 100 / 255);
        state = SOUND_PLAYING;
        if (listener != null) listener.soundStateChanged(this, SOUND_PLAYING);
    }
    public void stop() {
        if (handle > 0) pspj2me.Native.soundStop(handle);
        handle = 0;
        state = SOUND_STOPPED;
        if (listener != null) listener.soundStateChanged(this, SOUND_STOPPED);
    }
    public void resume() { play(1); }
    public void release() { stop(); data = null; state = SOUND_UNINITIALIZED; }
    public int getState() { if (state == SOUND_PLAYING && handle > 0 && !pspj2me.Native.soundIsPlaying(handle)) state = SOUND_STOPPED; return state; }
    public void setGain(int g) { gain = Math.max(0, Math.min(255, g)); }
    public int getGain() { return gain; }
    public void setSoundListener(SoundListener l) { listener = l; }
}
