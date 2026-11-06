package javax.microedition.lcdui;

import java.util.Vector;

public class TextField extends Item {
    public static final int ANY = 0, EMAILADDR = 1, NUMERIC = 2, PHONENUMBER = 3, URL = 4, DECIMAL = 5;
    public static final int PASSWORD = 0x10000, UNEDITABLE = 0x20000, SENSITIVE = 0x40000, NON_PREDICTIVE = 0x80000,
        INITIAL_CAPS_WORD = 0x100000, INITIAL_CAPS_SENTENCE = 0x200000, CONSTRAINT_MASK = 0xFFFF;

    String text = "";
    int maxSize;
    int constraints;

    public TextField(String label, String text, int maxSize, int constraints) {
        super(label);
        if (maxSize <= 0) throw new IllegalArgumentException();
        this.maxSize = maxSize;
        this.constraints = constraints;
        setString(text);
    }

    static String check(String s, int max, int constraints) {
        if (s == null) return "";
        if (s.length() > max) throw new IllegalArgumentException("text too long");
        return s;
    }

    public String getString() { return text; }
    public void setString(String s) { text = check(s, maxSize, constraints); invalidate0(); }
    public int getChars(char[] data) {
        if (data.length < text.length()) throw new ArrayIndexOutOfBoundsException();
        text.getChars(0, text.length(), data, 0);
        return text.length();
    }
    public void setChars(char[] data, int offset, int length) { setString(data == null ? "" : new String(data, offset, length)); }
    public void insert(String src, int pos) {
        if (pos < 0) pos = 0;
        if (pos > text.length()) pos = text.length();
        setString(text.substring(0, pos) + src + text.substring(pos));
    }
    public void insert(char[] data, int offset, int length, int pos) { insert(new String(data, offset, length), pos); }
    public void delete(int offset, int length) {
        if (offset < 0 || length < 0 || offset + length > text.length()) throw new StringIndexOutOfBoundsException();
        setString(text.substring(0, offset) + text.substring(offset + length));
    }
    public int getMaxSize() { return maxSize; }
    public int setMaxSize(int m) {
        if (m <= 0) throw new IllegalArgumentException();
        maxSize = m;
        if (text.length() > m) text = text.substring(0, m);
        return m;
    }
    public int size() { return text.length(); }
    public int getCaretPosition() { return text.length(); }
    public void setConstraints(int c) { constraints = c; }
    public int getConstraints() { return constraints; }
    public void setInitialInputMode(String m) {}

    static String display(String text, int constraints) {
        if ((constraints & PASSWORD) != 0) {
            StringBuffer sb = new StringBuffer();
            for (int i = 0; i < text.length(); i++) sb.append('*');
            return sb.toString();
        }
        return text;
    }

    int contentHeight(int w) {
        Font f = Screen.textFont();
        int lines = Math.max(1, Screen.wrap(display(text, constraints), f, w - 10).size());
        return lines * f.getHeight() + 6;
    }
    void paintContent(Graphics g, int x, int y, int w, boolean focused) {
        int h = contentHeight(w);
        g.setColor(0xffffff);
        g.fillRect(x, y, w - 4, h - 2);
        g.setColor(focused ? Screen.HL_BG : 0x808080);
        g.drawRect(x, y, w - 5, h - 2);
        if (focused) g.drawRect(x + 1, y + 1, w - 7, h - 4);
        Font f = Screen.textFont();
        g.setFont(f);
        g.setColor(Screen.FG);
        Vector lines = Screen.wrap(display(text, constraints), f, w - 10);
        int ly = y + 2;
        for (int i = 0; i < lines.size(); i++) {
            g.drawString((String) lines.elementAt(i), x + 4, ly, Graphics.TOP | Graphics.LEFT);
            ly += f.getHeight();
        }
    }
    boolean focusable() { return true; }
    void activate() {
        if ((constraints & UNEDITABLE) != 0) return;
        String r = pspj2me.Native.editText(label != null ? label : "", text, maxSize, constraints);
        if (r != null) {
            if (r.length() > maxSize) r = r.substring(0, maxSize);
            text = r;
            notifyStateChanged();
            invalidate0();
        }
    }
}
