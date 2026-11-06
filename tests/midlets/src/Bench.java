import javax.microedition.midlet.*;
import javax.microedition.lcdui.*;
import javax.microedition.lcdui.game.*;

/* Measures typical game workloads: logic (calls + fields + arrays), tiles, alpha sprites, text, fills. */
public class Bench extends MIDlet {
    static class Ent { int x, y, vx, vy, hp; Ent next; int step(int t) { x += vx; y += vy; if (x < 0 || x > 240) vx = -vx; if (y < 0 || y > 320) vy = -vy; return hp + t; } }
    static abstract class Base { abstract int f(int a); }
    static class Impl extends Base { int k = 3; int f(int a) { return a * k + 1; } }

    protected void startApp() {
        final Display d = Display.getDisplay(this);
        final BC c = new BC();
        c.setFullScreenMode(true);
        d.setCurrent(c);
        new Thread() { public void run() { try { Thread.sleep(300); bench(c); } catch (Throwable t) { t.printStackTrace(); } } }.start();
    }

    static class BC extends GameCanvas { BC() { super(false); } Graphics gg() { return getGraphics(); } }
    static void bench(BC c) throws Exception {
        // logic
        Ent[] es = new Ent[200];
        for (int i = 0; i < es.length; i++) { es[i] = new Ent(); es[i].vx = i % 5 - 2; es[i].vy = i % 7 - 3; es[i].hp = i; }
        Base b = new Impl();
        int[] arr = new int[1024];
        long t0 = System.currentTimeMillis();
        int acc = 0;
        for (int r = 0; r < 300; r++) {
            for (int i = 0; i < es.length; i++) acc += es[i].step(r);
            for (int i = 0; i < 1024; i++) { arr[i] = arr[(i + 7) & 1023] + b.f(i); }
            StringBuffer sb = new StringBuffer(); sb.append("score ").append(r).append('/').append(acc); acc += sb.toString().length();
        }
        long tLogic = System.currentTimeMillis() - t0;

        Image tiles = Image.createImage("/tiles.png");
        Image spr = Image.createImage("/sprites.png");
        Graphics g = c.gg();
        Font f = Font.getFont(Font.FACE_SYSTEM, Font.STYLE_PLAIN, Font.SIZE_SMALL);
        long tTile = 0, tSpr = 0, tText = 0, tFill = 0, tFlush = 0;
        int frames = 40;
        for (int fr = 0; fr < frames; fr++) {
            long a = System.currentTimeMillis();
            for (int y = 0; y < 20; y++) for (int x = 0; x < 15; x++)
                g.drawRegion(tiles, ((x + y + fr) & 3) * 16, 0, 16, 16, 0, x * 16, y * 16, 20);
            long bb = System.currentTimeMillis();
            for (int i = 0; i < 60; i++) g.drawRegion(spr, (i & 3) * 16, 0, 16, 16, i & 7, (i * 37 + fr * 3) % 224, (i * 53) % 304, 20);
            long cc = System.currentTimeMillis();
            g.setFont(f);
            g.setColor(0xffffff);
            for (int i = 0; i < 20; i++) g.drawString("Здоровье HP 120/150 line " + i, 4, i * 15, 20);
            long dd = System.currentTimeMillis();
            g.setColor(0x203040);
            for (int i = 0; i < 30; i++) g.fillRect(i * 7, i * 9, 60, 40);
            long ee = System.currentTimeMillis();
            c.flushGraphics();
            long ff = System.currentTimeMillis();
            tTile += bb - a; tSpr += cc - bb; tText += dd - cc; tFill += ee - dd; tFlush += ff - ee;
        }
        System.out.println("BENCH logic " + tLogic + " ms (acc " + acc + ")");
        System.out.println("BENCH per frame: tiles " + (tTile * 10 / frames) / 10.0 + " sprites " + (tSpr * 10 / frames) / 10.0
            + " text " + (tText * 10 / frames) / 10.0 + " fills " + (tFill * 10 / frames) / 10.0 + " flush " + (tFlush * 10 / frames) / 10.0 + " ms");
    }
    protected void pauseApp() {}
    protected void destroyApp(boolean u) {}
}
