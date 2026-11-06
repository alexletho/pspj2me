package java.util;
public abstract class TimeZone {
    private static TimeZone defaultZone;
    private String id;
    public TimeZone() {}
    public abstract int getOffset(int era, int year, int month, int day, int dayOfWeek, int millis);
    public abstract int getRawOffset();
    public abstract boolean useDaylightTime();
    public String getID() { return id; }
    public static synchronized TimeZone getDefault() {
        if (defaultZone == null) {
            String tz = System.getProperty("pspj2me.tzoffset");
            int off = 0;
            try { if (tz != null) off = Integer.parseInt(tz); } catch (Exception e) {}
            defaultZone = new Simple(off * 60000, off == 0 ? "GMT" : "LOCAL");
        }
        return defaultZone;
    }
    public static synchronized TimeZone getTimeZone(String id) {
        if (id.equals("GMT") || id.equals("UTC")) return new Simple(0, id);
        if (id.startsWith("GMT") && id.length() > 4) {
            try {
                int sign = id.charAt(3) == '-' ? -1 : 1;
                String r = id.substring(4);
                int colon = r.indexOf(':');
                int h = Integer.parseInt(colon >= 0 ? r.substring(0, colon) : r);
                int m = colon >= 0 ? Integer.parseInt(r.substring(colon + 1)) : 0;
                return new Simple(sign * (h * 60 + m) * 60000, id);
            } catch (Exception e) {}
        }
        return getDefault();
    }
    public static String[] getAvailableIDs() { return new String[] { "GMT", "UTC" }; }
    static final class Simple extends TimeZone {
        private int raw;
        Simple(int raw, String id) { this.raw = raw; ((TimeZone) this).id = id; }
        public int getOffset(int era, int year, int month, int day, int dow, int millis) { return raw; }
        public int getRawOffset() { return raw; }
        public boolean useDaylightTime() { return false; }
    }
}
