package java.lang.ref;
public class WeakReference extends Reference {
    private Object referent; /* not traced by the GC */
    public WeakReference(Object ref) { referent = ref; }
    public Object get() { return referent; }
    public void clear() { referent = null; }
}
