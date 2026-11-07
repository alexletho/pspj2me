package com.nokia.mid.sound;

import javax.microedition.media.MediaException;
import javax.microedition.media.Player;
import javax.microedition.media.PlayerListener;
import pspj2me.SoundPlayer;

/* Nokia UI sound: WAV, Smart Messaging ringtones (FORMAT_TONE) and plain beeps, played
 * through the same player as MMAPI. */
public class Sound {
    public static final int FORMAT_TONE = 1, FORMAT_WAV = 5, SOUND_PLAYING = 0, SOUND_STOPPED = 1, SOUND_UNINITIALIZED = 3;
    private int state = SOUND_UNINITIALIZED;
    private int gain = 255;
    private SoundPlayer player;
    private SoundListener listener;

    public Sound(byte[] data, int type) { init(data, type); }
    public Sound(int freq, long duration) { init(freq, duration); }

    public void init(byte[] data, int type) {
        if (data == null) throw new NullPointerException();
        if (type != FORMAT_TONE && type != FORMAT_WAV) throw new IllegalArgumentException("unsupported format " + type);
        setPlayer(type == FORMAT_WAV ? new SoundPlayer(data, "audio/x-wav") : SoundPlayer.nokiaTone(data));
    }

    public void init(int freq, long duration) {
        if (freq < 0 || duration <= 0) throw new IllegalArgumentException();
        SoundPlayer p = SoundPlayer.beep(freq == 0 ? 440 : freq, (int) Math.min(duration, 60000));
        if (freq == 0) p.setLevel(0);   /* frequency 0 = silence */
        setPlayer(p);
    }

    private void setPlayer(SoundPlayer p) {
        if (player != null) player.close();
        player = p;
        player.setLevel(gain * 100 / 255);
        player.addPlayerListener(new PlayerListener() {
            public void playerUpdate(Player pl, String event, Object data) {
                if (pl == player && END_OF_MEDIA.equals(event) && pl.getState() != Player.STARTED) changed(SOUND_STOPPED);
            }
        });
        state = SOUND_STOPPED;
    }

    private void changed(int s) {
        state = s;
        if (listener != null) {
            try { listener.soundStateChanged(this, s); } catch (Throwable t) { t.printStackTrace(); }
        }
    }

    public static int getConcurrentSoundCount(int type) { return 1; }
    public static int[] getSupportedFormats() { return new int[] { FORMAT_TONE, FORMAT_WAV }; }

    /** loop: 0 = forever, 1..255 = number of times */
    public void play(int loop) {
        if (loop < 0) throw new IllegalArgumentException();
        if (player == null) return;
        try {
            player.stop();
            player.setMediaTime(0);
            player.setLoopCount(loop == 0 ? -1 : loop);
            player.start();
            changed(SOUND_PLAYING);
        } catch (MediaException e) {
            changed(SOUND_STOPPED);
        }
    }

    public void stop() {
        if (player == null) return;
        try { player.stop(); } catch (MediaException e) {}
        if (state == SOUND_PLAYING) changed(SOUND_STOPPED);
    }

    public void resume() {
        if (player == null) return;
        try { player.start(); changed(SOUND_PLAYING); } catch (MediaException e) {}
    }

    public void release() {
        if (player != null) player.close();
        player = null;
        changed(SOUND_UNINITIALIZED);
    }

    public int getState() {
        if (state == SOUND_PLAYING && player != null && player.getState() != Player.STARTED) state = SOUND_STOPPED;
        return state;
    }

    public void setGain(int g) {
        gain = Math.max(0, Math.min(255, g));
        if (player != null) player.setLevel(gain * 100 / 255);
    }
    public int getGain() { return gain; }
    public void setSoundListener(SoundListener l) { listener = l; }
}
