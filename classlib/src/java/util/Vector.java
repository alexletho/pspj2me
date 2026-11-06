package java.util;

public class Vector {
    protected Object[] elementData;
    protected int elementCount;
    protected int capacityIncrement;

    public Vector(int initialCapacity, int capacityIncrement) {
        if (initialCapacity < 0) throw new IllegalArgumentException();
        elementData = new Object[initialCapacity];
        this.capacityIncrement = capacityIncrement;
    }
    public Vector(int initialCapacity) { this(initialCapacity, 0); }
    public Vector() { this(10); }

    public synchronized void copyInto(Object[] anArray) { System.arraycopy(elementData, 0, anArray, 0, elementCount); }
    public synchronized void trimToSize() {
        if (elementCount < elementData.length) {
            Object[] n = new Object[elementCount];
            System.arraycopy(elementData, 0, n, 0, elementCount);
            elementData = n;
        }
    }
    public synchronized void ensureCapacity(int min) { if (min > elementData.length) grow(min); }
    private void grow(int min) {
        int n = capacityIncrement > 0 ? elementData.length + capacityIncrement : elementData.length * 2;
        if (n < min) n = min;
        Object[] a = new Object[n];
        System.arraycopy(elementData, 0, a, 0, elementCount);
        elementData = a;
    }
    public synchronized void setSize(int newSize) {
        if (newSize > elementData.length) grow(newSize);
        for (int i = newSize; i < elementCount; i++) elementData[i] = null;
        elementCount = newSize;
    }
    public int capacity() { return elementData.length; }
    public int size() { return elementCount; }
    public boolean isEmpty() { return elementCount == 0; }
    public synchronized Enumeration elements() {
        return new Enumeration() {
            int i = 0;
            public boolean hasMoreElements() { return i < elementCount; }
            public Object nextElement() {
                synchronized (Vector.this) {
                    if (i < elementCount) return elementData[i++];
                }
                throw new NoSuchElementException();
            }
        };
    }
    public boolean contains(Object elem) { return indexOf(elem, 0) >= 0; }
    public int indexOf(Object elem) { return indexOf(elem, 0); }
    public synchronized int indexOf(Object elem, int index) {
        for (int i = index; i < elementCount; i++)
            if (elem == null ? elementData[i] == null : elem.equals(elementData[i])) return i;
        return -1;
    }
    public int lastIndexOf(Object elem) { return lastIndexOf(elem, elementCount - 1); }
    public synchronized int lastIndexOf(Object elem, int index) {
        if (index >= elementCount) throw new IndexOutOfBoundsException();
        for (int i = index; i >= 0; i--)
            if (elem == null ? elementData[i] == null : elem.equals(elementData[i])) return i;
        return -1;
    }
    public synchronized Object elementAt(int index) {
        if (index >= elementCount) throw new ArrayIndexOutOfBoundsException(index + " >= " + elementCount);
        return elementData[index];
    }
    public Object get(int index) { return elementAt(index); }
    public synchronized Object firstElement() {
        if (elementCount == 0) throw new NoSuchElementException();
        return elementData[0];
    }
    public synchronized Object lastElement() {
        if (elementCount == 0) throw new NoSuchElementException();
        return elementData[elementCount - 1];
    }
    public synchronized void setElementAt(Object obj, int index) {
        if (index >= elementCount) throw new ArrayIndexOutOfBoundsException(index + " >= " + elementCount);
        elementData[index] = obj;
    }
    public synchronized Object set(int index, Object obj) {
        Object old = elementAt(index);
        elementData[index] = obj;
        return old;
    }
    public synchronized void removeElementAt(int index) {
        if (index >= elementCount || index < 0) throw new ArrayIndexOutOfBoundsException(index);
        int j = elementCount - index - 1;
        if (j > 0) System.arraycopy(elementData, index + 1, elementData, index, j);
        elementCount--;
        elementData[elementCount] = null;
    }
    public synchronized Object remove(int index) {
        Object o = elementAt(index);
        removeElementAt(index);
        return o;
    }
    public synchronized void insertElementAt(Object obj, int index) {
        if (index > elementCount || index < 0) throw new ArrayIndexOutOfBoundsException(index);
        if (elementCount >= elementData.length) grow(elementCount + 1);
        System.arraycopy(elementData, index, elementData, index + 1, elementCount - index);
        elementData[index] = obj;
        elementCount++;
    }
    public synchronized void addElement(Object obj) {
        if (elementCount >= elementData.length) grow(elementCount + 1);
        elementData[elementCount++] = obj;
    }
    public boolean add(Object obj) { addElement(obj); return true; }
    public synchronized boolean removeElement(Object obj) {
        int i = indexOf(obj);
        if (i >= 0) { removeElementAt(i); return true; }
        return false;
    }
    public boolean remove(Object obj) { return removeElement(obj); }
    public synchronized void removeAllElements() {
        for (int i = 0; i < elementCount; i++) elementData[i] = null;
        elementCount = 0;
    }
    public void clear() { removeAllElements(); }
    public synchronized String toString() {
        StringBuffer sb = new StringBuffer("[");
        for (int i = 0; i < elementCount; i++) {
            if (i > 0) sb.append(", ");
            sb.append(String.valueOf(elementData[i]));
        }
        return sb.append("]").toString();
    }
}
