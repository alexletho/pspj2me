package javax.microedition.media.control;
public interface RateControl extends javax.microedition.media.Control {
    int setRate(int millirate);
    int getRate();
    int getMaxRate();
    int getMinRate();
}
