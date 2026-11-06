import javax.microedition.midlet.*;
import javax.microedition.lcdui.*;
import javax.microedition.lcdui.game.Sprite;

public class GfxTest extends MIDlet {
    protected void startApp() {
        Display.getDisplay(this).setCurrent(new C());
    }
    protected void pauseApp() {}
    protected void destroyApp(boolean u) {}

    static class C extends Canvas {
        Image sprites, pal, arrow, rgb, mut;
        C() {
            setFullScreenMode(true);
            try {
                sprites = Image.createImage("/sprites.png");
                pal = Image.createImage("/pal.png");
                arrow = Image.createImage("/arrow.png");
                rgb = Image.createImage("/rgb.png");
            } catch (Exception e) { e.printStackTrace(); }
            mut = Image.createImage(40, 30);
            Graphics mg = mut.getGraphics();
            mg.setColor(0x880088);
            mg.fillRect(0, 0, 40, 30);
            mg.setColor(0xffffff);
            mg.drawString("mut", 20, 8, Graphics.TOP | Graphics.HCENTER);
        }
        protected void paint(Graphics g) {
            int w = getWidth(), h = getHeight();
            g.setColor(0x223344);
            g.fillRect(0, 0, w, h);
            // gradient
            for (int i = 0; i < w; i++) { g.setColor(i * 255 / w, 64, 255 - i * 255 / w); g.drawLine(i, 0, i, 8); }
            g.setColor(0xffffff);
            g.setFont(Font.getFont(Font.FACE_SYSTEM, Font.STYLE_BOLD, Font.SIZE_LARGE));
            g.drawString("PSPJ2ME gfx test", w / 2, 12, Graphics.TOP | Graphics.HCENTER);
            g.setFont(Font.getFont(Font.FACE_SYSTEM, Font.STYLE_PLAIN, Font.SIZE_SMALL));
            g.drawString("Small: Привет éàü 123", 4, 34, 0);
            g.setFont(Font.getDefaultFont());
            g.drawString("Medium " + w + "x" + h, 4, 48, 0);
            // shapes
            g.setColor(0xff4040); g.fillRect(4, 66, 30, 20);
            g.setColor(0x40ff40); g.drawRect(38, 66, 30, 20);
            g.setColor(0x4080ff); g.fillRoundRect(72, 66, 30, 20, 10, 10);
            g.setColor(0xffff40); g.drawRoundRect(106, 66, 30, 20, 10, 10);
            g.setColor(0xff80ff); g.fillArc(140, 62, 28, 28, 0, 360);
            g.setColor(0x80ffff); g.fillArc(172, 62, 28, 28, 45, 270);
            g.setColor(0xffffff); g.drawArc(204, 62, 28, 28, 0, 360);
            g.setColor(0xffa000); g.fillTriangle(4, 120, 34, 92, 64, 120);
            g.setStrokeStyle(Graphics.DOTTED); g.setColor(0xffffff); g.drawLine(70, 92, 130, 120); g.setStrokeStyle(Graphics.SOLID);
            g.drawLine(70, 120, 130, 92);
            // images and transforms
            int y = 128;
            for (int t = 0; t < 8; t++) g.drawRegion(arrow, 0, 0, 24, 24, t, 4 + t * 28, y, Graphics.TOP | Graphics.LEFT);
            y += 30;
            g.drawImage(sprites, 4, y, 0);
            for (int f = 0; f < 4; f++) {
                g.drawRegion(sprites, f * 16, 0, 16, 16, Sprite.TRANS_NONE, 80 + f * 20, y, 0);
            }
            g.drawImage(pal, 170, y - 4, 0);
            g.drawImage(rgb, 210, y, 0);
            y += 36;
            g.drawImage(mut, 4, y, 0);
            // clipping
            g.setClip(60, y, 50, 20);
            g.setColor(0x00ff00);
            g.fillArc(50, y - 10, 70, 40, 0, 360);
            g.setClip(0, 0, w, h);
            // anchors
            g.setColor(0xffffff);
            g.drawLine(180, y, 180, y + 30); g.drawLine(150, y + 15, 210, y + 15);
            g.drawString("R", 180, y + 15, Graphics.RIGHT | Graphics.BASELINE);
            g.drawString("B", 180, y + 15, Graphics.LEFT | Graphics.BOTTOM);
            g.drawImage(arrow, 180, y + 15, Graphics.HCENTER | Graphics.VCENTER);
            // drawRGB alpha
            int[] px = new int[40 * 20];
            for (int i = 0; i < px.length; i++) px[i] = ((i % 40) * 6 << 24) | 0xff0000;
            g.drawRGB(px, 0, 40, 4, y + 34, 40, 20, true);
            // translate
            g.translate(120, y + 34);
            g.setColor(0x00ffff);
            g.drawRect(0, 0, 20, 20);
            g.translate(-120, -(y + 34));
            g.setColor(0xaaaaaa);
            String info = "free=" + (Runtime.getRuntime().freeMemory() / 1024) + "K";
            g.drawString(info, w - 2, h - 2, Graphics.RIGHT | Graphics.BOTTOM);
        }
    }
}
