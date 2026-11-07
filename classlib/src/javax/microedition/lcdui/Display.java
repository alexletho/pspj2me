package javax.microedition.lcdui;

import java.util.Vector;
import javax.microedition.midlet.MIDlet;
import pspj2me.Native;

public class Display {
    public static final int LIST_ELEMENT = 1, CHOICE_GROUP_ELEMENT = 2, ALERT = 3;
    public static final int COLOR_BACKGROUND = 0, COLOR_FOREGROUND = 1, COLOR_HIGHLIGHTED_BACKGROUND = 2,
        COLOR_HIGHLIGHTED_FOREGROUND = 3, COLOR_BORDER = 4, COLOR_HIGHLIGHTED_BORDER = 5;

    static final Command MENU = new Command("Menu", Command.SCREEN, 0);
    static final Command MENU_SELECT = new Command("Select", Command.OK, 0);
    static final Command MENU_BACK = new Command("Back", Command.BACK, 0);

    static Display instance;
    static MIDlet midlet;
    static int W, H;
    static int SOFTBAR_H;
    static boolean FORCE_FULL;
    static Image screen;
    static Graphics screenG;
    static final Object paintLock = new Object();
    static volatile boolean repaintPending;
    static boolean painting;
    static final Vector serial = new Vector();

    Displayable current;
    static Displayable pendingNext;

    /* alert state */
    static Displayable alertNext;
    static long alertDeadline;

    /* command menu overlay */
    static Displayable menuOwner;
    static Vector menuItems;
    static int menuSel;

    static {
        W = Native.screenWidth();
        H = Native.screenHeight();
        FORCE_FULL = Native.forceFullScreen();
        SOFTBAR_H = Font.getFont(Font.FACE_SYSTEM, Font.STYLE_BOLD, Font.SIZE_SMALL).getHeight() + 4;
        screen = new Image(W, H, true);
        screenG = new Graphics(screen);
    }

    private Display() {}

    public static Display getDisplay(MIDlet m) {
        if (instance == null) instance = new Display();
        return instance;
    }

    public static void startMidlet(MIDlet m) {
        midlet = m;
        getDisplay(m);
        try {
            MIDlet.callStartApp(m);
        } catch (Throwable t) {
            System.out.println("startApp failed: " + t);
            t.printStackTrace();
            pspj2me.Native.reportProblem(t, 2);
        }
    }

    public Displayable getCurrent() { Displayable p = pendingNext; return p != null ? p : current; }

    public void setCurrent(Displayable next) {
        if (next == null) { requestRepaint(); return; }
        if (next instanceof Alert && current != null && current != next) {
            setCurrent((Alert) next, current);
            return;
        }
        switchTo(next);
    }

    public void setCurrent(Alert alert, Displayable next) {
        if (alert == null || next == null) throw new NullPointerException();
        if (next instanceof Alert) throw new IllegalArgumentException();
        alertNext = next;
        int t = alert.getTimeout();
        alertDeadline = t == Alert.FOREVER ? 0 : System.currentTimeMillis() + t;
        alert.returnTo = next;
        switchTo(alert);
    }

    public void setCurrentItem(Item item) {
        if (item.owner != null) {
            switchTo(item.owner);
            if (item.owner instanceof Form) ((Form) item.owner).focusItem(item);
        }
    }

    /* Like on real phones the switch is asynchronous: the event thread calls hideNotify/showNotify
       and only then paints, so a game never sees paint() before showNotify() and the caller's locks
       can't deadlock against the event thread. */
    void switchTo(Displayable next) {
        System.out.println("[lcdui] show " + next.getClass().getName() + (next instanceof Canvas ? " (canvas)" : ""));
        pendingNext = next;
        Native.wakeEventLoop();
    }

    static void applyPendingSwitch() {
        Displayable next = pendingNext;
        if (next == null || instance == null) return;
        pendingNext = null;
        Displayable old;
        synchronized (paintLock) {
            old = instance.current;
            if (old == next) { requestRepaint(); return; }
            instance.current = next;
            menuOwner = null;
            Native.setOverlayActive(false);
        }
        try {
            if (old != null) old.hideNotify0();
            next.showNotify0();
        } catch (Throwable t) { Native.gameError(t); }
        requestRepaint();
    }

    static void dismissAlert(Alert a) {
        if (instance != null && instance.current == a && a.returnTo != null) {
            alertDeadline = 0;
            instance.switchTo(a.returnTo);
        }
    }

    public void callSerially(Runnable r) {
        serial.addElement(r);
        Native.wakeEventLoop();
    }

    public boolean flashBacklight(int duration) { return true; }
    public boolean vibrate(int duration) {
        if (duration < 0) throw new IllegalArgumentException();
        Native.vibrate(duration);
        return true;
    }
    public boolean isColor() { return true; }
    public int numColors() { return 1 << 24; }
    public int numAlphaLevels() { return 256; }
    public int getColor(int colorSpecifier) {
        switch (colorSpecifier) {
            case COLOR_BACKGROUND: return 0xffffff;
            case COLOR_FOREGROUND: return 0x000000;
            case COLOR_HIGHLIGHTED_BACKGROUND: return 0x3060c0;
            case COLOR_HIGHLIGHTED_FOREGROUND: return 0xffffff;
            case COLOR_BORDER: return 0x808080;
            case COLOR_HIGHLIGHTED_BORDER: return 0x3060c0;
        }
        throw new IllegalArgumentException();
    }
    public int getBorderStyle(boolean highlighted) { return highlighted ? Graphics.SOLID : Graphics.DOTTED; }
    public int getBestImageWidth(int imageType) { return imageType == ALERT ? W : 16; }
    public int getBestImageHeight(int imageType) { return imageType == ALERT ? H / 2 : 16; }

    /* ---------------- painting ---------------- */
    static void requestRepaint() {
        repaintPending = true;
        Native.wakeEventLoop();
    }

    static void serviceRepaints(Displayable d) {
        if (instance == null || instance.current != d) return;
        if (repaintPending) doPaint();
    }

    static void doPaint() {
        synchronized (paintLock) {
            if (painting) return;
            painting = true;
            try {
                repaintPending = false;
                Displayable d = instance == null ? null : instance.current;
                if (d != null) {
                    screenG.setBounds(0, 0, W, H);
                    d.paintScreen(screenG);
                    screenG.setBounds(0, 0, W, H);
                    paintOverlays(screenG);
                    Native.present(screen.pixels, W, H);
                }
            } catch (Throwable t) {
                pspj2me.Native.gameError(t);
            } finally {
                painting = false;
            }
        }
    }

    /* called with paintLock held */
    static void presentGameCanvas(Canvas c, Image buffer) {
        if (instance == null || instance.current != c) return;
        if (c.isFull() && menuOwner == null && buffer.width == W && buffer.height == H) {
            Native.present(buffer.pixels, W, H);
            return;
        }
        screenG.setBounds(0, 0, W, H);
        screenG.drawRegion0(buffer, 0, 0, buffer.width, Math.min(buffer.height, c.getHeight()), 0, 0, 0, Graphics.TOP | Graphics.LEFT);
        if (!c.isFull()) paintSoftBar(screenG, c);
        paintOverlays(screenG);
        Native.present(screen.pixels, W, H);
    }

    static void paintSoftBar(Graphics g, Displayable d) {
        int y = H - SOFTBAR_H;
        g.setColor(0x202020);
        g.fillRect(0, y, W, SOFTBAR_H);
        g.setColor(0x505050);
        g.drawLine(0, y, W, y);
        Command[] sk = d.softKeys();
        Font f = Font.getFont(Font.FACE_SYSTEM, Font.STYLE_BOLD, Font.SIZE_SMALL);
        g.setFont(f);
        g.setColor(0xffffff);
        if (sk[0] != null) g.drawString(sk[0].getLabel(), 3, y + 2, Graphics.TOP | Graphics.LEFT);
        if (sk[1] != null) g.drawString(sk[1].getLabel(), W - 3, y + 2, Graphics.TOP | Graphics.RIGHT);
    }

    static void paintOverlays(Graphics g) {
        if (menuOwner == null || menuItems == null) return;
        Font f = Font.getFont(Font.FACE_SYSTEM, Font.STYLE_PLAIN, Font.SIZE_MEDIUM);
        int lh = f.getHeight() + 4;
        int n = menuItems.size();
        int maxRows = (H - SOFTBAR_H - 8) / lh;
        int rows = Math.min(n, maxRows);
        int mw = W * 3 / 4;
        int mh = rows * lh + 4;
        int mx = 2, my = H - SOFTBAR_H - mh - 1;
        int first = 0;
        if (menuSel >= rows) first = menuSel - rows + 1;
        g.setColor(0xf0f0f0);
        g.fillRect(mx, my, mw, mh);
        g.setColor(0x404040);
        g.drawRect(mx, my, mw - 1, mh - 1);
        g.setFont(f);
        for (int i = 0; i < rows; i++) {
            int idx = first + i;
            Command c = (Command) menuItems.elementAt(idx);
            int ry = my + 2 + i * lh;
            if (idx == menuSel) {
                g.setColor(0x3060c0);
                g.fillRect(mx + 2, ry, mw - 4, lh);
                g.setColor(0xffffff);
            } else g.setColor(0x000000);
            String label = c.getLongLabel() != null ? c.getLongLabel() : c.getLabel();
            g.drawString((idx + 1) + ". " + label, mx + 6, ry + 2, Graphics.TOP | Graphics.LEFT);
        }
        int y = H - SOFTBAR_H;
        g.setColor(0x202020);
        g.fillRect(0, y, W, SOFTBAR_H);
        Font bf = Font.getFont(Font.FACE_SYSTEM, Font.STYLE_BOLD, Font.SIZE_SMALL);
        g.setFont(bf);
        g.setColor(0xffffff);
        g.drawString("Select", 3, y + 2, Graphics.TOP | Graphics.LEFT);
        g.drawString("Back", W - 3, y + 2, Graphics.TOP | Graphics.RIGHT);
    }

    static void openMenu(Displayable owner, Vector items) {
        if (items.isEmpty()) return;
        menuOwner = owner;
        menuItems = items;
        menuSel = 0;
        Native.setOverlayActive(true);
        requestRepaint();
    }

    static void closeMenu() {
        menuOwner = null;
        menuItems = null;
        Native.setOverlayActive(false);
        requestRepaint();
    }

    static void menuKey(int code) {
        int n = menuItems.size();
        int ga = Canvas.gameActionOf(code);
        if (code == -7 || code == -8) { closeMenu(); return; }
        if (ga == Canvas.UP) { menuSel = (menuSel + n - 1) % n; requestRepaint(); return; }
        if (ga == Canvas.DOWN) { menuSel = (menuSel + 1) % n; requestRepaint(); return; }
        int pick = -1;
        if (ga == Canvas.FIRE || code == -6) pick = menuSel;
        else if (code >= '1' && code <= '9' && code - '1' < n) pick = code - '1';
        if (pick >= 0) {
            Displayable o = menuOwner;
            Command c = (Command) menuItems.elementAt(pick);
            closeMenu();
            o.fireCommand(c);
        }
    }

    /* ---------------- event loop ---------------- */
    public static void runEventLoop() {
        int[] ev = new int[3];
        while (true) {
            applyPendingSwitch();
            while (Native.pollEvent(ev)) {
                /* like a phone: an exception in a key/pointer/command handler doesn't end the game */
                try { dispatch(ev); } catch (Throwable t) { Native.gameError(t); }
                applyPendingSwitch();
            }
            if (alertDeadline != 0 && System.currentTimeMillis() >= alertDeadline && instance != null && instance.current instanceof Alert) {
                alertDeadline = 0;
                Alert a = (Alert) instance.current;
                a.timeoutExpired();
            }
            /* MIDP: serial calls run after pending repaints; only run the ones queued so far, so a
               runnable that re-queues itself (a common game loop) doesn't starve painting and input */
            if (repaintPending) doPaint();
            int n = serial.size();
            for (int i = 0; i < n && !serial.isEmpty(); i++) {
                Runnable r = (Runnable) serial.elementAt(0);
                serial.removeElementAt(0);
                try { r.run(); } catch (Throwable t) { pspj2me.Native.gameError(t); }
            }
            if (!serial.isEmpty()) { Native.waitEvent(1); continue; }
            int timeout = 0;
            if (alertDeadline != 0) {
                long left = alertDeadline - System.currentTimeMillis();
                timeout = left <= 0 ? 1 : (int) Math.min(left, 1000);
            }
            Displayable cur = instance == null ? null : instance.current;
            if (cur instanceof Screen && cur.ticker != null) {
                timeout = timeout == 0 ? 100 : Math.min(timeout, 100);
                repaintPending = true;
            }
            Native.waitEvent(timeout);
        }
    }

    static void dispatch(int[] ev) {
        int type = ev[0];
        if (type == Native.EV_EXIT) {
            try { MIDlet.callDestroyApp(midlet, true); } catch (Throwable t) { t.printStackTrace(); }
            Native.exit();
            return;
        }
        if (type == Native.EV_PAUSE) {
            try { MIDlet.callPauseApp(midlet); } catch (Throwable t) { Native.gameError(t); }
            if (instance != null && instance.current != null) instance.current.hideNotify0();
            return;
        }
        if (type == Native.EV_RESUME) {
            if (instance != null && instance.current != null) instance.current.showNotify0();
            try { MIDlet.callStartApp(midlet); } catch (Throwable t) { Native.gameError(t); }
            requestRepaint();
            return;
        }
        Displayable d = instance == null ? null : instance.current;
        if (d == null) return;
        if (type >= Native.EV_KEY_PRESS && type <= Native.EV_KEY_REPEAT) {
            int code = ev[1];
            if (menuOwner != null) {
                if (type != Native.EV_KEY_RELEASE) menuKey(code);
                return;
            }
            if ((code == -6 || code == -7) && d.hasCommands()) {
                if (type == Native.EV_KEY_PRESS) d.softKey(code);
                return;
            }
            d.keyEvent(type, code);
        } else if (type >= Native.EV_PTR_PRESS && type <= Native.EV_PTR_DRAG) {
            d.pointerEvent(type, ev[1], ev[2]);
        }
    }
}
