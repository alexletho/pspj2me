#ifndef VM_H
#define VM_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef int32_t jint;
typedef int64_t jlong;
typedef uint32_t ref_t;   /* object reference = byte offset into heap; 0 == null */
typedef float jfloat;
typedef double jdouble;

struct Class;
struct Method;
struct Thread;

/* ---------- heap / object layout ----------
 * chunk: [u32 sizeFlags][u32 classId][u32 lock][u32 len/aux][data...]
 * ref points to chunk start; data begins at +16; chunks are 8-byte aligned.
 */
extern uint8_t *g_heap;
#define HDR 16
#define O_SIZE(r)   (*(uint32_t *)(g_heap + (r)))
#define O_CLS(r)    (*(uint32_t *)(g_heap + (r) + 4))
#define O_LOCK(r)   (*(uint32_t *)(g_heap + (r) + 8))
#define A_LEN(r)    (*(int32_t *)(g_heap + (r) + 12))
#define O_DATA(r)   ((void *)(g_heap + (r) + HDR))
#define O_FIELD(r, off) ((void *)(g_heap + (r) + HDR + (off)))
#define F_INT(r, off)  (*(int32_t *)O_FIELD(r, off))
#define F_REF(r, off)  (*(ref_t *)O_FIELD(r, off))
#define A_INT(r)  ((int32_t *)O_DATA(r))
#define A_BYTE(r) ((int8_t *)O_DATA(r))
#define A_CHAR(r) ((uint16_t *)O_DATA(r))
#define A_SHORT(r) ((int16_t *)O_DATA(r))
#define A_REF(r)  ((ref_t *)O_DATA(r))
#define CLASS_OF(r) (g_classes[O_CLS(r)])

/* ---------- constant pool ---------- */
enum {
    CP_Utf8 = 1, CP_Integer = 3, CP_Float = 4, CP_Long = 5, CP_Double = 6,
    CP_Class = 7, CP_String = 8, CP_Fieldref = 9, CP_Methodref = 10,
    CP_InterfaceMethodref = 11, CP_NameAndType = 12,
    CP_MethodHandle = 15, CP_MethodType = 16, CP_InvokeDynamic = 18
};

typedef struct {
    uint8_t tag;
    uint8_t resolved;
    uint16_t a, b;
    union {
        int32_t i;
        float f;
        int64_t l;
        double d;
        void *ptr;
        ref_t ref;
        const char *utf8;
    } v;
} CPEntry;

typedef struct Field {
    struct Class *cls;
    const char *name;
    const char *desc;
    uint16_t access;
    char type;          /* B C D F I J S Z L ('[' mapped to L) */
    uint32_t offset;    /* instance: offset from data start; static: offset into statics */
    uint16_t constIdx;  /* ConstantValue index or 0 */
} Field;

typedef struct {
    uint16_t startPc, endPc, handlerPc, catchType;
} ExHandler;

typedef int (*NativeFn)(struct Thread *t, int32_t *args);
/* native return codes */
#define NAT_OK 0
#define NAT_EXC 1
#define NAT_RETRY 2   /* re-execute the invoke instruction (frames may have been pushed) */
#define NAT_YIELD 3   /* thread blocked; result already stored, resume after invoke */

typedef struct Method {
    struct Class *cls;
    const char *name;
    const char *desc;
    uint16_t access;
    uint16_t maxStack, maxLocals;
    uint16_t argSlots;   /* including this */
    uint8_t retSlots;
    char retType;
    uint8_t *code;
    uint32_t codeLen;
    ExHandler *handlers;
    uint16_t nHandlers;
    uint16_t nLines;
    uint16_t *lines;     /* pairs (startPc, line) */
    NativeFn native;
    int32_t vtIndex;
    uint32_t invokeCount;
} Method;

enum { CLS_LOADED = 0, CLS_LINKED, CLS_INITIALIZING, CLS_INITED, CLS_ERROR };

typedef struct Class {
    const char *name;           /* internal form, e.g. java/lang/String or [I */
    struct Class *super;
    uint16_t access;
    uint32_t id;
    int state;
    struct Thread *initThread;

    CPEntry *cp;
    uint16_t cpCount;
    uint8_t *classBytes;        /* raw class file (utf8 strings point into copies) */

    uint16_t nInterfaces;
    struct Class **interfaces;
    uint16_t nFields;
    Field *fields;
    uint16_t nMethods;
    Method *methods;

    uint32_t instanceSize;      /* bytes of instance fields (incl. super) */
    uint32_t staticSize;
    uint8_t *statics;
    uint16_t nRefOffs;          /* instance reference field offsets (incl. super) */
    uint16_t *refOffs;
    uint16_t nStaticRefs;
    uint16_t *staticRefOffs;

    Method **vtable;
    int vtableSize;

    /* arrays */
    uint8_t isArray;
    char elemType;              /* B C D F I J S Z L */
    uint8_t elemSize;
    struct Class *elemClass;    /* for reference arrays */
    struct Class *arrayClass;   /* cached array-of-this class */

    uint8_t isInterface;
    uint8_t hasFinalizer;
    ref_t classObj;             /* java.lang.Class mirror */
    const char *sourceFile;
    struct Class *hashNext;
    int fromBoot;
} Class;

/* ---------- threads ---------- */
enum { TH_NEW = 0, TH_RUNNABLE, TH_SLEEPING, TH_WAITING, TH_BLOCKED, TH_REACQUIRE, TH_EVENTWAIT, TH_DEAD };

typedef struct Frame {
    Method *m;
    uint8_t *pc;
    int32_t *locals;
    int32_t *sp;
    ref_t lockObj;      /* for synchronized methods */
    uint8_t flags;      /* FR_CLINIT, FR_NATIVE_CALLBACK */
    Class *initClass;   /* class being initialized (FR_CLINIT) */
} Frame;
#define FR_CLINIT 1
#define FR_DROP_RESULT 2 /* when this frame returns, discard return value */

typedef struct Monitor {
    ref_t obj;
    struct Thread *owner;
    int count;
    int inUse;
    struct Thread *waitHead; /* threads waiting (wait()) linked by waitNext */
} Monitor;

#define MAX_NATIVE_ROOTS 64

typedef struct Thread {
    int id;
    int state;
    ref_t jthread;
    int32_t *stack;
    int32_t *stackEnd;
    Frame *frames;
    int fp;              /* number of active frames */
    int maxFrames;
    ref_t exception;
    int64_t wakeTime;    /* ms; 0 = none */
    Monitor *mon;        /* monitor blocked on / waiting on */
    int savedCount;
    int interrupted;
    int notified;
    struct Thread *waitNext;
    struct Thread *next;
    ref_t roots[MAX_NATIVE_ROOTS];
    int nRoots;
    int priority;
    int64_t sliceStart;
} Thread;

/* ---------- VM globals ---------- */
#define MAX_CLASSES 8192
extern Class *g_classes[MAX_CLASSES];
extern int g_numClasses;
extern Thread *g_threads;
extern Thread *g_current;
extern volatile int g_vmExit;
extern int g_exitCode;

/* well-known classes */
extern Class *C_Object, *C_String, *C_Class, *C_Throwable, *C_Thread;
extern Class *C_intArray, *C_byteArray, *C_charArray, *C_shortArray, *C_longArray,
             *C_floatArray, *C_doubleArray, *C_boolArray, *C_objArray, *C_stringArray;

/* String field offsets */
extern uint32_t g_strValueOff, g_strOffsetOff, g_strCountOff;
extern uint32_t g_thrMessageOff, g_thrTraceOff;

/* ---------- API ---------- */
/* heap.c */
int heap_init(size_t bytes);
ref_t heap_alloc(uint32_t classId, uint32_t dataBytes);
void gc_collect(void);
int heap_is_object(ref_t r);
size_t heap_free_bytes(void);
size_t heap_total_bytes(void);
void gc_add_root(ref_t *slot);
void gc_remove_root(ref_t *slot);

ref_t new_object(Class *c);
ref_t new_array(Class *arrCls, int32_t len);
ref_t new_prim_array(char type, int32_t len);
ref_t new_obj_array(Class *elem, int32_t len);

/* class.c */
void classes_init(void);
int vm_add_classpath_mem(uint8_t *data, size_t len, int isBoot);
int vm_add_classpath_file(const char *path);
Class *load_class(const char *name);     /* NULL + exception pending on failure */
Class *find_loaded(const char *name);
Class *array_class_of(Class *elem);
Class *prim_array_class(char t);
Class *array_class_by_name(const char *name);
Method *find_method(Class *c, const char *name, const char *desc);      /* searches supers */
Method *find_method_iface(Class *c, const char *name, const char *desc); /* supers + interfaces */
Field *find_field(Class *c, const char *name, const char *desc);
Field *find_field_by_name(Class *c, const char *name);
int is_subclass(Class *c, Class *of);
int is_assignable(Class *from, Class *to);
int instance_of(ref_t obj, Class *c);
int class_needs_init(Thread *t, Class *c); /* 1 if frames pushed / must wait */
ref_t class_mirror(Class *c);
uint8_t *vm_read_resource(const char *name, uint32_t *len);
void vm_register_native(const char *cls, const char *name, const char *desc, NativeFn fn);
int arg_slots(const char *desc, int isStatic);
char ret_type(const char *desc);
void link_natives(Class *c);

/* string.c */
ref_t new_string_utf8(const char *s);
ref_t new_string_chars(const uint16_t *chars, int len);
ref_t intern_string(ref_t s);
ref_t intern_utf8(const char *modUtf8);
int string_to_utf8(ref_t s, char *buf, int bufLen);
char *string_to_cstr(ref_t s); /* malloc'ed */
int string_length(ref_t s);
uint16_t *string_chars(ref_t s);
int string_equals_c(ref_t s, const char *c);

/* thread.c */
Thread *thread_create(ref_t jthread);
void thread_push_frame_for(Thread *t, Method *m, int32_t *args); /* copies args */
Monitor *get_monitor(ref_t obj, int create);
int monitor_enter(Thread *t, ref_t obj);  /* 1 = acquired, 0 = blocked */
int monitor_exit(Thread *t, ref_t obj);   /* 0 on IllegalMonitorState */
int monitor_wait(Thread *t, ref_t obj, int64_t ms);
int monitor_notify(Thread *t, ref_t obj, int all);
void monitor_notify_all_force(ref_t obj);
void monitors_sweep(void);
int64_t vm_time_ms(void);
void scheduler_run(void);
void thread_terminate(Thread *t);
int count_live_threads(void);
void wake_event_waiters(void);

/* interp.c */
void interp_run(Thread *t, int budget);
int push_frame(Thread *t, Method *m, int32_t *args); /* args are at top of caller stack */
void throw_exception(Thread *t, const char *cls, const char *msg);
void throw_object(Thread *t, ref_t ex);
void print_stack_trace(Thread *t, ref_t ex);
ref_t make_exception(Thread *t, const char *cls, const char *msg);
void fill_stack_trace(Thread *t, ref_t ex);

/* natives */
void natives_init(void);
void natives_lang_init(void);
void natives_midp_init(void);

/* logging */
void vm_log(const char *fmt, ...);
void vm_fatal(const char *fmt, ...);

#define ACC_PUBLIC 0x0001
#define ACC_PRIVATE 0x0002
#define ACC_PROTECTED 0x0004
#define ACC_STATIC 0x0008
#define ACC_FINAL 0x0010
#define ACC_SYNCHRONIZED 0x0020
#define ACC_SUPER 0x0020
#define ACC_NATIVE 0x0100
#define ACC_INTERFACE 0x0200
#define ACC_ABSTRACT 0x0400

static inline int64_t slot_get_long(const int32_t *p) { int64_t v; memcpy(&v, p, 8); return v; }
static inline void slot_put_long(int32_t *p, int64_t v) { memcpy(p, &v, 8); }
static inline double slot_get_double(const int32_t *p) { double v; memcpy(&v, p, 8); return v; }
static inline void slot_put_double(int32_t *p, double v) { memcpy(p, &v, 8); }
static inline float slot_get_float(const int32_t *p) { float v; memcpy(&v, p, 4); return v; }
static inline void slot_put_float(int32_t *p, float v) { memcpy(p, &v, 4); }

#define PUSH_ROOT(t, r) ((t)->roots[(t)->nRoots++] = (r))
#define POP_ROOTS(t, n) ((t)->nRoots -= (n))

#endif
