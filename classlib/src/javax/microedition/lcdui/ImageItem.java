package javax.microedition.lcdui;

public class ImageItem extends Item {
    private Image img;
    private String altText;
    private int appearance;

    public ImageItem(String label, Image img, int layout, String altText) { this(label, img, layout, altText, PLAIN); }
    public ImageItem(String label, Image img, int layout, String altText, int appearanceMode) {
        super(label);
        this.img = img; this.layout = layout; this.altText = altText; appearance = appearanceMode;
    }
    public Image getImage() { return img; }
    public void setImage(Image i) { img = i; invalidate0(); }
    public String getAltText() { return altText; }
    public void setAltText(String t) { altText = t; }
    public int getAppearanceMode() { return appearance; }

    int contentHeight(int w) { return img == null ? 0 : img.getHeight() + 2; }
    void paintContent(Graphics g, int x, int y, int w, boolean focused) {
        if (img == null) return;
        int h = layout & 3;
        int ix = h == LAYOUT_CENTER ? x + (w - img.getWidth()) / 2 : h == LAYOUT_RIGHT ? x + w - img.getWidth() : x;
        g.drawImage(img, ix, y, Graphics.TOP | Graphics.LEFT);
        if (focused) { g.setColor(Screen.HL_BG); g.drawRect(ix - 1, y - 1, img.getWidth() + 1, img.getHeight() + 1); }
    }
}
