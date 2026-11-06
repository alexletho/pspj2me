import javax.microedition.midlet.*;
import javax.microedition.lcdui.*;

/* Game loop driven by Display.callSerially (the runnable re-queues itself every frame). */
public class SerialTest extends MIDlet {
    protected void startApp() {
        final Display d = Display.getDisplay(this);
        final C c = new C();
        d.setCurrent(c);
        d.callSerially(new Runnable() {
            public void run() {
                c.frame++;
                c.repaint();
                d.callSerially(this);
            }
        });
    }
    protected void pauseApp() {}
    protected void destroyApp(boolean u) { System.out.println("SerialTest destroyApp frame=" + C.last); }
    static class C extends Canvas {
        int frame; static int last;
        protected void paint(Graphics g) {
            last = frame;
            g.setColor(0x000040); g.fillRect(0, 0, getWidth(), getHeight());
            g.setColor(0xffffff); g.drawString("serial frame " + frame, 10, 10, 0);
        }
    }
}
