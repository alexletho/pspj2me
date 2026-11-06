package java.lang;

public class Thread implements Runnable {
    public static final int MIN_PRIORITY = 1;
    public static final int NORM_PRIORITY = 5;
    public static final int MAX_PRIORITY = 10;

    private Runnable target;
    private String name;
    private int priority = NORM_PRIORITY;
    private int vmThread;
    private boolean started;
    private static int counter;

    public Thread() { this(null, null); }
    public Thread(Runnable target) { this(target, null); }
    public Thread(String name) { this(null, name); }
    public Thread(Runnable target, String name) {
        this.target = target;
        this.name = name != null ? name : "Thread-" + (counter++);
    }

    public static native Thread currentThread();
    public static native void yield();
    public static native void sleep(long millis) throws InterruptedException;
    public static native int activeCount();

    public synchronized void start() {
        if (started) throw new IllegalThreadStateException();
        started = true;
        start0();
    }
    private native void start0();

    public void run() { if (target != null) target.run(); }

    public final int getPriority() { return priority; }
    public final void setPriority(int p) {
        if (p < MIN_PRIORITY || p > MAX_PRIORITY) throw new IllegalArgumentException();
        priority = p;
    }
    public final String getName() { return name; }
    public final void setName(String n) { name = n; }
    public final native boolean isAlive();
    public void interrupt() { interrupt0(); }
    private native void interrupt0();
    public boolean isInterrupted() { return false; }
    public final boolean isDaemon() { return false; }
    public final void setDaemon(boolean b) {}

    public final void join() throws InterruptedException {
        synchronized (this) {
            while (isAlive()) wait(0);
        }
    }
    public final void join(long ms) throws InterruptedException {
        if (ms <= 0) { join(); return; }
        long end = System.currentTimeMillis() + ms;
        synchronized (this) {
            while (isAlive()) {
                long left = end - System.currentTimeMillis();
                if (left <= 0) break;
                wait(left);
            }
        }
    }
    public String toString() { return "Thread[" + name + "," + priority + "]"; }
}
