package javax.microedition.lcdui.game;

import javax.microedition.lcdui.*;

public abstract class GameCanvas extends Canvas {
    public static final int UP_PRESSED = 1 << Canvas.UP, DOWN_PRESSED = 1 << Canvas.DOWN, LEFT_PRESSED = 1 << Canvas.LEFT,
        RIGHT_PRESSED = 1 << Canvas.RIGHT, FIRE_PRESSED = 1 << Canvas.FIRE, GAME_A_PRESSED = 1 << Canvas.GAME_A,
        GAME_B_PRESSED = 1 << Canvas.GAME_B, GAME_C_PRESSED = 1 << Canvas.GAME_C, GAME_D_PRESSED = 1 << Canvas.GAME_D;

    private final Image buffer;
    private final boolean suppress;
    private int keyStates, latched;

    protected GameCanvas(boolean suppressKeyEvents) {
        suppress = suppressKeyEvents;
        buffer = Internal.newMutableImage(Internal.screenWidth(), Internal.screenHeight(), 0xffffffff);
    }

    protected Graphics getGraphics() { return Internal.newGraphics(buffer, getWidth(), getHeight()); }

    public int getKeyStates() {
        int r = keyStates | latched;
        latched = 0;
        return r;
    }

    public void paint(Graphics g) { g.drawImage(buffer, 0, 0, Graphics.TOP | Graphics.LEFT); }

    public void flushGraphics(int x, int y, int width, int height) { flushGraphics(); }
    public void flushGraphics() { Internal.flushGameCanvas(this, buffer); }

    /* runtime hook: returns true if the key event must be suppressed */
    public final boolean pspj2me$keyState(int type, int code) {
        int ga = Internal.gameAction(code);
        if (ga == 0) return false;
        int bit = 1 << ga;
        if (type == 1) { keyStates |= bit; latched |= bit; }
        else if (type == 2) keyStates &= ~bit;
        return suppress;
    }
}
