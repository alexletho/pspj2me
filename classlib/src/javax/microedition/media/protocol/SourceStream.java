package javax.microedition.media.protocol;
import java.io.IOException;
public interface SourceStream extends javax.microedition.media.Controllable {
    int NOT_SEEKABLE = 0, SEEKABLE_TO_START = 1, RANDOM_ACCESSIBLE = 2;
    ContentDescriptor getContentDescriptor();
    long getContentLength();
    int read(byte[] b, int off, int len) throws IOException;
    int getTransferSize();
    long seek(long where) throws IOException;
    long tell();
    int getSeekType();
}
