package javax.microedition.lcdui;

import java.util.Vector;

public class TextBox extends Screen {
    private String text;
    private int maxSize, constraints;

    public TextBox(String title, String text, int maxSize, int constraints) {
        if (maxSize <= 0) throw new IllegalArgumentException();
        this.title = title;
        this.maxSize = maxSize;
        this.constraints = constraints;
        this.text = TextField.check(text, maxSize, constraints);
    }
    public String getString() { return text; }
    public void setString(String s) { text = TextField.check(s, maxSize, constraints); repaintIfShown(); }
    public int getChars(char[] data) { text.getChars(0, text.length(), data, 0); return text.length(); }
    public void setChars(char[] data, int offset, int length) { setString(data == null ? "" : new String(data, offset, length)); }
    public void insert(String src, int pos) {
        if (pos < 0) pos = 0;
        if (pos > text.length()) pos = text.length();
        setString(text.substring(0, pos) + src + text.substring(pos));
    }
    public void insert(char[] data, int offset, int length, int pos) { insert(new String(data, offset, length), pos); }
    public void delete(int offset, int length) { setString(text.substring(0, offset) + text.substring(offset + length)); }
    public int getMaxSize() { return maxSize; }
    public int setMaxSize(int m) { maxSize = m; if (text.length() > m) text = text.substring(0, m); return m; }
    public int size() { return text.length(); }
    public int getCaretPosition() { return text.length(); }
    public void setConstraints(int c) { constraints = c; }
    public int getConstraints() { return constraints; }
    public void setInitialInputMode(String m) {}

    void showNotify0() {
        super.showNotify0();
        /* open the editor right away, like a phone would focus the text box */
        Display.instance.callSerially(new Runnable() { public void run() { if (isShown()) edit(); } });
    }

    void edit() {
        if ((constraints & TextField.UNEDITABLE) != 0) return;
        String r = pspj2me.Native.editText(title != null ? title : "", text, maxSize, constraints);
        if (r != null) {
            if (r.length() > maxSize) r = r.substring(0, maxSize);
            text = r;
        }
        Display.requestRepaint();
    }

    void paintContent(Graphics g, int w, int h) {
        Font f = textFont();
        g.setColor(0x808080);
        g.drawRect(2, 2, w - 5, h - 5);
        g.setFont(f);
        g.setColor(FG);
        Vector lines = wrap(TextField.display(text, constraints), f, w - 10);
        int y = 4;
        for (int i = 0; i < lines.size(); i++) { g.drawString((String) lines.elementAt(i), 5, y, Graphics.TOP | Graphics.LEFT); y += f.getHeight(); }
        g.setColor(DIM);
        g.drawString(text.length() + "/" + maxSize, w - 5, h - 4, Graphics.BOTTOM | Graphics.RIGHT);
    }

    void keyEvent(int type, int code) {
        if (type == 1 && Canvas.gameActionOf(code) == Canvas.FIRE) edit();
    }
}
