package javax.microedition.lcdui;

import java.util.Vector;

public class StringItem extends Item {
    private String text;
    private int appearance;
    private Font font;

    public StringItem(String label, String text) { this(label, text, PLAIN); }
    public StringItem(String label, String text, int appearanceMode) {
        super(label);
        this.text = text;
        appearance = appearanceMode;
    }
    public String getText() { return text; }
    public void setText(String t) { text = t; invalidate0(); }
    public int getAppearanceMode() { return appearance; }
    public void setFont(Font f) { font = f; invalidate0(); }
    public Font getFont() { return font != null ? font : Screen.textFont(); }

    int contentHeight(int w) {
        if (text == null || text.length() == 0) return 0;
        Font f = getFont();
        int h = Screen.wrap(text, f, w - (appearance == BUTTON ? 8 : 0)).size() * f.getHeight();
        return appearance == BUTTON ? h + 6 : h;
    }
    void paintContent(Graphics g, int x, int y, int w, boolean focused) {
        if (text == null) return;
        Font f = getFont();
        g.setFont(f);
        if (appearance == BUTTON) {
            int h = contentHeight(w);
            g.setColor(focused ? Screen.HL_BG : 0xe0e0e0);
            g.fillRoundRect(x, y, w - 4, h - 1, 6, 6);
            g.setColor(0x606060);
            g.drawRoundRect(x, y, w - 4, h - 1, 6, 6);
            x += 4; y += 3; w -= 8;
        }
        g.setColor(focused && appearance != PLAIN ? (appearance == BUTTON ? Screen.HL_FG : Screen.HL_BG) : (appearance == HYPERLINK ? 0x0000c0 : Screen.FG));
        Vector lines = Screen.wrap(text, f, w);
        for (int i = 0; i < lines.size(); i++) {
            g.drawString((String) lines.elementAt(i), x, y, Graphics.TOP | Graphics.LEFT);
            y += f.getHeight();
        }
    }
}
