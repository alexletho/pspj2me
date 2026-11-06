/* Bytecode interpreter: non-recursive, so green threads can switch at any point. */
#include "vm.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

uint32_t g_thrMessageOff, g_thrTraceOff;
static ref_t preallocOOM;
static inline uint8_t *heap_base(void) { return g_heap; }
Class *load_class_quiet(const char *name);

/* ---------------- frames ---------------- */
int push_frame(Thread *t, Method *m, int32_t *args) {
    if (t->fp >= t->maxFrames || args + m->maxLocals + m->maxStack + 8 > t->stackEnd) {
        throw_exception(t, "java/lang/StackOverflowError", NULL);
        return -1;
    }
    if (t->fp > 0) t->frames[t->fp - 1].sp = args;
    Frame *f = &t->frames[t->fp++];
    f->m = m;
    f->pc = m->code;
    f->locals = args;
    f->sp = args + m->maxLocals;
    f->lockObj = 0;
    f->flags = 0;
    f->initClass = NULL;
    return 0;
}

int push_clinit_frame(Thread *t, Method *m, Class *c) {
    int32_t *base = t->fp > 0 ? t->frames[t->fp - 1].sp : t->stack;
    int32_t *savedSp = t->fp > 0 ? t->frames[t->fp - 1].sp : NULL;
    if (push_frame(t, m, base) != 0) return -1;
    /* push_frame overwrote caller sp with base, which is identical */
    if (savedSp) t->frames[t->fp - 2].sp = savedSp;
    Frame *f = &t->frames[t->fp - 1];
    f->flags = FR_CLINIT;
    f->initClass = c;
    return 0;
}

void thread_push_frame_for(Thread *t, Method *m, int32_t *args) {
    int32_t *base = t->fp > 0 ? t->frames[t->fp - 1].sp : t->stack;
    memcpy(base, args, m->argSlots * 4);
    if (t->fp > 0) t->frames[t->fp - 1].sp = base + m->argSlots;
    push_frame(t, m, base);
}

/* ---------------- exceptions ---------------- */
void fill_stack_trace(Thread *t, ref_t ex) {
    int n = t->fp;
    if (n > 64) n = 64;
    PUSH_ROOT(t, ex);
    ref_t arr = new_prim_array('I', n * 2);
    POP_ROOTS(t, 1);
    if (!arr) return;
    int32_t *a = A_INT(arr);
    for (int i = 0; i < n; i++) {
        Frame *f = &t->frames[t->fp - 1 - i];
        Method *m = f->m;
        int mi = (int)(m - m->cls->methods);
        a[i * 2] = (int32_t)((m->cls->id << 16) | (mi & 0xffff));
        a[i * 2 + 1] = m->code ? (int32_t)(f->pc - m->code) : -1;
    }
    F_REF(ex, g_thrTraceOff) = arr;
}

ref_t make_exception(Thread *t, const char *clsName, const char *msg) {
    Class *c = load_class(clsName);
    if (!c) c = load_class("java/lang/Error");
    if (c->state != CLS_INITED) c->state = CLS_INITED; /* exception classes have trivial <clinit> */
    ref_t ex = new_object(c);
    if (!ex) {
        if (!preallocOOM) return 0;
        return preallocOOM;
    }
    if (msg) {
        PUSH_ROOT(t, ex);
        ref_t m = new_string_utf8(msg);
        POP_ROOTS(t, 1);
        F_REF(ex, g_thrMessageOff) = m;
    }
    if (t) fill_stack_trace(t, ex);
    return ex;
}

void throw_exception(Thread *t, const char *cls, const char *msg) {
    ref_t ex = make_exception(t, cls, msg);
    t->exception = ex ? ex : preallocOOM;
}

void throw_object(Thread *t, ref_t ex) { t->exception = ex; }

static int lineFor(Method *m, int pc) {
    int line = -1;
    for (int i = 0; i < m->nLines; i++)
        if (m->lines[i * 2] <= pc) line = m->lines[i * 2 + 1];
    return line;
}

void print_stack_trace(Thread *t, ref_t ex) {
    (void)t;
    if (!ex) return;
    Class *c = CLASS_OF(ex);
    ref_t msg = F_REF(ex, g_thrMessageOff);
    char buf[512];
    if (msg) { string_to_utf8(msg, buf, sizeof buf); vm_log("%s: %s\n", c->name, buf); }
    else vm_log("%s\n", c->name);
    ref_t tr = F_REF(ex, g_thrTraceOff);
    if (!tr) return;
    int n = A_LEN(tr) / 2;
    for (int i = 0; i < n; i++) {
        int32_t id = A_INT(tr)[i * 2];
        int pc = A_INT(tr)[i * 2 + 1];
        Class *mc = g_classes[(id >> 16) & 0xffff];
        if (!mc) continue;
        Method *m = &mc->methods[id & 0xffff];
        vm_log("    at %s.%s(%s:%d)\n", mc->name, m->name, mc->sourceFile ? mc->sourceFile : "?", lineFor(m, pc > 0 ? pc - 1 : 0));
    }
}

/* ---------------- resolution ---------------- */
static const char *cpStr(Class *c, int idx) { return c->cp[idx].v.utf8; }

static Class *resolveClass(Thread *t, Class *cur, int idx) {
    CPEntry *e = &cur->cp[idx];
    if (e->resolved) return (Class *)e->v.ptr;
    const char *name = cpStr(cur, e->a);
    Class *c = load_class(name);
    if (!c) {
        if (!t->exception) throw_exception(t, "java/lang/NoClassDefFoundError", name);
        return NULL;
    }
    e->v.ptr = c;
    e->resolved = 1;
    return c;
}

static Field *resolveField(Thread *t, Class *cur, int idx) {
    CPEntry *e = &cur->cp[idx];
    if (e->resolved) return (Field *)e->v.ptr;
    Class *c = resolveClass(t, cur, e->a);
    if (!c) return NULL;
    CPEntry *nt = &cur->cp[e->b];
    const char *name = cpStr(cur, nt->a), *desc = cpStr(cur, nt->b);
    Field *f = find_field(c, name, desc);
    if (!f) {
        char buf[256];
        snprintf(buf, sizeof buf, "%s.%s", c->name, name);
        throw_exception(t, "java/lang/NoSuchFieldError", buf);
        return NULL;
    }
    e->v.ptr = f;
    e->resolved = 1;
    return f;
}

static Method *resolveMethod(Thread *t, Class *cur, int idx) {
    CPEntry *e = &cur->cp[idx];
    if (e->resolved) return (Method *)e->v.ptr;
    Class *c = resolveClass(t, cur, e->a);
    if (!c) return NULL;
    CPEntry *nt = &cur->cp[e->b];
    const char *name = cpStr(cur, nt->a), *desc = cpStr(cur, nt->b);
    Method *m = find_method_iface(c, name, desc);
    if (!m) {
        char buf[300];
        snprintf(buf, sizeof buf, "%s.%s%s", c->name, name, desc);
        vm_log("NoSuchMethodError: %s\n", buf);
        throw_exception(t, "java/lang/NoSuchMethodError", buf);
        return NULL;
    }
    e->v.ptr = m;
    e->resolved = 1;
    return m;
}

static ref_t resolveString(Class *cur, int idx) {
    CPEntry *e = &cur->cp[idx];
    if (e->resolved) return e->v.ref;
    ref_t s = intern_utf8(cpStr(cur, e->a));
    if (!s) return 0;
    e->v.ref = s;
    e->resolved = 1;
    return s;
}

/* interface dispatch cache */
#define ICACHE 4096
typedef struct { Class *cls; Method *im; Method *target; } ICEntry;
static ICEntry icache[ICACHE];

static Method *lookupVirtual(Class *oc, Method *im) {
    unsigned h = ((uintptr_t)oc >> 4 ^ (uintptr_t)im >> 3) & (ICACHE - 1);
    ICEntry *e = &icache[h];
    if (e->cls == oc && e->im == im) return e->target;
    Method *m = NULL;
    for (Class *c = oc; c && !m; c = c->super) {
        for (int i = 0; i < c->nMethods; i++) {
            Method *cm = &c->methods[i];
            if (!(cm->access & ACC_STATIC) && strcmp(cm->name, im->name) == 0 && strcmp(cm->desc, im->desc) == 0) { m = cm; break; }
        }
    }
    e->cls = oc; e->im = im; e->target = m;
    return m;
}

/* ---------------- numeric helpers ---------------- */
static inline int32_t d2i(double d) {
    if (d != d) return 0;
    if (d >= 2147483647.0) return INT32_MAX;
    if (d <= -2147483648.0) return INT32_MIN;
    return (int32_t)d;
}
static inline int64_t d2l(double d) {
    if (d != d) return 0;
    if (d >= 9223372036854775807.0) return INT64_MAX;
    if (d <= -9223372036854775808.0) return INT64_MIN;
    return (int64_t)d;
}
static inline int32_t f2i(float f) {
    if (f != f) return 0;
    if (f >= 2147483647.0f) return INT32_MAX;
    if (f <= -2147483648.0f) return INT32_MIN;
    return (int32_t)f;
}
static inline int64_t f2l(float f) {
    if (f != f) return 0;
    if (f >= 9223372036854775807.0f) return INT64_MAX;
    if (f <= -9223372036854775808.0f) return INT64_MIN;
    return (int64_t)f;
}

static ref_t multiAlloc(Thread *t, Class *c, int dims, int32_t *counts) {
    ref_t a = new_array(c, counts[0]);
    if (!a || dims <= 1) return a;
    PUSH_ROOT(t, a);
    for (int32_t i = 0; i < counts[0]; i++) {
        ref_t sub = multiAlloc(t, c->elemClass ? c->elemClass : prim_array_class(c->name[1]), dims - 1, counts + 1);
        if (!sub) { POP_ROOTS(t, 1); return 0; }
        A_REF(a)[i] = sub;
    }
    POP_ROOTS(t, 1);
    return a;
}

#define U2(p) ((uint16_t)(((p)[0] << 8) | (p)[1]))
#define S2(p) ((int16_t)(((p)[0] << 8) | (p)[1]))
#define S4(p) ((int32_t)(((uint32_t)(p)[0] << 24) | ((p)[1] << 16) | ((p)[2] << 8) | (p)[3]))

#define PUSH_I(v) (*sp++ = (int32_t)(v))
#define POP_I() (*--sp)
#define PUSH_F(v) do { float _f = (v); memcpy(sp, &_f, 4); sp++; } while (0)
#define POP_F() (sp--, slot_get_float(sp))
#define PUSH_L(v) do { slot_put_long(sp, (v)); sp += 2; } while (0)
#define POP_L() (sp -= 2, slot_get_long(sp))
#define PUSH_D(v) do { slot_put_double(sp, (v)); sp += 2; } while (0)
#define POP_D() (sp -= 2, slot_get_double(sp))

#define LOAD_FRAME() do { fr = &t->frames[t->fp - 1]; m = fr->m; pc = fr->pc; sp = fr->sp; lv = fr->locals; cls = m->cls; } while (0)
#define SYNC() (fr->sp = sp)
#define THROW(c, msg) do { fr->pc = opc + 1; fr->sp = sp; throw_exception(t, c, msg); goto exception; } while (0)
#define CHECK_NULL(r) do { if (!(r)) THROW("java/lang/NullPointerException", NULL); } while (0)
#define EXC_HERE() do { fr->pc = opc + 1; fr->sp = sp; goto exception; } while (0)
#define ENSURE_INIT(c) do { if ((c)->state != CLS_INITED) { \
        fr->pc = opc; fr->sp = sp; \
        int _r = class_needs_init(t, (c)); \
        if (_r == 1) { LOAD_FRAME(); goto next; } \
        if (_r == 2) { return; } \
        if (_r < 0) { fr->pc = opc + 1; goto exception; } } } while (0)
#define BRANCH(off) do { int32_t _o = (off); pc = opc + _o; if (_o <= 0 && --budget <= 0) { fr->pc = pc; fr->sp = sp; return; } goto next; } while (0)
#define ARR_CHECK(a, i) do { CHECK_NULL(a); if ((uint32_t)(i) >= (uint32_t)A_LEN(a)) { char _b[32]; snprintf(_b, 32, "%d", (int)(i)); THROW("java/lang/ArrayIndexOutOfBoundsException", _b); } } while (0)

/* Inside the interpreter the heap base lives in a local (register) variable: as a global it would be
   reloaded after every store, because the compiler can't prove the store didn't change it. */
#define g_heap H
void interp_run(Thread *t, int budget) {
    uint8_t *const H = heap_base();
    Frame *fr;
    Method *m;
    uint8_t *pc, *opc;
    int32_t *sp, *lv;
    Class *cls;

#ifdef __GNUC__
    static void *disp[256] = {
        &&L_0x00, &&L_0x01, &&L_0x02, &&L_0x03, &&L_0x04, &&L_0x05, &&L_0x06, &&L_0x07,
        &&L_0x08, &&L_0x09, &&L_0x0a, &&L_0x0b, &&L_0x0c, &&L_0x0d, &&L_0x0e, &&L_0x0f,
        &&L_0x10, &&L_0x11, &&L_0x12, &&L_0x13, &&L_0x14, &&L_0x15, &&L_0x16, &&L_0x17,
        &&L_0x18, &&L_0x19, &&L_0x1a, &&L_0x1b, &&L_0x1c, &&L_0x1d, &&L_0x1e, &&L_0x1f,
        &&L_0x20, &&L_0x21, &&L_0x22, &&L_0x23, &&L_0x24, &&L_0x25, &&L_0x26, &&L_0x27,
        &&L_0x28, &&L_0x29, &&L_0x2a, &&L_0x2b, &&L_0x2c, &&L_0x2d, &&L_0x2e, &&L_0x2f,
        &&L_0x30, &&L_0x31, &&L_0x32, &&L_0x33, &&L_0x34, &&L_0x35, &&L_0x36, &&L_0x37,
        &&L_0x38, &&L_0x39, &&L_0x3a, &&L_0x3b, &&L_0x3c, &&L_0x3d, &&L_0x3e, &&L_0x3f,
        &&L_0x40, &&L_0x41, &&L_0x42, &&L_0x43, &&L_0x44, &&L_0x45, &&L_0x46, &&L_0x47,
        &&L_0x48, &&L_0x49, &&L_0x4a, &&L_0x4b, &&L_0x4c, &&L_0x4d, &&L_0x4e, &&L_0x4f,
        &&L_0x50, &&L_0x51, &&L_0x52, &&L_0x53, &&L_0x54, &&L_0x55, &&L_0x56, &&L_0x57,
        &&L_0x58, &&L_0x59, &&L_0x5a, &&L_0x5b, &&L_0x5c, &&L_0x5d, &&L_0x5e, &&L_0x5f,
        &&L_0x60, &&L_0x61, &&L_0x62, &&L_0x63, &&L_0x64, &&L_0x65, &&L_0x66, &&L_0x67,
        &&L_0x68, &&L_0x69, &&L_0x6a, &&L_0x6b, &&L_0x6c, &&L_0x6d, &&L_0x6e, &&L_0x6f,
        &&L_0x70, &&L_0x71, &&L_0x72, &&L_0x73, &&L_0x74, &&L_0x75, &&L_0x76, &&L_0x77,
        &&L_0x78, &&L_0x79, &&L_0x7a, &&L_0x7b, &&L_0x7c, &&L_0x7d, &&L_0x7e, &&L_0x7f,
        &&L_0x80, &&L_0x81, &&L_0x82, &&L_0x83, &&L_0x84, &&L_0x85, &&L_0x86, &&L_0x87,
        &&L_0x88, &&L_0x89, &&L_0x8a, &&L_0x8b, &&L_0x8c, &&L_0x8d, &&L_0x8e, &&L_0x8f,
        &&L_0x90, &&L_0x91, &&L_0x92, &&L_0x93, &&L_0x94, &&L_0x95, &&L_0x96, &&L_0x97,
        &&L_0x98, &&L_0x99, &&L_0x9a, &&L_0x9b, &&L_0x9c, &&L_0x9d, &&L_0x9e, &&L_0x9f,
        &&L_0xa0, &&L_0xa1, &&L_0xa2, &&L_0xa3, &&L_0xa4, &&L_0xa5, &&L_0xa6, &&L_0xa7,
        &&L_0xa8, &&L_0xa9, &&L_0xaa, &&L_0xab, &&L_0xac, &&L_0xad, &&L_0xae, &&L_0xaf,
        &&L_0xb0, &&L_0xb1, &&L_0xb2, &&L_0xb3, &&L_0xb4, &&L_0xb5, &&L_0xb6, &&L_0xb7,
        &&L_0xb8, &&L_0xb9, &&L_0xba, &&L_0xbb, &&L_0xbc, &&L_0xbd, &&L_0xbe, &&L_0xbf,
        &&L_0xc0, &&L_0xc1, &&L_0xc2, &&L_0xc3, &&L_0xc4, &&L_0xc5, &&L_0xc6, &&L_0xc7,
        &&L_0xc8, &&L_0xc9, &&L_default, &&L_0xcb, &&L_0xcc, &&L_0xcd, &&L_0xce, &&L_0xcf,
        &&L_0xd0, &&L_0xd1, &&L_0xd2, &&L_0xd3, &&L_default, &&L_default, &&L_default, &&L_default,
        &&L_default, &&L_default, &&L_default, &&L_default, &&L_default, &&L_default, &&L_default, &&L_default,
        &&L_default, &&L_default, &&L_default, &&L_default, &&L_default, &&L_default, &&L_default, &&L_default,
        &&L_default, &&L_default, &&L_default, &&L_default, &&L_default, &&L_default, &&L_default, &&L_default,
        &&L_default, &&L_default, &&L_default, &&L_default, &&L_default, &&L_default, &&L_default, &&L_default,
        &&L_default, &&L_default, &&L_default, &&L_default, &&L_default, &&L_default, &&L_default, &&L_default,
    };
#endif
    if (t->fp == 0) return;
    LOAD_FRAME();
    if (t->exception) { goto exception; }

next:
    opc = pc;
#ifdef __GNUC__
    goto *disp[*pc];
#endif
    switch (*pc) {
    case 0x00: L_0x00: pc++; goto next;
    case 0x01: L_0x01: PUSH_I(0); pc++; goto next;
    case 0x02: L_0x02: case 0x03: L_0x03: case 0x04: L_0x04: case 0x05: L_0x05: case 0x06: L_0x06: case 0x07: L_0x07: case 0x08: L_0x08:
        PUSH_I((int)*pc - 3); pc++; goto next;
    case 0x09: L_0x09: case 0x0a: L_0x0a: PUSH_L((int64_t)(*pc - 0x09)); pc++; goto next;
    case 0x0b: L_0x0b: case 0x0c: L_0x0c: case 0x0d: L_0x0d: PUSH_F((float)(*pc - 0x0b)); pc++; goto next;
    case 0x0e: L_0x0e: case 0x0f: L_0x0f: PUSH_D((double)(*pc - 0x0e)); pc++; goto next;
    case 0x10: L_0x10: PUSH_I((int8_t)pc[1]); pc += 2; goto next;
    case 0x11: L_0x11: PUSH_I(S2(pc + 1)); pc += 3; goto next;
    case 0x12: L_0x12: case 0x13: L_0x13: {
        int idx = (*pc == 0x12) ? pc[1] : U2(pc + 1);
        CPEntry *e = &cls->cp[idx];
        if (e->tag == CP_Integer || e->tag == CP_Float) PUSH_I(e->v.i);
        else if (e->tag == CP_String) {
            SYNC();
            ref_t s = resolveString(cls, idx);
            if (!s) THROW("java/lang/OutOfMemoryError", NULL);
            PUSH_I(s);
        } else if (e->tag == CP_Class) {
            SYNC();
            Class *c = resolveClass(t, cls, idx);
            if (!c) EXC_HERE();
            PUSH_I(class_mirror(c));
        } else THROW("java/lang/VerifyError", "bad ldc");
        pc += (*pc == 0x12) ? 2 : 3;
        goto next;
    }
    case 0x14: L_0x14: {
        CPEntry *e = &cls->cp[U2(pc + 1)];
        slot_put_long(sp, e->v.l); sp += 2;
        pc += 3; goto next;
    }
    case 0x15: L_0x15: case 0x17: L_0x17: case 0x19: L_0x19: PUSH_I(lv[pc[1]]); pc += 2; goto next;
    case 0x16: L_0x16: case 0x18: L_0x18: sp[0] = lv[pc[1]]; sp[1] = lv[pc[1] + 1]; sp += 2; pc += 2; goto next;
    case 0x1a: L_0x1a: case 0x1b: L_0x1b: case 0x1c: L_0x1c: case 0x1d: L_0x1d: PUSH_I(lv[*pc - 0x1a]); pc++; goto next;
    case 0x1e: L_0x1e: case 0x1f: L_0x1f: case 0x20: L_0x20: case 0x21: L_0x21: { int i = *pc - 0x1e; sp[0] = lv[i]; sp[1] = lv[i + 1]; sp += 2; pc++; goto next; }
    case 0x22: L_0x22: case 0x23: L_0x23: case 0x24: L_0x24: case 0x25: L_0x25: PUSH_I(lv[*pc - 0x22]); pc++; goto next;
    case 0x26: L_0x26: case 0x27: L_0x27: case 0x28: L_0x28: case 0x29: L_0x29: { int i = *pc - 0x26; sp[0] = lv[i]; sp[1] = lv[i + 1]; sp += 2; pc++; goto next; }
    case 0x2a: L_0x2a: case 0x2b: L_0x2b: case 0x2c: L_0x2c: case 0x2d: L_0x2d: PUSH_I(lv[*pc - 0x2a]); pc++; goto next;

    /* array loads */
    case 0x2e: L_0x2e: case 0x30: L_0x30: case 0x32: L_0x32: {
        int32_t i = POP_I(); ref_t a = (ref_t)POP_I();
        ARR_CHECK(a, i);
        PUSH_I(A_INT(a)[i]); pc++; goto next;
    }
    case 0x2f: L_0x2f: case 0x31: L_0x31: {
        int32_t i = POP_I(); ref_t a = (ref_t)POP_I();
        ARR_CHECK(a, i);
        memcpy(sp, (int64_t *)O_DATA(a) + i, 8); sp += 2; pc++; goto next;
    }
    case 0x33: L_0x33: {
        int32_t i = POP_I(); ref_t a = (ref_t)POP_I();
        ARR_CHECK(a, i);
        PUSH_I(A_BYTE(a)[i]); pc++; goto next;
    }
    case 0x34: L_0x34: {
        int32_t i = POP_I(); ref_t a = (ref_t)POP_I();
        ARR_CHECK(a, i);
        PUSH_I(A_CHAR(a)[i]); pc++; goto next;
    }
    case 0x35: L_0x35: {
        int32_t i = POP_I(); ref_t a = (ref_t)POP_I();
        ARR_CHECK(a, i);
        PUSH_I(A_SHORT(a)[i]); pc++; goto next;
    }
    /* stores */
    case 0x36: L_0x36: case 0x38: L_0x38: case 0x3a: L_0x3a: lv[pc[1]] = POP_I(); pc += 2; goto next;
    case 0x37: L_0x37: case 0x39: L_0x39: sp -= 2; lv[pc[1]] = sp[0]; lv[pc[1] + 1] = sp[1]; pc += 2; goto next;
    case 0x3b: L_0x3b: case 0x3c: L_0x3c: case 0x3d: L_0x3d: case 0x3e: L_0x3e: lv[*pc - 0x3b] = POP_I(); pc++; goto next;
    case 0x3f: L_0x3f: case 0x40: L_0x40: case 0x41: L_0x41: case 0x42: L_0x42: { int i = *pc - 0x3f; sp -= 2; lv[i] = sp[0]; lv[i + 1] = sp[1]; pc++; goto next; }
    case 0x43: L_0x43: case 0x44: L_0x44: case 0x45: L_0x45: case 0x46: L_0x46: lv[*pc - 0x43] = POP_I(); pc++; goto next;
    case 0x47: L_0x47: case 0x48: L_0x48: case 0x49: L_0x49: case 0x4a: L_0x4a: { int i = *pc - 0x47; sp -= 2; lv[i] = sp[0]; lv[i + 1] = sp[1]; pc++; goto next; }
    case 0x4b: L_0x4b: case 0x4c: L_0x4c: case 0x4d: L_0x4d: case 0x4e: L_0x4e: lv[*pc - 0x4b] = POP_I(); pc++; goto next;

    case 0x4f: L_0x4f: case 0x51: L_0x51: {
        int32_t v = POP_I(); int32_t i = POP_I(); ref_t a = (ref_t)POP_I();
        ARR_CHECK(a, i);
        A_INT(a)[i] = v; pc++; goto next;
    }
    case 0x50: L_0x50: case 0x52: L_0x52: {
        sp -= 2; int32_t *v = sp; int32_t i = POP_I(); ref_t a = (ref_t)POP_I();
        ARR_CHECK(a, i);
        memcpy((int64_t *)O_DATA(a) + i, v, 8); pc++; goto next;
    }
    case 0x53: L_0x53: {
        ref_t v = (ref_t)POP_I(); int32_t i = POP_I(); ref_t a = (ref_t)POP_I();
        ARR_CHECK(a, i);
        if (v) {
            Class *ac = CLASS_OF(a);
            if (ac->elemClass && !is_assignable(CLASS_OF(v), ac->elemClass))
                THROW("java/lang/ArrayStoreException", CLASS_OF(v)->name);
        }
        A_REF(a)[i] = v; pc++; goto next;
    }
    case 0x54: L_0x54: {
        int32_t v = POP_I(); int32_t i = POP_I(); ref_t a = (ref_t)POP_I();
        ARR_CHECK(a, i);
        A_BYTE(a)[i] = (int8_t)v; pc++; goto next;
    }
    case 0x55: L_0x55: case 0x56: L_0x56: {
        int32_t v = POP_I(); int32_t i = POP_I(); ref_t a = (ref_t)POP_I();
        ARR_CHECK(a, i);
        A_CHAR(a)[i] = (uint16_t)v; pc++; goto next;
    }

    /* stack ops */
    case 0x57: L_0x57: sp--; pc++; goto next;
    case 0x58: L_0x58: sp -= 2; pc++; goto next;
    case 0x59: L_0x59: sp[0] = sp[-1]; sp++; pc++; goto next;
    case 0x5a: L_0x5a: { int32_t v1 = sp[-1], v2 = sp[-2]; sp[-2] = v1; sp[-1] = v2; sp[0] = v1; sp++; pc++; goto next; }
    case 0x5b: L_0x5b: { int32_t v1 = sp[-1], v2 = sp[-2], v3 = sp[-3]; sp[-3] = v1; sp[-2] = v3; sp[-1] = v2; sp[0] = v1; sp++; pc++; goto next; }
    case 0x5c: L_0x5c: sp[0] = sp[-2]; sp[1] = sp[-1]; sp += 2; pc++; goto next;
    case 0x5d: L_0x5d: { int32_t v1 = sp[-1], v2 = sp[-2], v3 = sp[-3];
        sp[-3] = v2; sp[-2] = v1; sp[-1] = v3; sp[0] = v2; sp[1] = v1; sp += 2; pc++; goto next; }
    case 0x5e: L_0x5e: { int32_t v1 = sp[-1], v2 = sp[-2], v3 = sp[-3], v4 = sp[-4];
        sp[-4] = v2; sp[-3] = v1; sp[-2] = v4; sp[-1] = v3; sp[0] = v2; sp[1] = v1; sp += 2; pc++; goto next; }
    case 0x5f: L_0x5f: { int32_t v = sp[-1]; sp[-1] = sp[-2]; sp[-2] = v; pc++; goto next; }

    /* arithmetic */
    case 0x60: L_0x60: sp[-2] = (int32_t)((uint32_t)sp[-2] + (uint32_t)sp[-1]); sp--; pc++; goto next;
    case 0x61: L_0x61: { int64_t b = POP_L(), a = POP_L(); PUSH_L((int64_t)((uint64_t)a + (uint64_t)b)); pc++; goto next; }
    case 0x62: L_0x62: { float b = POP_F(), a = POP_F(); PUSH_F(a + b); pc++; goto next; }
    case 0x63: L_0x63: { double b = POP_D(), a = POP_D(); PUSH_D(a + b); pc++; goto next; }
    case 0x64: L_0x64: sp[-2] = (int32_t)((uint32_t)sp[-2] - (uint32_t)sp[-1]); sp--; pc++; goto next;
    case 0x65: L_0x65: { int64_t b = POP_L(), a = POP_L(); PUSH_L((int64_t)((uint64_t)a - (uint64_t)b)); pc++; goto next; }
    case 0x66: L_0x66: { float b = POP_F(), a = POP_F(); PUSH_F(a - b); pc++; goto next; }
    case 0x67: L_0x67: { double b = POP_D(), a = POP_D(); PUSH_D(a - b); pc++; goto next; }
    case 0x68: L_0x68: sp[-2] = (int32_t)((uint32_t)sp[-2] * (uint32_t)sp[-1]); sp--; pc++; goto next;
    case 0x69: L_0x69: { int64_t b = POP_L(), a = POP_L(); PUSH_L((int64_t)((uint64_t)a * (uint64_t)b)); pc++; goto next; }
    case 0x6a: L_0x6a: { float b = POP_F(), a = POP_F(); PUSH_F(a * b); pc++; goto next; }
    case 0x6b: L_0x6b: { double b = POP_D(), a = POP_D(); PUSH_D(a * b); pc++; goto next; }
    case 0x6c: L_0x6c: {
        int32_t b = sp[-1], a = sp[-2];
        if (b == 0) THROW("java/lang/ArithmeticException", "/ by zero");
        sp[-2] = (b == -1) ? (int32_t)(0u - (uint32_t)a) : a / b; sp--; pc++; goto next;
    }
    case 0x6d: L_0x6d: {
        int64_t b = slot_get_long(sp - 2), a = slot_get_long(sp - 4);
        if (b == 0) THROW("java/lang/ArithmeticException", "/ by zero");
        sp -= 4; PUSH_L(b == -1 ? (int64_t)(0ull - (uint64_t)a) : a / b); pc++; goto next;
    }
    case 0x6e: L_0x6e: { float b = POP_F(), a = POP_F(); PUSH_F(a / b); pc++; goto next; }
    case 0x6f: L_0x6f: { double b = POP_D(), a = POP_D(); PUSH_D(a / b); pc++; goto next; }
    case 0x70: L_0x70: {
        int32_t b = sp[-1], a = sp[-2];
        if (b == 0) THROW("java/lang/ArithmeticException", "/ by zero");
        sp[-2] = (b == -1) ? 0 : a % b; sp--; pc++; goto next;
    }
    case 0x71: L_0x71: {
        int64_t b = slot_get_long(sp - 2), a = slot_get_long(sp - 4);
        if (b == 0) THROW("java/lang/ArithmeticException", "/ by zero");
        sp -= 4; PUSH_L(b == -1 ? 0 : a % b); pc++; goto next;
    }
    case 0x72: L_0x72: { float b = POP_F(), a = POP_F(); PUSH_F(fmodf(a, b)); pc++; goto next; }
    case 0x73: L_0x73: { double b = POP_D(), a = POP_D(); PUSH_D(fmod(a, b)); pc++; goto next; }
    case 0x74: L_0x74: sp[-1] = (int32_t)(0u - (uint32_t)sp[-1]); pc++; goto next;
    case 0x75: L_0x75: { int64_t a = POP_L(); PUSH_L((int64_t)(0ull - (uint64_t)a)); pc++; goto next; }
    case 0x76: L_0x76: { float a = POP_F(); PUSH_F(-a); pc++; goto next; }
    case 0x77: L_0x77: { double a = POP_D(); PUSH_D(-a); pc++; goto next; }
    case 0x78: L_0x78: sp[-2] = (int32_t)((uint32_t)sp[-2] << (sp[-1] & 31)); sp--; pc++; goto next;
    case 0x79: L_0x79: { int32_t s = POP_I(); int64_t a = POP_L(); PUSH_L((int64_t)((uint64_t)a << (s & 63))); pc++; goto next; }
    case 0x7a: L_0x7a: sp[-2] = sp[-2] >> (sp[-1] & 31); sp--; pc++; goto next;
    case 0x7b: L_0x7b: { int32_t s = POP_I(); int64_t a = POP_L(); PUSH_L(a >> (s & 63)); pc++; goto next; }
    case 0x7c: L_0x7c: sp[-2] = (int32_t)((uint32_t)sp[-2] >> (sp[-1] & 31)); sp--; pc++; goto next;
    case 0x7d: L_0x7d: { int32_t s = POP_I(); int64_t a = POP_L(); PUSH_L((int64_t)((uint64_t)a >> (s & 63))); pc++; goto next; }
    case 0x7e: L_0x7e: sp[-2] &= sp[-1]; sp--; pc++; goto next;
    case 0x7f: L_0x7f: { int64_t b = POP_L(), a = POP_L(); PUSH_L(a & b); pc++; goto next; }
    case 0x80: L_0x80: sp[-2] |= sp[-1]; sp--; pc++; goto next;
    case 0x81: L_0x81: { int64_t b = POP_L(), a = POP_L(); PUSH_L(a | b); pc++; goto next; }
    case 0x82: L_0x82: sp[-2] ^= sp[-1]; sp--; pc++; goto next;
    case 0x83: L_0x83: { int64_t b = POP_L(), a = POP_L(); PUSH_L(a ^ b); pc++; goto next; }
    case 0x84: L_0x84: lv[pc[1]] = (int32_t)((uint32_t)lv[pc[1]] + (uint32_t)(int8_t)pc[2]); pc += 3; goto next;

    /* conversions */
    case 0x85: L_0x85: { int32_t a = POP_I(); PUSH_L((int64_t)a); pc++; goto next; }
    case 0x86: L_0x86: { int32_t a = POP_I(); PUSH_F((float)a); pc++; goto next; }
    case 0x87: L_0x87: { int32_t a = POP_I(); PUSH_D((double)a); pc++; goto next; }
    case 0x88: L_0x88: { int64_t a = POP_L(); PUSH_I((int32_t)a); pc++; goto next; }
    case 0x89: L_0x89: { int64_t a = POP_L(); PUSH_F((float)a); pc++; goto next; }
    case 0x8a: L_0x8a: { int64_t a = POP_L(); PUSH_D((double)a); pc++; goto next; }
    case 0x8b: L_0x8b: { float a = POP_F(); PUSH_I(f2i(a)); pc++; goto next; }
    case 0x8c: L_0x8c: { float a = POP_F(); PUSH_L(f2l(a)); pc++; goto next; }
    case 0x8d: L_0x8d: { float a = POP_F(); PUSH_D((double)a); pc++; goto next; }
    case 0x8e: L_0x8e: { double a = POP_D(); PUSH_I(d2i(a)); pc++; goto next; }
    case 0x8f: L_0x8f: { double a = POP_D(); PUSH_L(d2l(a)); pc++; goto next; }
    case 0x90: L_0x90: { double a = POP_D(); PUSH_F((float)a); pc++; goto next; }
    case 0x91: L_0x91: sp[-1] = (int8_t)sp[-1]; pc++; goto next;
    case 0x92: L_0x92: sp[-1] = (uint16_t)sp[-1]; pc++; goto next;
    case 0x93: L_0x93: sp[-1] = (int16_t)sp[-1]; pc++; goto next;

    /* comparisons */
    case 0x94: L_0x94: { int64_t b = POP_L(), a = POP_L(); PUSH_I(a > b ? 1 : a == b ? 0 : -1); pc++; goto next; }
    case 0x95: L_0x95: case 0x96: L_0x96: {
        float b = POP_F(), a = POP_F();
        PUSH_I(a > b ? 1 : a == b ? 0 : a < b ? -1 : (*pc == 0x95 ? -1 : 1)); pc++; goto next;
    }
    case 0x97: L_0x97: case 0x98: L_0x98: {
        double b = POP_D(), a = POP_D();
        PUSH_I(a > b ? 1 : a == b ? 0 : a < b ? -1 : (*pc == 0x97 ? -1 : 1)); pc++; goto next;
    }
    case 0x99: L_0x99: { int32_t v = POP_I(); if (v == 0) BRANCH(S2(pc + 1)); pc += 3; goto next; }
    case 0x9a: L_0x9a: { int32_t v = POP_I(); if (v != 0) BRANCH(S2(pc + 1)); pc += 3; goto next; }
    case 0x9b: L_0x9b: { int32_t v = POP_I(); if (v < 0) BRANCH(S2(pc + 1)); pc += 3; goto next; }
    case 0x9c: L_0x9c: { int32_t v = POP_I(); if (v >= 0) BRANCH(S2(pc + 1)); pc += 3; goto next; }
    case 0x9d: L_0x9d: { int32_t v = POP_I(); if (v > 0) BRANCH(S2(pc + 1)); pc += 3; goto next; }
    case 0x9e: L_0x9e: { int32_t v = POP_I(); if (v <= 0) BRANCH(S2(pc + 1)); pc += 3; goto next; }
    case 0x9f: L_0x9f: case 0xa5: L_0xa5: { int32_t b = POP_I(), a = POP_I(); if (a == b) BRANCH(S2(pc + 1)); pc += 3; goto next; }
    case 0xa0: L_0xa0: case 0xa6: L_0xa6: { int32_t b = POP_I(), a = POP_I(); if (a != b) BRANCH(S2(pc + 1)); pc += 3; goto next; }
    case 0xa1: L_0xa1: { int32_t b = POP_I(), a = POP_I(); if (a < b) BRANCH(S2(pc + 1)); pc += 3; goto next; }
    case 0xa2: L_0xa2: { int32_t b = POP_I(), a = POP_I(); if (a >= b) BRANCH(S2(pc + 1)); pc += 3; goto next; }
    case 0xa3: L_0xa3: { int32_t b = POP_I(), a = POP_I(); if (a > b) BRANCH(S2(pc + 1)); pc += 3; goto next; }
    case 0xa4: L_0xa4: { int32_t b = POP_I(), a = POP_I(); if (a <= b) BRANCH(S2(pc + 1)); pc += 3; goto next; }
    case 0xa7: L_0xa7: BRANCH(S2(pc + 1));
    case 0xa8: L_0xa8: PUSH_I((int32_t)(pc + 3 - m->code)); BRANCH(S2(pc + 1));
    case 0xa9: L_0xa9: pc = m->code + lv[pc[1]]; goto next;
    case 0xaa: L_0xaa: {
        uint32_t pos = (uint32_t)(pc - m->code) + 1;
        pos = (pos + 3) & ~3u;
        uint8_t *p = m->code + pos;
        int32_t def = S4(p), lo = S4(p + 4), hi = S4(p + 8);
        int32_t key = POP_I();
        if (key < lo || key > hi) BRANCH(def);
        BRANCH(S4(p + 12 + (key - lo) * 4));
    }
    case 0xab: L_0xab: {
        uint32_t pos = (uint32_t)(pc - m->code) + 1;
        pos = (pos + 3) & ~3u;
        uint8_t *p = m->code + pos;
        int32_t def = S4(p), n = S4(p + 4);
        int32_t key = POP_I();
        int lo = 0, hi = n - 1;
        while (lo <= hi) {
            int mid = (lo + hi) >> 1;
            int32_t k = S4(p + 8 + mid * 8);
            if (k == key) BRANCH(S4(p + 12 + mid * 8));
            if (k < key) lo = mid + 1; else hi = mid - 1;
        }
        BRANCH(def);
    }

    /* returns */
    case 0xac: L_0xac: case 0xad: L_0xad: case 0xae: L_0xae: case 0xaf: L_0xaf: case 0xb0: L_0xb0: case 0xb1: L_0xb1: {
        int n = m->retSlots;
        int32_t r0 = n ? sp[-n] : 0, r1 = n == 2 ? sp[-1] : 0;
        if (fr->lockObj) monitor_exit(t, fr->lockObj);
        int flags = fr->flags;
        if (flags & FR_CLINIT) { fr->initClass->state = CLS_INITED; fr->initClass->initThread = NULL; }
        int32_t *dst = fr->locals;
        t->fp--;
        if (t->fp == 0) { thread_terminate(t); return; }
        LOAD_FRAME();
        if (!(flags & (FR_CLINIT | FR_DROP_RESULT))) {
            sp = dst;
            if (n) { sp[0] = r0; if (n == 2) sp[1] = r1; sp += n; }
        }
        if (--budget <= 0) { fr->pc = pc; fr->sp = sp; return; }
        goto next;
    }

    /* fields */
    case 0xb2: L_0xb2: case 0xb3: L_0xb3: {
        SYNC();
        Field *f = resolveField(t, cls, U2(pc + 1));
        if (!f) EXC_HERE();
        ENSURE_INIT(f->cls);
        uint8_t *p = f->cls->statics + f->offset;
        if (f->type != 'J' && f->type != 'D') *opc = (*pc == 0xb2) ? 0xd1 : 0xd2;
        if (*pc == 0xb2 || *pc == 0xd1) {
            if (f->type == 'J' || f->type == 'D') { memcpy(sp, p, 8); sp += 2; }
            else { memcpy(sp, p, 4); sp++; }
        } else {
            if (f->type == 'J' || f->type == 'D') { sp -= 2; memcpy(p, sp, 8); }
            else { sp--; memcpy(p, sp, 4); }
        }
        pc += 3; goto next;
    }
    case 0xb4: L_0xb4: {
        CPEntry *e = &cls->cp[U2(pc + 1)];
        Field *f;
        if (e->resolved) f = (Field *)e->v.ptr;
        else { SYNC(); f = resolveField(t, cls, U2(pc + 1)); if (!f) EXC_HERE(); }
        *opc = (f->type == 'J' || f->type == 'D') ? 0xcc : 0xcb;
        ref_t o = (ref_t)sp[-1];
        CHECK_NULL(o);
        if (f->type == 'J' || f->type == 'D') { memcpy(sp - 1, O_FIELD(o, f->offset), 8); sp++; }
        else sp[-1] = F_INT(o, f->offset);
        pc += 3; goto next;
    }
    case 0xb5: L_0xb5: {
        CPEntry *e = &cls->cp[U2(pc + 1)];
        Field *f;
        if (e->resolved) f = (Field *)e->v.ptr;
        else { SYNC(); f = resolveField(t, cls, U2(pc + 1)); if (!f) EXC_HERE(); }
        *opc = (f->type == 'J' || f->type == 'D') ? 0xce : 0xcd;
        if (f->type == 'J' || f->type == 'D') {
            ref_t o = (ref_t)sp[-3];
            CHECK_NULL(o);
            memcpy(O_FIELD(o, f->offset), sp - 2, 8); sp -= 3;
        } else {
            ref_t o = (ref_t)sp[-2];
            CHECK_NULL(o);
            F_INT(o, f->offset) = sp[-1]; sp -= 2;
        }
        pc += 3; goto next;
    }

    /* invokes */
    case 0xb6: L_0xb6: case 0xb7: L_0xb7: case 0xb8: L_0xb8: case 0xb9: L_0xb9: {
        int op = *pc;
        if (op == 0xcf) op = 0xb6; else if (op == 0xd0) op = 0xb8; else if (op == 0xd3) op = 0xb7;
        int idx = U2(pc + 1);
        CPEntry *e = &cls->cp[idx];
        Method *rm;
        if (e->resolved) rm = (Method *)e->v.ptr;
        else { SYNC(); rm = resolveMethod(t, cls, idx); if (!rm) EXC_HERE(); }
        Method *target = rm;
        int32_t *args = sp - rm->argSlots;
        if (op == 0xb8) {
            if (!(rm->access & ACC_STATIC)) THROW("java/lang/IncompatibleClassChangeError", rm->name);
            ENSURE_INIT(rm->cls);
        } else {
            ref_t obj = (ref_t)args[0];
            CHECK_NULL(obj);
            if (op == 0xb6) {
                Class *oc = CLASS_OF(obj);
                if (rm->vtIndex >= 0 && !rm->cls->isInterface && rm->vtIndex < oc->vtableSize) target = oc->vtable[rm->vtIndex];
                else if (!(rm->access & ACC_PRIVATE) || rm->cls->isInterface) target = lookupVirtual(oc, rm);
            } else if (op == 0xb9) {
                target = lookupVirtual(CLASS_OF(obj), rm);
            } else { /* invokespecial */
                if (rm->name[0] != '<' && !(rm->access & ACC_PRIVATE) && rm->cls != cls &&
                    (cls->access & ACC_SUPER) && cls->super && is_subclass(cls->super, rm->cls)) {
                    Method *sm = find_method(cls->super, rm->name, rm->desc);
                    if (sm) target = sm;
                }
            }
            if (!target) THROW("java/lang/AbstractMethodError", rm->name);
        }
        int len = (op == 0xb9) ? 5 : 3;
        if (!(target->access & ACC_SYNCHRONIZED) && !target->native && target->code) {
            if (op == 0xb6 && rm->vtIndex >= 0 && !rm->cls->isInterface) *opc = 0xcf;
            else if (op == 0xb8 && rm->cls->state == CLS_INITED) *opc = 0xd0;
            else if (op == 0xb7 && target == rm) *opc = 0xd3;
        }
        if (target->access & ACC_SYNCHRONIZED) {
            ref_t lockObj = (target->access & ACC_STATIC) ? class_mirror(target->cls) : (ref_t)args[0];
            if (!monitor_enter(t, lockObj)) { fr->pc = opc; fr->sp = sp; return; }
            if (target->native) {
                /* synchronized natives: hold the monitor only for the call */
                fr->pc = opc + len; fr->sp = sp;
                int r = target->native(t, args);
                monitor_exit(t, lockObj);
                if (r == NAT_OK) { sp = args + target->retSlots; pc = opc + len; goto next; }
                if (r == NAT_EXC) goto exception;
                if (r == NAT_YIELD) { fr->sp = args + target->retSlots; return; }
                fr->pc = opc; LOAD_FRAME(); goto next;
            }
            fr->pc = opc + len;
            if (push_frame(t, target, args) != 0) { monitor_exit(t, lockObj); goto exception; }
            t->frames[t->fp - 1].lockObj = lockObj;
            LOAD_FRAME();
            if (--budget <= 0) { fr->pc = pc; fr->sp = sp; return; }
            goto next;
        }
        if (target->native) {
            int fpBefore = t->fp;
            fr->pc = opc + len; fr->sp = sp;
            int r = target->native(t, args);
            switch (r) {
                case NAT_OK:
                    if (t->fp != fpBefore) { LOAD_FRAME(); goto next; } /* native pushed a call */
                    sp = args + target->retSlots; pc = opc + len;
                    if (t->state != TH_RUNNABLE || g_vmExit) { fr->pc = pc; fr->sp = sp; return; }
                    goto next;
                case NAT_EXC: goto exception;
                case NAT_YIELD:
                    t->frames[fpBefore - 1].sp = args + target->retSlots;
                    t->frames[fpBefore - 1].pc = opc + len;
                    return;
                case NAT_RETRY:
                default:
                    t->frames[fpBefore - 1].pc = opc;
                    if (t->fp == fpBefore) return; /* wait and retry later */
                    LOAD_FRAME(); goto next;
            }
        }
        if (!target->code) THROW("java/lang/AbstractMethodError", target->name);
        fr->pc = opc + len;
        if (push_frame(t, target, args) != 0) goto exception;
        LOAD_FRAME();
        if (--budget <= 0) { fr->pc = pc; fr->sp = sp; return; }
        goto next;
    }

    /* ---- quickened instructions (rewritten in place after first resolution) ---- */
    case 0xcb: L_0xcb: {
        Field *f = (Field *)cls->cp[U2(pc + 1)].v.ptr;
        ref_t o = (ref_t)sp[-1];
        CHECK_NULL(o);
        sp[-1] = F_INT(o, f->offset);
        pc += 3; goto next;
    }
    case 0xcc: L_0xcc: {
        Field *f = (Field *)cls->cp[U2(pc + 1)].v.ptr;
        ref_t o = (ref_t)sp[-1];
        CHECK_NULL(o);
        memcpy(sp - 1, O_FIELD(o, f->offset), 8); sp++;
        pc += 3; goto next;
    }
    case 0xcd: L_0xcd: {
        Field *f = (Field *)cls->cp[U2(pc + 1)].v.ptr;
        ref_t o = (ref_t)sp[-2];
        CHECK_NULL(o);
        F_INT(o, f->offset) = sp[-1]; sp -= 2;
        pc += 3; goto next;
    }
    case 0xce: L_0xce: {
        Field *f = (Field *)cls->cp[U2(pc + 1)].v.ptr;
        ref_t o = (ref_t)sp[-3];
        CHECK_NULL(o);
        memcpy(O_FIELD(o, f->offset), sp - 2, 8); sp -= 3;
        pc += 3; goto next;
    }
    case 0xd1: L_0xd1: {
        Field *f = (Field *)cls->cp[U2(pc + 1)].v.ptr;
        memcpy(sp, f->cls->statics + f->offset, 4); sp++;
        pc += 3; goto next;
    }
    case 0xd2: L_0xd2: {
        Field *f = (Field *)cls->cp[U2(pc + 1)].v.ptr;
        sp--; memcpy(f->cls->statics + f->offset, sp, 4);
        pc += 3; goto next;
    }
#define FAST_CALL(TG, ARGS) do { Method *_m = (TG); int32_t *_a = (ARGS); \
        fr->pc = opc + 3; \
        if (t->fp >= t->maxFrames || _a + _m->maxLocals + _m->maxStack + 8 > t->stackEnd) { \
            fr->sp = sp; throw_exception(t, "java/lang/StackOverflowError", NULL); goto exception; } \
        fr->sp = _a; \
        fr = &t->frames[t->fp++]; \
        fr->m = _m; fr->locals = _a; fr->lockObj = 0; fr->flags = 0; fr->initClass = NULL; \
        m = _m; pc = _m->code; lv = _a; sp = _a + _m->maxLocals; cls = _m->cls; \
        fr->pc = pc; fr->sp = sp; \
        if (--budget <= 0) return; \
        goto next; } while (0)
    case 0xcf: L_0xcf: {
        Method *rm = (Method *)cls->cp[U2(pc + 1)].v.ptr;
        int32_t *args = sp - rm->argSlots;
        ref_t obj = (ref_t)args[0];
        CHECK_NULL(obj);
        Method *target = CLASS_OF(obj)->vtable[rm->vtIndex];
        if ((target->access & ACC_SYNCHRONIZED) || target->native || !target->code) goto L_0xb6;
        FAST_CALL(target, args);
    }
    case 0xd0: L_0xd0: {
        Method *target = (Method *)cls->cp[U2(pc + 1)].v.ptr;
        FAST_CALL(target, sp - target->argSlots);
    }
    case 0xd3: L_0xd3: {
        Method *target = (Method *)cls->cp[U2(pc + 1)].v.ptr;
        int32_t *args = sp - target->argSlots;
        CHECK_NULL((ref_t)args[0]);
        FAST_CALL(target, args);
    }
    case 0xba: L_0xba: THROW("java/lang/VerifyError", "invokedynamic not supported");

    /* objects */
    case 0xbb: L_0xbb: {
        SYNC();
        Class *c = resolveClass(t, cls, U2(pc + 1));
        if (!c) EXC_HERE();
        if (c->access & (ACC_INTERFACE | ACC_ABSTRACT)) THROW("java/lang/InstantiationError", c->name);
        ENSURE_INIT(c);
        ref_t o = new_object(c);
        if (!o) THROW("java/lang/OutOfMemoryError", NULL);
        PUSH_I(o); pc += 3; goto next;
    }
    case 0xbc: L_0xbc: {
        int32_t n = sp[-1];
        if (n < 0) THROW("java/lang/NegativeArraySizeException", NULL);
        static const char types[] = "????ZCFDBSIJ";
        SYNC();
        ref_t a = new_prim_array(types[pc[1] & 15], n);
        if (!a) THROW("java/lang/OutOfMemoryError", NULL);
        sp[-1] = a; pc += 2; goto next;
    }
    case 0xbd: L_0xbd: {
        int32_t n = sp[-1];
        SYNC();
        Class *c = resolveClass(t, cls, U2(pc + 1));
        if (!c) EXC_HERE();
        if (n < 0) THROW("java/lang/NegativeArraySizeException", NULL);
        ref_t a = new_obj_array(c, n);
        if (!a) THROW("java/lang/OutOfMemoryError", NULL);
        sp[-1] = a; pc += 3; goto next;
    }
    case 0xbe: L_0xbe: {
        ref_t a = (ref_t)sp[-1];
        CHECK_NULL(a);
        sp[-1] = A_LEN(a); pc++; goto next;
    }
    case 0xbf: L_0xbf: {
        ref_t ex = (ref_t)sp[-1];
        CHECK_NULL(ex);
        t->exception = ex;
        EXC_HERE();
    }
    case 0xc0: L_0xc0: case 0xc1: L_0xc1: {
        ref_t o = (ref_t)sp[-1];
        Class *c;
        CPEntry *e = &cls->cp[U2(pc + 1)];
        if (e->resolved) c = (Class *)e->v.ptr;
        else { SYNC(); c = resolveClass(t, cls, U2(pc + 1)); if (!c) EXC_HERE(); }
        if (*pc == 0xc0) {
            if (o && !instance_of(o, c)) {
                char buf[300];
                snprintf(buf, sizeof buf, "%s cannot be cast to %s", CLASS_OF(o)->name, c->name);
                THROW("java/lang/ClassCastException", buf);
            }
        } else sp[-1] = o ? instance_of(o, c) : 0;
        pc += 3; goto next;
    }
    case 0xc2: L_0xc2: {
        ref_t o = (ref_t)sp[-1];
        CHECK_NULL(o);
        if (!monitor_enter(t, o)) { fr->pc = opc; fr->sp = sp; return; }
        sp--; pc++; goto next;
    }
    case 0xc3: L_0xc3: {
        ref_t o = (ref_t)sp[-1];
        CHECK_NULL(o);
        sp--;
        if (!monitor_exit(t, o)) THROW("java/lang/IllegalMonitorStateException", NULL);
        pc++; goto next;
    }
    case 0xc4: L_0xc4: {
        int op = pc[1];
        int idx = U2(pc + 2);
        switch (op) {
            case 0x15: case 0x17: case 0x19: PUSH_I(lv[idx]); break;
            case 0x16: case 0x18: sp[0] = lv[idx]; sp[1] = lv[idx + 1]; sp += 2; break;
            case 0x36: case 0x38: case 0x3a: lv[idx] = POP_I(); break;
            case 0x37: case 0x39: sp -= 2; lv[idx] = sp[0]; lv[idx + 1] = sp[1]; break;
            case 0xa9: pc = m->code + lv[idx]; goto next;
            case 0x84: lv[idx] = (int32_t)((uint32_t)lv[idx] + (uint32_t)(int32_t)S2(pc + 4)); pc += 6; goto next;
            default: THROW("java/lang/VerifyError", "bad wide");
        }
        pc += 4; goto next;
    }
    case 0xc5: L_0xc5: {
        int dims = pc[3];
        SYNC();
        Class *c = resolveClass(t, cls, U2(pc + 1));
        if (!c) EXC_HERE();
        int32_t counts[256];
        for (int i = 0; i < dims; i++) {
            counts[i] = sp[-dims + i];
            if (counts[i] < 0) THROW("java/lang/NegativeArraySizeException", NULL);
        }
        ref_t a = multiAlloc(t, c, dims, counts);
        if (!a) THROW("java/lang/OutOfMemoryError", NULL);
        sp -= dims;
        PUSH_I(a);
        pc += 4; goto next;
    }
    case 0xc6: L_0xc6: { ref_t v = (ref_t)POP_I(); if (!v) BRANCH(S2(pc + 1)); pc += 3; goto next; }
    case 0xc7: L_0xc7: { ref_t v = (ref_t)POP_I(); if (v) BRANCH(S2(pc + 1)); pc += 3; goto next; }
    case 0xc8: L_0xc8: BRANCH(S4(pc + 1));
    case 0xc9: L_0xc9: PUSH_I((int32_t)(pc + 5 - m->code)); BRANCH(S4(pc + 1));
    default: L_default: {
        char buf[64];
        snprintf(buf, sizeof buf, "bad opcode 0x%02x in %s.%s", *pc, cls->name, m->name);
        THROW("java/lang/VerifyError", buf);
    }
    }

exception: {
        /* top frame's pc-1 lies inside the faulting instruction */
        ref_t ex = t->exception;
        for (;;) {
            fr = &t->frames[t->fp - 1];
            m = fr->m;
            uint32_t off = (uint32_t)(fr->pc - m->code) - 1;
            int handled = 0;
            for (int i = 0; i < m->nHandlers; i++) {
                ExHandler *h = &m->handlers[i];
                if (off < h->startPc || off >= h->endPc) continue;
                if (h->catchType) {
                    ref_t pending = t->exception;
                    t->exception = 0;
                    Class *cc = resolveClass(t, m->cls, h->catchType);
                    t->exception = pending;
                    if (!cc || !instance_of(ex, cc)) continue;
                }
                fr->pc = m->code + h->handlerPc;
                fr->sp = fr->locals + m->maxLocals;
                *fr->sp++ = (int32_t)ex;
                t->exception = 0;
                handled = 1;
                break;
            }
            if (handled) break;
            if (fr->lockObj) monitor_exit(t, fr->lockObj);
            int wasClinit = fr->flags & FR_CLINIT;
            if (wasClinit) {
                Class *ic = fr->initClass;
                ic->state = CLS_ERROR;
                ic->initThread = NULL;
                vm_log("exception in static initializer of %s\n", ic->name);
                print_stack_trace(t, ex);
                Class *errC = load_class("java/lang/Error");
                if (!instance_of(ex, errC)) {
                    PUSH_ROOT(t, ex);
                    ref_t wrapped = make_exception(t, "java/lang/ExceptionInInitializerError", ic->name);
                    POP_ROOTS(t, 1);
                    if (wrapped) { ex = wrapped; t->exception = ex; }
                }
            }
            t->fp--;
            if (t->fp == 0) {
                ref_t name = 0; (void)name;
                vm_log("Uncaught exception in thread %d:\n", t->id);
                print_stack_trace(t, ex);
                t->exception = 0;
                thread_terminate(t);
                return;
            }
            if (wasClinit) t->frames[t->fp - 1].pc++;
        }
        LOAD_FRAME();
        goto next;
    }
}

#undef g_heap
void interp_set_oom(ref_t r) { preallocOOM = r; gc_add_root(&preallocOOM); }

void interp_reset(void) { preallocOOM = 0; memset(icache, 0, sizeof icache); }
