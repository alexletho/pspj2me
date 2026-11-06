package javax.microedition.lcdui;

import java.util.Calendar;
import java.util.Date;
import java.util.TimeZone;

public class DateField extends Item {
    public static final int DATE = 1, TIME = 2, DATE_TIME = 3;
    private Date date;
    private int mode;
    private TimeZone zone;
    public DateField(String label, int mode) { this(label, mode, null); }
    public DateField(String label, int mode, TimeZone timeZone) {
        super(label);
        if (mode < 1 || mode > 3) throw new IllegalArgumentException();
        this.mode = mode; zone = timeZone;
    }
    public Date getDate() { return date; }
    public void setDate(Date d) { date = d; invalidate0(); }
    public int getInputMode() { return mode; }
    public void setInputMode(int m) { mode = m; invalidate0(); }
    private static String two(int v) { return v < 10 ? "0" + v : String.valueOf(v); }
    private String text() {
        if (date == null) return "<date>";
        Calendar c = zone == null ? Calendar.getInstance() : Calendar.getInstance(zone);
        c.setTime(date);
        String d = two(c.get(Calendar.DATE)) + "." + two(c.get(Calendar.MONTH) + 1) + "." + c.get(Calendar.YEAR);
        String t = two(c.get(Calendar.HOUR_OF_DAY)) + ":" + two(c.get(Calendar.MINUTE));
        return mode == DATE ? d : mode == TIME ? t : d + " " + t;
    }
    int contentHeight(int w) { return Screen.textFont().getHeight() + 4; }
    void paintContent(Graphics g, int x, int y, int w, boolean focused) {
        g.setColor(focused ? Screen.HL_BG : 0x808080);
        g.drawRect(x, y, w - 5, contentHeight(w) - 2);
        g.setColor(Screen.FG);
        g.setFont(Screen.textFont());
        g.drawString(text(), x + 3, y + 2, Graphics.TOP | Graphics.LEFT);
    }
    boolean focusable() { return true; }
}
