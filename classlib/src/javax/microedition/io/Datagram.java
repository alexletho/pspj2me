package javax.microedition.io;
public interface Datagram extends java.io.DataInput, java.io.DataOutput {
    String getAddress();
    byte[] getData();
    int getLength();
    int getOffset();
    void setAddress(String addr) throws java.io.IOException;
    void setAddress(Datagram reference);
    void setLength(int len);
    void setData(byte[] buffer, int offset, int len);
    void reset();
}
