import javax.microedition.lcdui.*;
import javax.microedition.midlet.MIDlet;

/* Prints the key codes a game receives, and what getGameAction/getKeyCode/getKeyName say about
 * them, so the phone key profiles (--phone N) can be checked. */
public class KeyTest extends MIDlet {
    protected void startApp() {
        Display.getDisplay(this).setCurrent(new Canvas() {
            protected void paint(Graphics g) { g.setColor(0); g.fillRect(0, 0, getWidth(), getHeight()); }
            protected void keyPressed(int k) {
                System.out.println("KeyTest key " + k + " action " + getGameAction(k) + " name " + getKeyName(k));
            }
            protected void showNotify() {
                System.out.println("KeyTest getKeyCode UP " + getKeyCode(UP) + " FIRE " + getKeyCode(FIRE)
                    + " platform " + System.getProperty("microedition.platform"));
            }
        });
    }
    protected void pauseApp() {}
    protected void destroyApp(boolean u) {}
}
