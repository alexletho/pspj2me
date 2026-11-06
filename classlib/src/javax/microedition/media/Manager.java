package javax.microedition.media;

import java.io.*;
import pspj2me.SoundPlayer;

public final class Manager {
    public static final String TONE_DEVICE_LOCATOR = "device://tone";
    public static final String MIDI_DEVICE_LOCATOR = "device://midi";
    private static final TimeBase SYSTEM_TB = new TimeBase() { public long getTime() { return System.currentTimeMillis() * 1000L; } };
    private Manager() {}

    public static String[] getSupportedContentTypes(String protocol) {
        return new String[] { "audio/x-wav", "audio/midi", "audio/mpeg", "audio/x-tone-seq", "audio/amr" };
    }
    public static String[] getSupportedProtocols(String contentType) { return new String[] { "device", "resource" }; }

    public static Player createPlayer(String locator) throws IOException, MediaException {
        if (locator == null) throw new IllegalArgumentException();
        if (locator.startsWith("resource:")) {
            String n = locator.substring(9);
            InputStream is = "".getClass().getResourceAsStream(n.startsWith("/") ? n : "/" + n);
            if (is == null) throw new MediaException("resource not found: " + n);
            return createPlayer(is, guessType(n));
        }
        if (locator.startsWith("device://")) return new SoundPlayer(null, locator.equals(TONE_DEVICE_LOCATOR) ? "audio/x-tone-seq" : "audio/midi");
        throw new MediaException("unsupported locator: " + locator);
    }

    static String guessType(String n) {
        String l = n.toLowerCase();
        if (l.endsWith(".wav")) return "audio/x-wav";
        if (l.endsWith(".mid") || l.endsWith(".midi")) return "audio/midi";
        if (l.endsWith(".mp3")) return "audio/mpeg";
        if (l.endsWith(".amr")) return "audio/amr";
        return "application/octet-stream";
    }

    public static Player createPlayer(InputStream stream, String type) throws IOException, MediaException {
        if (stream == null) throw new IllegalArgumentException();
        ByteArrayOutputStream bos = new ByteArrayOutputStream(4096);
        byte[] buf = new byte[4096];
        int n;
        while ((n = stream.read(buf, 0, buf.length)) > 0) bos.write(buf, 0, n);
        return new SoundPlayer(bos.toByteArray(), type);
    }

    public static void playTone(int note, int duration, int volume) throws MediaException {
        if (note < 0 || note > 127 || duration <= 0) throw new IllegalArgumentException();
        pspj2me.Native.tone(note, duration, volume);
    }

    public static TimeBase getSystemTimeBase() { return SYSTEM_TB; }
}
