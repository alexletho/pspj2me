package javax.microedition.rms;

import java.io.*;
import java.util.Hashtable;
import java.util.Vector;
import pspj2me.Native;

public class RecordStore {
    public static final int AUTHMODE_PRIVATE = 0, AUTHMODE_ANY = 1;
    private static final Hashtable open = new Hashtable();
    private static final int MAX_SIZE = 4 * 1024 * 1024;

    private final String name;
    private int openCount;
    private int nextId = 1;
    private int version;
    private long lastModified;
    private final Vector ids = new Vector();      /* Integer */
    private final Vector data = new Vector();     /* byte[] */
    private final Vector listeners = new Vector();
    private int authMode;
    private boolean writable = true;

    private RecordStore(String name) { this.name = name; }

    public static RecordStore openRecordStore(String recordStoreName, boolean createIfNecessary)
            throws RecordStoreException, RecordStoreFullException, RecordStoreNotFoundException {
        if (recordStoreName == null) throw new NullPointerException();
        if (recordStoreName.length() < 1 || recordStoreName.length() > 32) throw new IllegalArgumentException();
        synchronized (open) {
            RecordStore rs = (RecordStore) open.get(recordStoreName);
            if (rs == null) {
                rs = new RecordStore(recordStoreName);
                byte[] raw = Native.rmsRead(recordStoreName);
                if (raw == null) {
                    if (!createIfNecessary) throw new RecordStoreNotFoundException(recordStoreName);
                    rs.lastModified = System.currentTimeMillis();
                    rs.save();
                } else {
                    try { rs.load(raw); } catch (IOException e) { throw new RecordStoreException("corrupt record store " + recordStoreName); }
                }
                open.put(recordStoreName, rs);
            }
            rs.openCount++;
            return rs;
        }
    }

    public static RecordStore openRecordStore(String recordStoreName, boolean createIfNecessary, int authmode, boolean writable)
            throws RecordStoreException {
        RecordStore rs = openRecordStore(recordStoreName, createIfNecessary);
        rs.authMode = authmode;
        rs.writable = writable;
        return rs;
    }

    public static RecordStore openRecordStore(String recordStoreName, String vendorName, String suiteName) throws RecordStoreException {
        return openRecordStore(recordStoreName, false);
    }

    public static void deleteRecordStore(String recordStoreName) throws RecordStoreException, RecordStoreNotFoundException {
        synchronized (open) {
            RecordStore rs = (RecordStore) open.get(recordStoreName);
            if (rs != null && rs.openCount > 0) throw new RecordStoreException("record store is open");
            open.remove(recordStoreName);
            if (!Native.rmsDelete(recordStoreName)) throw new RecordStoreNotFoundException(recordStoreName);
        }
    }

    public static String[] listRecordStores() {
        String[] l = Native.rmsList();
        return l == null || l.length == 0 ? null : l;
    }

    public void setMode(int authmode, boolean writable) throws RecordStoreException {
        checkOpen();
        authMode = authmode;
        this.writable = writable;
    }

    public void closeRecordStore() throws RecordStoreNotOpenException, RecordStoreException {
        synchronized (open) {
            checkOpen();
            if (--openCount == 0) {
                listeners.removeAllElements();
                open.remove(name);
            }
        }
    }

    private void checkOpen() throws RecordStoreNotOpenException {
        if (openCount <= 0) throw new RecordStoreNotOpenException(name);
    }

    private void load(byte[] raw) throws IOException {
        DataInputStream in = new DataInputStream(new ByteArrayInputStream(raw));
        if (in.readInt() != 0x524d5331) throw new IOException("bad magic");
        nextId = in.readInt();
        version = in.readInt();
        lastModified = in.readLong();
        int n = in.readInt();
        for (int i = 0; i < n; i++) {
            int id = in.readInt();
            int len = in.readInt();
            byte[] b = new byte[len];
            in.readFully(b);
            ids.addElement(new Integer(id));
            data.addElement(b);
        }
    }

    private void save() throws RecordStoreException {
        try {
            ByteArrayOutputStream bos = new ByteArrayOutputStream(256 + sizeOfData());
            DataOutputStream out = new DataOutputStream(bos);
            out.writeInt(0x524d5331);
            out.writeInt(nextId);
            out.writeInt(version);
            out.writeLong(lastModified);
            out.writeInt(ids.size());
            for (int i = 0; i < ids.size(); i++) {
                byte[] b = (byte[]) data.elementAt(i);
                out.writeInt(((Integer) ids.elementAt(i)).intValue());
                out.writeInt(b.length);
                out.write(b, 0, b.length);
            }
            if (!Native.rmsWrite(name, bos.toByteArray())) throw new RecordStoreException("write failed");
        } catch (IOException e) {
            throw new RecordStoreException(e.toString());
        }
    }

    private int sizeOfData() {
        int s = 0;
        for (int i = 0; i < data.size(); i++) s += ((byte[]) data.elementAt(i)).length + 8;
        return s;
    }

    private void touch() throws RecordStoreException {
        version++;
        lastModified = System.currentTimeMillis();
        save();
    }

    private int indexOf(int id) throws InvalidRecordIDException {
        for (int i = 0; i < ids.size(); i++) if (((Integer) ids.elementAt(i)).intValue() == id) return i;
        throw new InvalidRecordIDException(String.valueOf(id));
    }

    public String getName() throws RecordStoreNotOpenException { checkOpen(); return name; }
    public int getVersion() throws RecordStoreNotOpenException { checkOpen(); return version; }
    public int getNumRecords() throws RecordStoreNotOpenException { checkOpen(); return ids.size(); }
    public int getSize() throws RecordStoreNotOpenException { checkOpen(); return sizeOfData() + 32; }
    public int getSizeAvailable() throws RecordStoreNotOpenException { checkOpen(); return MAX_SIZE - getSize(); }
    public long getLastModified() throws RecordStoreNotOpenException { checkOpen(); return lastModified; }
    public void addRecordListener(RecordListener l) { if (!listeners.contains(l)) listeners.addElement(l); }
    public void removeRecordListener(RecordListener l) { listeners.removeElement(l); }
    public int getNextRecordID() throws RecordStoreNotOpenException, RecordStoreException { checkOpen(); return nextId; }

    public int addRecord(byte[] d, int offset, int numBytes) throws RecordStoreNotOpenException, RecordStoreException, RecordStoreFullException {
        int id;
        synchronized (this) {
            checkOpen();
            if (d == null && numBytes > 0) throw new NullPointerException();
            if (getSize() + numBytes > MAX_SIZE) throw new RecordStoreFullException();
            byte[] b = new byte[numBytes];
            if (numBytes > 0) System.arraycopy(d, offset, b, 0, numBytes);
            id = nextId++;
            ids.addElement(new Integer(id));
            data.addElement(b);
            touch();
        }
        for (int i = 0; i < listeners.size(); i++) ((RecordListener) listeners.elementAt(i)).recordAdded(this, id);
        return id;
    }

    public void deleteRecord(int recordId) throws RecordStoreNotOpenException, InvalidRecordIDException, RecordStoreException {
        synchronized (this) {
            checkOpen();
            int i = indexOf(recordId);
            ids.removeElementAt(i);
            data.removeElementAt(i);
            touch();
        }
        for (int i = 0; i < listeners.size(); i++) ((RecordListener) listeners.elementAt(i)).recordDeleted(this, recordId);
    }

    public int getRecordSize(int recordId) throws RecordStoreNotOpenException, InvalidRecordIDException, RecordStoreException {
        checkOpen();
        return ((byte[]) data.elementAt(indexOf(recordId))).length;
    }

    public synchronized int getRecord(int recordId, byte[] buffer, int offset) throws RecordStoreNotOpenException, InvalidRecordIDException, RecordStoreException {
        checkOpen();
        byte[] b = (byte[]) data.elementAt(indexOf(recordId));
        System.arraycopy(b, 0, buffer, offset, b.length);
        return b.length;
    }

    public synchronized byte[] getRecord(int recordId) throws RecordStoreNotOpenException, InvalidRecordIDException, RecordStoreException {
        checkOpen();
        byte[] b = (byte[]) data.elementAt(indexOf(recordId));
        if (b.length == 0) return null;
        byte[] r = new byte[b.length];
        System.arraycopy(b, 0, r, 0, b.length);
        return r;
    }

    public void setRecord(int recordId, byte[] newData, int offset, int numBytes) throws RecordStoreNotOpenException, InvalidRecordIDException, RecordStoreException, RecordStoreFullException {
        synchronized (this) {
            checkOpen();
            int i = indexOf(recordId);
            byte[] b = new byte[numBytes];
            if (numBytes > 0) System.arraycopy(newData, offset, b, 0, numBytes);
            data.setElementAt(b, i);
            touch();
        }
        for (int i = 0; i < listeners.size(); i++) ((RecordListener) listeners.elementAt(i)).recordChanged(this, recordId);
    }

    public RecordEnumeration enumerateRecords(RecordFilter filter, RecordComparator comparator, boolean keepUpdated) throws RecordStoreNotOpenException {
        checkOpen();
        return new Enum(this, filter, comparator, keepUpdated);
    }

    synchronized int[] snapshotIds() {
        int[] r = new int[ids.size()];
        for (int i = 0; i < r.length; i++) r[i] = ((Integer) ids.elementAt(i)).intValue();
        return r;
    }
    synchronized byte[] rawRecord(int id) {
        for (int i = 0; i < ids.size(); i++) if (((Integer) ids.elementAt(i)).intValue() == id) return (byte[]) data.elementAt(i);
        return null;
    }

    private static final class Enum implements RecordEnumeration, RecordListener {
        private final RecordStore rs;
        private final RecordFilter filter;
        private final RecordComparator comp;
        private boolean keep;
        private int[] list;
        private int pos;   /* index of next element */

        Enum(RecordStore rs, RecordFilter f, RecordComparator c, boolean keep) {
            this.rs = rs; filter = f; comp = c;
            rebuild();
            keepUpdated(keep);
        }
        public int numRecords() { return list.length; }
        public void rebuild() {
            int[] all = rs.snapshotIds();
            int n = 0;
            int[] tmp = new int[all.length];
            for (int i = 0; i < all.length; i++) {
                byte[] b = rs.rawRecord(all[i]);
                if (filter == null || filter.matches(b == null || b.length == 0 ? null : copy(b))) tmp[n++] = all[i];
            }
            list = new int[n];
            System.arraycopy(tmp, 0, list, 0, n);
            if (comp != null) {
                for (int i = 1; i < n; i++) {
                    int v = list[i], k = i - 1;
                    byte[] bv = copy(rs.rawRecord(v));
                    while (k >= 0 && comp.compare(copy(rs.rawRecord(list[k])), bv) == RecordComparator.FOLLOWS) { list[k + 1] = list[k]; k--; }
                    list[k + 1] = v;
                }
            }
            pos = 0;
        }
        private static byte[] copy(byte[] b) {
            if (b == null) return null;
            byte[] r = new byte[b.length];
            System.arraycopy(b, 0, r, 0, b.length);
            return r;
        }
        public int nextRecordId() throws InvalidRecordIDException {
            if (pos >= list.length) throw new InvalidRecordIDException();
            return list[pos++];
        }
        public byte[] nextRecord() throws InvalidRecordIDException, RecordStoreNotOpenException, RecordStoreException {
            return rs.getRecord(nextRecordId());
        }
        public int previousRecordId() throws InvalidRecordIDException {
            if (pos <= 0) {
                if (list.length == 0) throw new InvalidRecordIDException();
                pos = list.length;
            }
            return list[--pos];
        }
        public byte[] previousRecord() throws InvalidRecordIDException, RecordStoreNotOpenException, RecordStoreException {
            return rs.getRecord(previousRecordId());
        }
        public boolean hasNextElement() { return pos < list.length; }
        public boolean hasPreviousElement() { return pos > 0; }
        public void reset() { pos = 0; }
        public void keepUpdated(boolean k) {
            if (k == keep) return;
            keep = k;
            if (k) { rs.addRecordListener(this); rebuild(); } else rs.removeRecordListener(this);
        }
        public boolean isKeptUpdated() { return keep; }
        public void destroy() { keepUpdated(false); }
        public void recordAdded(RecordStore r, int id) { int p = pos; rebuild(); pos = Math.min(p, list.length); }
        public void recordChanged(RecordStore r, int id) { int p = pos; rebuild(); pos = Math.min(p, list.length); }
        public void recordDeleted(RecordStore r, int id) { int p = pos; rebuild(); pos = Math.min(p, list.length); }
    }
}
