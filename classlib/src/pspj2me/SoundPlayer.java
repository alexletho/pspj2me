package pspj2me;

import java.util.Vector;
import javax.microedition.media.*;
import javax.microedition.media.control.*;

/* MMAPI Player on top of the native mixer (src/audio/mixer.c).
 *
 * The native side keeps the decoded sound and its position. An idle sound can be evicted
 * there when memory runs short; this class then loads it again from `data` and restores
 * its position, so games never notice. Formats the mixer can't play are "played" silently
 * for about a second per loop, so games that wait for END_OF_MEDIA keep going. */
public final class SoundPlayer implements Player, VolumeControl, ToneControl, StopTimeControl, TempoControl,
        PitchControl, MIDIControl, MetaDataControl {
    private static final int K_SAMPLED = 0, K_MIDI = 1, K_TONE = 2, K_MIDI_DEVICE = 3, K_BEEP = 4, K_NOKIA = 5;
    private static final Vector playing = new Vector();
    private static Thread watcher;

    private byte[] data;
    private final String type;
    private final int kind;
    private int beepFreq, beepMs;
    private int state = UNREALIZED;
    private int handle;
    private int loops = 1;
    private boolean loopsPending = true;
    private int loopsSeen;
    private int level = 100;
    private boolean muted;
    private int rate = 100000;
    private int pitch;
    private long stopTime = StopTimeControl.RESET;
    private long savedTime;                 /* media time kept across native eviction */
    private long fakeStart, fakeEnd;        /* unsupported format: silent stand-in */
    private final Vector listeners = new Vector();

    public SoundPlayer(byte[] data, String type) {
        this.data = data;
        this.type = type == null ? "application/octet-stream" : type;
        this.kind = kindOf(data, this.type);
    }

    private SoundPlayer(byte[] data, String type, int kind) {
        this.data = data;
        this.type = type;
        this.kind = kind;
    }

    /** "device://midi": a synthesizer driven by MIDIControl */
    public static SoundPlayer midiDevice() { return new SoundPlayer(null, "audio/midi", K_MIDI_DEVICE); }
    /** "device://tone": plays the sequence given to ToneControl.setSequence */
    public static SoundPlayer toneDevice() { return new SoundPlayer(null, "audio/x-tone-seq", K_TONE); }
    /** Nokia Smart Messaging ringtone (com.nokia.mid.sound.Sound.FORMAT_TONE) */
    public static SoundPlayer nokiaTone(byte[] data) { return new SoundPlayer(data, "audio/x-nokia-ota", K_NOKIA); }

    private SoundPlayer(int freq, int ms) {
        type = "audio/x-tone-seq";
        kind = K_BEEP;
        beepFreq = freq;
        beepMs = ms;
    }

    /** a beep of freq Hz (Nokia Sound(freq, duration)) */
    public static SoundPlayer beep(int freq, int ms) { return new SoundPlayer(freq, ms); }

    private static int kindOf(byte[] d, String type) {
        if (d == null) return type.equals("audio/x-tone-seq") ? K_TONE : K_SAMPLED;
        if (d.length >= 4 && d[0] == 'M' && d[1] == 'T' && d[2] == 'h' && d[3] == 'd') return K_MIDI;
        if (d.length >= 2 && d[0] == -2 && d[1] == 1) return K_TONE;
        return K_SAMPLED;
    }

    private void checkClosed() { if (state == CLOSED) throw new IllegalStateException("Player is closed"); }

    /* the native sound for this player, loading it (again) when needed; false if unsupported */
    private boolean ensureLoaded() {
        if (handle > 0 && Native.soundState(handle) >= 0) return true;
        if (handle == -1) return false;   /* known unsupported */
        int h;
        switch (kind) {
            case K_MIDI_DEVICE: h = Native.soundMidiDevice(); break;
            case K_BEEP: h = Native.soundBeep(beepFreq, beepMs); break;
            case K_NOKIA: h = data == null ? -1 : Native.soundLoad(data, 1); break;
            default: h = data == null ? -1 : Native.soundLoad(data, 0);
        }
        if (h <= 0) { handle = -1; return false; }
        handle = h;
        Native.soundVolume(h, muted ? 0 : level);
        if (rate != 100000) Native.soundRate(h, rate);
        if (pitch != 0) Native.soundPitch(h, pitch);
        if (savedTime > 0) {
            long d = Native.soundDuration(h);
            if (d > 0 && savedTime >= d) savedTime = 0;   /* was at the end: the next start begins again */
            else Native.soundSeek(h, savedTime);
        }
        loopsPending = true;
        loopsSeen = 0;
        return true;
    }

    public void realize() throws MediaException {
        checkClosed();
        if (state >= REALIZED) return;
        ensureLoaded();
        state = REALIZED;
    }

    public void prefetch() throws MediaException {
        checkClosed();
        realize();
        if (state < PREFETCHED) state = PREFETCHED;
    }

    public void start() throws MediaException {
        checkClosed();
        prefetch();
        if (state == STARTED) return;
        if (ensureLoaded()) {
            int st = Native.soundState(handle);
            if (loopsPending || (st & 4) != 0) { Native.soundLoops(handle, loops); loopsPending = false; }
            if (stopTime != StopTimeControl.RESET) Native.soundStopAt(handle, stopTime);
            Native.soundStart(handle);
            loopsSeen = Native.soundLoopsDone(handle);
        } else {
            fakeStart = System.currentTimeMillis();
            fakeEnd = loops == -1 ? Long.MAX_VALUE : fakeStart + 1000L * loops;
        }
        state = STARTED;
        fire(PlayerListener.STARTED, new Long(getMediaTime()));
        synchronized (playing) {
            if (!playing.contains(this)) playing.addElement(this);
            if (watcher == null) {
                watcher = new Thread(new Runnable() { public void run() { watch(); } }, "MediaWatcher");
                watcher.start();
            }
            playing.notify();
        }
    }

    /* posts END_OF_MEDIA / STOPPED_AT_TIME for players that reached the end or their stop time */
    private static void watch() {
        Vector events = new Vector();
        while (true) {
            synchronized (playing) {
                while (playing.isEmpty()) {
                    try { playing.wait(); } catch (InterruptedException e) {}
                }
                long now = System.currentTimeMillis();
                for (int i = playing.size() - 1; i >= 0; i--) {
                    SoundPlayer p = (SoundPlayer) playing.elementAt(i);
                    if (p.state != STARTED) { playing.removeElementAt(i); continue; }
                    if (p.handle > 0) {
                        int st = Native.soundState(p.handle);
                        int done = st < 0 ? p.loopsSeen + 1 : Native.soundLoopsDone(p.handle);
                        boolean stopped = st < 0 || (st & 1) == 0;
                        if (stopped) { p.state = PREFETCHED; playing.removeElementAt(i); }
                        while (p.loopsSeen < done) { p.loopsSeen++; events.addElement(p); events.addElement(PlayerListener.END_OF_MEDIA); }
                        if (stopped && st >= 0 && (st & 2) != 0) { events.addElement(p); events.addElement(PlayerListener.STOPPED_AT_TIME); }
                    } else if (now >= p.fakeEnd) {
                        p.state = PREFETCHED;
                        playing.removeElementAt(i);
                        events.addElement(p);
                        events.addElement(PlayerListener.END_OF_MEDIA);
                    }
                }
            }
            for (int i = 0; i + 1 < events.size(); i += 2) {
                SoundPlayer p = (SoundPlayer) events.elementAt(i);
                p.fire((String) events.elementAt(i + 1), new Long(p.getMediaTime0()));
            }
            events.removeAllElements();
            try { Thread.sleep(40); } catch (InterruptedException e) {}
        }
    }

    public void stop() throws MediaException {
        checkClosed();
        if (state != STARTED) return;
        if (handle > 0) {
            Native.soundPause(handle);
            savedTime = Native.soundTime(handle);
        }
        state = PREFETCHED;
        synchronized (playing) { playing.removeElement(this); }
        fire(PlayerListener.STOPPED, new Long(getMediaTime0()));
    }

    public void deallocate() {
        checkClosed();
        if (state == STARTED) { try { stop(); } catch (MediaException e) {} }
        if (handle > 0) { savedTime = Native.soundTime(handle); Native.soundFree(handle); handle = 0; }
        if (state > REALIZED) state = REALIZED;
    }

    public void close() {
        if (state == CLOSED) return;
        deallocate();
        state = CLOSED;
        data = null;
        fire(PlayerListener.CLOSED, null);
    }

    public void setTimeBase(TimeBase master) throws MediaException {
        checkClosed();
        if (master != null && master != Manager.getSystemTimeBase()) throw new MediaException("only the system time base is supported");
    }
    public TimeBase getTimeBase() { checkClosed(); return Manager.getSystemTimeBase(); }

    public long setMediaTime(long now) throws MediaException {
        checkClosed();
        if (now < 0) now = 0;
        if (handle > 0 && Native.soundState(handle) >= 0) savedTime = Native.soundSeek(handle, now);
        else savedTime = now;
        return savedTime;
    }

    public long getMediaTime() { checkClosed(); return getMediaTime0(); }
    private long getMediaTime0() {
        if (handle > 0 && Native.soundState(handle) >= 0) return Native.soundTime(handle);
        if (handle == -1 && state == STARTED) return (System.currentTimeMillis() - fakeStart) * 1000L;
        return savedTime;
    }

    public int getState() { return state; }

    public long getDuration() {
        checkClosed();
        if (handle > 0 && Native.soundState(handle) >= 0) return Native.soundDuration(handle);
        return TIME_UNKNOWN;
    }

    public String getContentType() { checkClosed(); return type; }

    public void setLoopCount(int count) {
        checkClosed();
        if (count == 0) throw new IllegalArgumentException("loop count 0");
        if (state == STARTED) throw new IllegalStateException("Player is started");
        loops = count < 0 ? -1 : count;
        loopsPending = true;
    }

    public void addPlayerListener(PlayerListener l) { checkClosed(); if (l != null && !listeners.contains(l)) listeners.addElement(l); }
    public void removePlayerListener(PlayerListener l) { checkClosed(); listeners.removeElement(l); }

    private void fire(String ev, Object d) {
        for (int i = 0; i < listeners.size(); i++) {
            try { ((PlayerListener) listeners.elementAt(i)).playerUpdate(this, ev, d); } catch (Throwable t) { t.printStackTrace(); }
        }
    }

    /* ---------------- controls ---------------- */
    private boolean isMidi() { return kind == K_MIDI || kind == K_MIDI_DEVICE; }
    private boolean isSynth() { return isMidi() || kind == K_TONE || kind == K_NOKIA; }

    public Control[] getControls() {
        checkClosed();
        Vector v = new Vector();
        String[] names = { "VolumeControl", "StopTimeControl", "RateControl", "TempoControl", "PitchControl", "MIDIControl", "ToneControl", "MetaDataControl" };
        for (int i = 0; i < names.length; i++) if (getControl(names[i]) != null) v.addElement(this);
        Control[] r = new Control[v.size()];
        for (int i = 0; i < r.length; i++) r[i] = this;
        return r;
    }

    public Control getControl(String t) {
        checkClosed();
        if (t == null) throw new IllegalArgumentException();
        String n = t.startsWith("javax.microedition.media.control.") ? t.substring(33) : t;
        if (n.equals("VolumeControl") || n.equals("StopTimeControl") || n.equals("RateControl") || n.equals("MetaDataControl")) return this;
        if (n.equals("TempoControl") || n.equals("PitchControl")) return isSynth() ? this : null;
        if (n.equals("MIDIControl")) return isMidi() ? this : null;
        if (n.equals("ToneControl")) return kind == K_TONE ? this : null;
        return null;
    }

    /* VolumeControl */
    public void setMute(boolean m) {
        if (muted == m) return;
        muted = m;
        if (handle > 0) Native.soundVolume(handle, m ? 0 : level);
        fire(PlayerListener.VOLUME_CHANGED, this);
    }
    public boolean isMuted() { return muted; }
    public int setLevel(int l) {
        l = Math.max(0, Math.min(100, l));
        if (l != level) {
            level = l;
            if (handle > 0 && !muted) Native.soundVolume(handle, level);
            fire(PlayerListener.VOLUME_CHANGED, this);
        }
        return level;
    }
    public int getLevel() { return level; }

    /* ToneControl */
    public void setSequence(byte[] seq) {
        if (state >= PREFETCHED) throw new IllegalStateException("Player is prefetched");
        if (seq == null || seq.length < 2 || seq[0] != ToneControl.VERSION) throw new IllegalArgumentException("not a tone sequence");
        data = seq;
        if (handle > 0) Native.soundFree(handle);
        handle = 0;
    }

    /* StopTimeControl */
    public void setStopTime(long t) {
        if (state == STARTED && stopTime != StopTimeControl.RESET && t != StopTimeControl.RESET) throw new IllegalStateException("stop time already set");
        stopTime = t;
        if (handle > 0) Native.soundStopAt(handle, t == StopTimeControl.RESET ? -1 : t);
    }
    public long getStopTime() { return stopTime; }

    /* RateControl / TempoControl (tempo relative to 120 beats per minute) */
    public int setRate(int millirate) {
        rate = Math.max(getMinRate(), Math.min(getMaxRate(), millirate));
        if (handle > 0) Native.soundRate(handle, rate);
        return rate;
    }
    public int getRate() { return rate; }
    public int getMaxRate() { return 400000; }
    public int getMinRate() { return 25000; }
    public int setTempo(int millitempo) { return setRate((int) ((long) millitempo * 100000 / 120000)) * 120000 / 100000; }
    public int getTempo() { return (int) ((long) rate * 120000 / 100000); }

    /* PitchControl (MIDI and tones) */
    public int setPitch(int millisemitones) {
        pitch = Math.max(getMinPitch(), Math.min(getMaxPitch(), millisemitones));
        if (handle > 0) Native.soundPitch(handle, pitch);
        return pitch;
    }
    public int getPitch() { return pitch; }
    public int getMaxPitch() { return 24000; }
    public int getMinPitch() { return -24000; }

    /* MIDIControl */
    private void midiReady() {
        if (state < PREFETCHED) throw new IllegalStateException("Player is not prefetched");
        if (!ensureLoaded()) throw new IllegalStateException("no MIDI synthesizer");
    }
    public boolean isBankQuerySupported() { return false; }
    public int[] getProgram(int channel) throws MediaException {
        if (channel < 0 || channel > 15) throw new IllegalArgumentException("channel");
        midiReady();
        return new int[] { 0, Native.soundMidiQuery(handle, 0, channel) };
    }
    public int getChannelVolume(int channel) {
        if (channel < 0 || channel > 15) throw new IllegalArgumentException("channel");
        midiReady();
        return Native.soundMidiQuery(handle, 1, channel);
    }
    public void setProgram(int channel, int bank, int program) {
        if (channel < 0 || channel > 15 || program < 0 || program > 127 || bank < -1 || bank > 16383) throw new IllegalArgumentException();
        midiReady();
        Native.soundMidi(handle, 0xC0 | channel, program, 0);
    }
    public void setChannelVolume(int channel, int volume) {
        if (channel < 0 || channel > 15 || volume < 0 || volume > 127) throw new IllegalArgumentException();
        midiReady();
        Native.soundMidi(handle, 0xB0 | channel, 7, volume);
    }
    public int[] getBankList(boolean custom) throws MediaException { throw new MediaException("bank query not supported"); }
    public int[] getProgramList(int bank) throws MediaException { throw new MediaException("bank query not supported"); }
    public String getProgramName(int bank, int prog) throws MediaException { throw new MediaException("bank query not supported"); }
    public String getKeyName(int bank, int prog, int key) throws MediaException { throw new MediaException("bank query not supported"); }
    public void shortMidiEvent(int type, int data1, int data2) {
        if (type < 0x80 || type > 0xFF || type == 0xF0 || type == 0xF7 || data1 < 0 || data1 > 127 || data2 < 0 || data2 > 127)
            throw new IllegalArgumentException("bad MIDI event");
        midiReady();
        if (type < 0xF0) Native.soundMidi(handle, type, data1, data2);
    }
    public int longMidiEvent(byte[] d, int offset, int length) {
        if (d == null || offset < 0 || length < 0 || offset + length > d.length) throw new IllegalArgumentException();
        midiReady();
        int i = offset, end = offset + length, status = 0;
        while (i < end) {
            int b = d[i] & 0xff;
            if (b == 0xF0) { while (i < end && (d[i] & 0xff) != 0xF7) i++; i++; continue; }   /* SysEx: ignored */
            if (b >= 0x80) { status = b; i++; }
            if (status == 0) { i++; continue; }
            int hi = status & 0xF0, n = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
            if (i + n > end) break;
            int d1 = d[i] & 0x7f, d2 = n == 2 ? d[i + 1] & 0x7f : 0;
            if (status < 0xF0) Native.soundMidi(handle, status, d1, d2);
            i += n;
        }
        return length;
    }

    /* MetaDataControl: no metadata */
    public String[] getKeys() { return new String[0]; }
    public String getKeyValue(String key) {
        if (key == null) throw new IllegalArgumentException();
        return null;
    }
}
