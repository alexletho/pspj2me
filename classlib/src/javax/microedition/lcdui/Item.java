package javax.microedition.lcdui;

import java.util.Vector;

public abstract class Item {
    public static final int LAYOUT_DEFAULT = 0, LAYOUT_LEFT = 1, LAYOUT_RIGHT = 2, LAYOUT_CENTER = 3, LAYOUT_TOP = 0x10,
        LAYOUT_BOTTOM = 0x20, LAYOUT_VCENTER = 0x30, LAYOUT_NEWLINE_BEFORE = 0x100, LAYOUT_NEWLINE_AFTER = 0x200,
        LAYOUT_SHRINK = 0x400, LAYOUT_EXPAND = 0x800, LAYOUT_VSHRINK = 0x1000, LAYOUT_VEXPAND = 0x2000, LAYOUT_2 = 0x4000;
    public static final int PLAIN = 0, HYPERLINK = 1, BUTTON = 2;

    String label;
    Screen owner;
    int layout;
    final Vector commands = new Vector();
    Command defaultCommand;
    ItemCommandListener commandListener;
    int prefW = -1, prefH = -1;

    Item(String label) { this.label = label; }

    public String getLabel() { return label; }
    public void setLabel(String l) { label = l; invalidate0(); }
    public int getLayout() { return layout; }
    public void setLayout(int l) { layout = l; invalidate0(); }
    public void addCommand(Command c) {
        if (c == null) throw new NullPointerException();
        if (!commands.contains(c)) commands.addElement(c);
        invalidate0();
    }
    public void removeCommand(Command c) {
        commands.removeElement(c);
        if (c == defaultCommand) defaultCommand = null;
        invalidate0();
    }
    public void setItemCommandListener(ItemCommandListener l) { commandListener = l; }
    public int getPreferredWidth() { return prefW >= 0 ? prefW : Display.W - 8; }
    public int getPreferredHeight() { return prefH >= 0 ? prefH : totalHeight(Display.W - 8); }
    public void setPreferredSize(int w, int h) {
        if (w < -1 || h < -1) throw new IllegalArgumentException();
        prefW = w; prefH = h; invalidate0();
    }
    public int getMinimumWidth() { return 16; }
    public int getMinimumHeight() { return 8; }
    public void setDefaultCommand(Command c) {
        defaultCommand = c;
        if (c != null) addCommand(c);
    }
    public void notifyStateChanged() {
        if (owner instanceof Form) ((Form) owner).itemChanged(this);
    }

    void invalidate0() { if (owner != null) owner.repaintIfShown(); }

    int labelHeight() { return label != null && label.length() > 0 ? Screen.labelFont().getHeight() : 0; }
    int totalHeight(int w) { return labelHeight() + contentHeight(w); }

    abstract int contentHeight(int w);
    abstract void paintContent(Graphics g, int x, int y, int w, boolean focused);
    boolean focusable() { return !commands.isEmpty(); }
    /* returns true if consumed */
    boolean key(int gameAction, int code) { return false; }
    void activate() {
        if (defaultCommand != null && commandListener != null) commandListener.commandAction(defaultCommand, this);
    }
    void paintItem(Graphics g, int x, int y, int w, boolean focused) {
        if (label != null && label.length() > 0) {
            g.setFont(Screen.labelFont());
            g.setColor(focused ? Screen.HL_BG : 0x404040);
            g.drawString(label, x, y, Graphics.TOP | Graphics.LEFT);
            y += labelHeight();
        }
        paintContent(g, x, y, w, focused);
    }
}
