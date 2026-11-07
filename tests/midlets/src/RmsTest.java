import javax.microedition.lcdui.*;
import javax.microedition.midlet.MIDlet;
import javax.microedition.rms.*;

/* Record store (game save) behaviour. Run it twice with the same data directory: the first run
 * writes saves ("RmsTest: PASS 1"), the second checks they survived a restart ("RmsTest: PASS 2"). */
public class RmsTest extends MIDlet {
    private int failures;
    private int listenerEvents;

    protected void startApp() {
        Display.getDisplay(this).setCurrent(new Form("RmsTest"));
        try {
            String[] stores = RecordStore.listRecordStores();
            StringBuffer sb = new StringBuffer("RmsTest stores:");
            if (stores != null) for (int i = 0; i < stores.length; i++) sb.append(" [").append(stores[i]).append(']');
            System.out.println(sb.toString());
            boolean second = false;
            if (stores != null) for (int i = 0; i < stores.length; i++) if (stores[i].equals("persist")) second = true;
            if (second) secondRun(); else firstRun();
        } catch (Throwable t) {
            failures++;
            System.out.println("RmsTest FAIL: exception " + t);
            t.printStackTrace();
        }
        System.out.println(failures == 0 ? "RmsTest: PASS " + (phase) : "RmsTest: " + failures + " FAILURES");
        notifyDestroyed();
    }
    private int phase;
    protected void pauseApp() {}
    protected void destroyApp(boolean u) {}

    private void check(boolean ok, String what) {
        if (!ok) { failures++; System.out.println("RmsTest FAIL: " + what); }
        else System.out.println("RmsTest ok: " + what);
    }
    private static byte[] bytes(String s) { return s.getBytes(); }

    private void firstRun() throws Exception {
        phase = 1;
        boolean threw = false;
        try { RecordStore.openRecordStore("missing", false); } catch (RecordStoreNotFoundException e) { threw = true; }
        check(threw, "opening a missing store without create throws");

        RecordStore rs = RecordStore.openRecordStore("persist", true);
        rs.addRecordListener(new RecordListener() {
            public void recordAdded(RecordStore r, int id) { listenerEvents++; }
            public void recordChanged(RecordStore r, int id) { listenerEvents++; }
            public void recordDeleted(RecordStore r, int id) { listenerEvents++; }
        });
        int v0 = rs.getVersion();
        int a = rs.addRecord(bytes("level 7"), 0, 7);
        int b = rs.addRecord(bytes("xxhighscore 12345yy"), 2, 15);
        int c = rs.addRecord(null, 0, 0);
        check(a == 1 && b == 2 && c == 3, "record IDs start at 1 and count up");
        check(new String(rs.getRecord(b)).equals("highscore 12345"), "record with offset");
        check(rs.getRecord(c) == null && rs.getRecordSize(c) == 0, "empty record");
        rs.deleteRecord(a);
        int d = rs.addRecord(bytes("new"), 0, 3);
        check(d == 4 && rs.getNextRecordID() == 5, "deleted IDs are not reused");
        threw = false;
        try { rs.getRecord(a); } catch (InvalidRecordIDException e) { threw = true; }
        check(threw, "deleted record throws InvalidRecordIDException");
        rs.setRecord(d, bytes("replaced, longer"), 0, 16);
        byte[] buf = new byte[40];
        int n = rs.getRecord(d, buf, 5);
        check(n == 16 && new String(buf, 5, n).equals("replaced, longer"), "setRecord + getRecord into a buffer");
        check(rs.getVersion() > v0 && rs.getLastModified() > 0, "version and last-modified change");
        check(listenerEvents == 6, "listener events (" + listenerEvents + ")");
        check(rs.getNumRecords() == 3, "record count");

        RecordEnumeration en = rs.enumerateRecords(null, new RecordComparator() {
            public int compare(byte[] x, byte[] y) {
                int lx = x == null ? 0 : x.length, ly = y == null ? 0 : y.length;
                return lx < ly ? PRECEDES : lx > ly ? FOLLOWS : EQUIVALENT;
            }
        }, true);
        check(en.numRecords() == 3 && en.nextRecordId() == c, "enumeration with comparator");
        rs.addRecord(bytes("added later"), 0, 11);
        check(en.numRecords() == 4, "kept-updated enumeration sees new records");
        en.destroy();
        RecordEnumeration f = rs.enumerateRecords(new RecordFilter() {
            public boolean matches(byte[] r) { return r != null && r.length > 0 && r[0] == 'h'; }
        }, null, false);
        check(f.numRecords() == 1, "enumeration with filter");

        byte[] big = new byte[200 * 1024];
        for (int i = 0; i < big.length; i++) big[i] = (byte) (i * 7);
        int bid = rs.addRecord(big, 0, big.length);
        check(rs.getRecordSize(bid) == big.length, "200 KB record");

        RecordStore again = RecordStore.openRecordStore("persist", false);
        check(again == rs, "opening an open store returns the same store");
        again.closeRecordStore();
        check(rs.getNumRecords() == 5, "still open after one of two closes");
        threw = false;
        try { RecordStore.deleteRecordStore("persist"); } catch (RecordStoreException e) { threw = true; }
        check(threw, "deleting an open store throws");
        rs.closeRecordStore();
        threw = false;
        try { rs.getNumRecords(); } catch (RecordStoreNotOpenException e) { threw = true; }
        check(threw, "closed store throws RecordStoreNotOpenException");

        /* a store with a name that isn't a valid file name, and one that gets deleted */
        RecordStore odd = RecordStore.openRecordStore("Сохр/1:*?\"<>|", true);
        odd.addRecord(bytes("odd"), 0, 3);
        odd.closeRecordStore();
        RecordStore tmp = RecordStore.openRecordStore("temp", true);
        tmp.closeRecordStore();
        RecordStore.deleteRecordStore("temp");
        String[] names = RecordStore.listRecordStores();
        boolean hasOdd = false, hasTemp = false;
        for (int i = 0; i < names.length; i++) { if (names[i].equals("Сохр/1:*?\"<>|")) hasOdd = true; if (names[i].equals("temp")) hasTemp = true; }
        check(hasOdd && !hasTemp, "listRecordStores (odd names kept, deleted store gone)");

        /* saved without closing: the second run must still see this (game quit with HOME) */
        RecordStore unclosed = RecordStore.openRecordStore("unclosed", true);
        unclosed.addRecord(bytes("not closed"), 0, 10);
    }

    private void secondRun() throws Exception {
        phase = 2;
        RecordStore rs = RecordStore.openRecordStore("persist", false);
        check(rs.getNumRecords() == 5 && rs.getNextRecordID() == 7, "records and next ID survive a restart");
        check(new String(rs.getRecord(2)).equals("highscore 12345") && new String(rs.getRecord(4)).equals("replaced, longer"), "record contents survive");
        byte[] big = rs.getRecord(6);
        boolean ok = big.length == 200 * 1024;
        for (int i = 0; ok && i < big.length; i++) ok = big[i] == (byte) (i * 7);
        check(ok, "200 KB record survives byte for byte");
        RecordStore odd = RecordStore.openRecordStore("Сохр/1:*?\"<>|", false);
        check(new String(odd.getRecord(1)).equals("odd"), "store with an odd name survives");
        RecordStore unclosed = RecordStore.openRecordStore("unclosed", false);
        check(new String(unclosed.getRecord(1)).equals("not closed"), "saves of a store never closed survive");
        rs.closeRecordStore();
        odd.closeRecordStore();
        unclosed.closeRecordStore();
    }
}
