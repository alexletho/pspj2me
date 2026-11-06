package pspj2me;

import java.util.Vector;
import javax.microedition.media.*;
import javax.microedition.media.control.*;

/* Player implementation; audio output is delegated to the native layer, which may or may not support the format. */
public final class SoundPlayer implements Player, VolumeControl, ToneControl {
    private static final Vector playing = new Vector();
    private static Thread watcher;

    private byte[] data;
    private final String type;
    private int state = UNREALIZED;
    private int loops = 1;
    private int level = 100;
    private boolean muted;
    private int handle;
    private long startedAt;
    private long fakeEnd;
    private final Vector listeners = new Vector();

    public SoundPlayer(byte[] data, String type) {
        this.data = data;
        this.type = type == null ? "application/octet-stream" : type;
    }

    private void checkClosed() { if (state == CLOSED) throw new IllegalStateException("closed"); }

    public void realize() throws MediaException { checkClosed(); if (state < REALIZED) state = REALIZED; }
    public void prefetch() throws MediaException { checkClosed(); realize(); if (state < PREFETCHED) state = PREFETCHED; }

    public void start() throws MediaException {
        checkClosed();
        prefetch();
        if (state == STARTED) return;
        state = STARTED;
        startedAt = System.currentTimeMillis();
        handle = data == null ? -1 : Native.soundPlay(data, type, loops, muted ? 0 : level);
        fakeEnd = handle <= 0 ? (loops == -1 ? Long.MAX_VALUE : startedAt + 1000L * Math.max(1, loops)) : 0;
        fire(PlayerListener.STARTED, new Long(0));
        synchronized (playing) {
            playing.addElement(this);
            if (watcher == null) {
                watcher = new Thread(new Runnable() { public void run() { watch(); } }, "MediaWatcher");
                watcher.start();
            }
            playing.notify();
        }
    }

    private static void watch() {
        while (true) {
            SoundPlayer done = null;
            synchronized (playing) {
                while (playing.isEmpty()) {
                    try { playing.wait(); } catch (InterruptedException e) {}
                }
                long now = System.currentTimeMillis();
                for (int i = 0; i < playing.size(); i++) {
                    SoundPlayer p = (SoundPlayer) playing.elementAt(i);
                    boolean finished = p.handle > 0 ? !Native.soundIsPlaying(p.handle) : now >= p.fakeEnd;
                    if (finished) { done = p; playing.removeElementAt(i); break; }
                }
            }
            if (done != null) {
                if (done.state == STARTED) {
                    done.state = PREFETCHED;
                    done.handle = 0;
                    done.fire(PlayerListener.END_OF_MEDIA, new Long(done.getMediaTime()));
                }
                continue;
            }
            try { Thread.sleep(50); } catch (InterruptedException e) {}
        }
    }

    public void stop() throws MediaException {
        checkClosed();
        if (state != STARTED) return;
        if (handle > 0) Native.soundStop(handle);
        handle = 0;
        state = PREFETCHED;
        synchronized (playing) { playing.removeElement(this); }
        fire(PlayerListener.STOPPED, new Long(getMediaTime()));
    }

    public void deallocate() {
        if (state == STARTED) { try { stop(); } catch (MediaException e) {} }
        if (state > REALIZED) state = REALIZED;
    }

    public void close() {
        if (state == CLOSED) return;
        deallocate();
        state = CLOSED;
        data = null;
        fire(PlayerListener.CLOSED, null);
    }

    public void setTimeBase(TimeBase master) throws MediaException {}
    public TimeBase getTimeBase() { return Manager.getSystemTimeBase(); }
    public long setMediaTime(long now) throws MediaException { return 0; }
    public long getMediaTime() { return state == STARTED ? (System.currentTimeMillis() - startedAt) * 1000L : 0; }
    public int getState() { return state; }
    public long getDuration() { return TIME_UNKNOWN; }
    public String getContentType() { return type; }
    public void setLoopCount(int count) {
        if (count == 0) throw new IllegalArgumentException();
        loops = count;
    }
    public void addPlayerListener(PlayerListener l) { if (l != null && !listeners.contains(l)) listeners.addElement(l); }
    public void removePlayerListener(PlayerListener l) { listeners.removeElement(l); }

    private void fire(String ev, Object d) {
        for (int i = 0; i < listeners.size(); i++) {
            try { ((PlayerListener) listeners.elementAt(i)).playerUpdate(this, ev, d); } catch (Throwable t) { t.printStackTrace(); }
        }
    }

    public Control[] getControls() { return new Control[] { this }; }
    public Control getControl(String t) {
        if (t.endsWith("VolumeControl") || t.endsWith("ToneControl")) return this;
        return null;
    }

    public void setMute(boolean m) { muted = m; if (handle > 0) Native.soundVolume(handle, m ? 0 : level); }
    public boolean isMuted() { return muted; }
    public int setLevel(int l) {
        level = Math.max(0, Math.min(100, l));
        if (handle > 0 && !muted) Native.soundVolume(handle, level);
        fire(PlayerListener.VOLUME_CHANGED, this);
        return level;
    }
    public int getLevel() { return level; }
    public void setSequence(byte[] seq) { data = seq; }
}
