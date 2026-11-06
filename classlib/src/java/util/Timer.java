package java.util;

public class Timer {
    private final Vector queue = new Vector();
    private boolean cancelled;
    private final Thread thread;

    public Timer() {
        thread = new Thread(new Runnable() { public void run() { mainLoop(); } }, "Timer");
        thread.start();
    }

    private void mainLoop() {
        while (true) {
            TimerTask task;
            synchronized (queue) {
                while (queue.isEmpty() && !cancelled) {
                    try { queue.wait(); } catch (InterruptedException e) {}
                }
                if (cancelled) return;
                task = null;
                long best = Long.MAX_VALUE;
                for (int i = 0; i < queue.size(); i++) {
                    TimerTask t = (TimerTask) queue.elementAt(i);
                    if (t.state == TimerTask.CANCELLED) { queue.removeElementAt(i--); continue; }
                    if (t.nextExecutionTime < best) { best = t.nextExecutionTime; task = t; }
                }
                if (task == null) continue;
                long now = System.currentTimeMillis();
                if (best > now) {
                    try { queue.wait(best - now); } catch (InterruptedException e) {}
                    continue;
                }
                if (task.period == 0) { queue.removeElement(task); task.state = TimerTask.EXECUTED; }
                else task.nextExecutionTime = task.fixedRate ? task.nextExecutionTime + task.period : now + task.period;
            }
            try {
                task.run();
            } catch (Throwable t) {
                t.printStackTrace();
            }
        }
    }

    private void sched(TimerTask task, long time, long period, boolean fixed) {
        if (time < 0) throw new IllegalArgumentException();
        synchronized (queue) {
            if (cancelled) throw new IllegalStateException("Timer already cancelled.");
            if (task.state != TimerTask.VIRGIN) throw new IllegalStateException("Task already scheduled or cancelled");
            task.nextExecutionTime = time;
            task.period = period;
            task.fixedRate = fixed;
            task.state = TimerTask.SCHEDULED;
            queue.addElement(task);
            queue.notify();
        }
    }

    public void schedule(TimerTask task, long delay) {
        if (delay < 0) throw new IllegalArgumentException();
        sched(task, System.currentTimeMillis() + delay, 0, false);
    }
    public void schedule(TimerTask task, Date time) { sched(task, time.getTime(), 0, false); }
    public void schedule(TimerTask task, long delay, long period) {
        if (delay < 0 || period <= 0) throw new IllegalArgumentException();
        sched(task, System.currentTimeMillis() + delay, period, false);
    }
    public void schedule(TimerTask task, Date first, long period) {
        if (period <= 0) throw new IllegalArgumentException();
        sched(task, first.getTime(), period, false);
    }
    public void scheduleAtFixedRate(TimerTask task, long delay, long period) {
        if (delay < 0 || period <= 0) throw new IllegalArgumentException();
        sched(task, System.currentTimeMillis() + delay, period, true);
    }
    public void scheduleAtFixedRate(TimerTask task, Date first, long period) {
        if (period <= 0) throw new IllegalArgumentException();
        sched(task, first.getTime(), period, true);
    }
    public void cancel() {
        synchronized (queue) {
            cancelled = true;
            queue.removeAllElements();
            queue.notify();
        }
    }
}
