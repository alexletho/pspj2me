package java.util;

public class Hashtable {
    private static final class Entry {
        int hash; Object key; Object value; Entry next;
        Entry(int h, Object k, Object v, Entry n) { hash = h; key = k; value = v; next = n; }
    }
    private Entry[] table;
    private int count;
    private int threshold;

    public Hashtable(int initialCapacity) {
        if (initialCapacity < 0) throw new IllegalArgumentException();
        if (initialCapacity == 0) initialCapacity = 1;
        table = new Entry[initialCapacity];
        threshold = (initialCapacity * 3) / 4;
    }
    public Hashtable() { this(11); }

    public int size() { return count; }
    public boolean isEmpty() { return count == 0; }

    private class Enum implements Enumeration {
        boolean keys; int index; Entry entry;
        Enum(boolean keys) { this.keys = keys; index = table.length; }
        public boolean hasMoreElements() {
            while (entry == null && index > 0) entry = table[--index];
            return entry != null;
        }
        public Object nextElement() {
            while (entry == null && index > 0) entry = table[--index];
            if (entry == null) throw new NoSuchElementException();
            Entry e = entry;
            entry = e.next;
            return keys ? e.key : e.value;
        }
    }
    public synchronized Enumeration keys() { return new Enum(true); }
    public synchronized Enumeration elements() { return new Enum(false); }

    public synchronized boolean contains(Object value) {
        if (value == null) throw new NullPointerException();
        for (int i = table.length; i-- > 0;)
            for (Entry e = table[i]; e != null; e = e.next) if (e.value.equals(value)) return true;
        return false;
    }
    public boolean containsValue(Object v) { return contains(v); }
    public synchronized boolean containsKey(Object key) {
        int h = key.hashCode();
        for (Entry e = table[(h & 0x7fffffff) % table.length]; e != null; e = e.next)
            if (e.hash == h && e.key.equals(key)) return true;
        return false;
    }
    public synchronized Object get(Object key) {
        int h = key.hashCode();
        for (Entry e = table[(h & 0x7fffffff) % table.length]; e != null; e = e.next)
            if (e.hash == h && e.key.equals(key)) return e.value;
        return null;
    }
    protected void rehash() {
        Entry[] old = table;
        int nc = old.length * 2 + 1;
        Entry[] nt = new Entry[nc];
        threshold = (nc * 3) / 4;
        table = nt;
        for (int i = old.length; i-- > 0;) {
            for (Entry e = old[i]; e != null;) {
                Entry next = e.next;
                int idx = (e.hash & 0x7fffffff) % nc;
                e.next = nt[idx];
                nt[idx] = e;
                e = next;
            }
        }
    }
    public synchronized Object put(Object key, Object value) {
        if (value == null || key == null) throw new NullPointerException();
        int h = key.hashCode();
        int idx = (h & 0x7fffffff) % table.length;
        for (Entry e = table[idx]; e != null; e = e.next)
            if (e.hash == h && e.key.equals(key)) { Object old = e.value; e.value = value; return old; }
        if (count >= threshold) { rehash(); idx = (h & 0x7fffffff) % table.length; }
        table[idx] = new Entry(h, key, value, table[idx]);
        count++;
        return null;
    }
    public synchronized Object remove(Object key) {
        int h = key.hashCode();
        int idx = (h & 0x7fffffff) % table.length;
        for (Entry e = table[idx], prev = null; e != null; prev = e, e = e.next) {
            if (e.hash == h && e.key.equals(key)) {
                if (prev != null) prev.next = e.next; else table[idx] = e.next;
                count--;
                return e.value;
            }
        }
        return null;
    }
    public synchronized void clear() {
        for (int i = table.length; --i >= 0;) table[i] = null;
        count = 0;
    }
    public synchronized String toString() {
        StringBuffer sb = new StringBuffer("{");
        boolean first = true;
        for (int i = table.length; i-- > 0;)
            for (Entry e = table[i]; e != null; e = e.next) {
                if (!first) sb.append(", ");
                first = false;
                sb.append(String.valueOf(e.key)).append('=').append(String.valueOf(e.value));
            }
        return sb.append('}').toString();
    }
}
