package javax.microedition.media.control;
public interface FramePositioningControl extends javax.microedition.media.Control {
    int seek(int frameNumber);
    int skip(int framesToSkip);
    long mapFrameToTime(int frameNumber);
    int mapTimeToFrame(long mediaTime);
}
