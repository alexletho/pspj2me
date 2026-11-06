package java.util;
public abstract class TimerTask implements Runnable {
    static final int VIRGIN = 0, SCHEDULED = 1, EXECUTED = 2, CANCELLED = 3;
    int state = VIRGIN;
    long nextExecutionTime;
    long period;
    boolean fixedRate;
    protected TimerTask() {}
    public abstract void run();
    public boolean cancel() {
        boolean r = state == SCHEDULED;
        state = CANCELLED;
        return r;
    }
    public long scheduledExecutionTime() { return period < 0 ? nextExecutionTime + period : nextExecutionTime - period; }
}
