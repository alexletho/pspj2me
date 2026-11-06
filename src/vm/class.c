/* Class loading, linking, lookup. */
#include "vm.h"
#include "zip.h"
#include <stdlib.h>
#include <stdio.h>

Class *g_classes[MAX_CLASSES];
int g_numClasses = 1;
Class *C_Object, *C_String, *C_Class, *C_Throwable, *C_Thread;
Class *C_intArray, *C_byteArray, *C_charArray, *C_shortArray, *C_longArray,
      *C_floatArray, *C_doubleArray, *C_boolArray, *C_objArray, *C_stringArray;

#define CLASS_HASH 2048
static Class *classHash[CLASS_HASH];

#define MAX_CP 8
static Zip *cpZips[MAX_CP];
static int cpIsBoot[MAX_CP];
static int nCp;

static unsigned strhash(const char *s) {
    unsigned h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

int vm_add_classpath_mem(uint8_t *data, size_t len, int isBoot) {
    Zip *z = zip_open_mem(data, len, 0);
    if (!z || nCp >= MAX_CP) return -1;
    cpZips[nCp] = z; cpIsBoot[nCp] = isBoot; nCp++;
    return 0;
}

int vm_add_classpath_file(const char *path) {
    Zip *z = zip_open_file(path);
    if (!z || nCp >= MAX_CP) return -1;
    cpZips[nCp] = z; cpIsBoot[nCp] = 0; nCp++;
    return 0;
}

uint8_t *vm_read_resource(const char *name, uint32_t *len) {
    while (*name == '/') name++;
    /* app jars first */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < nCp; i++) {
            if (cpIsBoot[i] != pass) continue;
            ZipEntry *e = zip_find(cpZips[i], name);
            if (e) return zip_read(cpZips[i], e, len);
        }
    }
    return NULL;
}

static uint8_t *readClassBytes(const char *name, uint32_t *len, int *fromBoot) {
    char buf[512];
    snprintf(buf, sizeof buf, "%s.class", name);
    for (int pass = 1; pass >= 0; pass--) {
        for (int i = 0; i < nCp; i++) {
            if (cpIsBoot[i] != pass) continue;
            ZipEntry *e = zip_find(cpZips[i], buf);
            if (e) { *fromBoot = pass; return zip_read(cpZips[i], e, len); }
        }
    }
    return NULL;
}

/* ---------------- natives registry ---------------- */
typedef struct NatEntry { const char *cls, *name, *desc; NativeFn fn; struct NatEntry *next; } NatEntry;
#define NAT_HASH 1024
static NatEntry *natHash[NAT_HASH];

static unsigned natKey(const char *c, const char *n) { return (strhash(c) * 31 + strhash(n)) & (NAT_HASH - 1); }

void vm_register_native(const char *cls, const char *name, const char *desc, NativeFn fn) {
    NatEntry *e = malloc(sizeof *e);
    e->cls = cls; e->name = name; e->desc = desc; e->fn = fn;
    unsigned k = natKey(cls, name);
    e->next = natHash[k];
    natHash[k] = e;
}

static int nat_unsatisfied(Thread *t, int32_t *args) {
    (void)args;
    Frame *f = &t->frames[t->fp - 1];
    /* find which method: decode invoke at caller pc */
    (void)f;
    throw_exception(t, "java/lang/Error", "UnsatisfiedLinkError");
    return NAT_EXC;
}

void link_natives(Class *c) {
    for (int i = 0; i < c->nMethods; i++) {
        Method *m = &c->methods[i];
        unsigned k = natKey(c->name, m->name);
        NativeFn fn = NULL;
        for (NatEntry *e = natHash[k]; e; e = e->next) {
            if (strcmp(e->cls, c->name) == 0 && strcmp(e->name, m->name) == 0 &&
                (e->desc == NULL || strcmp(e->desc, m->desc) == 0)) { fn = e->fn; break; }
        }
        if (fn) m->native = fn;
        else if (m->access & ACC_NATIVE) {
            vm_log("warning: missing native %s.%s%s\n", c->name, m->name, m->desc);
            m->native = nat_unsatisfied;
        }
    }
}

/* ---------------- descriptor helpers ---------------- */
int arg_slots(const char *desc, int isStatic) {
    int n = isStatic ? 0 : 1;
    const char *p = desc + 1;
    while (*p && *p != ')') {
        if (*p == 'J' || *p == 'D') { n += 2; p++; }
        else if (*p == 'L') { n++; while (*p && *p != ';') p++; if (*p) p++; }
        else if (*p == '[') {
            while (*p == '[') p++;
            if (*p == 'L') { while (*p && *p != ';') p++; }
            if (*p) p++;
            n++;
        } else { n++; p++; }
    }
    return n;
}

char ret_type(const char *desc) {
    const char *p = strchr(desc, ')');
    if (!p) return 'V';
    p++;
    if (*p == '[') return 'L';
    return *p;
}

/* ---------------- lookup ---------------- */
Class *find_loaded(const char *name) {
    unsigned h = strhash(name) & (CLASS_HASH - 1);
    for (Class *c = classHash[h]; c; c = c->hashNext)
        if (strcmp(c->name, name) == 0) return c;
    return NULL;
}

static void registerClass(Class *c) {
    if (g_numClasses >= MAX_CLASSES) vm_fatal("too many classes");
    c->id = g_numClasses;
    g_classes[g_numClasses++] = c;
    unsigned h = strhash(c->name) & (CLASS_HASH - 1);
    c->hashNext = classHash[h];
    classHash[h] = c;
}

Method *find_method(Class *c, const char *name, const char *desc) {
    for (; c; c = c->super) {
        for (int i = 0; i < c->nMethods; i++) {
            Method *m = &c->methods[i];
            if (strcmp(m->name, name) == 0 && strcmp(m->desc, desc) == 0) return m;
        }
    }
    return NULL;
}

static Method *findInIfaces(Class *c, const char *name, const char *desc) {
    for (; c; c = c->super) {
        for (int i = 0; i < c->nInterfaces; i++) {
            Class *ic = c->interfaces[i];
            for (int k = 0; k < ic->nMethods; k++)
                if (strcmp(ic->methods[k].name, name) == 0 && strcmp(ic->methods[k].desc, desc) == 0)
                    return &ic->methods[k];
            Method *m = findInIfaces(ic, name, desc);
            if (m) return m;
        }
    }
    return NULL;
}

Method *find_method_iface(Class *c, const char *name, const char *desc) {
    Method *m = find_method(c, name, desc);
    if (m) return m;
    m = findInIfaces(c, name, desc);
    if (m) return m;
    if (c->isInterface && C_Object) return find_method(C_Object, name, desc);
    return NULL;
}

Field *find_field(Class *c, const char *name, const char *desc) {
    for (; c; c = c->super) {
        for (int i = 0; i < c->nFields; i++)
            if (strcmp(c->fields[i].name, name) == 0 && (!desc || strcmp(c->fields[i].desc, desc) == 0))
                return &c->fields[i];
        /* static fields of interfaces */
        for (int i = 0; i < c->nInterfaces; i++) {
            Field *f = find_field(c->interfaces[i], name, desc);
            if (f) return f;
        }
    }
    return NULL;
}

Field *find_field_by_name(Class *c, const char *name) { return find_field(c, name, NULL); }

int is_subclass(Class *c, Class *of) {
    for (; c; c = c->super) if (c == of) return 1;
    return 0;
}

static int implements(Class *c, Class *iface) {
    for (; c; c = c->super) {
        if (c == iface) return 1;
        for (int i = 0; i < c->nInterfaces; i++)
            if (implements(c->interfaces[i], iface)) return 1;
    }
    return 0;
}

int is_assignable(Class *from, Class *to) {
    if (from == to) return 1;
    if (to == C_Object) return 1;
    if (from->isArray) {
        if (to->isInterface) return strcmp(to->name, "java/lang/Cloneable") == 0 || strcmp(to->name, "java/io/Serializable") == 0;
        if (!to->isArray) return 0;
        if (from->elemType != 'L' || to->elemType != 'L') return from->elemType == to->elemType;
        return is_assignable(from->elemClass, to->elemClass);
    }
    if (to->isInterface) return implements(from, to);
    if (to->isArray) return 0;
    return is_subclass(from, to);
}

int instance_of(ref_t obj, Class *c) {
    if (!obj) return 0;
    return is_assignable(CLASS_OF(obj), c);
}

/* ---------------- array classes ---------------- */
static Class *makeArrayClass(const char *name, char elemType, Class *elemClass) {
    Class *c = calloc(1, sizeof(Class));
    c->name = strdup(name);
    c->isArray = 1;
    c->elemType = elemType;
    c->elemClass = elemClass;
    switch (elemType) {
        case 'B': case 'Z': c->elemSize = 1; break;
        case 'C': case 'S': c->elemSize = 2; break;
        case 'J': case 'D': c->elemSize = 8; break;
        default: c->elemSize = 4;
    }
    c->super = C_Object;
    c->access = ACC_PUBLIC | ACC_FINAL;
    c->state = CLS_INITED;
    if (C_Object) { c->vtable = C_Object->vtable; c->vtableSize = C_Object->vtableSize; }
    registerClass(c);
    return c;
}

Class *prim_array_class(char t) {
    switch (t) {
        case 'I': return C_intArray;
        case 'B': return C_byteArray;
        case 'C': return C_charArray;
        case 'S': return C_shortArray;
        case 'J': return C_longArray;
        case 'F': return C_floatArray;
        case 'D': return C_doubleArray;
        case 'Z': return C_boolArray;
    }
    return NULL;
}

Class *array_class_of(Class *elem) {
    if (elem->arrayClass) return elem->arrayClass;
    char buf[512];
    if (elem->isArray) snprintf(buf, sizeof buf, "[%s", elem->name);
    else snprintf(buf, sizeof buf, "[L%s;", elem->name);
    Class *c = find_loaded(buf);
    if (!c) c = makeArrayClass(buf, 'L', elem);
    elem->arrayClass = c;
    return c;
}

Class *array_class_by_name(const char *name) {
    Class *c = find_loaded(name);
    if (c) return c;
    const char *e = name + 1;
    if (*e == '[') {
        Class *inner = array_class_by_name(e);
        if (!inner) return NULL;
        return array_class_of(inner);
    }
    if (*e == 'L') {
        char buf[512];
        size_t n = strlen(e);
        if (n < 3 || n > sizeof(buf)) return NULL;
        memcpy(buf, e + 1, n - 2);
        buf[n - 2] = 0;
        Class *ec = load_class(buf);
        if (!ec) return NULL;
        return array_class_of(ec);
    }
    return prim_array_class(*e);
}

/* ---------------- parsing ---------------- */
typedef struct { const uint8_t *p, *end; int err; } Rd;
static uint8_t u1(Rd *r) { if (r->p + 1 > r->end) { r->err = 1; return 0; } return *r->p++; }
static uint16_t u2(Rd *r) { if (r->p + 2 > r->end) { r->err = 1; return 0; } uint16_t v = (r->p[0] << 8) | r->p[1]; r->p += 2; return v; }
static uint32_t u4(Rd *r) { if (r->p + 4 > r->end) { r->err = 1; return 0; } uint32_t v = ((uint32_t)r->p[0] << 24) | (r->p[1] << 16) | (r->p[2] << 8) | r->p[3]; r->p += 4; return v; }

static const char *cpUtf8(Class *c, int idx) {
    if (idx <= 0 || idx >= c->cpCount || c->cp[idx].tag != CP_Utf8) return "";
    return c->cp[idx].v.utf8;
}

static const char *cpClassName(Class *c, int idx) {
    if (idx <= 0 || idx >= c->cpCount || c->cp[idx].tag != CP_Class) return NULL;
    return cpUtf8(c, c->cp[idx].a);
}

static Class *parseClass(const uint8_t *data, uint32_t len) {
    Rd r = { data, data + len, 0 };
    if (u4(&r) != 0xCAFEBABE) return NULL;
    u2(&r); u2(&r);
    Class *c = calloc(1, sizeof(Class));
    c->cpCount = u2(&r);
    c->cp = calloc(c->cpCount + 1, sizeof(CPEntry));
    for (int i = 1; i < c->cpCount; i++) {
        CPEntry *e = &c->cp[i];
        e->tag = u1(&r);
        switch (e->tag) {
            case CP_Utf8: {
                uint16_t n = u2(&r);
                if (r.p + n > r.end) { r.err = 1; break; }
                char *s = malloc(n + 1);
                memcpy(s, r.p, n); s[n] = 0;
                r.p += n;
                e->v.utf8 = s;
                break;
            }
            case CP_Integer: e->v.i = (int32_t)u4(&r); break;
            case CP_Float: { uint32_t b = u4(&r); memcpy(&e->v.f, &b, 4); break; }
            case CP_Long: case CP_Double: {
                uint64_t hi = u4(&r), lo = u4(&r);
                uint64_t b = (hi << 32) | lo;
                memcpy(&e->v.l, &b, 8);
                i++;
                break;
            }
            case CP_Class: case CP_String: case CP_MethodType: e->a = u2(&r); break;
            case CP_Fieldref: case CP_Methodref: case CP_InterfaceMethodref:
            case CP_NameAndType: case CP_InvokeDynamic:
                e->a = u2(&r); e->b = u2(&r); break;
            case CP_MethodHandle: u1(&r); e->a = u2(&r); break;
            default: r.err = 1;
        }
        if (r.err) break;
    }
    if (r.err) { free(c->cp); free(c); return NULL; }
    c->access = u2(&r);
    c->isInterface = (c->access & ACC_INTERFACE) != 0;
    c->name = cpClassName(c, u2(&r));
    uint16_t superIdx = u2(&r);
    const char *superName = superIdx ? cpClassName(c, superIdx) : NULL;
    c->nInterfaces = u2(&r);
    const char **ifNames = calloc(c->nInterfaces + 1, sizeof(char *));
    for (int i = 0; i < c->nInterfaces; i++) ifNames[i] = cpClassName(c, u2(&r));

    c->nFields = u2(&r);
    c->fields = calloc(c->nFields + 1, sizeof(Field));
    for (int i = 0; i < c->nFields && !r.err; i++) {
        Field *f = &c->fields[i];
        f->cls = c;
        f->access = u2(&r);
        f->name = cpUtf8(c, u2(&r));
        f->desc = cpUtf8(c, u2(&r));
        f->type = f->desc[0] == '[' ? 'L' : f->desc[0];
        uint16_t na = u2(&r);
        for (int a = 0; a < na && !r.err; a++) {
            const char *an = cpUtf8(c, u2(&r));
            uint32_t al = u4(&r);
            if (strcmp(an, "ConstantValue") == 0 && al == 2) f->constIdx = u2(&r);
            else r.p += al;
        }
    }
    c->nMethods = u2(&r);
    c->methods = calloc(c->nMethods + 1, sizeof(Method));
    for (int i = 0; i < c->nMethods && !r.err; i++) {
        Method *m = &c->methods[i];
        m->cls = c;
        m->access = u2(&r);
        m->name = cpUtf8(c, u2(&r));
        m->desc = cpUtf8(c, u2(&r));
        m->vtIndex = -1;
        m->argSlots = arg_slots(m->desc, m->access & ACC_STATIC);
        m->retType = ret_type(m->desc);
        m->retSlots = (m->retType == 'V') ? 0 : (m->retType == 'J' || m->retType == 'D') ? 2 : 1;
        uint16_t na = u2(&r);
        for (int a = 0; a < na && !r.err; a++) {
            const char *an = cpUtf8(c, u2(&r));
            uint32_t al = u4(&r);
            const uint8_t *next = r.p + al;
            if (strcmp(an, "Code") == 0) {
                m->maxStack = u2(&r);
                m->maxLocals = u2(&r);
                m->codeLen = u4(&r);
                if (r.p + m->codeLen > r.end) { r.err = 1; break; }
                m->code = malloc(m->codeLen + 1);
                memcpy(m->code, r.p, m->codeLen);
                r.p += m->codeLen;
                m->nHandlers = u2(&r);
                m->handlers = calloc(m->nHandlers + 1, sizeof(ExHandler));
                for (int h = 0; h < m->nHandlers; h++) {
                    m->handlers[h].startPc = u2(&r);
                    m->handlers[h].endPc = u2(&r);
                    m->handlers[h].handlerPc = u2(&r);
                    m->handlers[h].catchType = u2(&r);
                }
                uint16_t nca = u2(&r);
                for (int ca = 0; ca < nca && !r.err; ca++) {
                    const char *cn = cpUtf8(c, u2(&r));
                    uint32_t cl = u4(&r);
                    const uint8_t *cnext = r.p + cl;
                    if (strcmp(cn, "LineNumberTable") == 0) {
                        uint16_t nl = u2(&r);
                        m->lines = realloc(m->lines, (m->nLines + nl) * 4);
                        for (int l = 0; l < nl; l++) {
                            m->lines[(m->nLines + l) * 2] = u2(&r);
                            m->lines[(m->nLines + l) * 2 + 1] = u2(&r);
                        }
                        m->nLines += nl;
                    }
                    r.p = cnext;
                }
                if (m->maxLocals < m->argSlots) m->maxLocals = m->argSlots;
            }
            r.p = next;
        }
    }
    if (!r.err) {
        uint16_t na = u2(&r);
        for (int a = 0; a < na && !r.err; a++) {
            const char *an = cpUtf8(c, u2(&r));
            uint32_t al = u4(&r);
            if (strcmp(an, "SourceFile") == 0 && al == 2) c->sourceFile = cpUtf8(c, u2(&r));
            else r.p += al;
        }
    }
    if (r.err || !c->name) { free(ifNames); return NULL; }
    /* stash for linking */
    c->super = (Class *)superName;   /* temporarily store name */
    c->interfaces = (Class **)ifNames;
    return c;
}

static int isWeakReferent(Class *c, Field *f) {
    return strcmp(c->name, "java/lang/ref/WeakReference") == 0 && strcmp(f->name, "referent") == 0;
}

static int linkClass(Class *c) {
    const char *superName = (const char *)c->super;
    const char **ifNames = (const char **)c->interfaces;
    c->super = NULL;
    if (superName) {
        Class *s = load_class(superName);
        if (!s) return -1;
        c->super = s;
    }
    c->interfaces = calloc(c->nInterfaces + 1, sizeof(Class *));
    for (int i = 0; i < c->nInterfaces; i++) {
        Class *ic = load_class(ifNames[i]);
        if (!ic) return -1;
        c->interfaces[i] = ic;
    }
    free(ifNames);

    /* field layout */
    Class *s = c->super;
    uint32_t size = s ? s->instanceSize : 0;
    int nRef = s ? s->nRefOffs : 0;
    int nNewRefs = 0, nStatRefs = 0;
    uint32_t ssize = 0;
    for (int i = 0; i < c->nFields; i++) {
        Field *f = &c->fields[i];
        int fsz = (f->type == 'J' || f->type == 'D') ? 8 : 4;
        if (f->access & ACC_STATIC) {
            f->offset = ssize; ssize += fsz;
            if (f->type == 'L') nStatRefs++;
        } else {
            f->offset = size; size += fsz;
            if (f->type == 'L' && !isWeakReferent(c, f)) nNewRefs++;
        }
    }
    c->instanceSize = size;
    c->nRefOffs = nRef + nNewRefs;
    c->refOffs = malloc((c->nRefOffs + 1) * sizeof(uint16_t));
    if (nRef) memcpy(c->refOffs, s->refOffs, nRef * sizeof(uint16_t));
    c->staticSize = ssize;
    c->statics = calloc(ssize + 8, 1);
    c->nStaticRefs = nStatRefs;
    c->staticRefOffs = malloc((nStatRefs + 1) * sizeof(uint16_t));
    int ri = nRef, si = 0;
    for (int i = 0; i < c->nFields; i++) {
        Field *f = &c->fields[i];
        if (f->type != 'L') continue;
        if (f->access & ACC_STATIC) c->staticRefOffs[si++] = f->offset;
        else if (!isWeakReferent(c, f)) c->refOffs[ri++] = f->offset;
    }
    if (size > 65535) vm_fatal("class too big: %s", c->name);

    /* vtable */
    int cap = (s ? s->vtableSize : 0) + c->nMethods;
    c->vtable = malloc((cap + 1) * sizeof(Method *));
    c->vtableSize = 0;
    if (s) { memcpy(c->vtable, s->vtable, s->vtableSize * sizeof(Method *)); c->vtableSize = s->vtableSize; }
    if (!c->isInterface) {
        for (int i = 0; i < c->nMethods; i++) {
            Method *m = &c->methods[i];
            if (m->access & (ACC_STATIC | ACC_PRIVATE)) continue;
            if (m->name[0] == '<') continue;
            int found = -1;
            for (int k = 0; k < c->vtableSize; k++) {
                Method *sm = c->vtable[k];
                if (strcmp(sm->name, m->name) == 0 && strcmp(sm->desc, m->desc) == 0) { found = k; break; }
            }
            if (found >= 0) { c->vtable[found] = m; m->vtIndex = found; }
            else { m->vtIndex = c->vtableSize; c->vtable[c->vtableSize++] = m; }
        }
    }
    link_natives(c);
    /* constant values for static fields */
    for (int i = 0; i < c->nFields; i++) {
        Field *f = &c->fields[i];
        if (!(f->access & ACC_STATIC) || !f->constIdx) continue;
        CPEntry *e = &c->cp[f->constIdx];
        uint8_t *dst = c->statics + f->offset;
        switch (e->tag) {
            case CP_Integer: case CP_Float: memcpy(dst, &e->v.i, 4); break;
            case CP_Long: case CP_Double: memcpy(dst, &e->v.l, 8); break;
            case CP_String: {
                ref_t sr = intern_utf8(cpUtf8(c, e->a));
                memcpy(dst, &sr, 4);
                break;
            }
        }
    }
    c->state = CLS_LINKED;
    return 0;
}

static Class *defineClass(const char *name, int quiet) {
    uint32_t len;
    int fromBoot = 0;
    uint8_t *bytes = readClassBytes(name, &len, &fromBoot);
    if (!bytes) {
        if (!quiet) vm_log("class not found: %s\n", name);
        return NULL;
    }
    Class *c = parseClass(bytes, len);
    free(bytes);
    if (!c) { vm_log("bad class file: %s\n", name); return NULL; }
    if (strcmp(c->name, name) != 0) { vm_log("class name mismatch %s vs %s\n", c->name, name); return NULL; }
    c->fromBoot = fromBoot;
    registerClass(c);
    if (linkClass(c) != 0) {
        c->state = CLS_ERROR;
        return NULL;
    }
    return c;
}

static int loadQuiet;

Class *load_class(const char *name) {
    Class *c = find_loaded(name);
    if (c) return c->state == CLS_ERROR ? NULL : c;
    if (name[0] == '[') return array_class_by_name(name);
    return defineClass(name, loadQuiet);
}

Class *load_class_quiet(const char *name) {
    loadQuiet++;
    Class *c = load_class(name);
    loadQuiet--;
    return c;
}

void classes_init(void) {
    C_Object = load_class("java/lang/Object");
    if (!C_Object) vm_fatal("cannot load java/lang/Object");
    C_intArray = makeArrayClass("[I", 'I', NULL);
    C_byteArray = makeArrayClass("[B", 'B', NULL);
    C_charArray = makeArrayClass("[C", 'C', NULL);
    C_shortArray = makeArrayClass("[S", 'S', NULL);
    C_longArray = makeArrayClass("[J", 'J', NULL);
    C_floatArray = makeArrayClass("[F", 'F', NULL);
    C_doubleArray = makeArrayClass("[D", 'D', NULL);
    C_boolArray = makeArrayClass("[Z", 'Z', NULL);
    C_String = load_class("java/lang/String");
    C_Class = load_class("java/lang/Class");
    C_Throwable = load_class("java/lang/Throwable");
    C_Thread = load_class("java/lang/Thread");
    if (!C_String || !C_Class || !C_Throwable || !C_Thread) vm_fatal("cannot load core classes");
    C_objArray = array_class_of(C_Object);
    C_stringArray = array_class_of(C_String);
    g_strValueOff = find_field_by_name(C_String, "value")->offset;
    g_strOffsetOff = find_field_by_name(C_String, "offset")->offset;
    g_strCountOff = find_field_by_name(C_String, "count")->offset;
    g_thrMessageOff = find_field_by_name(C_Throwable, "detailMessage")->offset;
    g_thrTraceOff = find_field_by_name(C_Throwable, "backtrace")->offset;
}

/* ---------------- init & mirrors ---------------- */
int push_clinit_frame(Thread *t, Method *m, Class *c);

int class_needs_init(Thread *t, Class *c) {
    if (c->state == CLS_INITED) return 0;
    if (c->state == CLS_ERROR) {
        throw_exception(t, "java/lang/NoClassDefFoundError", c->name);
        return -1;
    }
    if (c->state == CLS_INITIALIZING) {
        if (c->initThread == t) return 0;
        return 2; /* wait for other thread */
    }
    if (c->super && c->super->state != CLS_INITED) {
        int r = class_needs_init(t, c->super);
        if (r != 0) return r;
    }
    Method *clinit = NULL;
    for (int i = 0; i < c->nMethods; i++)
        if (strcmp(c->methods[i].name, "<clinit>") == 0) { clinit = &c->methods[i]; break; }
    if (!clinit) { c->state = CLS_INITED; return 0; }
    c->state = CLS_INITIALIZING;
    c->initThread = t;
    if (push_clinit_frame(t, clinit, c) != 0) return -1;
    return 1;
}

ref_t class_mirror(Class *c) {
    if (c->classObj) return c->classObj;
    ref_t o = new_object(C_Class);
    if (!o) return 0;
    Field *f = find_field_by_name(C_Class, "vmClass");
    F_INT(o, f->offset) = c->id;
    c->classObj = o;
    return o;
}

/* Frees all classes and class path entries so the VM can be started again. */
void classes_shutdown(void) {
    for (int i = 1; i < g_numClasses; i++) {
        Class *c = g_classes[i];
        if (!c) continue;
        if (c->isArray) {
            free((char *)c->name);
        } else {
            for (int k = 1; k < c->cpCount; k++)
                if (c->cp[k].tag == CP_Utf8) free((char *)c->cp[k].v.utf8);
            free(c->cp);
            for (int m = 0; m < c->nMethods; m++) {
                free(c->methods[m].code);
                free(c->methods[m].handlers);
                free(c->methods[m].lines);
            }
            free(c->methods);
            free(c->fields);
            free(c->interfaces);
            free(c->refOffs);
            free(c->statics);
            free(c->staticRefOffs);
            free(c->vtable);
        }
        free(c);
        g_classes[i] = NULL;
    }
    g_numClasses = 1;
    memset(classHash, 0, sizeof classHash);
    for (int i = 0; i < nCp; i++) zip_close(cpZips[i]);
    nCp = 0;
    C_Object = C_String = C_Class = C_Throwable = C_Thread = NULL;
    C_intArray = C_byteArray = C_charArray = C_shortArray = C_longArray = NULL;
    C_floatArray = C_doubleArray = C_boolArray = C_objArray = C_stringArray = NULL;
}
