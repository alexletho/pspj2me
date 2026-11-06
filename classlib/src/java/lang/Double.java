package java.lang;
public final class Double extends Number implements Comparable {
    public static final double POSITIVE_INFINITY = 1.0 / 0.0;
    public static final double NEGATIVE_INFINITY = -1.0 / 0.0;
    public static final double NaN = 0.0d / 0.0;
    public static final double MAX_VALUE = 1.7976931348623157e+308;
    public static final double MIN_VALUE = 4.9e-324;
    private final double value;
    public Double(double value) { this.value = value; }
    public static native String toString(double d);
    public static Double valueOf(String s) throws NumberFormatException { return new Double(parseDouble(s)); }
    public static Double valueOf(double d) { return new Double(d); }
    public static double parseDouble(String s) throws NumberFormatException {
        if (s == null) throw new NullPointerException();
        String t = s.trim();
        if (t.length() == 0) throw new NumberFormatException(s);
        return parse0(t);
    }
    private static native double parse0(String s) throws NumberFormatException;
    public static boolean isNaN(double v) { return v != v; }
    public static boolean isInfinite(double v) { return v == POSITIVE_INFINITY || v == NEGATIVE_INFINITY; }
    public boolean isNaN() { return isNaN(value); }
    public boolean isInfinite() { return isInfinite(value); }
    public String toString() { return toString(value); }
    public byte byteValue() { return (byte) value; }
    public short shortValue() { return (short) value; }
    public int intValue() { return (int) value; }
    public long longValue() { return (long) value; }
    public float floatValue() { return (float) value; }
    public double doubleValue() { return value; }
    public int hashCode() { long b = doubleToLongBits(value); return (int) (b ^ (b >>> 32)); }
    public boolean equals(Object o) { return o instanceof Double && doubleToLongBits(((Double) o).value) == doubleToLongBits(value); }
    public static native long doubleToLongBits(double value);
    public static long doubleToRawLongBits(double value) { return doubleToLongBits(value); }
    public static native double longBitsToDouble(long bits);
    public static int compare(double a, double b) { return a < b ? -1 : a > b ? 1 : 0; }
    public int compareTo(Object o) { return compare(value, ((Double) o).value); }
}
