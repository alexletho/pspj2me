package javax.microedition.io.file;
public interface FileSystemListener {
    int ROOT_ADDED = 0, ROOT_REMOVED = 1;
    void rootChanged(int state, String rootName);
}
