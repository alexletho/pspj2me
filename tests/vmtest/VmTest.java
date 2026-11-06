import java.util.*;
import java.io.*;

public class VmTest {
    static int passed, failed;
    static void check(String name, boolean ok) {
        if (ok) passed++; else { failed++; System.out.println("FAIL: " + name); }
    }
    static void eq(String name, Object a, Object b) {
        boolean ok = a == null ? b == null : a.equals(b);
        if (!ok) System.out.println("FAIL: " + name + " got [" + a + "] expected [" + b + "]");
        if (ok) passed++; else failed++;
    }

    interface Shape { int area(); }
    static abstract class Base implements Shape {
        int w, h;
        Base(int w, int h) { this.w = w; this.h = h; }
        public String toString() { return getClass().getName() + ":" + area(); }
        abstract String kind();
    }
    static class Rect extends Base {
        Rect(int w, int h) { super(w, h); }
        public int area() { return w * h; }
        String kind() { return "rect"; }
    }
    static class Tri extends Base {
        Tri(int w, int h) { super(w, h); }
        public int area() { return w * h / 2; }
        String kind() { return "tri"; }
    }
    static class Counter { int n; synchronized void inc() { n++; } }

    static int initOrder = 0;
    static class A { static int v; static { v = ++initOrder; } }
    static class B extends A { static int w; static { w = ++initOrder; } }

    static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

    static class MyEx extends Exception { int code; MyEx(int c) { super("code" + c); code = c; } }
    static void thrower(int c) throws MyEx { if (c > 0) throw new MyEx(c); }

    public static void main(String[] args) throws Exception {
        long t0 = System.currentTimeMillis();
        // arithmetic
        eq("int add", new Integer(2 + 3), new Integer(5));
        int x = Integer.MAX_VALUE; x++;
        eq("overflow", new Integer(x), new Integer(Integer.MIN_VALUE));
        eq("div", new Integer(-7 / 2), new Integer(-3));
        eq("rem", new Integer(-7 % 2), new Integer(-1));
        eq("minint/-1", new Integer(Integer.MIN_VALUE / -1), new Integer(Integer.MIN_VALUE));
        eq("shift", new Integer(-16 >> 2), new Integer(-4));
        eq("ushift", new Integer(-16 >>> 28), new Integer(15));
        long L = 1L << 40;
        eq("long shift", new Long(L), new Long(1099511627776L));
        eq("long mul", new Long(123456789L * 987654321L), new Long(121932631112635269L));
        eq("long div", new Long(-1000000000000L / 7), new Long(-142857142857L));
        eq("long cmp", new Boolean(5L > -5L), Boolean.TRUE);
        eq("float", Float.toString(1.5f * 2.25f), "3.375");
        eq("double", Double.toString(0.1 + 0.2), "0.30000000000000004");
        eq("double2", Double.toString(1e10), "1.0E10");
        eq("double3", Double.toString(100.0), "100.0");
        eq("float2", Float.toString(0.001f), "0.001");
        eq("f2i", new Integer((int) 3.99f), new Integer(3));
        eq("d2i neg", new Integer((int) -3.99), new Integer(-3));
        eq("d2i nan", new Integer((int) Double.NaN), new Integer(0));
        eq("d2i big", new Integer((int) 1e20), new Integer(Integer.MAX_VALUE));
        eq("sqrt", new Double(Math.sqrt(16)), new Double(4));
        eq("parseInt", new Integer(Integer.parseInt("-12345")), new Integer(-12345));
        eq("parseDouble", new Double(Double.parseDouble("3.25")), new Double(3.25));
        eq("hex", Integer.toHexString(-1), "ffffffff");
        eq("toString radix", Integer.toString(255, 2), "11111111");
        eq("char", new Character((char) ('a' + 1)), new Character('b'));
        byte b = (byte) 200;
        eq("byte", new Integer(b), new Integer(-56));
        short sh = (short) 70000;
        eq("short", new Integer(sh), new Integer(4464));
        try { int z = 0; int q = 5 / z; check("div0", false); } catch (ArithmeticException e) { check("div0", true); }

        // strings
        String s = "Hello";
        s += ", World";
        eq("concat", s, "Hello, World");
        eq("substring", s.substring(7), "World");
        eq("indexOf", new Integer(s.indexOf("World")), new Integer(7));
        eq("upper", s.toUpperCase(), "HELLO, WORLD");
        eq("intern", new Boolean(new String("abc").intern() == "abc"), Boolean.TRUE);
        eq("hash", new Integer("hello".hashCode()), new Integer(99162322));
        eq("sb", new StringBuffer("abc").reverse().append(42).append('x').append(1.5).toString(), "cba42x1.5");
        eq("utf8", new String("hélloЖ".getBytes("UTF-8"), "UTF-8"), "hélloЖ");
        eq("trim", "  pad ".trim(), "pad");
        eq("valueOf", String.valueOf(-42L), "-42");
        eq("compare", new Boolean("abc".compareTo("abd") < 0), Boolean.TRUE);
        eq("switch", sw(3) + sw(100) + sw(-1), "threehundredother");

        // objects / interfaces / virtual dispatch
        Shape[] shapes = { new Rect(3, 4), new Tri(6, 4) };
        int total = 0;
        for (int i = 0; i < shapes.length; i++) total += shapes[i].area();
        eq("iface", new Integer(total), new Integer(24));
        eq("virtual", ((Base) shapes[1]).kind(), "tri");
        eq("toString", shapes[0].toString(), "VmTest$Rect:12");
        check("instanceof", shapes[0] instanceof Base && !(shapes[0] instanceof Tri));
        try { Tri t = (Tri) (Object) shapes[0]; check("cce", false); } catch (ClassCastException e) { check("cce", true); }

        // class init order
        eq("clinit", new Integer(B.w), new Integer(2));
        eq("clinit2", new Integer(A.v), new Integer(1));

        // arrays
        int[][] grid = new int[4][5];
        grid[3][4] = 7;
        eq("multiarray", new Integer(grid[3][4] + grid.length + grid[0].length), new Integer(16));
        long[] la = new long[3]; la[2] = -5L;
        eq("long array", new Long(la[2]), new Long(-5));
        double[] da = { 1.5, 2.5 };
        eq("double array", new Double(da[0] + da[1]), new Double(4.0));
        int[] cl = (int[]) new int[] { 1, 2, 3 }.clone();
        eq("clone", new Integer(cl[2]), new Integer(3));
        try { int[] a = new int[2]; a[2] = 1; check("aioobe", false); } catch (ArrayIndexOutOfBoundsException e) { check("aioobe", true); }
        Object[] oa = new String[1];
        try { oa[0] = new Integer(1); check("ase", false); } catch (ArrayStoreException e) { check("ase", true); }
        char[] ca = new char[10];
        "abcdef".getChars(1, 4, ca, 2);
        eq("getChars", new String(ca, 2, 3), "bcd");
        int[] src = { 1, 2, 3, 4, 5 };
        System.arraycopy(src, 0, src, 1, 4);
        eq("arraycopy overlap", new Integer(src[4] * 10 + src[1]), new Integer(41));

        // exceptions
        try { thrower(3); } catch (MyEx e) { eq("custom ex", e.getMessage(), "code3"); }
        String r = "";
        try { try { thrower(1); } finally { r += "f"; } } catch (MyEx e) { r += "c"; }
        eq("finally", r, "fc");
        try { Object o = null; o.toString(); check("npe", false); } catch (NullPointerException e) { check("npe", true); }

        // recursion
        eq("fib", new Integer(fib(20)), new Integer(6765));

        // collections
        Vector v = new Vector();
        for (int i = 0; i < 100; i++) v.addElement(new Integer(i));
        v.removeElementAt(0);
        eq("vector", v.elementAt(0), new Integer(1));
        Hashtable ht = new Hashtable();
        for (int i = 0; i < 200; i++) ht.put("k" + i, new Integer(i * i));
        eq("hashtable", ht.get("k150"), new Integer(22500));
        int cnt = 0;
        for (Enumeration e = ht.keys(); e.hasMoreElements(); e.nextElement()) cnt++;
        eq("ht enum", new Integer(cnt), new Integer(200));
        Stack st = new Stack(); st.push("a"); st.push("b");
        eq("stack", st.pop(), "b");
        Random rnd = new Random(42);
        eq("random", new Integer(rnd.nextInt(100)), new Integer(30));

        // IO
        ByteArrayOutputStream bos = new ByteArrayOutputStream();
        DataOutputStream dos = new DataOutputStream(bos);
        dos.writeInt(0x12345678); dos.writeUTF("hé"); dos.writeLong(-2L); dos.writeDouble(2.5); dos.writeBoolean(true);
        DataInputStream dis = new DataInputStream(new ByteArrayInputStream(bos.toByteArray()));
        eq("readInt", new Integer(dis.readInt()), new Integer(0x12345678));
        eq("readUTF", dis.readUTF(), "hé");
        eq("readLong", new Long(dis.readLong()), new Long(-2));
        eq("readDouble", new Double(dis.readDouble()), new Double(2.5));
        eq("readBool", new Boolean(dis.readBoolean()), Boolean.TRUE);

        // threads
        final Counter c = new Counter();
        Thread[] ts = new Thread[4];
        for (int i = 0; i < 4; i++) {
            ts[i] = new Thread() { public void run() { for (int k = 0; k < 10000; k++) c.inc(); } };
            ts[i].start();
        }
        for (int i = 0; i < 4; i++) ts[i].join();
        eq("threads", new Integer(c.n), new Integer(40000));

        final Object lock = new Object();
        final int[] box = new int[1];
        Thread prod = new Thread(new Runnable() {
            public void run() {
                try { Thread.sleep(30); } catch (InterruptedException e) {}
                synchronized (lock) { box[0] = 99; lock.notifyAll(); }
            }
        });
        prod.start();
        synchronized (lock) { while (box[0] == 0) lock.wait(); }
        eq("wait/notify", new Integer(box[0]), new Integer(99));

        final int[] ticks = new int[1];
        Timer timer = new Timer();
        timer.schedule(new TimerTask() { public void run() { ticks[0]++; } }, 0, 10);
        Thread.sleep(105);
        timer.cancel();
        check("timer ticks " + ticks[0], ticks[0] >= 5 && ticks[0] <= 13);

        // GC stress
        Vector keep = new Vector();
        for (int i = 0; i < 20000; i++) {
            byte[] junk = new byte[1000];
            junk[0] = (byte) i;
            if (i % 1000 == 0) keep.addElement(junk);
        }
        eq("gc keep", new Integer(((byte[]) keep.elementAt(5))[0]), new Integer((byte) 5000));
        eq("gc keep2", new Integer(keep.size()), new Integer(20));

        // Class.forName / newInstance
        Object o = Class.forName("java.util.Vector").newInstance();
        check("newInstance", o instanceof Vector);
        try { Class.forName("no.such.Clazz"); check("cnfe", false); } catch (ClassNotFoundException e) { check("cnfe", true); }

        // Calendar
        Calendar cal = Calendar.getInstance(TimeZone.getTimeZone("GMT"));
        cal.setTime(new Date(1700000000000L));
        eq("calendar", cal.get(Calendar.YEAR) + "-" + cal.get(Calendar.MONTH) + "-" + cal.get(Calendar.DATE) + " " + cal.get(Calendar.HOUR_OF_DAY) + " dow" + cal.get(Calendar.DAY_OF_WEEK), "2023-10-14 22 dow3");

        // perf
        long p0 = System.currentTimeMillis();
        int acc = 0;
        for (int i = 0; i < 3000000; i++) { acc += i ^ (acc >>> 3); }
        long p1 = System.currentTimeMillis();
        System.out.println("loop 3M iters: " + (p1 - p0) + " ms (acc=" + acc + ")");
        System.out.println("passed=" + passed + " failed=" + failed + " time=" + (System.currentTimeMillis() - t0) + "ms");
    }

    static String sw(int i) {
        switch (i) {
            case 1: return "one";
            case 3: return "three";
            case 100: return "hundred";
            default: return "other";
        }
    }
}
