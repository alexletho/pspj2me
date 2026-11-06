package javax.microedition.lcdui;
public class Gauge extends Item {
    public static final int INDEFINITE = -1, CONTINUOUS_IDLE = 0, INCREMENTAL_IDLE = 1, CONTINUOUS_RUNNING = 2, INCREMENTAL_UPDATING = 3;
    private boolean interactive;
    private int max, value;
    public Gauge(String label, boolean interactive, int maxValue, int initialValue) {
        super(label);
        this.interactive = interactive;
        max = maxValue;
        value = initialValue;
        if (max != INDEFINITE && value > max) value = max;
        if (value < 0 && max != INDEFINITE) value = 0;
    }
    public void setValue(int v) {
        if (max != INDEFINITE) { if (v < 0) v = 0; if (v > max) v = max; }
        value = v; invalidate0();
    }
    public int getValue() { return value; }
    public void setMaxValue(int m) { max = m; if (max != INDEFINITE && value > max) value = max; invalidate0(); }
    public int getMaxValue() { return max; }
    public boolean isInteractive() { return interactive; }
    int contentHeight(int w) { return 12; }
    void paintContent(Graphics g, int x, int y, int w, boolean focused) {
        g.setColor(focused ? Screen.HL_BG : 0x808080);
        g.drawRect(x, y + 1, w - 5, 9);
        if (max > 0) {
            int fw = (w - 7) * value / max;
            g.setColor(0x40a040);
            g.fillRect(x + 2, y + 3, fw, 6);
        } else if (max == INDEFINITE) {
            int p = (int) ((System.currentTimeMillis() / 50) % (w - 20));
            g.setColor(0x40a040);
            g.fillRect(x + 2 + p, y + 3, 12, 6);
        }
    }
    boolean focusable() { return interactive || super.focusable(); }
    boolean key(int ga, int code) {
        if (!interactive || max <= 0) return false;
        if (ga == Canvas.LEFT && value > 0) { value--; notifyStateChanged(); invalidate0(); return true; }
        if (ga == Canvas.RIGHT && value < max) { value++; notifyStateChanged(); invalidate0(); return true; }
        return false;
    }
}
