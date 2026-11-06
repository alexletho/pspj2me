package javax.microedition.io.file;
import java.util.Enumeration;
import java.util.Vector;
public class FileSystemRegistry {
    private FileSystemRegistry() {}
    public static boolean addFileSystemListener(FileSystemListener l) { return false; }
    public static boolean removeFileSystemListener(FileSystemListener l) { return false; }
    public static Enumeration listRoots() { return new Vector().elements(); }
}
