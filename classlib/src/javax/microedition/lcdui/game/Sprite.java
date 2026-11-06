package javax.microedition.lcdui.game;

import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;
import javax.microedition.lcdui.Internal;

public class Sprite extends Layer {
    public static final int TRANS_NONE = 0, TRANS_ROT90 = 5, TRANS_ROT180 = 3, TRANS_ROT270 = 6, TRANS_MIRROR = 2,
        TRANS_MIRROR_ROT90 = 7, TRANS_MIRROR_ROT180 = 1, TRANS_MIRROR_ROT270 = 4;

    private Image image;
    private int frameW, frameH, numFrames, framesPerRow;
    private int[] sequence;
    private int seqIndex;
    private int refX, refY;
    private int transform;
    private int colX, colY, colW, colH;          /* untransformed collision rect */
    private int tColX, tColY, tColW, tColH;      /* transformed */

    public Sprite(Image image) {
        super(image.getWidth(), image.getHeight());
        initFrames(image, image.getWidth(), image.getHeight(), false);
        initCollision();
        setTransformImpl(TRANS_NONE);
    }

    public Sprite(Image image, int frameWidth, int frameHeight) {
        super(frameWidth, frameHeight);
        if (frameWidth < 1 || frameHeight < 1 || image.getWidth() % frameWidth != 0 || image.getHeight() % frameHeight != 0)
            throw new IllegalArgumentException();
        initFrames(image, frameWidth, frameHeight, false);
        initCollision();
        setTransformImpl(TRANS_NONE);
    }

    public Sprite(Sprite s) {
        super(s.getWidth(), s.getHeight());
        image = s.image;
        frameW = s.frameW; frameH = s.frameH; numFrames = s.numFrames; framesPerRow = s.framesPerRow;
        sequence = new int[s.sequence.length];
        System.arraycopy(s.sequence, 0, sequence, 0, sequence.length);
        seqIndex = s.seqIndex;
        x = s.x; y = s.y; refX = s.refX; refY = s.refY;
        colX = s.colX; colY = s.colY; colW = s.colW; colH = s.colH;
        transform = s.transform;
        visible = s.visible;
        computeTransformedBounds(transform);
    }

    private void initFrames(Image img, int fw, int fh, boolean keepSeq) {
        image = img;
        frameW = fw; frameH = fh;
        framesPerRow = img.getWidth() / fw;
        numFrames = framesPerRow * (img.getHeight() / fh);
        if (!keepSeq) {
            sequence = new int[numFrames];
            for (int i = 0; i < numFrames; i++) sequence[i] = i;
            seqIndex = 0;
        }
    }

    private void initCollision() { colX = 0; colY = 0; colW = frameW; colH = frameH; }

    public void defineReferencePixel(int x, int y) { refX = x; refY = y; }

    public void setRefPixelPosition(int x, int y) {
        this.x = x - tPtX(refX, refY, transform);
        this.y = y - tPtY(refX, refY, transform);
    }
    public int getRefPixelX() { return x + tPtX(refX, refY, transform); }
    public int getRefPixelY() { return y + tPtY(refX, refY, transform); }

    public void setFrame(int sequenceIndex) {
        if (sequenceIndex < 0 || sequenceIndex >= sequence.length) throw new IndexOutOfBoundsException();
        seqIndex = sequenceIndex;
    }
    public final int getFrame() { return seqIndex; }
    public int getRawFrameCount() { return numFrames; }
    public int getFrameSequenceLength() { return sequence.length; }
    public void nextFrame() { seqIndex = (seqIndex + 1) % sequence.length; }
    public void prevFrame() { seqIndex = seqIndex == 0 ? sequence.length - 1 : seqIndex - 1; }

    public void setFrameSequence(int[] seq) {
        if (seq == null) {
            sequence = new int[numFrames];
            for (int i = 0; i < numFrames; i++) sequence[i] = i;
            seqIndex = 0;
            return;
        }
        if (seq.length < 1) throw new IllegalArgumentException();
        for (int i = 0; i < seq.length; i++) if (seq[i] < 0 || seq[i] >= numFrames) throw new ArrayIndexOutOfBoundsException();
        sequence = new int[seq.length];
        System.arraycopy(seq, 0, sequence, 0, seq.length);
        seqIndex = 0;
    }

    public void setImage(Image img, int frameWidth, int frameHeight) {
        if (img == null) throw new NullPointerException();
        if (frameWidth < 1 || frameHeight < 1 || img.getWidth() % frameWidth != 0 || img.getHeight() % frameHeight != 0) throw new IllegalArgumentException();
        int oldFrames = numFrames;
        boolean sizeChange = frameWidth != frameW || frameHeight != frameH;
        int oldRefX = getRefPixelX(), oldRefY = getRefPixelY();
        int newFrames = (img.getWidth() / frameWidth) * (img.getHeight() / frameHeight);
        initFrames(img, frameWidth, frameHeight, newFrames >= oldFrames);
        if (newFrames < oldFrames) { /* sequence reset */ }
        if (sizeChange) {
            initCollision();
            width = frameW; height = frameH;
            computeTransformedBounds(transform);
            setRefPixelPosition(oldRefX, oldRefY);
        } else computeTransformedBounds(transform);
    }

    public void defineCollisionRectangle(int x, int y, int w, int h) {
        if (w < 0 || h < 0) throw new IllegalArgumentException();
        colX = x; colY = y; colW = w; colH = h;
        setTransformImpl(transform);
    }

    public void setTransform(int t) {
        if (t < 0 || t > 7) throw new IllegalArgumentException();
        setTransformImpl(t);
    }

    private void setTransformImpl(int t) {
        x = x + tPtX(refX, refY, transform) - tPtX(refX, refY, t);
        y = y + tPtY(refX, refY, transform) - tPtY(refX, refY, t);
        computeTransformedBounds(t);
        transform = t;
    }

    private int tPtX(int px, int py, int t) {
        switch (t) {
            case TRANS_MIRROR: case TRANS_ROT180: return frameW - px - 1;
            case TRANS_ROT90: case TRANS_MIRROR_ROT90: return frameH - py - 1;
            case TRANS_ROT270: case TRANS_MIRROR_ROT270: return py;
            default: return px;
        }
    }
    private int tPtY(int px, int py, int t) {
        switch (t) {
            case TRANS_MIRROR_ROT180: case TRANS_ROT180: return frameH - py - 1;
            case TRANS_ROT90: case TRANS_MIRROR_ROT270: return px;
            case TRANS_ROT270: case TRANS_MIRROR_ROT90: return frameW - px - 1;
            default: return py;
        }
    }

    private void computeTransformedBounds(int t) {
        switch (t) {
            case TRANS_NONE: tColX = colX; tColY = colY; tColW = colW; tColH = colH; width = frameW; height = frameH; break;
            case TRANS_MIRROR: tColX = frameW - (colX + colW); tColY = colY; tColW = colW; tColH = colH; width = frameW; height = frameH; break;
            case TRANS_MIRROR_ROT180: tColX = colX; tColY = frameH - (colY + colH); tColW = colW; tColH = colH; width = frameW; height = frameH; break;
            case TRANS_ROT90: tColX = frameH - (colH + colY); tColY = colX; tColW = colH; tColH = colW; width = frameH; height = frameW; break;
            case TRANS_ROT180: tColX = frameW - (colW + colX); tColY = frameH - (colH + colY); tColW = colW; tColH = colH; width = frameW; height = frameH; break;
            case TRANS_ROT270: tColX = colY; tColY = frameW - (colW + colX); tColW = colH; tColH = colW; width = frameH; height = frameW; break;
            case TRANS_MIRROR_ROT90: tColX = frameH - (colH + colY); tColY = frameW - (colW + colX); tColW = colH; tColH = colW; width = frameH; height = frameW; break;
            case TRANS_MIRROR_ROT270: tColX = colY; tColY = colX; tColW = colH; tColH = colW; width = frameH; height = frameW; break;
        }
    }

    public final void paint(Graphics g) {
        if (g == null) throw new NullPointerException();
        if (!visible) return;
        int f = sequence[seqIndex];
        int fx = (f % framesPerRow) * frameW, fy = (f / framesPerRow) * frameH;
        Internal.drawRegion(g, image, fx, fy, frameW, frameH, transform, x, y, Graphics.TOP | Graphics.LEFT);
    }

    /* opaque test at layer-local (transformed) coords */
    boolean opaqueAt(int lx, int ly) {
        if (lx < 0 || ly < 0 || lx >= width || ly >= height) return false;
        int W = frameW, H = frameH, sx, sy;
        switch (transform) {
            case TRANS_ROT90: sx = ly; sy = H - 1 - lx; break;
            case TRANS_ROT180: sx = W - 1 - lx; sy = H - 1 - ly; break;
            case TRANS_ROT270: sx = W - 1 - ly; sy = lx; break;
            case TRANS_MIRROR: sx = W - 1 - lx; sy = ly; break;
            case TRANS_MIRROR_ROT90: sx = W - 1 - ly; sy = H - 1 - lx; break;
            case TRANS_MIRROR_ROT180: sx = lx; sy = H - 1 - ly; break;
            case TRANS_MIRROR_ROT270: sx = ly; sy = lx; break;
            default: sx = lx; sy = ly;
        }
        int f = sequence[seqIndex];
        int px = (f % framesPerRow) * frameW + sx, py = (f / framesPerRow) * frameH + sy;
        return (Internal.pixels(image)[py * image.getWidth() + px] >>> 24) != 0;
    }

    private static boolean overlap(int ax1, int ay1, int ax2, int ay2, int bx1, int by1, int bx2, int by2) {
        return ax1 < bx2 && bx1 < ax2 && ay1 < by2 && by1 < ay2;
    }

    public final boolean collidesWith(Sprite s, boolean pixelLevel) {
        if (!visible || !s.visible) return false;
        int ax1 = x + tColX, ay1 = y + tColY, ax2 = ax1 + tColW, ay2 = ay1 + tColH;
        int bx1 = s.x + s.tColX, by1 = s.y + s.tColY, bx2 = bx1 + s.tColW, by2 = by1 + s.tColH;
        if (!overlap(ax1, ay1, ax2, ay2, bx1, by1, bx2, by2)) return false;
        if (!pixelLevel) return true;
        int x1 = Math.max(ax1, bx1), y1 = Math.max(ay1, by1), x2 = Math.min(ax2, bx2), y2 = Math.min(ay2, by2);
        for (int yy = y1; yy < y2; yy++)
            for (int xx = x1; xx < x2; xx++)
                if (opaqueAt(xx - x, yy - y) && s.opaqueAt(xx - s.x, yy - s.y)) return true;
        return false;
    }

    public final boolean collidesWith(TiledLayer t, boolean pixelLevel) {
        if (!visible || !t.visible) return false;
        int ax1 = x + tColX, ay1 = y + tColY, ax2 = ax1 + tColW, ay2 = ay1 + tColH;
        int bx1 = t.x, by1 = t.y, bx2 = bx1 + t.width, by2 = by1 + t.height;
        if (!overlap(ax1, ay1, ax2, ay2, bx1, by1, bx2, by2)) return false;
        int x1 = Math.max(ax1, bx1), y1 = Math.max(ay1, by1), x2 = Math.min(ax2, bx2), y2 = Math.min(ay2, by2);
        int cw = t.getCellWidth(), ch = t.getCellHeight();
        if (!pixelLevel) {
            int c0 = (x1 - t.x) / cw, c1 = (x2 - 1 - t.x) / cw, r0 = (y1 - t.y) / ch, r1 = (y2 - 1 - t.y) / ch;
            for (int r = r0; r <= r1; r++) for (int c = c0; c <= c1; c++) if (t.tileAt(c, r) != 0) return true;
            return false;
        }
        for (int yy = y1; yy < y2; yy++)
            for (int xx = x1; xx < x2; xx++)
                if (opaqueAt(xx - x, yy - y) && t.opaqueAt(xx - t.x, yy - t.y)) return true;
        return false;
    }

    public final boolean collidesWith(Image img, int ix, int iy, boolean pixelLevel) {
        if (!visible) return false;
        int ax1 = x + tColX, ay1 = y + tColY, ax2 = ax1 + tColW, ay2 = ay1 + tColH;
        int bx1 = ix, by1 = iy, bx2 = ix + img.getWidth(), by2 = iy + img.getHeight();
        if (!overlap(ax1, ay1, ax2, ay2, bx1, by1, bx2, by2)) return false;
        if (!pixelLevel) return true;
        int[] px = Internal.pixels(img);
        int iw = img.getWidth();
        int x1 = Math.max(ax1, bx1), y1 = Math.max(ay1, by1), x2 = Math.min(ax2, bx2), y2 = Math.min(ay2, by2);
        for (int yy = y1; yy < y2; yy++)
            for (int xx = x1; xx < x2; xx++)
                if (opaqueAt(xx - x, yy - y) && (px[(yy - iy) * iw + (xx - ix)] >>> 24) != 0) return true;
        return false;
    }
}
