/* Natives for java.lang.* */
#include "../vm/vm.h"
#include "../platform/platform.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

int push_frame(Thread *t, Method *m, int32_t *args);
void thread_interrupt(Thread *t);
Class *load_class_quiet(const char *name);

static uint32_t classVmOff, threadVmOff, threadNameOff;

static Class *mirrorClass(ref_t mirror) { return g_classes[F_INT(mirror, classVmOff)]; }

#define NPE_IF(x) do { if (!(x)) { throw_exception(t, "java/lang/NullPointerException", NULL); return NAT_EXC; } } while (0)

/* ---------- Object ---------- */
static int Object_getClass(Thread *t, int32_t *a) {
    ref_t m = class_mirror(CLASS_OF((ref_t)a[0]));
    if (!m) { throw_exception(t, "java/lang/OutOfMemoryError", NULL); return NAT_EXC; }
    a[0] = m;
    return NAT_OK;
}
static int Object_hashCode(Thread *t, int32_t *a) { (void)t; a[0] = (int32_t)(a[0] >> 3) * 0x9E3779B1u; return NAT_OK; }
static int Object_clone(Thread *t, int32_t *a) {
    ref_t o = (ref_t)a[0];
    Class *c = CLASS_OF(o);
    ref_t n;
    if (c->isArray) {
        n = new_array(c, A_LEN(o));
        if (!n) { throw_exception(t, "java/lang/OutOfMemoryError", NULL); return NAT_EXC; }
        memcpy(O_DATA(n), O_DATA(o), (size_t)A_LEN(o) * c->elemSize);
    } else {
        Class *cl = load_class("java/lang/Cloneable");
        if (!cl || !is_assignable(c, cl)) { throw_exception(t, "java/lang/CloneNotSupportedException", c->name); return NAT_EXC; }
        n = new_object(c);
        if (!n) { throw_exception(t, "java/lang/OutOfMemoryError", NULL); return NAT_EXC; }
        memcpy(O_DATA(n), O_DATA(o), c->instanceSize);
    }
    a[0] = n;
    return NAT_OK;
}
static int Object_notify(Thread *t, int32_t *a) {
    if (monitor_notify(t, (ref_t)a[0], 0) < 0) { throw_exception(t, "java/lang/IllegalMonitorStateException", NULL); return NAT_EXC; }
    return NAT_OK;
}
static int Object_notifyAll(Thread *t, int32_t *a) {
    if (monitor_notify(t, (ref_t)a[0], 1) < 0) { throw_exception(t, "java/lang/IllegalMonitorStateException", NULL); return NAT_EXC; }
    return NAT_OK;
}
static int Object_wait(Thread *t, int32_t *a) {
    int64_t ms = slot_get_long(a + 1);
    if (ms < 0) { throw_exception(t, "java/lang/IllegalArgumentException", "timeout value is negative"); return NAT_EXC; }
    if (t->interrupted) { t->interrupted = 0; throw_exception(t, "java/lang/InterruptedException", NULL); return NAT_EXC; }
    if (monitor_wait(t, (ref_t)a[0], ms) < 0) { throw_exception(t, "java/lang/IllegalMonitorStateException", NULL); return NAT_EXC; }
    return NAT_YIELD;
}

/* ---------- Class ---------- */
static int Class_forName(Thread *t, int32_t *a) {
    NPE_IF(a[0]);
    char buf[512];
    string_to_utf8((ref_t)a[0], buf, sizeof buf);
    for (char *p = buf; *p; p++) if (*p == '.') *p = '/';
    Class *c = buf[0] == '[' ? array_class_by_name(buf) : load_class_quiet(buf);
    if (!c) {
        t->exception = 0;
        string_to_utf8((ref_t)a[0], buf, sizeof buf);
        throw_exception(t, "java/lang/ClassNotFoundException", buf);
        return NAT_EXC;
    }
    int r = class_needs_init(t, c);
    if (r == 1 || r == 2) return NAT_RETRY;
    if (r < 0) return NAT_EXC;
    a[0] = class_mirror(c);
    return NAT_OK;
}
static int Class_newInstance(Thread *t, int32_t *a) {
    Class *c = mirrorClass((ref_t)a[0]);
    if (c->isArray || (c->access & (ACC_ABSTRACT | ACC_INTERFACE))) {
        throw_exception(t, "java/lang/InstantiationException", c->name); return NAT_EXC;
    }
    Method *init = NULL;
    for (int i = 0; i < c->nMethods; i++)
        if (strcmp(c->methods[i].name, "<init>") == 0 && strcmp(c->methods[i].desc, "()V") == 0) { init = &c->methods[i]; break; }
    if (!init) { throw_exception(t, "java/lang/InstantiationException", c->name); return NAT_EXC; }
    int r = class_needs_init(t, c);
    if (r == 1 || r == 2) return NAT_RETRY;
    if (r < 0) return NAT_EXC;
    ref_t o = new_object(c);
    if (!o) { throw_exception(t, "java/lang/OutOfMemoryError", NULL); return NAT_EXC; }
    a[0] = o;
    a[1] = o;
    if (push_frame(t, init, a + 1) != 0) return NAT_EXC;
    return NAT_OK;
}
static int Class_isInstance(Thread *t, int32_t *a) { (void)t; a[0] = a[1] ? instance_of((ref_t)a[1], mirrorClass((ref_t)a[0])) : 0; return NAT_OK; }
static int Class_isAssignableFrom(Thread *t, int32_t *a) {
    NPE_IF(a[1]);
    a[0] = is_assignable(mirrorClass((ref_t)a[1]), mirrorClass((ref_t)a[0]));
    return NAT_OK;
}
static int Class_isInterface(Thread *t, int32_t *a) { (void)t; a[0] = mirrorClass((ref_t)a[0])->isInterface; return NAT_OK; }
static int Class_isArray(Thread *t, int32_t *a) { (void)t; a[0] = mirrorClass((ref_t)a[0])->isArray; return NAT_OK; }
static int Class_getName(Thread *t, int32_t *a) {
    Class *c = mirrorClass((ref_t)a[0]);
    char buf[512];
    snprintf(buf, sizeof buf, "%s", c->name);
    for (char *p = buf; *p; p++) if (*p == '/') *p = '.';
    ref_t s = new_string_utf8(buf);
    if (!s) { throw_exception(t, "java/lang/OutOfMemoryError", NULL); return NAT_EXC; }
    a[0] = s;
    return NAT_OK;
}
static int Class_getSuperclass(Thread *t, int32_t *a) {
    (void)t;
    Class *c = mirrorClass((ref_t)a[0]);
    a[0] = (c->super && !c->isInterface) ? class_mirror(c->super) : 0;
    return NAT_OK;
}
static int Class_getResourceBytes(Thread *t, int32_t *a) {
    NPE_IF(a[0]);
    char buf[512];
    string_to_utf8((ref_t)a[0], buf, sizeof buf);
    /* normalise "a/./b" and leading slashes */
    char *p = buf;
    while (*p == '/') p++;
    uint32_t len;
    uint8_t *data = vm_read_resource(p, &len);
    if (!data) { a[0] = 0; return NAT_OK; }
    ref_t arr = new_prim_array('B', len);
    if (!arr) { free(data); throw_exception(t, "java/lang/OutOfMemoryError", NULL); return NAT_EXC; }
    memcpy(O_DATA(arr), data, len);
    free(data);
    a[0] = arr;
    return NAT_OK;
}

/* ---------- Throwable ---------- */
static int Throwable_fill(Thread *t, int32_t *a) { fill_stack_trace(t, (ref_t)a[0]); return NAT_OK; }
static int Throwable_print(Thread *t, int32_t *a) { print_stack_trace(t, (ref_t)a[0]); return NAT_OK; }

/* ---------- String ---------- */
static int String_intern(Thread *t, int32_t *a) { (void)t; a[0] = intern_string((ref_t)a[0]); return NAT_OK; }

/* ---------- Float / Double ---------- */
static void javaNumStr(double d, int isFloat, char *out) {
    if (d != d) { strcpy(out, "NaN"); return; }
    if (isinf(d)) { strcpy(out, d > 0 ? "Infinity" : "-Infinity"); return; }
    if (d == 0) { strcpy(out, signbit(d) ? "-0.0" : "0.0"); return; }
    char buf[64];
    int maxp = isFloat ? 9 : 17;
    int p;
    for (p = 1; p <= maxp; p++) {
        snprintf(buf, sizeof buf, "%.*e", p - 1, d);
        double back = strtod(buf, NULL);
        if (isFloat ? ((float)back == (float)d) : (back == d)) break;
    }
    if (p > maxp) { p = maxp; snprintf(buf, sizeof buf, "%.*e", p - 1, d); }
    /* parse digits and exponent */
    char digits[32]; int nd = 0; int neg = 0;
    const char *s = buf;
    if (*s == '-') { neg = 1; s++; }
    while (*s && *s != 'e') { if (*s >= '0' && *s <= '9') digits[nd++] = *s; s++; }
    int exp = atoi(s + 1);
    while (nd > 1 && digits[nd - 1] == '0') nd--;
    digits[nd] = 0;
    char *o = out;
    if (neg) *o++ = '-';
    double ad = fabs(d);
    if (ad >= 1e-3 && ad < 1e7) {
        if (exp >= 0) {
            for (int i = 0; i <= exp; i++) *o++ = i < nd ? digits[i] : '0';
            *o++ = '.';
            if (nd > exp + 1) for (int i = exp + 1; i < nd; i++) *o++ = digits[i];
            else *o++ = '0';
        } else {
            *o++ = '0'; *o++ = '.';
            for (int i = 0; i < -exp - 1; i++) *o++ = '0';
            for (int i = 0; i < nd; i++) *o++ = digits[i];
        }
        *o = 0;
    } else {
        *o++ = digits[0]; *o++ = '.';
        if (nd > 1) for (int i = 1; i < nd; i++) *o++ = digits[i];
        else *o++ = '0';
        sprintf(o, "E%d", exp);
    }
}
static int Float_toString(Thread *t, int32_t *a) {
    char buf[64];
    javaNumStr(slot_get_float(a), 1, buf);
    a[0] = new_string_utf8(buf);
    if (!a[0]) { throw_exception(t, "java/lang/OutOfMemoryError", NULL); return NAT_EXC; }
    return NAT_OK;
}
static int Double_toString(Thread *t, int32_t *a) {
    char buf[64];
    javaNumStr(slot_get_double(a), 0, buf);
    a[0] = new_string_utf8(buf);
    if (!a[0]) { throw_exception(t, "java/lang/OutOfMemoryError", NULL); return NAT_EXC; }
    return NAT_OK;
}
static int Double_parse(Thread *t, int32_t *a) {
    char buf[128];
    string_to_utf8((ref_t)a[0], buf, sizeof buf);
    char *s = buf;
    double v;
    if (strcmp(s, "NaN") == 0 || strcmp(s, "+NaN") == 0 || strcmp(s, "-NaN") == 0) v = NAN;
    else if (strcmp(s, "Infinity") == 0 || strcmp(s, "+Infinity") == 0) v = INFINITY;
    else if (strcmp(s, "-Infinity") == 0) v = -INFINITY;
    else {
        /* reject hex / inf / nan forms that strtod accepts */
        for (char *p = s; *p; p++) {
            char c = *p;
            if (!((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-' ||
                  ((c == 'f' || c == 'F' || c == 'd' || c == 'D') && p[1] == 0))) {
                throw_exception(t, "java/lang/NumberFormatException", buf); return NAT_EXC;
            }
        }
        char *end;
        v = strtod(s, &end);
        if (end == s || (*end && !(end[1] == 0 && (*end == 'f' || *end == 'F' || *end == 'd' || *end == 'D')))) {
            throw_exception(t, "java/lang/NumberFormatException", buf); return NAT_EXC;
        }
    }
    slot_put_double(a, v);
    return NAT_OK;
}
static int Float_floatToIntBits(Thread *t, int32_t *a) {
    (void)t;
    float f = slot_get_float(a);
    if (f != f) a[0] = 0x7fc00000;
    return NAT_OK;
}
static int Float_intBitsToFloat(Thread *t, int32_t *a) { (void)t; (void)a; return NAT_OK; }
static int Double_doubleToLongBits(Thread *t, int32_t *a) {
    (void)t;
    double d = slot_get_double(a);
    if (d != d) slot_put_long(a, 0x7ff8000000000000LL);
    return NAT_OK;
}
static int Double_longBitsToDouble(Thread *t, int32_t *a) { (void)t; (void)a; return NAT_OK; }

/* ---------- Math ---------- */
#define MATH1(name, fn) static int Math_##name(Thread *t, int32_t *a) { (void)t; slot_put_double(a, fn(slot_get_double(a))); return NAT_OK; }
MATH1(sin, sin) MATH1(cos, cos) MATH1(tan, tan) MATH1(asin, asin) MATH1(acos, acos) MATH1(atan, atan)
MATH1(sqrt, sqrt) MATH1(ceil, ceil) MATH1(floor, floor) MATH1(exp, exp) MATH1(log, log)
static int Math_atan2(Thread *t, int32_t *a) { (void)t; slot_put_double(a, atan2(slot_get_double(a), slot_get_double(a + 2))); return NAT_OK; }
static int Math_pow(Thread *t, int32_t *a) { (void)t; slot_put_double(a, pow(slot_get_double(a), slot_get_double(a + 2))); return NAT_OK; }

/* ---------- System / Runtime ---------- */
static int System_currentTimeMillis(Thread *t, int32_t *a) {
    (void)t;
    extern int64_t plat_wallclock_ms(void);
    slot_put_long(a, plat_wallclock_ms());
    return NAT_OK;
}
static int System_arraycopy(Thread *t, int32_t *a) {
    ref_t src = (ref_t)a[0], dst = (ref_t)a[2];
    int32_t sp = a[1], dp = a[3], len = a[4];
    NPE_IF(src && dst);
    Class *sc = CLASS_OF(src), *dc = CLASS_OF(dst);
    if (!sc->isArray || !dc->isArray || (sc->elemType != dc->elemType)) {
        throw_exception(t, "java/lang/ArrayStoreException", NULL); return NAT_EXC;
    }
    if (len < 0 || sp < 0 || dp < 0 || (int64_t)sp + len > A_LEN(src) || (int64_t)dp + len > A_LEN(dst)) {
        throw_exception(t, "java/lang/ArrayIndexOutOfBoundsException", NULL); return NAT_EXC;
    }
    if (len == 0) return NAT_OK;
    int es = sc->elemSize;
    if (sc->elemType == 'L' && sc != dc && !is_assignable(sc->elemClass, dc->elemClass)) {
        ref_t *s = A_REF(src) + sp, *d = A_REF(dst) + dp;
        for (int32_t i = 0; i < len; i++) {
            if (s[i] && !is_assignable(CLASS_OF(s[i]), dc->elemClass)) {
                throw_exception(t, "java/lang/ArrayStoreException", NULL); return NAT_EXC;
            }
            d[i] = s[i];
        }
        return NAT_OK;
    }
    memmove((uint8_t *)O_DATA(dst) + (size_t)dp * es, (uint8_t *)O_DATA(src) + (size_t)sp * es, (size_t)len * es);
    return NAT_OK;
}
static int System_identityHashCode(Thread *t, int32_t *a) { return Object_hashCode(t, a); }

typedef struct { const char *k, *v; } Prop;
static Prop props[32] = {
    { "microedition.configuration", "CLDC-1.1" },
    { "microedition.profiles", "MIDP-2.0" },
    { "microedition.platform", "PSPJ2ME" },
    { "microedition.encoding", "UTF-8" },
    { "microedition.locale", "en-US" },
    { "microedition.jtwi.version", "1.0" },
    { "microedition.media.version", "1.1" },
    { "supports.mixing", "true" },
    { "audio.encodings", NULL },
    { "streamable.contents", NULL },
    { "supports.audio.capture", "false" },
    { "supports.video.capture", "false" },
    { "supports.recording", "false" },
    { "microedition.io.file.FileConnection.version", NULL },
    { "fileconn.dir.photos", NULL },
    { NULL, NULL }
};
void vm_set_property(const char *k, const char *v) {
    int i;
    for (i = 0; props[i].k; i++) if (strcmp(props[i].k, k) == 0) { props[i].v = v; return; }
    if (i < 31) { props[i].k = k; props[i].v = v; props[i + 1].k = NULL; }
}
void lang_set_platform(const char *name) {
    for (int i = 0; props[i].k; i++) if (!strcmp(props[i].k, "microedition.platform")) props[i].v = name;
}

static int System_getProperty0(Thread *t, int32_t *a) {
    char buf[256];
    string_to_utf8((ref_t)a[0], buf, sizeof buf);
    a[0] = 0;
    for (int i = 0; props[i].k; i++) {
        if (strcmp(props[i].k, buf) == 0 && props[i].v) {
            a[0] = new_string_utf8(props[i].v);
            if (!a[0]) { throw_exception(t, "java/lang/OutOfMemoryError", NULL); return NAT_EXC; }
            break;
        }
    }
    return NAT_OK;
}
static int Runtime_exit(Thread *t, int32_t *a) { (void)t; g_exitCode = a[1]; g_vmExit = 1; return NAT_YIELD; }
static int Runtime_freeMemory(Thread *t, int32_t *a) { (void)t; slot_put_long(a, (int64_t)heap_free_bytes()); return NAT_OK; }
static int Runtime_totalMemory(Thread *t, int32_t *a) { (void)t; slot_put_long(a, (int64_t)heap_total_bytes()); return NAT_OK; }
static int Runtime_gc(Thread *t, int32_t *a) { (void)t; (void)a; gc_collect(); return NAT_OK; }

/* ---------- Thread ---------- */
static Thread *findThread(int id) {
    for (Thread *t = g_threads; t; t = t->next) if (t->id == id) return t;
    return NULL;
}
static int Thread_currentThread(Thread *t, int32_t *a) { a[0] = t->jthread; return NAT_OK; }
static int Thread_yield(Thread *t, int32_t *a) { (void)t; (void)a; return NAT_YIELD; }
int g_sleepScale = 100;
static int Thread_sleep(Thread *t, int32_t *a) {
    int64_t ms = slot_get_long(a);
    if (g_sleepScale != 100 && ms > 0) { ms = ms * g_sleepScale / 100; if (ms < 1) ms = 1; }
    if (ms < 0) { throw_exception(t, "java/lang/IllegalArgumentException", "timeout value is negative"); return NAT_EXC; }
    if (t->interrupted) { t->interrupted = 0; throw_exception(t, "java/lang/InterruptedException", NULL); return NAT_EXC; }
    if (ms == 0) return NAT_YIELD;
    t->state = TH_SLEEPING;
    t->wakeTime = vm_time_ms() + ms;
    return NAT_YIELD;
}
static int Thread_activeCount(Thread *t, int32_t *a) { (void)t; a[0] = count_live_threads(); return NAT_OK; }
static int Thread_start0(Thread *t, int32_t *a) {
    ref_t jt = (ref_t)a[0];
    Method *run = find_method(CLASS_OF(jt), "run", "()V");
    if (!run) { throw_exception(t, "java/lang/AbstractMethodError", "run"); return NAT_EXC; }
    Thread *nt = thread_create(jt);
    F_INT(jt, threadVmOff) = nt->id;
    int32_t args[1] = { (int32_t)jt };
    thread_push_frame_for(nt, run, args);
    nt->state = TH_RUNNABLE;
    return NAT_OK;
}
static int Thread_isAlive(Thread *t, int32_t *a) {
    (void)t;
    int id = F_INT((ref_t)a[0], threadVmOff);
    Thread *th = id ? findThread(id) : NULL;
    a[0] = th && th->state != TH_DEAD;
    return NAT_OK;
}
static int Thread_interrupt0(Thread *t, int32_t *a) {
    (void)t;
    int id = F_INT((ref_t)a[0], threadVmOff);
    Thread *th = id ? findThread(id) : NULL;
    if (th && th->state != TH_DEAD) thread_interrupt(th);
    return NAT_OK;
}

ref_t make_main_thread_object(Thread *t) {
    ref_t jt = new_object(C_Thread);
    PUSH_ROOT(t, jt);
    ref_t name = new_string_utf8("main");
    POP_ROOTS(t, 1);
    F_REF(jt, threadNameOff) = name;
    F_INT(jt, threadVmOff) = t->id;
    Field *pf = find_field_by_name(C_Thread, "priority");
    F_INT(jt, pf->offset) = 5;
    Field *sf = find_field_by_name(C_Thread, "started");
    F_INT(jt, sf->offset) = 1;
    return jt;
}

/* ---------- LogStream ---------- */
static char logBuf[1024];
static int logLen;
static void logByte(int b) {
    if (b == '\n' || logLen >= (int)sizeof(logBuf) - 2) {
        logBuf[logLen] = 0;
        vm_log("%s\n", logBuf);
        logLen = 0;
        if (b == '\n') return;
    }
    logBuf[logLen++] = (char)b;
}
static int Log_write(Thread *t, int32_t *a) { (void)t; logByte(a[1] & 0xff); return NAT_OK; }
static int Log_writeArr(Thread *t, int32_t *a) {
    ref_t b = (ref_t)a[1];
    NPE_IF(b);
    int off = a[2], len = a[3];
    if (off < 0 || len < 0 || off + len > A_LEN(b)) { throw_exception(t, "java/lang/IndexOutOfBoundsException", NULL); return NAT_EXC; }
    for (int i = 0; i < len; i++) logByte(A_BYTE(b)[off + i] & 0xff);
    return NAT_OK;
}

void natives_lang_init(void) {
#define R(c, n, d, f) vm_register_native(c, n, d, f)
    R("java/lang/Object", "getClass", "()Ljava/lang/Class;", Object_getClass);
    R("java/lang/Object", "hashCode", "()I", Object_hashCode);
    R("java/lang/Object", "clone", "()Ljava/lang/Object;", Object_clone);
    R("java/lang/Object", "notify", "()V", Object_notify);
    R("java/lang/Object", "notifyAll", "()V", Object_notifyAll);
    R("java/lang/Object", "wait", "(J)V", Object_wait);
    R("java/lang/Class", "forName", NULL, Class_forName);
    R("java/lang/Class", "newInstance", NULL, Class_newInstance);
    R("java/lang/Class", "isInstance", NULL, Class_isInstance);
    R("java/lang/Class", "isAssignableFrom", NULL, Class_isAssignableFrom);
    R("java/lang/Class", "isInterface", NULL, Class_isInterface);
    R("java/lang/Class", "isArray", NULL, Class_isArray);
    R("java/lang/Class", "getName", NULL, Class_getName);
    R("java/lang/Class", "getSuperclass", NULL, Class_getSuperclass);
    R("java/lang/Class", "getResourceBytes", NULL, Class_getResourceBytes);
    R("java/lang/Throwable", "fillInStackTrace0", NULL, Throwable_fill);
    R("java/lang/Throwable", "printStackTrace", NULL, Throwable_print);
    R("java/lang/String", "intern", NULL, String_intern);
    R("java/lang/Float", "toString", "(F)Ljava/lang/String;", Float_toString);
    R("java/lang/Float", "floatToIntBits", NULL, Float_floatToIntBits);
    R("java/lang/Float", "intBitsToFloat", NULL, Float_intBitsToFloat);
    R("java/lang/Double", "toString", "(D)Ljava/lang/String;", Double_toString);
    R("java/lang/Double", "parse0", NULL, Double_parse);
    R("java/lang/Double", "doubleToLongBits", NULL, Double_doubleToLongBits);
    R("java/lang/Double", "longBitsToDouble", NULL, Double_longBitsToDouble);
    R("java/lang/Math", "sin", NULL, Math_sin);
    R("java/lang/Math", "cos", NULL, Math_cos);
    R("java/lang/Math", "tan", NULL, Math_tan);
    R("java/lang/Math", "asin", NULL, Math_asin);
    R("java/lang/Math", "acos", NULL, Math_acos);
    R("java/lang/Math", "atan", NULL, Math_atan);
    R("java/lang/Math", "atan2", NULL, Math_atan2);
    R("java/lang/Math", "sqrt", NULL, Math_sqrt);
    R("java/lang/Math", "ceil", NULL, Math_ceil);
    R("java/lang/Math", "floor", NULL, Math_floor);
    R("java/lang/Math", "exp", NULL, Math_exp);
    R("java/lang/Math", "log", NULL, Math_log);
    R("java/lang/Math", "pow", NULL, Math_pow);
    R("java/lang/System", "currentTimeMillis", NULL, System_currentTimeMillis);
    R("java/lang/System", "arraycopy", NULL, System_arraycopy);
    R("java/lang/System", "identityHashCode", NULL, System_identityHashCode);
    R("java/lang/System", "getProperty0", NULL, System_getProperty0);
    R("java/lang/Runtime", "exit", NULL, Runtime_exit);
    R("java/lang/Runtime", "freeMemory", NULL, Runtime_freeMemory);
    R("java/lang/Runtime", "totalMemory", NULL, Runtime_totalMemory);
    R("java/lang/Runtime", "gc", NULL, Runtime_gc);
    R("java/lang/Thread", "currentThread", NULL, Thread_currentThread);
    R("java/lang/Thread", "yield", NULL, Thread_yield);
    R("java/lang/Thread", "sleep", NULL, Thread_sleep);
    R("java/lang/Thread", "activeCount", NULL, Thread_activeCount);
    R("java/lang/Thread", "start0", NULL, Thread_start0);
    R("java/lang/Thread", "isAlive", NULL, Thread_isAlive);
    R("java/lang/Thread", "interrupt0", NULL, Thread_interrupt0);
    R("pspj2me/LogStream", "write", "(I)V", Log_write);
    R("pspj2me/LogStream", "write", "([BII)V", Log_writeArr);
#undef R
}

/* called after core classes are loaded */
void natives_lang_post_init(void) {
    classVmOff = find_field_by_name(C_Class, "vmClass")->offset;
    threadVmOff = find_field_by_name(C_Thread, "vmThread")->offset;
    threadNameOff = find_field_by_name(C_Thread, "name")->offset;
}

void natives_lang_reset(void) { logLen = 0; }
