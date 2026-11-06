package javax.microedition.lcdui;
public class Ticker {
    private String str;
    public Ticker(String str) { if (str == null) throw new NullPointerException(); this.str = str; }
    public void setString(String s) { if (s == null) throw new NullPointerException(); str = s; }
    public String getString() { return str; }
}
