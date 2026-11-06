package javax.microedition.lcdui;

public class ChoiceGroup extends Item implements Choice {
    final ChoiceData data;
    private boolean popupOpen;

    public ChoiceGroup(String label, int choiceType) { this(label, choiceType, new String[0], null); }
    public ChoiceGroup(String label, int choiceType, String[] stringElements, Image[] imageElements) {
        super(label);
        if (choiceType != EXCLUSIVE && choiceType != MULTIPLE && choiceType != POPUP) throw new IllegalArgumentException();
        data = new ChoiceData(choiceType);
        for (int i = 0; i < stringElements.length; i++) data.append(stringElements[i], imageElements == null ? null : imageElements[i]);
    }

    public int size() { return data.size(); }
    public String getString(int i) { return data.getString(i); }
    public Image getImage(int i) { return data.getImage(i); }
    public int append(String s, Image img) { int r = data.append(s, img); invalidate0(); return r; }
    public void insert(int i, String s, Image img) { data.insert(i, s, img); invalidate0(); }
    public void delete(int i) { data.delete(i); invalidate0(); }
    public void deleteAll() { data.deleteAll(); invalidate0(); }
    public void set(int i, String s, Image img) { data.set(i, s, img); invalidate0(); }
    public boolean isSelected(int i) { return data.isSelected(i); }
    public int getSelectedIndex() { return data.getSelectedIndex(); }
    public int getSelectedFlags(boolean[] f) { return data.getSelectedFlags(f); }
    public void setSelectedIndex(int i, boolean s) { data.setSelectedIndex(i, s); invalidate0(); }
    public void setSelectedFlags(boolean[] f) { data.setSelectedFlags(f); invalidate0(); }
    public void setFitPolicy(int p) { data.fitPolicy = p; }
    public int getFitPolicy() { return data.fitPolicy; }
    public void setFont(int i, Font f) { data.setFont(i, f); invalidate0(); }
    public Font getFont(int i) { return data.getFont(i); }

    int contentHeight(int w) {
        if (data.type == POPUP && !popupOpen) return Screen.textFont().getHeight() + 6;
        return data.totalHeight();
    }
    void paintContent(Graphics g, int x, int y, int w, boolean focused) {
        if (data.type == POPUP && !popupOpen) {
            int h = contentHeight(w);
            g.setColor(focused ? Screen.HL_BG : 0x808080);
            g.drawRect(x, y, w - 5, h - 2);
            int s = data.getSelectedIndex();
            g.setColor(Screen.FG);
            g.setFont(Screen.textFont());
            if (s >= 0) g.drawString(data.getString(s), x + 4, y + 2, Graphics.TOP | Graphics.LEFT);
            g.fillTriangle(x + w - 16, y + 5, x + w - 8, y + 5, x + w - 12, y + h - 6);
            return;
        }
        data.paint(g, x, y, w - 4, focused, focused);
    }
    boolean focusable() { return size() > 0; }
    boolean key(int ga, int code) {
        if (data.type == POPUP && !popupOpen) return false;
        if (ga == Canvas.UP && data.highlight > 0) { data.highlight--; invalidate0(); return true; }
        if (ga == Canvas.DOWN && data.highlight < size() - 1) { data.highlight++; invalidate0(); return true; }
        return false;
    }
    void activate() {
        if (size() == 0) return;
        if (data.type == POPUP) {
            if (!popupOpen) { popupOpen = true; data.highlight = Math.max(0, data.getSelectedIndex()); invalidate0(); return; }
            data.setSelectedIndex(data.highlight, true);
            popupOpen = false;
        } else if (data.type == MULTIPLE) {
            data.setSelectedIndex(data.highlight, !data.isSelected(data.highlight));
        } else data.setSelectedIndex(data.highlight, true);
        notifyStateChanged();
        invalidate0();
    }
    int highlightTop() { return data.type == POPUP && !popupOpen ? 0 : data.rowTop(data.highlight); }
}
