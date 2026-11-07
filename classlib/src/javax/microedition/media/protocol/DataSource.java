package javax.microedition.media.protocol;
import java.io.IOException;
public abstract class DataSource implements javax.microedition.media.Controllable {
    private final String locator;
    public DataSource(String locator) { this.locator = locator; }
    public String getLocator() { return locator; }
    public abstract String getContentType();
    public abstract void connect() throws IOException;
    public abstract void disconnect();
    public abstract void start() throws IOException;
    public abstract void stop() throws IOException;
    public abstract SourceStream[] getStreams();
}
