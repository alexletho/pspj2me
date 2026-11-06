package java.lang;

public final class Integer extends Number implements Comparable {
    public static final int MIN_VALUE = 0x80000000;
    public static final int MAX_VALUE = 0x7fffffff;
    private final int value;

    public Integer(int value) { this.value = value; }

    public static String toString(int i, int radix) {
        if (radix < Character.MIN_RADIX || radix > Character.MAX_RADIX) radix = 10;
        if (i == 0) return "0";
        char[] buf = new char[33];
        boolean neg = i < 0;
        int p = 32;
        if (!neg) i = -i;
        while (i <= -radix) { buf[p--] = Character.forDigit(-(i % radix), radix); i /= radix; }
        buf[p] = Character.forDigit(-i, radix);
        if (neg) buf[--p] = '-';
        return new String(buf, p, 33 - p);
    }
    private static String toUnsigned(int i, int shift) {
        char[] buf = new char[32];
        int p = 32, radix = 1 << shift, mask = radix - 1;
        do { buf[--p] = Character.forDigit(i & mask, radix); i >>>= shift; } while (i != 0);
        return new String(buf, p, 32 - p);
    }
    public static String toHexString(int i) { return toUnsigned(i, 4); }
    public static String toOctalString(int i) { return toUnsigned(i, 3); }
    public static String toBinaryString(int i) { return toUnsigned(i, 1); }
    public static String toString(int i) { return toString(i, 10); }

    public static int parseInt(String s, int radix) throws NumberFormatException {
        if (s == null) throw new NumberFormatException("null");
        if (radix < Character.MIN_RADIX || radix > Character.MAX_RADIX) throw new NumberFormatException("radix");
        int len = s.length();
        if (len == 0) throw new NumberFormatException(s);
        int i = 0, result = 0;
        boolean neg = false;
        int limit = -MAX_VALUE;
        char first = s.charAt(0);
        if (first == '-') { neg = true; limit = MIN_VALUE; i++; }
        else if (first == '+') i++;
        if (i >= len) throw new NumberFormatException(s);
        int multmin = limit / radix;
        while (i < len) {
            int d = Character.digit(s.charAt(i++), radix);
            if (d < 0 || result < multmin) throw new NumberFormatException(s);
            result *= radix;
            if (result < limit + d) throw new NumberFormatException(s);
            result -= d;
        }
        return neg ? result : -result;
    }
    public static int parseInt(String s) throws NumberFormatException { return parseInt(s, 10); }
    public static Integer valueOf(String s, int radix) throws NumberFormatException { return new Integer(parseInt(s, radix)); }
    public static Integer valueOf(String s) throws NumberFormatException { return new Integer(parseInt(s, 10)); }
    public static Integer valueOf(int i) { return new Integer(i); }

    public byte byteValue() { return (byte) value; }
    public short shortValue() { return (short) value; }
    public int intValue() { return value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public String toString() { return toString(value); }
    public int hashCode() { return value; }
    public boolean equals(Object o) { return o instanceof Integer && ((Integer) o).value == value; }
    public int compareTo(Object o) { int v = ((Integer) o).value; return value < v ? -1 : value == v ? 0 : 1; }
}
