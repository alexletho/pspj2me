package javax.microedition.media.control;
import java.io.IOException;
import java.io.OutputStream;
import javax.microedition.media.MediaException;
public interface RecordControl extends javax.microedition.media.Control {
    void setRecordStream(OutputStream stream);
    void setRecordLocation(String locator) throws IOException, MediaException;
    String getContentType();
    void startRecord();
    void stopRecord();
    void commit() throws IOException;
    int setRecordSizeLimit(int size) throws MediaException;
    void reset() throws IOException;
}
