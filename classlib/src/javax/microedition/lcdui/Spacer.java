package javax.microedition.lcdui;
public class Spacer extends Item {
    private int minW, minH;
    public Spacer(int minWidth, int minHeight) {
        super(null);
        if (minWidth < 0 || minHeight < 0) throw new IllegalArgumentException();
        minW = minWidth; minH = minHeight;
    }
    public void setMinimumSize(int w, int h) { minW = w; minH = h; invalidate0(); }
    public void addCommand(Command c) { throw new IllegalStateException(); }
    public void setDefaultCommand(Command c) { throw new IllegalStateException(); }
    public void setLabel(String l) { throw new IllegalStateException(); }
    int contentHeight(int w) { return minH; }
    void paintContent(Graphics g, int x, int y, int w, boolean focused) {}
    boolean focusable() { return false; }
}
