import javax.microedition.midlet.*;
import javax.microedition.lcdui.*;

/* paint() waits (sleeping, holding the canvas lock) until showNotify() ran, and setCurrent is
   called from a game thread: must not deadlock (regression for Heroes Lore). */
public class SwitchTest extends MIDlet {
    protected void startApp() {
        final Display d = Display.getDisplay(this);
        final C first = new C("first"), second = new C("second");
        d.setCurrent(first);
        new Thread() {
            public void run() {
                try { Thread.sleep(200); } catch (InterruptedException e) {}
                synchronized (second) { d.setCurrent(second); second.repaint(); }
            }
        }.start();
    }
    protected void pauseApp() {}
    protected void destroyApp(boolean u) {}
    static class C extends Canvas {
        volatile boolean shown; String name; int paints;
        C(String n) { name = n; }
        protected synchronized void showNotify() { shown = true; System.out.println("showNotify " + name); }
        protected synchronized void paint(Graphics g) {
            int guard = 0;
            while (!shown && guard++ < 100) { try { Thread.sleep(20); } catch (InterruptedException e) {} }
            if (!shown) System.out.println("PAINT BEFORE SHOWNOTIFY " + name);
            if (paints++ == 0) System.out.println("painted " + name);
            g.setColor(0x004000); g.fillRect(0, 0, getWidth(), getHeight());
            g.setColor(0xffffff); g.drawString(name, 10, 10, 0);
        }
    }
}
