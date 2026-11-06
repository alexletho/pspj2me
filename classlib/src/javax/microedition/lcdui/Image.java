package javax.microedition.lcdui;

import java.io.IOException;
import java.io.InputStream;
import java.io.ByteArrayOutputStream;

public class Image {
    int[] pixels;
    int width, height;
    boolean mutable;
    int alpha;   /* 0 = opaque, 1 = has transparent pixels */

    Image() {}

    Image(int w, int h, boolean mutable) {
        width = w; height = h; this.mutable = mutable;
        pixels = new int[w * h];
    }

    public static Image createImage(int width, int height) {
        if (width <= 0 || height <= 0) throw new IllegalArgumentException();
        Image img = new Image(width, height, true);
        fill(img.pixels, 0xffffffff);
        return img;
    }

    public static Image createImage(Image source) {
        if (source == null) throw new NullPointerException();
        if (!source.mutable) return source;
        Image img = new Image(source.width, source.height, false);
        System.arraycopy(source.pixels, 0, img.pixels, 0, source.pixels.length);
        img.alpha = source.alpha;
        return img;
    }

    public static Image createImage(String name) throws IOException {
        if (name == null) throw new NullPointerException();
        String n = name.startsWith("/") ? name.substring(1) : name;
        byte[] data = loadResource(n);
        if (data == null) throw new IOException("Resource not found: " + name);
        Image img = new Image();
        if (!img.decode0(data, 0, data.length)) throw new IOException("Cannot decode image: " + name);
        return img;
    }

    private static native byte[] loadResource(String name);

    public static Image createImage(byte[] imageData, int imageOffset, int imageLength) {
        if (imageData == null) throw new NullPointerException();
        if (imageOffset < 0 || imageLength < 0 || imageOffset + imageLength > imageData.length) throw new ArrayIndexOutOfBoundsException();
        Image img = new Image();
        if (!img.decode0(imageData, imageOffset, imageLength)) throw new IllegalArgumentException("Cannot decode image data");
        return img;
    }

    public static Image createImage(Image image, int x, int y, int width, int height, int transform) {
        if (image == null) throw new NullPointerException();
        if (x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > image.width || y + height > image.height || transform < 0 || transform > 7)
            throw new IllegalArgumentException();
        boolean swap = transform >= 4;
        Image img = new Image(swap ? height : width, swap ? width : height, false);
        blit0(img.pixels, img.width, image.pixels, image.width, x, y, width, height, transform);
        img.alpha = image.alpha;
        return img;
    }

    public static Image createImage(InputStream stream) throws IOException {
        if (stream == null) throw new NullPointerException();
        ByteArrayOutputStream bos = new ByteArrayOutputStream(4096);
        byte[] buf = new byte[4096];
        int n;
        while ((n = stream.read(buf, 0, buf.length)) > 0) bos.write(buf, 0, n);
        byte[] data = bos.toByteArray();
        Image img = new Image();
        if (!img.decode0(data, 0, data.length)) throw new IOException("Cannot decode image");
        return img;
    }

    public static Image createRGBImage(int[] rgb, int width, int height, boolean processAlpha) {
        if (rgb == null) throw new NullPointerException();
        if (width <= 0 || height <= 0) throw new IllegalArgumentException();
        if (width * height > rgb.length) throw new ArrayIndexOutOfBoundsException();
        Image img = new Image(width, height, false);
        System.arraycopy(rgb, 0, img.pixels, 0, width * height);
        img.alpha = fixAlpha0(img.pixels, processAlpha) ? 1 : 0;
        return img;
    }

    public Graphics getGraphics() {
        if (!mutable) throw new IllegalStateException("Image is immutable");
        return new Graphics(this);
    }

    public int getWidth() { return width; }
    public int getHeight() { return height; }
    public boolean isMutable() { return mutable; }

    public void getRGB(int[] rgbData, int offset, int scanlength, int x, int y, int width, int height) {
        if (rgbData == null) throw new NullPointerException();
        if (x < 0 || y < 0 || x + width > this.width || y + height > this.height) throw new IllegalArgumentException();
        if (width <= 0 || height <= 0) return;
        if (Math.abs(scanlength) < width) throw new IllegalArgumentException();
        if (offset < 0 || offset + (height - 1) * scanlength + width > rgbData.length || offset + (height - 1) * scanlength < 0) throw new ArrayIndexOutOfBoundsException();
        for (int r = 0; r < height; r++)
            System.arraycopy(pixels, (y + r) * this.width + x, rgbData, offset + r * scanlength, width);
    }

    private native boolean decode0(byte[] data, int off, int len);
    static native void fill(int[] px, int argb);
    static native void blit0(int[] dst, int dstW, int[] src, int srcW, int x, int y, int w, int h, int transform);
    static native boolean fixAlpha0(int[] px, boolean processAlpha);
}
