package javax.microedition.lcdui;

import java.util.Vector;

public class Form extends Screen {
    final Vector items = new Vector();
    private ItemStateListener stateListener;
    int focus = -1;

    public Form(String title) { this.title = title; }
    public Form(String title, Item[] items) {
        this(title);
        if (items != null) for (int i = 0; i < items.length; i++) append(items[i]);
    }

    public int append(Item item) {
        if (item == null) throw new NullPointerException();
        if (item.owner != null) throw new IllegalStateException();
        item.owner = this;
        items.addElement(item);
        if (focus < 0 && item.focusable()) focus = items.size() - 1;
        repaintIfShown();
        return items.size() - 1;
    }
    public int append(String str) { return append(new StringItem(null, str)); }
    public int append(Image img) { return append(new ImageItem(null, img, ImageItem.LAYOUT_DEFAULT, null)); }
    public void insert(int itemNum, Item item) {
        if (itemNum < 0 || itemNum > items.size()) throw new IndexOutOfBoundsException();
        if (item.owner != null) throw new IllegalStateException();
        item.owner = this;
        items.insertElementAt(item, itemNum);
        if (focus >= itemNum) focus++;
        repaintIfShown();
    }
    public void delete(int itemNum) {
        Item it = (Item) items.elementAt(itemNum);
        it.owner = null;
        items.removeElementAt(itemNum);
        if (focus == itemNum) focus = firstFocusable(Math.min(itemNum, items.size() - 1), 1);
        else if (focus > itemNum) focus--;
        repaintIfShown();
    }
    public void deleteAll() {
        for (int i = 0; i < items.size(); i++) ((Item) items.elementAt(i)).owner = null;
        items.removeAllElements();
        focus = -1;
        scrollY = 0;
        repaintIfShown();
    }
    public void set(int itemNum, Item item) {
        Item old = (Item) items.elementAt(itemNum);
        old.owner = null;
        item.owner = this;
        items.setElementAt(item, itemNum);
        repaintIfShown();
    }
    public Item get(int itemNum) { return (Item) items.elementAt(itemNum); }
    public int size() { return items.size(); }
    public void setItemStateListener(ItemStateListener l) { stateListener = l; }
    public int getWidth() { return Display.W; }

    void itemChanged(Item it) {
        if (stateListener != null) {
            try { stateListener.itemStateChanged(it); } catch (Throwable t) { t.printStackTrace(); }
        }
    }

    void focusItem(Item it) {
        int i = items.indexOf(it);
        if (i >= 0) { focus = i; repaintIfShown(); }
    }

    private int firstFocusable(int from, int dir) {
        for (int i = from; i >= 0 && i < items.size(); i += dir)
            if (((Item) items.elementAt(i)).focusable()) return i;
        return -1;
    }

    Item focused() { return focus >= 0 && focus < items.size() ? (Item) items.elementAt(focus) : null; }

    void paintContent(Graphics g, int w, int h) {
        int iw = w - 8;
        int y = 2;
        int focusTop = 0, focusBottom = 0;
        int n = items.size();
        int[] tops = new int[n + 1];
        for (int i = 0; i < n; i++) {
            tops[i] = y;
            y += ((Item) items.elementAt(i)).totalHeight(iw) + 4;
        }
        tops[n] = y;
        int total = y;
        if (focus >= 0 && focus < n) {
            focusTop = tops[focus];
            focusBottom = tops[focus + 1];
            Item f = (Item) items.elementAt(focus);
            if (f instanceof ChoiceGroup) {
                int ht = focusTop + f.labelHeight() + ((ChoiceGroup) f).highlightTop();
                focusTop = ht;
                focusBottom = Math.min(focusBottom, ht + Screen.textFont().getHeight() + 6);
            }
            if (focusBottom - focusTop > h) focusBottom = focusTop + h;
            if (focusTop < scrollY) scrollY = focusTop;
            if (focusBottom > scrollY + h) scrollY = focusBottom - h;
        }
        if (scrollY > Math.max(0, total - h)) scrollY = Math.max(0, total - h);
        if (scrollY < 0) scrollY = 0;
        for (int i = 0; i < n; i++) {
            int top = tops[i] - scrollY;
            if (top > h || tops[i + 1] - scrollY < 0) continue;
            ((Item) items.elementAt(i)).paintItem(g, 4, top, iw, i == focus);
        }
        drawScrollbar(g, w, h, total, scrollY);
    }

    void keyEvent(int type, int code) {
        if (type == 2) return;
        int ga = Canvas.gameActionOf(code);
        Item f = focused();
        if (f != null) {
            if (f instanceof CustomItem) {
                CustomItem ci = (CustomItem) f;
                try {
                    if (type == 1) ci.keyPressed(code); else ci.keyRepeated(code);
                } catch (Throwable t) { t.printStackTrace(); }
                if (ga != Canvas.UP && ga != Canvas.DOWN) { Display.requestRepaint(); return; }
            } else if (f.key(ga, code)) { Display.requestRepaint(); return; }
            if (ga == Canvas.FIRE && type == 1) { f.activate(); Display.requestRepaint(); return; }
        }
        if (ga == Canvas.UP || ga == Canvas.DOWN) {
            int dir = ga == Canvas.UP ? -1 : 1;
            int nf = firstFocusable(focus + dir, dir);
            if (nf >= 0) focus = nf;
            else scrollY = Math.max(0, scrollY + dir * Screen.textFont().getHeight() * 2);
            Display.requestRepaint();
        }
    }

    Command[] softKeys() {
        Item f = focused();
        if (f == null || f.commands.isEmpty()) return super.softKeys();
        Vector all = allCommands();
        Command left = null, right = null;
        for (int i = 0; i < all.size(); i++) { Command c = (Command) all.elementAt(i); if (right == null && c.isNegative() && commands.contains(c)) right = c; }
        int others = 0;
        for (int i = 0; i < all.size(); i++) { Command c = (Command) all.elementAt(i); if (c != right) { others++; if (left == null) left = c; } }
        if (others > 1) left = Display.MENU;
        return new Command[] { left, right };
    }
    boolean hasCommands() { return !allCommands().isEmpty(); }
    Vector menuCommands() {
        Command[] sk = softKeys();
        Vector all = allCommands(), v = new Vector();
        for (int i = 0; i < all.size(); i++) if (all.elementAt(i) != sk[1]) v.addElement(all.elementAt(i));
        return v;
    }
    private Vector allCommands() {
        Vector v = new Vector();
        Item f = focused();
        if (f != null) for (int i = 0; i < f.commands.size(); i++) v.addElement(f.commands.elementAt(i));
        for (int i = 0; i < commands.size(); i++) v.addElement(commands.elementAt(i));
        return v;
    }
    void fireCommand(Command c) {
        Item f = focused();
        if (f != null && f.commands.contains(c)) {
            if (f.commandListener != null) {
                try { f.commandListener.commandAction(c, f); } catch (Throwable t) { t.printStackTrace(); }
            }
            return;
        }
        super.fireCommand(c);
    }
}
