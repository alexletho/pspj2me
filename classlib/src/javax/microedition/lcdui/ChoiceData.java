package javax.microedition.lcdui;

import java.util.Vector;

/* Shared element storage for List and ChoiceGroup. */
final class ChoiceData {
    final Vector strings = new Vector(), images = new Vector(), fonts = new Vector(), selected = new Vector();
    int type;
    int highlight;
    int fitPolicy;

    ChoiceData(int type) { this.type = type; }

    int size() { return strings.size(); }
    void check(int i) { if (i < 0 || i >= size()) throw new IndexOutOfBoundsException(); }
    String getString(int i) { check(i); return (String) strings.elementAt(i); }
    Image getImage(int i) { check(i); return (Image) images.elementAt(i); }

    int append(String s, Image img) {
        if (s == null) throw new NullPointerException();
        strings.addElement(s); images.addElement(img); fonts.addElement(null);
        selected.addElement(Boolean.FALSE);
        if ((type == Choice.EXCLUSIVE || type == Choice.POPUP || type == Choice.IMPLICIT) && size() == 1) selected.setElementAt(Boolean.TRUE, 0);
        return size() - 1;
    }
    void insert(int i, String s, Image img) {
        if (i < 0 || i > size()) throw new IndexOutOfBoundsException();
        if (s == null) throw new NullPointerException();
        strings.insertElementAt(s, i); images.insertElementAt(img, i); fonts.insertElementAt(null, i);
        selected.insertElementAt(Boolean.FALSE, i);
        if (type != Choice.MULTIPLE && size() == 1) selected.setElementAt(Boolean.TRUE, 0);
        if (highlight >= i && size() > 1) highlight++;
    }
    void delete(int i) {
        check(i);
        boolean wasSel = isSelected(i);
        strings.removeElementAt(i); images.removeElementAt(i); fonts.removeElementAt(i); selected.removeElementAt(i);
        if (highlight >= size()) highlight = Math.max(0, size() - 1);
        if (wasSel && type != Choice.MULTIPLE && size() > 0) selected.setElementAt(Boolean.TRUE, Math.min(i, size() - 1));
    }
    void deleteAll() { strings.removeAllElements(); images.removeAllElements(); fonts.removeAllElements(); selected.removeAllElements(); highlight = 0; }
    void set(int i, String s, Image img) {
        check(i);
        if (s == null) throw new NullPointerException();
        strings.setElementAt(s, i); images.setElementAt(img, i);
    }
    boolean isSelected(int i) { check(i); return ((Boolean) selected.elementAt(i)).booleanValue(); }
    int getSelectedIndex() {
        if (type == Choice.MULTIPLE) return -1;
        for (int i = 0; i < size(); i++) if (isSelected(i)) return i;
        return -1;
    }
    int getSelectedFlags(boolean[] flags) {
        if (flags.length < size()) throw new IllegalArgumentException();
        int n = 0;
        for (int i = 0; i < flags.length; i++) { flags[i] = i < size() && isSelected(i); if (flags[i]) n++; }
        return n;
    }
    void setSelectedIndex(int i, boolean sel) {
        check(i);
        if (type == Choice.MULTIPLE) selected.setElementAt(sel ? Boolean.TRUE : Boolean.FALSE, i);
        else if (sel) {
            for (int k = 0; k < size(); k++) selected.setElementAt(k == i ? Boolean.TRUE : Boolean.FALSE, k);
            highlight = i;
        }
    }
    void setSelectedFlags(boolean[] flags) {
        if (flags.length < size()) throw new IllegalArgumentException();
        if (type == Choice.MULTIPLE) {
            for (int i = 0; i < size(); i++) selected.setElementAt(flags[i] ? Boolean.TRUE : Boolean.FALSE, i);
        } else {
            int first = 0;
            for (int i = 0; i < size(); i++) if (flags[i]) { first = i; break; }
            if (size() > 0) setSelectedIndex(first, true);
        }
    }
    void setFont(int i, Font f) { check(i); fonts.setElementAt(f, i); }
    Font getFont(int i) { check(i); Font f = (Font) fonts.elementAt(i); return f == null ? Screen.textFont() : f; }

    int rowHeight(int i) {
        Font f = getFont(i);
        int h = f.getHeight() + 4;
        Image img = getImage(i);
        if (img != null && img.getHeight() + 4 > h) h = img.getHeight() + 4;
        return h;
    }

    int paint(Graphics g, int x, int y, int w, boolean focused, boolean showHighlight) {
        for (int i = 0; i < size(); i++) {
            int rh = rowHeight(i);
            boolean hl = showHighlight && i == highlight;
            if (hl) { g.setColor(Screen.HL_BG); g.fillRect(x, y, w, rh); }
            int tx = x + 3;
            if (type == Choice.EXCLUSIVE || type == Choice.POPUP || type == Choice.MULTIPLE) {
                int bs = 10, by = y + (rh - bs) / 2;
                g.setColor(hl ? Screen.HL_FG : 0x404040);
                if (type == Choice.MULTIPLE) {
                    g.drawRect(tx, by, bs, bs);
                    if (isSelected(i)) { g.fillRect(tx + 3, by + 3, bs - 5, bs - 5); }
                } else {
                    g.drawArc(tx, by, bs, bs, 0, 360);
                    if (isSelected(i)) g.fillArc(tx + 3, by + 3, bs - 5, bs - 5, 0, 360);
                }
                tx += bs + 5;
            }
            Image img = getImage(i);
            if (img != null) {
                g.drawImage(img, tx, y + (rh - img.getHeight()) / 2, Graphics.TOP | Graphics.LEFT);
                tx += img.getWidth() + 4;
            }
            Font f = getFont(i);
            g.setFont(f);
            g.setColor(hl ? Screen.HL_FG : Screen.FG);
            g.drawString(getString(i), tx, y + (rh - f.getHeight()) / 2, Graphics.TOP | Graphics.LEFT);
            y += rh;
        }
        return y;
    }

    int totalHeight() {
        int h = 0;
        for (int i = 0; i < size(); i++) h += rowHeight(i);
        return h;
    }
    int rowTop(int idx) {
        int h = 0;
        for (int i = 0; i < idx; i++) h += rowHeight(i);
        return h;
    }
}
