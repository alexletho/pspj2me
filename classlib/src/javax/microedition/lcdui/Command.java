package javax.microedition.lcdui;
public class Command {
    public static final int SCREEN = 1, BACK = 2, CANCEL = 3, OK = 4, HELP = 5, STOP = 6, EXIT = 7, ITEM = 8;
    private String shortLabel, longLabel;
    private int type, priority;
    public Command(String label, int commandType, int priority) { this(label, null, commandType, priority); }
    public Command(String shortLabel, String longLabel, int commandType, int priority) {
        if (shortLabel == null) throw new NullPointerException();
        if (commandType < 1 || commandType > 8) throw new IllegalArgumentException();
        this.shortLabel = shortLabel; this.longLabel = longLabel; this.type = commandType; this.priority = priority;
    }
    public String getLabel() { return shortLabel; }
    public String getLongLabel() { return longLabel; }
    public int getCommandType() { return type; }
    public int getPriority() { return priority; }
    boolean isNegative() { return type == BACK || type == CANCEL || type == EXIT || type == STOP; }
}
