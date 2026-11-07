package javax.microedition.media.protocol;
public class ContentDescriptor {
    private final String type;
    public ContentDescriptor(String contentType) { type = contentType; }
    public String getContentType() { return type; }
}
