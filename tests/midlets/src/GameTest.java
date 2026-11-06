import javax.microedition.midlet.*;
import javax.microedition.lcdui.*;
import javax.microedition.lcdui.game.*;
import javax.microedition.rms.*;

public class GameTest extends MIDlet {
    G canvas;
    protected void startApp() {
        if (canvas == null) {
            canvas = new G(this);
            Display.getDisplay(this).setCurrent(canvas);
            new Thread(canvas).start();
        }
    }
    protected void pauseApp() {}
    protected void destroyApp(boolean u) { canvas.running = false; }

    static class G extends GameCanvas implements Runnable {
        volatile boolean running = true;
        Sprite player;
        TiledLayer bg;
        LayerManager lm = new LayerManager();
        int frames, fires, best;
        String lastKey = "-";
        GameTest m;
        G(GameTest m) {
            super(false);
            this.m = m;
            setFullScreenMode(true);
            try {
                Image sp = Image.createImage("/sprites.png");
                player = new Sprite(sp, 16, 16);
                player.defineReferencePixel(8, 8);
                player.setRefPixelPosition(120, 160);
                Image tiles = Image.createImage("/tiles.png");
                bg = new TiledLayer(20, 25, tiles, 16, 16);
                int anim = bg.createAnimatedTile(1);
                for (int r = 0; r < 25; r++) for (int c = 0; c < 20; c++) bg.setCell(c, r, ((r + c) % 3) + 1);
                bg.fillCells(5, 5, 3, 3, anim);
                lm.append(player);
                lm.append(bg);
            } catch (Exception e) { e.printStackTrace(); }
            try {
                RecordStore rs = RecordStore.openRecordStore("score", true);
                if (rs.getNumRecords() > 0) {
                    byte[] b = rs.getRecord(1);
                    best = ((b[0] & 255) << 8) | (b[1] & 255);
                }
                rs.closeRecordStore();
            } catch (Exception e) { e.printStackTrace(); }
            System.out.println("GameTest: loaded best=" + best);
        }
        protected void keyPressed(int k) { lastKey = getKeyName(k) + "(" + k + ")"; if (getGameAction(k) == FIRE) fires++; }
        public void run() {
            Graphics g = getGraphics();
            long start = System.currentTimeMillis();
            while (running) {
                int ks = getKeyStates();
                if ((ks & LEFT_PRESSED) != 0) player.move(-3, 0);
                if ((ks & RIGHT_PRESSED) != 0) player.move(3, 0);
                if ((ks & UP_PRESSED) != 0) player.move(0, -3);
                if ((ks & DOWN_PRESSED) != 0) player.move(0, 3);
                if ((frames & 7) == 0) player.nextFrame();
                if ((frames & 15) == 0) bg.setAnimatedTile(-1, (frames / 16) % 4 + 1);
                player.setTransform((frames / 30) % 2 == 0 ? Sprite.TRANS_NONE : Sprite.TRANS_MIRROR);
                lm.setViewWindow(0, 0, getWidth(), getHeight());
                lm.paint(g, 0, 0);
                g.setColor(0xffffff);
                g.drawString("frames " + frames + " fps " + (frames * 1000 / Math.max(1, System.currentTimeMillis() - start)), 2, 2, 0);
                g.drawString("key " + lastKey + " fires " + fires + " best " + best, 2, 18, 0);
                g.drawString("pos " + player.getRefPixelX() + "," + player.getRefPixelY() + " col=" + player.collidesWith(bg, true), 2, 34, 0);
                flushGraphics();
                frames++;
                if (fires > best) {
                    best = fires;
                    try {
                        RecordStore rs = RecordStore.openRecordStore("score", true);
                        byte[] b = { (byte) (best >> 8), (byte) best };
                        if (rs.getNumRecords() == 0) rs.addRecord(b, 0, 2); else rs.setRecord(1, b, 0, 2);
                        rs.closeRecordStore();
                    } catch (Exception e) { e.printStackTrace(); }
                }
                try { Thread.sleep(30); } catch (InterruptedException e) {}
            }
        }
    }
}
