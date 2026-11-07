package javax.microedition.media;

import java.io.*;
import javax.microedition.media.protocol.DataSource;
import javax.microedition.media.protocol.SourceStream;
import pspj2me.SoundPlayer;

public final class Manager {
    public static final String TONE_DEVICE_LOCATOR = "device://tone";
    public static final String MIDI_DEVICE_LOCATOR = "device://midi";
    private static final TimeBase SYSTEM_TB = new TimeBase() { public long getTime() { return System.currentTimeMillis() * 1000L; } };
    /* only formats that actually play */
    private static final String[] TYPES = { "audio/x-wav", "audio/wav", "audio/midi", "audio/mid", "audio/x-midi", "audio/sp-midi",
        "audio/x-tone-seq", "audio/mpeg", "audio/mp3", "audio/x-mp3", "audio/amr" };
    private Manager() {}

    public static String[] getSupportedContentTypes(String protocol) {
        if (protocol != null && !protocol.equals("device") && !protocol.equals("resource") && !protocol.equals("file")) return new String[0];
        if ("device".equals(protocol)) return new String[] { "audio/midi", "audio/x-tone-seq" };
        String[] r = new String[TYPES.length];
        System.arraycopy(TYPES, 0, r, 0, r.length);
        return r;
    }

    public static String[] getSupportedProtocols(String contentType) {
        if (contentType == null) return new String[] { "device", "resource" };
        for (int i = 0; i < TYPES.length; i++)
            if (TYPES[i].equals(contentType))
                return contentType.equals("audio/midi") || contentType.equals("audio/x-tone-seq") ? new String[] { "device", "resource" } : new String[] { "resource" };
        return new String[0];
    }

    public static Player createPlayer(String locator) throws IOException, MediaException {
        if (locator == null) throw new IllegalArgumentException();
        if (locator.startsWith("resource:")) {
            String n = locator.substring(9);
            InputStream is = "".getClass().getResourceAsStream(n.startsWith("/") ? n : "/" + n);
            if (is == null) throw new MediaException("resource not found: " + n);
            return createPlayer(is, guessType(n));
        }
        if (locator.equals(TONE_DEVICE_LOCATOR)) return SoundPlayer.toneDevice();
        if (locator.equals(MIDI_DEVICE_LOCATOR)) return SoundPlayer.midiDevice();
        if (locator.startsWith("capture:")) throw new MediaException("audio/video capture is not supported");
        if (locator.startsWith("http:") || locator.startsWith("https:") || locator.startsWith("rtsp:")) throw new IOException("no network connection");
        throw new MediaException("unsupported locator: " + locator);
    }

    static String guessType(String n) {
        String l = n.toLowerCase();
        if (l.endsWith(".wav")) return "audio/x-wav";
        if (l.endsWith(".mid") || l.endsWith(".midi")) return "audio/midi";
        if (l.endsWith(".jts")) return "audio/x-tone-seq";
        if (l.endsWith(".mp3")) return "audio/mpeg";
        if (l.endsWith(".amr")) return "audio/amr";
        if (l.endsWith(".mmf")) return "application/vnd.smaf";
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

    public static Player createPlayer(DataSource source) throws IOException, MediaException {
        if (source == null) throw new IllegalArgumentException();
        source.connect();
        SourceStream[] streams = source.getStreams();
        if (streams == null || streams.length == 0) throw new MediaException("no stream");
        ByteArrayOutputStream bos = new ByteArrayOutputStream(4096);
        byte[] buf = new byte[4096];
        int n;
        while ((n = streams[0].read(buf, 0, buf.length)) > 0) bos.write(buf, 0, n);
        source.disconnect();
        return new SoundPlayer(bos.toByteArray(), source.getContentType());
    }

    public static void playTone(int note, int duration, int volume) throws MediaException {
        if (note < 0 || note > 127 || duration <= 0) throw new IllegalArgumentException();
        pspj2me.Native.tone(note, duration, Math.max(0, Math.min(100, volume)));
    }

    public static TimeBase getSystemTimeBase() { return SYSTEM_TB; }
}
