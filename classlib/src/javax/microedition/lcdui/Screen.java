package javax.microedition.lcdui;

import java.util.Vector;

public abstract class Screen extends Displayable {
    static final int BG = 0xffffff, FG = 0x000000, TITLE_BG = 0x203a70, TITLE_FG = 0xffffff, HL_BG = 0x3060c0, HL_FG = 0xffffff, DIM = 0x808080;
    int scrollY;

    Screen() {}

    static Font titleFont() { return Font.getFont(Font.FACE_SYSTEM, Font.STYLE_BOLD, Font.SIZE_SMALL); }
    static Font textFont() { return Font.getDefaultFont(); }
    static Font labelFont() { return Font.getFont(Font.FACE_SYSTEM, Font.STYLE_BOLD, Font.SIZE_SMALL); }

    int headerH() {
        int h = 0;
        if (title != null) h += titleFont().getHeight() + 4;
        if (ticker != null) h += titleFont().getHeight() + 2;
        return h;
    }

    public int getHeight() { return Display.H - Display.SOFTBAR_H - headerH(); }

    void paintScreen(Graphics g) {
        int W = Display.W;
        g.setColor(BG);
        g.fillRect(0, 0, W, Display.H);
        int y = 0;
        if (title != null) {
            Font f = titleFont();
            g.setColor(TITLE_BG);
            g.fillRect(0, 0, W, f.getHeight() + 4);
            g.setColor(TITLE_FG);
            g.setFont(f);
            g.drawString(title, W / 2, 2, Graphics.TOP | Graphics.HCENTER);
            y += f.getHeight() + 4;
        }
        if (ticker != null) {
            Font f = titleFont();
            int th = f.getHeight() + 2;
            g.setColor(0xfff0c0);
            g.fillRect(0, y, W, th);
            g.setColor(0x000000);
            g.setFont(f);
            String s = ticker.getString();
            int tw = f.stringWidth(s);
            int off = (int) ((System.currentTimeMillis() / 25) % (tw + W));
            g.setClip(0, y, W, th);
            g.drawString(s, W - off, y + 1, Graphics.TOP | Graphics.LEFT);
            g.setClip(0, 0, W, Display.H);
            y += th;
        }
        int h = Display.H - Display.SOFTBAR_H - y;
        g.setClip(0, y, W, h);
        g.translate(0, y);
        try { paintContent(g, W, h); } catch (Throwable t) { t.printStackTrace(); }
        g.translate(0, -y);
        g.setClip(0, 0, W, Display.H);
        Display.paintSoftBar(g, this);
    }

    abstract void paintContent(Graphics g, int w, int h);

    static Vector wrap(String text, Font f, int width) {
        Vector lines = new Vector();
        if (text == null) return lines;
        int len = text.length();
        int start = 0;
        while (start < len) {
            int nl = text.indexOf('\n', start);
            int end = nl < 0 ? len : nl;
            /* wrap segment [start, end) */
            int s = start;
            while (s < end) {
                int w = 0, i = s, lastSpace = -1;
                while (i < end) {
                    char c = text.charAt(i);
                    int cw = f.charWidth(c);
                    if (w + cw > width && i > s) break;
                    if (c == ' ') lastSpace = i;
                    w += cw;
                    i++;
                }
                if (i < end && lastSpace > s) i = lastSpace + 1;
                lines.addElement(text.substring(s, i));
                s = i;
            }
            if (nl < 0) break;
            if (end == start) lines.addElement("");
            start = end + 1;
        }
        return lines;
    }

    static void drawScrollbar(Graphics g, int w, int h, int total, int pos) {
        if (total <= h) return;
        g.setColor(0xd0d0d0);
        g.fillRect(w - 3, 0, 3, h);
        int bh = Math.max(8, h * h / total);
        int by = (h - bh) * pos / Math.max(1, total - h);
        g.setColor(0x606060);
        g.fillRect(w - 3, by, 3, bh);
    }
}
