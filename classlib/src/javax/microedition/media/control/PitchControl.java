package javax.microedition.media.control;
public interface PitchControl extends javax.microedition.media.Control {
    int setPitch(int millisemitones);
    int getPitch();
    int getMaxPitch();
    int getMinPitch();
}
