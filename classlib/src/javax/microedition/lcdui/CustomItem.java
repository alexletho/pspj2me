package javax.microedition.lcdui;

public abstract class CustomItem extends Item {
    protected static final int TRAVERSE_HORIZONTAL = 1, TRAVERSE_VERTICAL = 2;
    protected static final int KEY_PRESS = 4, KEY_RELEASE = 8, KEY_REPEAT = 0x10, POINTER_PRESS = 0x20,
        POINTER_RELEASE = 0x40, POINTER_DRAG = 0x80, NONE = 0;

    protected CustomItem(String label) { super(label); }

    public int getGameAction(int keyCode) { return Canvas.gameActionOf(keyCode); }
    protected final int getInteractionModes() { return KEY_PRESS | KEY_RELEASE | KEY_REPEAT | TRAVERSE_VERTICAL; }
    protected abstract int getMinContentWidth();
    protected abstract int getMinContentHeight();
    protected abstract int getPrefContentWidth(int height);
    protected abstract int getPrefContentHeight(int width);
    protected void sizeChanged(int w, int h) {}
    protected final void invalidate() { invalidate0(); }
    protected abstract void paint(Graphics g, int w, int h);
    protected final void repaint() { invalidate0(); }
    protected final void repaint(int x, int y, int w, int h) { invalidate0(); }
    protected boolean traverse(int dir, int viewportWidth, int viewportHeight, int[] visRect_inout) { return false; }
    protected void traverseOut() {}
    protected void keyPressed(int keyCode) {}
    protected void keyReleased(int keyCode) {}
    protected void keyRepeated(int keyCode) {}
    protected void pointerPressed(int x, int y) {}
    protected void pointerReleased(int x, int y) {}
    protected void pointerDragged(int x, int y) {}
    protected void showNotify() {}
    protected void hideNotify() {}

    int contentHeight(int w) {
        int h = prefH >= 0 ? prefH : getPrefContentHeight(-1);
        return Math.max(h, getMinContentHeight());
    }
    void paintContent(Graphics g, int x, int y, int w, boolean focused) {
        int cw = Math.min(w, Math.max(getMinContentWidth(), prefW >= 0 ? prefW : getPrefContentWidth(-1)));
        int ch = contentHeight(w);
        int ox = g.getClipX(), oy = g.getClipY(), ow = g.getClipWidth(), oh = g.getClipHeight();
        g.translate(x, y);
        g.clipRect(0, 0, cw, ch);
        try { paint(g, cw, ch); } catch (Throwable t) { pspj2me.Native.gameError(t); }
        g.translate(-x, -y);
        g.setClip(ox, oy, ow, oh);
    }
    boolean focusable() { return true; }
    boolean key(int ga, int code) {
        try { keyPressed(code); } catch (Throwable t) { pspj2me.Native.gameError(t); }
        int[] vis = new int[4];
        boolean stay = false;
        try {
            int dir = ga == Canvas.UP ? Canvas.UP : ga == Canvas.DOWN ? Canvas.DOWN : ga == Canvas.LEFT ? Canvas.LEFT : ga == Canvas.RIGHT ? Canvas.RIGHT : 0;
            if (dir != 0) stay = traverse(dir, Display.W, Display.H, vis);
        } catch (Throwable t) { pspj2me.Native.gameError(t); }
        return stay || (ga != Canvas.UP && ga != Canvas.DOWN);
    }
}
