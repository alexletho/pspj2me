import java.util.Vector;
import javax.microedition.lcdui.*;
import javax.microedition.media.*;
import javax.microedition.media.control.*;
import javax.microedition.midlet.MIDlet;

/* Exercises the MMAPI player and Nokia Sound: pause/resume, media time, duration, seeking,
 * loops and END_OF_MEDIA, stop time, rate/tempo/pitch, MIDIControl, device players,
 * Nokia ringtones and beeps, eviction of idle sounds, unsupported data. Prints "AudioTest: PASS". */
public class AudioTest extends MIDlet implements Runnable {
    private int failures;
    private final Vector events = new Vector();

    protected void startApp() {
        Form f = new Form("AudioTest");
        Display.getDisplay(this).setCurrent(f);
        new Thread(this).start();
    }
    protected void pauseApp() {}
    protected void destroyApp(boolean u) {}

    private void check(boolean ok, String what) {
        if (!ok) { failures++; System.out.println("AudioTest FAIL: " + what); }
        else System.out.println("AudioTest ok: " + what);
    }

    private PlayerListener recorder = new PlayerListener() {
        public void playerUpdate(Player p, String ev, Object d) { synchronized (events) { events.addElement(ev); } }
    };
    private int count(String ev) {
        int n = 0;
        synchronized (events) { for (int i = 0; i < events.size(); i++) if (ev.equals(events.elementAt(i))) n++; }
        return n;
    }
    private static void sleep(int ms) { try { Thread.sleep(ms); } catch (InterruptedException e) {} }
    private Player res(String name, String type) throws Exception {
        return Manager.createPlayer(getClass().getResourceAsStream(name), type);
    }

    public void run() {
        try {
            sampled();
            midi();
            devices();
            nokia();
            eviction();
            unsupported();
        } catch (Throwable t) {
            failures++;
            System.out.println("AudioTest FAIL: exception " + t);
            t.printStackTrace();
        }
        System.out.println(failures == 0 ? "AudioTest: PASS" : "AudioTest: " + failures + " FAILURES");
    }

    private void sampled() throws Exception {
        Player p = res("/half.wav", "audio/x-wav");
        p.addPlayerListener(recorder);
        p.realize();
        long d = p.getDuration();
        check(d > 490000 && d < 510000, "WAV duration " + d);
        check(p.getControl("VolumeControl") != null && p.getControl("MIDIControl") == null, "WAV controls");
        p.start();
        sleep(200);
        p.stop();
        long t1 = p.getMediaTime();
        check(t1 > 100000 && t1 < 400000, "time after 200 ms: " + t1);
        sleep(150);
        check(p.getMediaTime() == t1, "paused time holds");
        p.start();
        sleep(100);
        check(p.getMediaTime() > t1, "resumes from where it stopped");
        sleep(500);
        check(count(PlayerListener.END_OF_MEDIA) == 1 && p.getState() == Player.PREFETCHED, "END_OF_MEDIA, back to PREFETCHED");
        events.removeAllElements();
        p.setLoopCount(3);
        p.setMediaTime(0);
        p.start();
        sleep(1800);
        check(count(PlayerListener.END_OF_MEDIA) == 3, "3 loops -> 3 END_OF_MEDIA (" + count(PlayerListener.END_OF_MEDIA) + ")");
        events.removeAllElements();
        p.setLoopCount(1);
        long s = p.setMediaTime(250000);
        check(s > 240000 && s < 260000, "seek to 250 ms: " + s);
        ((StopTimeControl) p.getControl("StopTimeControl")).setStopTime(350000);
        p.start();
        sleep(300);
        check(count(PlayerListener.STOPPED_AT_TIME) == 1, "STOPPED_AT_TIME");
        long ts = p.getMediaTime();
        check(ts >= 345000 && ts < 380000, "stopped at 350 ms: " + ts);
        RateControl rc = (RateControl) p.getControl("RateControl");
        check(rc.setRate(200000) == 200000 && rc.setRate(1000000) == rc.getMaxRate(), "RateControl");
        p.close();
        check(p.getState() == Player.CLOSED, "closed");
        boolean threw = false;
        try { p.start(); } catch (IllegalStateException e) { threw = true; }
        check(threw, "start after close throws");
    }

    private void midi() throws Exception {
        events.removeAllElements();
        Player p = res("/music.mid", "audio/midi");
        p.addPlayerListener(recorder);
        p.prefetch();
        long d = p.getDuration();
        check(d > 100000, "MIDI duration " + d);
        MIDIControl mc = (MIDIControl) p.getControl("javax.microedition.media.control.MIDIControl");
        check(mc != null && p.getControl("TempoControl") != null && p.getControl("PitchControl") != null, "MIDI controls");
        mc.setProgram(0, -1, 40);
        check(mc.getProgram(0)[1] == 40, "setProgram/getProgram");
        mc.setChannelVolume(2, 77);
        check(mc.getChannelVolume(2) == 77, "channel volume");
        long half = p.setMediaTime(d / 2);
        check(half > d / 2 - 20000 && half < d / 2 + 20000, "MIDI seek to the middle: " + half);
        ((TempoControl) p.getControl("TempoControl")).setTempo(240000);
        ((PitchControl) p.getControl("PitchControl")).setPitch(-12000);
        p.start();
        sleep(300);
        long t = p.getMediaTime();
        check(t > half + 300000, "double tempo runs faster: " + (t - half));
        p.stop();
        mc.shortMidiEvent(0x90, 60, 100);
        boolean threw = false;
        try { mc.shortMidiEvent(0x90, 200, 100); } catch (IllegalArgumentException e) { threw = true; }
        check(threw, "invalid MIDI data rejected");
        p.close();
    }

    private void devices() throws Exception {
        Player m = Manager.createPlayer(Manager.MIDI_DEVICE_LOCATOR);
        m.prefetch();
        MIDIControl mc = (MIDIControl) m.getControl("MIDIControl");
        mc.setProgram(1, -1, 19);
        mc.shortMidiEvent(0x91, 64, 100);
        byte[] seq = { (byte) 0x91, 67, 100, 69, 100, (byte) 0x81, 64, 0 };
        check(mc.longMidiEvent(seq, 0, seq.length) == seq.length, "longMidiEvent with running status");
        m.start();
        sleep(100);
        check(m.getState() == Player.STARTED && mc.getProgram(1)[1] == 19, "MIDI device plays");
        m.close();

        events.removeAllElements();
        Player t = Manager.createPlayer(Manager.TONE_DEVICE_LOCATOR);
        t.addPlayerListener(recorder);
        t.realize();
        ToneControl tc = (ToneControl) t.getControl("ToneControl");
        byte[] tune = { ToneControl.VERSION, 1, ToneControl.TEMPO, 60, 60, 8, 62, 8, 64, 8 };
        tc.setSequence(tune);
        t.start();
        sleep(1200);
        check(count(PlayerListener.END_OF_MEDIA) == 1, "tone sequence plays to the end");
        t.close();
        Manager.playTone(72, 100, 50);
    }

    private int nokiaState = -1;
    private void nokia() {
        byte[] ota = { 2, 74, 116, -128, 8, 0, 30, 49, 36, 80, 77, 5, 80, 65, -122, 8 };
        com.nokia.mid.sound.Sound s = new com.nokia.mid.sound.Sound(ota, com.nokia.mid.sound.Sound.FORMAT_TONE);
        s.setSoundListener(new com.nokia.mid.sound.SoundListener() {
            public void soundStateChanged(com.nokia.mid.sound.Sound snd, int ev) { nokiaState = ev; }
        });
        s.play(1);
        check(s.getState() == com.nokia.mid.sound.Sound.SOUND_PLAYING, "Nokia ringtone plays");
        sleep(3500);
        check(nokiaState == com.nokia.mid.sound.Sound.SOUND_STOPPED && s.getState() == com.nokia.mid.sound.Sound.SOUND_STOPPED,
              "Nokia ringtone ends with SOUND_STOPPED (" + nokiaState + ")");
        com.nokia.mid.sound.Sound b = new com.nokia.mid.sound.Sound(880, 150);
        b.play(1);
        sleep(400);
        check(b.getState() == com.nokia.mid.sound.Sound.SOUND_STOPPED, "Nokia beep");
        s.release();
        b.release();
    }

    private void eviction() throws Exception {
        /* far more players than the mixer keeps loaded at once: the old ones must still work */
        Player[] ps = new Player[70];
        for (int i = 0; i < ps.length; i++) { ps[i] = res("/half.wav", "audio/x-wav"); ps[i].prefetch(); }
        ps[0].setMediaTime(100000);
        for (int i = 1; i < ps.length; i++) { ps[i].start(); ps[i].stop(); }
        long t = ps[0].getMediaTime();
        check(t >= 99000 && t <= 101000, "position survives eviction: " + t);
        events.removeAllElements();
        ps[0].addPlayerListener(recorder);
        ps[0].start();
        sleep(600);
        check(count(PlayerListener.END_OF_MEDIA) == 1, "evicted player plays again");
        for (int i = 0; i < ps.length; i++) ps[i].close();
    }

    private void unsupported() throws Exception {
        events.removeAllElements();
        byte[] junk = new byte[3000];
        for (int i = 0; i < junk.length; i++) junk[i] = (byte) (i * 31);
        Player p = Manager.createPlayer(new java.io.ByteArrayInputStream(junk), "audio/mpeg");
        p.addPlayerListener(recorder);
        p.start();
        sleep(1300);
        check(count(PlayerListener.END_OF_MEDIA) == 1, "unsupported data: silent, but END_OF_MEDIA arrives");
        String[] types = Manager.getSupportedContentTypes(null);
        boolean mp3 = false;
        for (int i = 0; i < types.length; i++) if (types[i].equals("audio/mpeg")) mp3 = true;
        check(!mp3, "MP3 not advertised while it can't play");
    }
}
