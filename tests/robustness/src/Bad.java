import javax.microedition.midlet.*;
import javax.microedition.lcdui.*;
import javax.microedition.rms.*;
import java.util.Vector;

/* Misbehaving MIDlets. Each nested class is a separate MIDlet-1 in its own jar (see build.sh). */
public class Bad {
    public static class CtorThrows extends MIDlet {
        public CtorThrows() { throw new RuntimeException("boom in constructor"); }
        protected void startApp() {} protected void pauseApp() {} protected void destroyApp(boolean u) {}
    }
    public static class StartThrows extends MIDlet {
        protected void startApp() { throw new IllegalStateException("boom in startApp"); }
        protected void pauseApp() {} protected void destroyApp(boolean u) {}
    }
    public static class Recursion extends MIDlet {
        static int depth(int n) { return depth(n + 1) + 1; }
        protected void startApp() {
            try { depth(0); } catch (StackOverflowError e) { System.out.println("caught StackOverflowError"); }
            new Thread() { public void run() { depth(0); } }.start();   /* uncaught in a thread */
            Display.getDisplay(this).setCurrent(new Form("still alive"));
        }
        protected void pauseApp() {} protected void destroyApp(boolean u) {}
    }
    public static class OutOfMemory extends MIDlet {
        protected void startApp() {
            Vector v = new Vector();
            try { while (true) v.addElement(new int[256 * 1024]); }
            catch (OutOfMemoryError e) { int n = v.size(); v = null; System.out.println("caught OOM after " + n + " MB"); }
            try { Image.createImage(4000, 4000); System.out.println("huge image ok"); }
            catch (OutOfMemoryError e) { System.out.println("caught OOM for huge image"); }
            new Thread() { public void run() { Vector w = new Vector(); while (true) w.addElement(new byte[1 << 20]); } }.start();
            Display.getDisplay(this).setCurrent(new Form("still alive after OOM"));
        }
        protected void pauseApp() {} protected void destroyApp(boolean u) {}
    }
    public static class MissingApi extends MIDlet {
        protected void startApp() {
            try { Class.forName("javax.microedition.m3g.Graphics3D"); } catch (ClassNotFoundException e) { System.out.println("no M3G (expected)"); }
            try { javax.microedition.io.Connector.open("http://example.com"); } catch (Exception e) { System.out.println("no network: " + e); }
            Display.getDisplay(this).setCurrent(new Form("missing api handled"));
            useBluetooth();   /* references a class that doesn't exist -> NoClassDefFoundError */
        }
        void useBluetooth() { new BtUser().go(); }
        protected void pauseApp() {} protected void destroyApp(boolean u) {}
    }
    public static class InstantExit extends MIDlet {
        protected void startApp() { notifyDestroyed(); }
        protected void pauseApp() {} protected void destroyApp(boolean u) {}
    }
    public static class SysExit extends MIDlet {
        protected void startApp() { System.exit(3); }
        protected void pauseApp() {} protected void destroyApp(boolean u) {}
    }
    public static class Hang extends MIDlet {
        protected void startApp() {
            Display.getDisplay(this).setCurrent(new Canvas() { protected void paint(Graphics g) { while (true) { } } });
        }
        protected void pauseApp() {}
        protected void destroyApp(boolean u) { while (true) { } }   /* ignores exit requests */
    }
    public static class BadRms extends MIDlet {
        protected void startApp() {
            try {
                RecordStore rs = RecordStore.openRecordStore("save", true);
                if (rs.getNumRecords() == 0) rs.addRecord(new byte[] { 1, 2, 3 }, 0, 3);
                System.out.println("records: " + rs.getNumRecords());
                rs.closeRecordStore();
            } catch (Exception e) { System.out.println("rms exception (handled): " + e); }
            Display.getDisplay(this).setCurrent(new Form("rms ok"));
        }
        protected void pauseApp() {} protected void destroyApp(boolean u) {}
    }
    public static class BadImages extends MIDlet {
        protected void startApp() {
            byte[] junk = new byte[100];
            for (int i = 0; i < junk.length; i++) junk[i] = (byte) (i * 7);
            try { Image.createImage(junk, 0, junk.length); } catch (IllegalArgumentException e) { System.out.println("bad image data rejected"); }
            try { Image.createImage("/missing.png"); } catch (java.io.IOException e) { System.out.println("missing image: IOException"); }
            byte[] trunc = { (byte) 137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82, 0, 0, 1, 0, 0, 0, 1, 0, 8, 6, 0, 0, 0 };
            try { Image.createImage(trunc, 0, trunc.length); } catch (IllegalArgumentException e) { System.out.println("truncated png rejected"); }
            Display.getDisplay(this).setCurrent(new Form("images ok"));
        }
        protected void pauseApp() {} protected void destroyApp(boolean u) {}
    }
    public static class NotAMidlet { }
}
