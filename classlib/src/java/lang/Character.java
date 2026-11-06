package java.lang;
public final class Character {
    public static final int MIN_RADIX = 2;
    public static final int MAX_RADIX = 36;
    public static final char MIN_VALUE = '\u0000';
    public static final char MAX_VALUE = '￿';
    private final char value;
    public Character(char value) { this.value = value; }
    public char charValue() { return value; }
    public static Character valueOf(char c) { return new Character(c); }
    public int hashCode() { return value; }
    public boolean equals(Object o) { return o instanceof Character && ((Character) o).value == value; }
    public String toString() { return String.valueOf(value); }

    public static boolean isLowerCase(char ch) {
        if (ch >= 'a' && ch <= 'z') return true;
        if (ch < 0xdf) return ch == 0xaa || ch == 0xb5 || ch == 0xba;
        if (ch <= 0xff) return ch != 0xf7;
        if (ch >= 0x430 && ch <= 0x45f) return true;
        if (ch >= 0x3b1 && ch <= 0x3c9) return true;
        if (ch >= 0x100 && ch <= 0x17f) return (ch & 1) == 1;
        return false;
    }
    public static boolean isUpperCase(char ch) {
        if (ch >= 'A' && ch <= 'Z') return true;
        if (ch >= 0xc0 && ch <= 0xde) return ch != 0xd7;
        if (ch >= 0x400 && ch <= 0x42f) return true;
        if (ch >= 0x391 && ch <= 0x3a9) return true;
        if (ch >= 0x100 && ch <= 0x17f) return (ch & 1) == 0;
        return false;
    }
    public static boolean isDigit(char ch) { return ch >= '0' && ch <= '9'; }
    public static boolean isLetter(char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || isLowerCase(ch) || isUpperCase(ch) || (ch >= 0x4e00 && ch <= 0x9fff);
    }
    public static boolean isLetterOrDigit(char ch) { return isLetter(ch) || isDigit(ch); }
    public static boolean isSpace(char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f'; }
    public static boolean isWhitespace(char ch) { return isSpace(ch) || ch == 0x0b || (ch >= 0x1c && ch <= 0x1f); }

    public static char toLowerCase(char ch) {
        if (ch >= 'A' && ch <= 'Z') return (char) (ch + 32);
        if (ch < 0xc0) return ch;
        if (ch <= 0xde) return ch == 0xd7 ? ch : (char) (ch + 32);
        if (ch >= 0x410 && ch <= 0x42f) return (char) (ch + 32);
        if (ch >= 0x400 && ch <= 0x40f) return (char) (ch + 80);
        if (ch >= 0x391 && ch <= 0x3a9) return (char) (ch + 32);
        if (ch >= 0x100 && ch <= 0x17f && (ch & 1) == 0) return (char) (ch + 1);
        return ch;
    }
    public static char toUpperCase(char ch) {
        if (ch >= 'a' && ch <= 'z') return (char) (ch - 32);
        if (ch < 0xe0) return ch;
        if (ch <= 0xfe) return ch == 0xf7 ? ch : (char) (ch - 32);
        if (ch >= 0x430 && ch <= 0x44f) return (char) (ch - 32);
        if (ch >= 0x450 && ch <= 0x45f) return (char) (ch - 80);
        if (ch >= 0x3b1 && ch <= 0x3c9 && ch != 0x3c2) return (char) (ch - 32);
        if (ch >= 0x100 && ch <= 0x17f && (ch & 1) == 1) return (char) (ch - 1);
        return ch;
    }
    public static int digit(char ch, int radix) {
        int d;
        if (ch >= '0' && ch <= '9') d = ch - '0';
        else if (ch >= 'a' && ch <= 'z') d = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'Z') d = ch - 'A' + 10;
        else return -1;
        return d < radix ? d : -1;
    }
    public static char forDigit(int digit, int radix) {
        if (digit < 0 || digit >= radix) return 0;
        return digit < 10 ? (char) ('0' + digit) : (char) ('a' + digit - 10);
    }
}
