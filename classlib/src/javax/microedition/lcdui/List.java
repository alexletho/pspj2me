package javax.microedition.lcdui;

public class List extends Screen implements Choice {
    public static final Command SELECT_COMMAND = new Command("", Command.SCREEN, 0);
    final ChoiceData data;
    private Command selectCommand = SELECT_COMMAND;

    public List(String title, int listType) { this(title, listType, new String[0], null); }
    public List(String title, int listType, String[] stringElements, Image[] imageElements) {
        if (listType != IMPLICIT && listType != EXCLUSIVE && listType != MULTIPLE) throw new IllegalArgumentException();
        this.title = title;
        data = new ChoiceData(listType);
        for (int i = 0; i < stringElements.length; i++) data.append(stringElements[i], imageElements == null ? null : imageElements[i]);
    }

    public int size() { return data.size(); }
    public String getString(int i) { return data.getString(i); }
    public Image getImage(int i) { return data.getImage(i); }
    public int append(String s, Image img) { int r = data.append(s, img); repaintIfShown(); return r; }
    public void insert(int i, String s, Image img) { data.insert(i, s, img); repaintIfShown(); }
    public void delete(int i) { data.delete(i); repaintIfShown(); }
    public void deleteAll() { data.deleteAll(); repaintIfShown(); }
    public void set(int i, String s, Image img) { data.set(i, s, img); repaintIfShown(); }
    public boolean isSelected(int i) { return data.isSelected(i); }
    public int getSelectedIndex() { return data.type == IMPLICIT ? (size() == 0 ? -1 : data.highlight) : data.getSelectedIndex(); }
    public int getSelectedFlags(boolean[] f) { return data.getSelectedFlags(f); }
    public void setSelectedIndex(int i, boolean s) { data.setSelectedIndex(i, s); if (s) data.highlight = i; repaintIfShown(); }
    public void setSelectedFlags(boolean[] f) { data.setSelectedFlags(f); repaintIfShown(); }
    public void setFitPolicy(int p) { data.fitPolicy = p; }
    public int getFitPolicy() { return data.fitPolicy; }
    public void setFont(int i, Font f) { data.setFont(i, f); repaintIfShown(); }
    public Font getFont(int i) { return data.getFont(i); }
    public void setSelectCommand(Command c) {
        if (data.type != IMPLICIT) return;
        if (c != null && c != SELECT_COMMAND) addCommand(c);
        selectCommand = c;
    }
    public void removeCommand(Command c) {
        if (c == selectCommand) selectCommand = null;
        super.removeCommand(c);
    }

    void paintContent(Graphics g, int w, int h) {
        int total = data.totalHeight();
        int top = data.rowTop(data.highlight), rh = size() > 0 ? data.rowHeight(data.highlight) : 0;
        if (top < scrollY) scrollY = top;
        if (top + rh > scrollY + h) scrollY = top + rh - h;
        if (scrollY > Math.max(0, total - h)) scrollY = Math.max(0, total - h);
        data.paint(g, 0, -scrollY, total > h ? w - 4 : w, true, true);
        drawScrollbar(g, w, h, total, scrollY);
    }

    void keyEvent(int type, int code) {
        if (type == 2 || size() == 0) return;
        int ga = Canvas.gameActionOf(code);
        if (ga == Canvas.UP) { data.highlight = (data.highlight + size() - 1) % size(); Display.requestRepaint(); }
        else if (ga == Canvas.DOWN) { data.highlight = (data.highlight + 1) % size(); Display.requestRepaint(); }
        else if (ga == Canvas.LEFT) { data.highlight = Math.max(0, data.highlight - 5); Display.requestRepaint(); }
        else if (ga == Canvas.RIGHT) { data.highlight = Math.min(size() - 1, data.highlight + 5); Display.requestRepaint(); }
        else if (ga == Canvas.FIRE && type == 1) {
            if (data.type == IMPLICIT) {
                data.setSelectedIndex(data.highlight, true);
                if (selectCommand != null) fireCommand(selectCommand);
            } else if (data.type == MULTIPLE) data.setSelectedIndex(data.highlight, !data.isSelected(data.highlight));
            else data.setSelectedIndex(data.highlight, true);
            Display.requestRepaint();
        }
    }

    Command[] softKeys() {
        Command[] sk = super.softKeys();
        if (sk[0] == null && data.type == IMPLICIT && selectCommand != null && selectCommand != SELECT_COMMAND) sk[0] = selectCommand;
        return sk;
    }
}
