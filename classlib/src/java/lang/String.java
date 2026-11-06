package java.lang;

import java.io.UnsupportedEncodingException;

public final class String implements CharSequence, Comparable {
    char[] value;
    int offset;
    int count;
    private int hash;

    public String() { value = new char[0]; }

    public String(String original) {
        value = original.value; offset = original.offset; count = original.count;
    }

    public String(char[] v) { this(v, 0, v.length); }

    public String(char[] v, int off, int len) {
        if (off < 0 || len < 0 || off > v.length - len) throw new StringIndexOutOfBoundsException();
        value = new char[len];
        System.arraycopy(v, off, value, 0, len);
        count = len;
    }

    String(int off, int len, char[] v) { value = v; offset = off; count = len; }

    public String(byte[] b) { this(b, 0, b.length); }
    public String(byte[] b, int off, int len) {
        if (off < 0 || len < 0 || off > b.length - len) throw new StringIndexOutOfBoundsException();
        value = Charsets.decode(b, off, len, Charsets.DEFAULT);
        count = value.length;
    }
    public String(byte[] b, int off, int len, String enc) throws UnsupportedEncodingException {
        if (off < 0 || len < 0 || off > b.length - len) throw new StringIndexOutOfBoundsException();
        value = Charsets.decode(b, off, len, Charsets.lookup(enc));
        count = value.length;
    }
    public String(byte[] b, String enc) throws UnsupportedEncodingException { this(b, 0, b.length, enc); }

    public String(StringBuffer sb) {
        String s = sb.toString();
        value = s.value; offset = s.offset; count = s.count;
    }

    public int length() { return count; }

    public char charAt(int index) {
        if (index < 0 || index >= count) throw new StringIndexOutOfBoundsException(index);
        return value[offset + index];
    }

    public void getChars(int srcBegin, int srcEnd, char[] dst, int dstBegin) {
        if (srcBegin < 0 || srcEnd > count || srcBegin > srcEnd) throw new StringIndexOutOfBoundsException();
        System.arraycopy(value, offset + srcBegin, dst, dstBegin, srcEnd - srcBegin);
    }

    public byte[] getBytes() { return Charsets.encode(value, offset, count, Charsets.DEFAULT); }
    public byte[] getBytes(String enc) throws UnsupportedEncodingException {
        return Charsets.encode(value, offset, count, Charsets.lookup(enc));
    }

    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof String)) return false;
        String s = (String) o;
        int n = count;
        if (n != s.count) return false;
        char[] a = value, b = s.value;
        int i = offset, j = s.offset;
        while (n-- != 0) if (a[i++] != b[j++]) return false;
        return true;
    }

    public boolean equalsIgnoreCase(String s) {
        return s != null && s.count == count && regionMatches(true, 0, s, 0, count);
    }

    public int compareTo(String s) {
        int n = Math.min(count, s.count);
        char[] a = value, b = s.value;
        int i = offset, j = s.offset;
        for (int k = 0; k < n; k++) {
            char c1 = a[i + k], c2 = b[j + k];
            if (c1 != c2) return c1 - c2;
        }
        return count - s.count;
    }
    public int compareTo(Object o) { return compareTo((String) o); }

    public boolean regionMatches(int toffset, String other, int ooffset, int len) {
        return regionMatches(false, toffset, other, ooffset, len);
    }

    public boolean regionMatches(boolean ignoreCase, int toffset, String other, int ooffset, int len) {
        if (ooffset < 0 || toffset < 0 || toffset > (long) count - len || ooffset > (long) other.count - len) return false;
        char[] ta = value, pa = other.value;
        int to = offset + toffset, po = other.offset + ooffset;
        while (len-- > 0) {
            char c1 = ta[to++], c2 = pa[po++];
            if (c1 == c2) continue;
            if (ignoreCase) {
                char u1 = Character.toUpperCase(c1), u2 = Character.toUpperCase(c2);
                if (u1 == u2) continue;
                if (Character.toLowerCase(u1) == Character.toLowerCase(u2)) continue;
            }
            return false;
        }
        return true;
    }

    public boolean startsWith(String prefix, int toffset) {
        if (toffset < 0 || toffset > count - prefix.count) return false;
        char[] ta = value, pa = prefix.value;
        int to = offset + toffset, po = prefix.offset, pc = prefix.count;
        while (--pc >= 0) if (ta[to++] != pa[po++]) return false;
        return true;
    }
    public boolean startsWith(String prefix) { return startsWith(prefix, 0); }
    public boolean endsWith(String suffix) { return startsWith(suffix, count - suffix.count); }

    public int hashCode() {
        int h = hash;
        if (h == 0 && count > 0) {
            char[] v = value;
            int o = offset;
            for (int i = 0; i < count; i++) h = 31 * h + v[o + i];
            hash = h;
        }
        return h;
    }

    public int indexOf(int ch) { return indexOf(ch, 0); }
    public int indexOf(int ch, int from) {
        if (from < 0) from = 0;
        char[] v = value;
        int end = offset + count;
        for (int i = offset + from; i < end; i++) if (v[i] == ch) return i - offset;
        return -1;
    }
    public int lastIndexOf(int ch) { return lastIndexOf(ch, count - 1); }
    public int lastIndexOf(int ch, int from) {
        if (from >= count) from = count - 1;
        char[] v = value;
        for (int i = offset + from; i >= offset; i--) if (v[i] == ch) return i - offset;
        return -1;
    }
    public int indexOf(String str) { return indexOf(str, 0); }
    public int indexOf(String str, int from) {
        if (from < 0) from = 0;
        int n = str.count;
        if (n == 0) return from <= count ? from : count;
        char[] v = value, s = str.value;
        char first = s[str.offset];
        int max = offset + count - n;
        for (int i = offset + from; i <= max; i++) {
            if (v[i] != first) continue;
            int j = 1;
            while (j < n && v[i + j] == s[str.offset + j]) j++;
            if (j == n) return i - offset;
        }
        return -1;
    }
    public int lastIndexOf(String str) { return lastIndexOf(str, count); }
    public int lastIndexOf(String str, int from) {
        int n = str.count;
        if (from > count - n) from = count - n;
        for (int i = from; i >= 0; i--) if (startsWith(str, i)) return i;
        return -1;
    }

    public boolean contains(String s) { return indexOf(s) >= 0; }

    public String substring(int begin) { return substring(begin, count); }
    public String substring(int begin, int end) {
        if (begin < 0 || end > count || begin > end) throw new StringIndexOutOfBoundsException("begin " + begin + ", end " + end + ", length " + count);
        if (begin == 0 && end == count) return this;
        return new String(offset + begin, end - begin, value);
    }

    public String concat(String str) {
        if (str.count == 0) return this;
        char[] buf = new char[count + str.count];
        getChars(0, count, buf, 0);
        str.getChars(0, str.count, buf, count);
        return new String(0, buf.length, buf);
    }

    public String replace(char oldChar, char newChar) {
        if (oldChar == newChar) return this;
        int i = indexOf(oldChar);
        if (i < 0) return this;
        char[] buf = toCharArray();
        for (; i < count; i++) if (buf[i] == oldChar) buf[i] = newChar;
        return new String(0, count, buf);
    }

    public String toLowerCase() {
        char[] buf = null;
        for (int i = 0; i < count; i++) {
            char c = value[offset + i], l = Character.toLowerCase(c);
            if (c != l) { if (buf == null) buf = toCharArray(); buf[i] = l; }
        }
        return buf == null ? this : new String(0, count, buf);
    }
    public String toUpperCase() {
        char[] buf = null;
        for (int i = 0; i < count; i++) {
            char c = value[offset + i], u = Character.toUpperCase(c);
            if (c != u) { if (buf == null) buf = toCharArray(); buf[i] = u; }
        }
        return buf == null ? this : new String(0, count, buf);
    }

    public String trim() {
        int st = 0, len = count;
        while (st < len && value[offset + st] <= ' ') st++;
        while (st < len && value[offset + len - 1] <= ' ') len--;
        return (st > 0 || len < count) ? substring(st, len) : this;
    }

    public String toString() { return this; }

    public char[] toCharArray() {
        char[] r = new char[count];
        System.arraycopy(value, offset, r, 0, count);
        return r;
    }

    public static String valueOf(Object obj) { return obj == null ? "null" : obj.toString(); }
    public static String valueOf(char[] data) { return new String(data); }
    public static String valueOf(char[] data, int off, int cnt) { return new String(data, off, cnt); }
    public static String copyValueOf(char[] data) { return new String(data); }
    public static String valueOf(boolean b) { return b ? "true" : "false"; }
    public static String valueOf(char c) { char[] a = { c }; return new String(0, 1, a); }
    public static String valueOf(int i) { return Integer.toString(i); }
    public static String valueOf(long l) { return Long.toString(l); }
    public static String valueOf(float f) { return Float.toString(f); }
    public static String valueOf(double d) { return Double.toString(d); }

    public native String intern();

    public boolean isEmpty() { return count == 0; }
}
