package javax.microedition.media.control;
public interface MetaDataControl extends javax.microedition.media.Control {
    String AUTHOR_KEY = "author", COPYRIGHT_KEY = "copyright", DATE_KEY = "date", TITLE_KEY = "title";
    String[] getKeys();
    String getKeyValue(String key);
}
