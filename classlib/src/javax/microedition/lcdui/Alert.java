package javax.microedition.lcdui;

import java.util.Vector;

public class Alert extends Screen {
    public static final int FOREVER = -2;
    public static final Command DISMISS_COMMAND = new Command("Done", Command.OK, 0);
    private String text;
    private Image image;
    private AlertType type;
    private int timeout = 2000;
    private Gauge indicator;
    Displayable returnTo;

    public Alert(String title) { this(title, null, null, null); }
    public Alert(String title, String alertText, Image alertImage, AlertType alertType) {
        this.title = title; text = alertText; image = alertImage; type = alertType;
    }
    public int getDefaultTimeout() { return 2000; }
    public int getTimeout() { return hasUserCommands() ? FOREVER : timeout; }
    public void setTimeout(int t) {
        if (t <= 0 && t != FOREVER) throw new IllegalArgumentException();
        timeout = t;
    }
    public AlertType getType() { return type; }
    public void setType(AlertType t) { type = t; }
    public String getString() { return text; }
    public void setString(String s) { text = s; repaintIfShown(); }
    public Image getImage() { return image; }
    public void setImage(Image img) { image = img; repaintIfShown(); }
    public void setIndicator(Gauge g) { indicator = g; }
    public Gauge getIndicator() { return indicator; }

    private boolean hasUserCommands() {
        for (int i = 0; i < commands.size(); i++) if (commands.elementAt(i) != DISMISS_COMMAND) return true;
        return false;
    }
    public void addCommand(Command c) { super.addCommand(c); }

    Command[] softKeys() {
        if (commands.isEmpty()) return new Command[] { DISMISS_COMMAND, null };
        return super.softKeys();
    }
    boolean hasCommands() { return true; }
    Vector menuCommands() {
        if (commands.isEmpty()) { Vector v = new Vector(); v.addElement(DISMISS_COMMAND); return v; }
        return super.menuCommands();
    }

    void fireCommand(Command c) {
        if (listener != null) super.fireCommand(c);
        else Display.dismissAlert(this);
    }

    void timeoutExpired() {
        if (listener != null && commands.isEmpty()) super.fireCommand(DISMISS_COMMAND);
        else Display.dismissAlert(this);
    }

    void keyEvent(int type, int code) {
        if (type != 1) return;
        int ga = Canvas.gameActionOf(code);
        if (ga == Canvas.FIRE && !hasUserCommands()) fireCommand(DISMISS_COMMAND);
        else if (ga == Canvas.UP) { scrollY = Math.max(0, scrollY - 12); Display.requestRepaint(); }
        else if (ga == Canvas.DOWN) { scrollY += 12; Display.requestRepaint(); }
    }

    void paintContent(Graphics g, int w, int h) {
        int y = 4 - scrollY;
        if (image != null) {
            g.drawImage(image, w / 2, y, Graphics.TOP | Graphics.HCENTER);
            y += image.getHeight() + 4;
        }
        if (text != null) {
            Font f = textFont();
            g.setFont(f);
            g.setColor(FG);
            Vector lines = wrap(text, f, w - 8);
            for (int i = 0; i < lines.size(); i++) {
                g.drawString((String) lines.elementAt(i), w / 2, y, Graphics.TOP | Graphics.HCENTER);
                y += f.getHeight();
            }
        }
        if (indicator != null) {
            y += 4;
            indicator.paintContent(g, 4, y, w - 8, false);
            y += indicator.contentHeight(w - 8);
        }
        int total = y + scrollY;
        if (scrollY > 0 && total - scrollY < h) { scrollY = Math.max(0, total - h); }
    }
}
