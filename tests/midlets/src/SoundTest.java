import javax.microedition.midlet.*;
import javax.microedition.lcdui.*;
import javax.microedition.media.*;

/* Loops a MIDI tune while animating, to check audio + game loop together. */
public class SoundTest extends MIDlet implements Runnable {
    C c = new C();
    protected void startApp() {
        Display.getDisplay(this).setCurrent(c);
        try {
            Player p = Manager.createPlayer(getClass().getResourceAsStream("/music.mid"), "audio/midi");
            p.setLoopCount(-1);
            p.start();
            System.out.println("music started, state=" + p.getState());
        } catch (Exception e) { e.printStackTrace(); }
        new Thread(this).start();
    }
    public void run() {
        long t0 = System.currentTimeMillis();
        while (true) {
            c.frame++;
            c.repaint();
            c.serviceRepaints();
            if (c.frame == 120) System.out.println("120 frames in " + (System.currentTimeMillis() - t0) + " ms");
            try { Thread.sleep(16); } catch (InterruptedException e) {}
        }
    }
    protected void pauseApp() {}
    protected void destroyApp(boolean u) {}
    static class C extends Canvas {
        int frame;
        protected void paint(Graphics g) {
            g.setColor(0x202040); g.fillRect(0, 0, getWidth(), getHeight());
            g.setColor(0xffff00); g.fillArc(100 + (int) (60 * Math.sin(frame / 10.0)), 150, 30, 30, 0, 360);
            g.setColor(0xffffff); g.drawString("frame " + frame, 10, 10, 0);
        }
    }
}
