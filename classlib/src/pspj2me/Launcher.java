package pspj2me;

import javax.microedition.midlet.MIDlet;
import javax.microedition.lcdui.Display;

public final class Launcher {
    public static MIDlet midlet;

    public static void main(String className) {
        try {
            Class c = Class.forName(className);
            midlet = (MIDlet) c.newInstance();
        } catch (Throwable t) {
            System.out.println("Cannot instantiate MIDlet " + className + ": " + t);
            t.printStackTrace();
            Native.reportProblem(t, 1);
            Native.exit();
            return;
        }
        Display.startMidlet(midlet);
        Display.runEventLoop();
    }
}
