package javax.microedition.lcdui;

/* Runtime-internal bridge used by other packages (game API, Nokia UI). Not part of MIDP. */
public final class Internal {
    private Internal() {}
    public static Graphics newGraphics(Image img, int boundW, int boundH) {
        Graphics g = new Graphics(img);
        g.setBounds(0, 0, Math.min(boundW, img.width), Math.min(boundH, img.height));
        return g;
    }
    public static int[] pixels(Image img) { return img.pixels; }
    public static boolean hasAlpha(Image img) { return img.alpha != 0; }
    public static void setAlpha(Image img, boolean a) { img.alpha = a ? 1 : 0; }
    public static Image newMutableImage(int w, int h, int argb) {
        Image img = new Image(w, h, true);
        Image.fill(img.pixels, argb);
        if ((argb >>> 24) != 0xff) img.alpha = 1;
        return img;
    }
    public static void flushGameCanvas(Canvas c, Image buffer) {
        synchronized (Display.paintLock) { Display.presentGameCanvas(c, buffer); }
    }
    public static int screenWidth() { return Display.W; }
    public static int screenHeight() { return Display.H; }
    public static void setARGBColor(Graphics g, int argb) { g.setARGB(argb); }
    public static int getARGBColor(Graphics g) { return g.color; }
    public static void fillPolygon(Graphics g, int[] xs, int xOff, int[] ys, int yOff, int n) { g.fillPolygon0(xs, xOff, ys, yOff, n); }
    public static void drawRegion(Graphics g, Image src, int sx, int sy, int w, int h, int transform, int dx, int dy, int anchor) {
        g.drawRegion0(src, sx, sy, w, h, transform, dx, dy, anchor);
    }
    public static int gameAction(int keyCode) { return Canvas.gameActionOf(keyCode); }
}
