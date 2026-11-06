package javax.microedition.lcdui.game;

import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;
import javax.microedition.lcdui.Internal;

public class TiledLayer extends Layer {
    private final int rows, cols;
    private int cellW, cellH;
    private Image image;
    private int numTiles;
    private int[] tileX, tileY;
    private final int[][] cells;
    private int[] animated = new int[4];
    private int numAnimated;

    public TiledLayer(int columns, int rows, Image image, int tileWidth, int tileHeight) {
        super(columns * tileWidth, rows * tileHeight);
        if (image == null) throw new NullPointerException();
        if (columns <= 0 || rows <= 0 || tileWidth <= 0 || tileHeight <= 0 ||
            image.getWidth() % tileWidth != 0 || image.getHeight() % tileHeight != 0) throw new IllegalArgumentException();
        this.cols = columns;
        this.rows = rows;
        cells = new int[rows][columns];
        setTiles(image, tileWidth, tileHeight);
    }

    private void setTiles(Image img, int tw, int th) {
        image = img; cellW = tw; cellH = th;
        int perRow = img.getWidth() / tw, perCol = img.getHeight() / th;
        numTiles = perRow * perCol;
        tileX = new int[numTiles + 1];
        tileY = new int[numTiles + 1];
        int k = 1;
        for (int r = 0; r < perCol; r++)
            for (int c = 0; c < perRow; c++) { tileX[k] = c * tw; tileY[k] = r * th; k++; }
        width = cols * tw;
        height = rows * th;
    }

    public int createAnimatedTile(int staticTileIndex) {
        if (staticTileIndex < 0 || staticTileIndex > numTiles) throw new IndexOutOfBoundsException();
        if (numAnimated == animated.length) {
            int[] n = new int[animated.length * 2];
            System.arraycopy(animated, 0, n, 0, numAnimated);
            animated = n;
        }
        animated[numAnimated++] = staticTileIndex;
        return -numAnimated;
    }
    public void setAnimatedTile(int animatedTileIndex, int staticTileIndex) {
        int i = -animatedTileIndex - 1;
        if (i < 0 || i >= numAnimated) throw new IndexOutOfBoundsException();
        if (staticTileIndex < 0 || staticTileIndex > numTiles) throw new IndexOutOfBoundsException();
        animated[i] = staticTileIndex;
    }
    public int getAnimatedTile(int animatedTileIndex) {
        int i = -animatedTileIndex - 1;
        if (i < 0 || i >= numAnimated) throw new IndexOutOfBoundsException();
        return animated[i];
    }
    public void setCell(int col, int row, int tileIndex) {
        if (col < 0 || col >= cols || row < 0 || row >= rows) throw new IndexOutOfBoundsException();
        if (tileIndex > numTiles || -tileIndex > numAnimated) throw new IndexOutOfBoundsException();
        cells[row][col] = tileIndex;
    }
    public int getCell(int col, int row) {
        if (col < 0 || col >= cols || row < 0 || row >= rows) throw new IndexOutOfBoundsException();
        return cells[row][col];
    }
    public void fillCells(int col, int row, int numCols, int numRows, int tileIndex) {
        if (numCols < 0 || numRows < 0 || col < 0 || row < 0 || col + numCols > cols || row + numRows > rows) throw new IndexOutOfBoundsException();
        if (tileIndex > numTiles || -tileIndex > numAnimated) throw new IndexOutOfBoundsException();
        for (int r = row; r < row + numRows; r++) for (int c = col; c < col + numCols; c++) cells[r][c] = tileIndex;
    }
    public final int getCellWidth() { return cellW; }
    public final int getCellHeight() { return cellH; }
    public final int getColumns() { return cols; }
    public final int getRows() { return rows; }

    public void setStaticTileSet(Image image, int tileWidth, int tileHeight) {
        if (image == null) throw new NullPointerException();
        if (tileWidth <= 0 || tileHeight <= 0 || image.getWidth() % tileWidth != 0 || image.getHeight() % tileHeight != 0) throw new IllegalArgumentException();
        int newCount = (image.getWidth() / tileWidth) * (image.getHeight() / tileHeight);
        boolean keep = newCount >= numTiles;
        setTiles(image, tileWidth, tileHeight);
        if (!keep) {
            for (int r = 0; r < rows; r++) for (int c = 0; c < cols; c++) cells[r][c] = 0;
            numAnimated = 0;
        }
    }

    int tileAt(int col, int row) {
        int t = cells[row][col];
        if (t < 0) t = animated[-t - 1];
        return t;
    }

    /* opaque test in layer-local pixel coords */
    boolean opaqueAt(int lx, int ly) {
        if (lx < 0 || ly < 0 || lx >= width || ly >= height) return false;
        int t = tileAt(lx / cellW, ly / cellH);
        if (t == 0) return false;
        int px = tileX[t] + lx % cellW, py = tileY[t] + ly % cellH;
        return (Internal.pixels(image)[py * image.getWidth() + px] >>> 24) != 0;
    }

    public final void paint(Graphics g) {
        if (g == null) throw new NullPointerException();
        if (!visible) return;
        int cx = g.getClipX(), cy = g.getClipY(), cw = g.getClipWidth(), ch = g.getClipHeight();
        int c0 = Math.max(0, (cx - x) / cellW), c1 = Math.min(cols - 1, (cx + cw - x - 1) / cellW);
        int r0 = Math.max(0, (cy - y) / cellH), r1 = Math.min(rows - 1, (cy + ch - y - 1) / cellH);
        for (int r = r0; r <= r1; r++) {
            int ty = y + r * cellH;
            int[] row = cells[r];
            for (int c = c0; c <= c1; c++) {
                int t = row[c];
                if (t == 0) continue;
                if (t < 0) { t = animated[-t - 1]; if (t == 0) continue; }
                Internal.drawRegion(g, image, tileX[t], tileY[t], cellW, cellH, 0, x + c * cellW, ty, Graphics.TOP | Graphics.LEFT);
            }
        }
    }
}
