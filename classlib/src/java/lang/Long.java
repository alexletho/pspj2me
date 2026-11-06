package java.lang;

public final class Long extends Number implements Comparable {
    public static final long MIN_VALUE = 0x8000000000000000L;
    public static final long MAX_VALUE = 0x7fffffffffffffffL;
    private final long value;

    public Long(long value) { this.value = value; }

    public static String toString(long i, int radix) {
        if (radix < Character.MIN_RADIX || radix > Character.MAX_RADIX) radix = 10;
        if (i == 0) return "0";
        char[] buf = new char[65];
        boolean neg = i < 0;
        int p = 64;
        if (!neg) i = -i;
        while (i <= -radix) { buf[p--] = Character.forDigit((int) (-(i % radix)), radix); i /= radix; }
        buf[p] = Character.forDigit((int) (-i), radix);
        if (neg) buf[--p] = '-';
        return new String(buf, p, 65 - p);
    }
    public static String toString(long i) { return toString(i, 10); }
    public static String toHexString(long i) {
        char[] buf = new char[16];
        int p = 16;
        do { buf[--p] = Character.forDigit((int) (i & 15), 16); i >>>= 4; } while (i != 0);
        return new String(buf, p, 16 - p);
    }

    public static long parseLong(String s, int radix) throws NumberFormatException {
        if (s == null) throw new NumberFormatException("null");
        int len = s.length();
        if (len == 0) throw new NumberFormatException(s);
        int i = 0;
        long result = 0;
        boolean neg = false;
        long limit = -MAX_VALUE;
        char first = s.charAt(0);
        if (first == '-') { neg = true; limit = MIN_VALUE; i++; }
        else if (first == '+') i++;
        if (i >= len) throw new NumberFormatException(s);
        long multmin = limit / radix;
        while (i < len) {
            int d = Character.digit(s.charAt(i++), radix);
            if (d < 0 || result < multmin) throw new NumberFormatException(s);
            result *= radix;
            if (result < limit + d) throw new NumberFormatException(s);
            result -= d;
        }
        return neg ? result : -result;
    }
    public static long parseLong(String s) throws NumberFormatException { return parseLong(s, 10); }
    public static Long valueOf(long l) { return new Long(l); }
    public static Long valueOf(String s) { return new Long(parseLong(s, 10)); }

    public int intValue() { return (int) value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public String toString() { return toString(value); }
    public int hashCode() { return (int) (value ^ (value >>> 32)); }
    public boolean equals(Object o) { return o instanceof Long && ((Long) o).value == value; }
    public int compareTo(Object o) { long v = ((Long) o).value; return value < v ? -1 : value == v ? 0 : 1; }
}
