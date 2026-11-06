package java.lang;
public final class Float extends Number implements Comparable {
    public static final float POSITIVE_INFINITY = 1.0f / 0.0f;
    public static final float NEGATIVE_INFINITY = -1.0f / 0.0f;
    public static final float NaN = 0.0f / 0.0f;
    public static final float MAX_VALUE = 3.4028235e+38f;
    public static final float MIN_VALUE = 1.4e-45f;
    private final float value;
    public Float(float value) { this.value = value; }
    public Float(double value) { this.value = (float) value; }
    public static native String toString(float f);
    public static Float valueOf(String s) throws NumberFormatException { return new Float(parseFloat(s)); }
    public static Float valueOf(float f) { return new Float(f); }
    public static float parseFloat(String s) throws NumberFormatException { return (float) Double.parseDouble(s); }
    public static boolean isNaN(float v) { return v != v; }
    public static boolean isInfinite(float v) { return v == POSITIVE_INFINITY || v == NEGATIVE_INFINITY; }
    public boolean isNaN() { return isNaN(value); }
    public boolean isInfinite() { return isInfinite(value); }
    public String toString() { return toString(value); }
    public byte byteValue() { return (byte) value; }
    public short shortValue() { return (short) value; }
    public int intValue() { return (int) value; }
    public long longValue() { return (long) value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public int hashCode() { return floatToIntBits(value); }
    public boolean equals(Object o) { return o instanceof Float && floatToIntBits(((Float) o).value) == floatToIntBits(value); }
    public static native int floatToIntBits(float value);
    public static int floatToRawIntBits(float value) { return floatToIntBits(value); }
    public static native float intBitsToFloat(int bits);
    public static int compare(float a, float b) { return a < b ? -1 : a > b ? 1 : 0; }
    public int compareTo(Object o) { return compare(value, ((Float) o).value); }
}
