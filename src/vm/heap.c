/* Non-moving mark-sweep heap. Refs are offsets from g_heap. */
#include "vm.h"
#include <stdlib.h>
#include <stdio.h>

uint8_t *g_heap;
static uint8_t *g_heapBase;
static uint32_t heapSize;
static uint32_t heapTop;      /* bump pointer */
static uint8_t *startBits;    /* object start bitmap: 1 bit per 8 bytes */
static uint32_t freeBytes;

#define F_USED 1u
#define F_MARK 2u
#define SZ(w) ((w) & ~7u)
#define NSMALL 64             /* exact lists for sizes 8..512 */
static uint32_t smallFree[NSMALL + 1];
static uint32_t largeFree;    /* singly linked via word at +4 */
static uint64_t allocSinceGC;

#define NEXTFREE(r) (*(uint32_t *)(g_heap + (r) + 4))

static inline void setStart(uint32_t r) { startBits[r >> 6] |= (uint8_t)(1u << ((r >> 3) & 7)); }
static inline void clrStart(uint32_t r) { startBits[r >> 6] &= (uint8_t)~(1u << ((r >> 3) & 7)); }
static inline int isStart(uint32_t r) { return (startBits[r >> 6] >> ((r >> 3) & 7)) & 1; }

int heap_init(size_t bytes) {
    bytes &= ~(size_t)7;
    extern void *plat_heap_alloc(size_t bytes);
    g_heapBase = g_heap = plat_heap_alloc(bytes + 8);
    if (!g_heap) return -1;
    startBits = calloc(bytes / 64 + 1, 1);
    extern void plat_heap_free(void *p);
    if (!startBits) { plat_heap_free(g_heapBase); g_heap = g_heapBase = NULL; return -1; }
    /* align */
    uintptr_t p = (uintptr_t)g_heap;
    if (p & 7) g_heap += 8 - (p & 7);
    heapSize = (uint32_t)bytes;
    heapTop = 8;              /* offset 0 is null */
    freeBytes = heapSize - 8;
    return 0;
}

void heap_release(void) {
    extern void plat_heap_free(void *p);
    plat_heap_free(g_heapBase);
    free(startBits);
    g_heap = NULL; g_heapBase = NULL; startBits = NULL;
}

size_t heap_free_bytes(void) { return freeBytes; }
size_t heap_total_bytes(void) { return heapSize; }

int heap_is_object(ref_t r) {
    if (r < 8 || r >= heapTop || (r & 7)) return 0;
    if (!isStart(r)) return 0;
    return (O_SIZE(r) & F_USED) != 0;
}

static void addFree(uint32_t r, uint32_t size) {
    O_SIZE(r) = size; /* not used */
    if (size / 8 <= NSMALL) {
        NEXTFREE(r) = smallFree[size / 8];
        smallFree[size / 8] = r;
    } else {
        NEXTFREE(r) = largeFree;
        largeFree = r;
    }
}

static uint32_t tryAlloc(uint32_t size) {
    uint32_t r;
    if (size / 8 <= NSMALL && smallFree[size / 8]) {
        r = smallFree[size / 8];
        smallFree[size / 8] = NEXTFREE(r);
        return r;
    }
    if (heapTop + size <= heapSize) {
        r = heapTop;
        heapTop += size;
        return r;
    }
    /* first fit in large list */
    uint32_t prev = 0, cur = largeFree;
    while (cur) {
        uint32_t csz = SZ(O_SIZE(cur));
        if (csz == size || csz >= size + 16) {
            uint32_t next = NEXTFREE(cur);
            if (prev) NEXTFREE(prev) = next; else largeFree = next;
            if (csz > size) {
                uint32_t rest = cur + size;
                addFree(rest, csz - size);
            }
            return cur;
        }
        prev = cur;
        cur = NEXTFREE(cur);
    }
    /* try splitting a bigger small chunk */
    for (uint32_t k = size / 8 + 2; k <= NSMALL; k++) {
        if (smallFree[k]) {
            r = smallFree[k];
            smallFree[k] = NEXTFREE(r);
            addFree(r + size, k * 8 - size);
            return r;
        }
    }
    return 0;
}

ref_t heap_alloc(uint32_t classId, uint32_t dataBytes) {
    uint32_t size = (HDR + dataBytes + 7) & ~7u;
    if (dataBytes > 0x7ffffff0u - HDR) return 0;
    if (allocSinceGC > heapSize / 2 && freeBytes < heapSize / 4) {
        /* proactive collection keeps fragmentation down */
        gc_collect();
    }
    uint32_t r = tryAlloc(size);
    if (!r) {
        gc_collect();
        r = tryAlloc(size);
        if (!r) return 0;
    }
    memset(g_heap + r, 0, size);
    O_SIZE(r) = size | F_USED;
    O_CLS(r) = classId;
    setStart(r);
    freeBytes -= size;
    allocSinceGC += size;
    return r;
}

/* ---------------- GC ---------------- */
static uint32_t *markStack;
static int markSp, markCap;
static ref_t *weakList; static int weakCount, weakCap;
static Class *C_WeakRef;
static uint32_t weakRefOff;

#define MAX_GLOBAL_ROOTS 256
static ref_t *globalRoots[MAX_GLOBAL_ROOTS];
static int nGlobalRoots;

void gc_add_root(ref_t *slot) {
    if (nGlobalRoots < MAX_GLOBAL_ROOTS) globalRoots[nGlobalRoots++] = slot;
}
void gc_remove_root(ref_t *slot) {
    for (int i = 0; i < nGlobalRoots; i++)
        if (globalRoots[i] == slot) { globalRoots[i] = globalRoots[--nGlobalRoots]; return; }
}

static inline void markRef(ref_t r) {
    if (!r) return;
    uint32_t *w = (uint32_t *)(g_heap + r);
    if (*w & F_MARK) return;
    *w |= F_MARK;
    if (markSp >= markCap) {
        markCap = markCap ? markCap * 2 : 4096;
        markStack = realloc(markStack, markCap * sizeof(uint32_t));
    }
    markStack[markSp++] = r;
}

static inline void markConservative(int32_t v) {
    ref_t r = (ref_t)v;
    if (heap_is_object(r)) markRef(r);
}

extern ref_t *g_internTable; extern int g_internCap;

static void drain(void) {
    while (markSp > 0) {
        ref_t r = markStack[--markSp];
        Class *c = g_classes[O_CLS(r)];
        if (!c) continue;
        if (c->isArray) {
            if (c->elemType == 'L') {
                int32_t n = A_LEN(r);
                ref_t *a = A_REF(r);
                for (int32_t i = 0; i < n; i++) if (a[i]) markRef(a[i]);
            }
        } else {
            for (int i = 0; i < c->nRefOffs; i++) {
                ref_t f = F_REF(r, c->refOffs[i]);
                if (f) markRef(f);
            }
            if (C_WeakRef && is_subclass(c, C_WeakRef)) {
                if (weakCount >= weakCap) {
                    weakCap = weakCap ? weakCap * 2 : 64;
                    weakList = realloc(weakList, weakCap * sizeof(ref_t));
                }
                weakList[weakCount++] = r;
            }
        }
    }
}

extern int64_t g_perfGcUs;
int64_t plat_time_us(void);
static void gc_collect_impl(void);
void gc_collect(void) {
    int64_t a = plat_time_us();
    gc_collect_impl();
    g_perfGcUs += plat_time_us() - a;
}
static void gc_collect_impl(void) {
    if (!C_WeakRef) {
        C_WeakRef = find_loaded("java/lang/ref/WeakReference");
        if (C_WeakRef) {
            Field *f = find_field_by_name(C_WeakRef, "referent");
            if (f) weakRefOff = f->offset;
        }
    }
    weakCount = 0;
    /* roots: classes */
    for (int i = 1; i < g_numClasses; i++) {
        Class *c = g_classes[i];
        if (!c) continue;
        if (c->classObj) markRef(c->classObj);
        for (int k = 0; k < c->nStaticRefs; k++) {
            ref_t s = *(ref_t *)(c->statics + c->staticRefOffs[k]);
            if (s) markRef(s);
        }
        if (c->cp) {
            for (int k = 1; k < c->cpCount; k++)
                if (c->cp[k].tag == CP_String && c->cp[k].resolved) markRef(c->cp[k].v.ref);
        }
    }
    drain();
    /* intern table */
    for (int i = 0; i < g_internCap; i++) if (g_internTable[i]) markRef(g_internTable[i]);
    for (int i = 0; i < nGlobalRoots; i++) if (*globalRoots[i]) markRef(*globalRoots[i]);
    drain();
    /* threads */
    for (Thread *t = g_threads; t; t = t->next) {
        if (t->jthread) markRef(t->jthread);
        if (t->exception) markRef(t->exception);
        for (int i = 0; i < t->nRoots; i++) if (t->roots[i]) markRef(t->roots[i]);
        if (t->state == TH_DEAD) continue;
        int32_t *top = t->stack;
        for (int f = 0; f < t->fp; f++) {
            Frame *fr = &t->frames[f];
            if (fr->lockObj) markRef(fr->lockObj);
            if (fr->sp > top) top = fr->sp;
        }
        if (top > t->stackEnd) top = t->stackEnd;
        for (int32_t *p = t->stack; p < top; p++) markConservative(*p);
        drain();
    }
    drain();
    /* weak refs */
    for (int i = 0; i < weakCount; i++) {
        ref_t w = weakList[i];
        ref_t ref = F_REF(w, weakRefOff);
        if (ref && !(O_SIZE(ref) & F_MARK)) F_REF(w, weakRefOff) = 0;
    }
    monitors_sweep(); /* needs mark bits intact */

    /* sweep + coalesce */
    for (int i = 0; i <= NSMALL; i++) smallFree[i] = 0;
    largeFree = 0;
    uint32_t r = 8;
    uint32_t runStart = 0, runSize = 0;
    uint32_t fb = 0;
    while (r < heapTop) {
        uint32_t w = O_SIZE(r);
        uint32_t sz = SZ(w);
        if (sz == 0) { vm_fatal("heap corrupt at %u", r); }
        int live = (w & F_USED) && (w & F_MARK);
        if (live) {
            O_SIZE(r) = w & ~F_MARK;
            if (runSize) { addFree(runStart, runSize); fb += runSize; runSize = 0; }
        } else {
            if (w & F_USED) clrStart(r);
            if (!runSize) runStart = r;
            runSize += sz;
        }
        r += sz;
    }
    if (runSize) {
        /* trailing free run: give back to bump region */
        heapTop = runStart;
    }
    freeBytes = fb + (heapSize - heapTop);
    allocSinceGC = 0;
}

/* ---------------- object helpers ---------------- */
ref_t new_object(Class *c) {
    ref_t r = heap_alloc(c->id, c->instanceSize);
    return r;
}

ref_t new_array(Class *arrCls, int32_t len) {
    if (len < 0) return 0;
    uint64_t bytes = (uint64_t)len * arrCls->elemSize;
    if (bytes > 0x7fffff00u) return 0;
    ref_t r = heap_alloc(arrCls->id, (uint32_t)bytes);
    if (r) A_LEN(r) = len;
    return r;
}

ref_t new_prim_array(char type, int32_t len) {
    return new_array(prim_array_class(type), len);
}

ref_t new_obj_array(Class *elem, int32_t len) {
    return new_array(array_class_of(elem), len);
}

void heap_reset(void) {
    heap_release();
    for (int i = 0; i <= NSMALL; i++) smallFree[i] = 0;
    largeFree = 0;
    heapTop = 8;
    heapSize = 0;
    freeBytes = 0;
    allocSinceGC = 0;
    C_WeakRef = NULL;
    weakCount = 0;
    nGlobalRoots = 0;
}
