package javax.microedition.lcdui;

import java.util.Vector;

public abstract class Displayable {
    String title;
    Ticker ticker;
    final Vector commands = new Vector();
    CommandListener listener;
    boolean fullScreen;

    Displayable() {}

    public String getTitle() { return title; }
    public void setTitle(String s) { title = s; repaintIfShown(); }
    public Ticker getTicker() { return ticker; }
    public void setTicker(Ticker t) { ticker = t; repaintIfShown(); }
    public boolean isShown() { Display d = Display.instance; return d != null && d.current == this; }

    public void addCommand(Command cmd) {
        if (cmd == null) throw new NullPointerException();
        synchronized (commands) {
            if (commands.contains(cmd)) return;
            /* keep sorted by priority (stable) */
            int i = 0;
            while (i < commands.size() && ((Command) commands.elementAt(i)).getPriority() <= cmd.getPriority()) i++;
            commands.insertElementAt(cmd, i);
        }
        repaintIfShown();
    }
    public void removeCommand(Command cmd) {
        synchronized (commands) { commands.removeElement(cmd); }
        repaintIfShown();
    }
    public void setCommandListener(CommandListener l) { listener = l; }

    public int getWidth() { return Display.W; }
    public int getHeight() { return Display.H - Display.SOFTBAR_H; }

    protected void sizeChanged(int w, int h) {}

    void repaintIfShown() { if (isShown()) Display.requestRepaint(); }

    /* --- runtime hooks --- */
    void showNotify0() {}
    void hideNotify0() {}
    abstract void paintScreen(Graphics g);
    void keyEvent(int type, int code) {}
    void pointerEvent(int type, int x, int y) {}

    /* soft key mapping: [0]=left, [1]=right; left may be Display.MENU when several commands */
    Command[] softKeys() {
        Command left = null, right = null;
        int others = 0;
        synchronized (commands) {
            for (int i = 0; i < commands.size(); i++) {
                Command c = (Command) commands.elementAt(i);
                if (right == null && c.isNegative()) right = c;
            }
            for (int i = 0; i < commands.size(); i++) {
                Command c = (Command) commands.elementAt(i);
                if (c == right) continue;
                others++;
                if (left == null) left = c;
            }
            if (right == null && others == 2) {
                right = (Command) commands.elementAt(1);
                others = 1;
            }
        }
        if (others > 1) left = Display.MENU;
        return new Command[] { left, right };
    }

    Vector menuCommands() {
        Command[] sk = softKeys();
        Vector v = new Vector();
        synchronized (commands) {
            for (int i = 0; i < commands.size(); i++) if (commands.elementAt(i) != sk[1]) v.addElement(commands.elementAt(i));
        }
        return v;
    }

    boolean hasCommands() { return !commands.isEmpty(); }

    void fireCommand(Command c) {
        CommandListener l = listener;
        if (l != null) {
            try { l.commandAction(c, this); } catch (Throwable t) { pspj2me.Native.gameError(t); }
        }
    }

    /* returns true if a soft key press was consumed by commands */
    boolean softKey(int code) {
        if (!hasCommands()) return false;
        Command[] sk = softKeys();
        Command c = code == -6 ? sk[0] : sk[1];
        if (c == null) return false;
        if (c == Display.MENU) Display.openMenu(this, menuCommands());
        else fireCommand(c);
        return true;
    }
}
