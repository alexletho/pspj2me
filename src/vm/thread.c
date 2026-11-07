/* Green threads, monitors and the scheduler. */
#include "vm.h"
#include "../platform/platform.h"
#include <stdlib.h>
#include <stdio.h>

Thread *g_threads;
Thread *g_current;
volatile int g_vmExit;
int g_exitCode;
static int nextThreadId = 1;
int (*g_eventPendingHook)(void);
volatile uint32_t g_schedTicks;

#define STACK_SLOTS (24 * 1024)
#define MAX_FRAMES 1536
#define SLICE_BUDGET 3000

int64_t vm_time_ms(void) { return plat_time_ms(); }

Thread *thread_create(ref_t jthread) {
    Thread *t = calloc(1, sizeof(Thread));
    t->id = nextThreadId++;
    t->jthread = jthread;
    t->stack = calloc(STACK_SLOTS, sizeof(int32_t));
    t->stackEnd = t->stack + STACK_SLOTS;
    t->maxFrames = MAX_FRAMES;
    t->frames = calloc(MAX_FRAMES, sizeof(Frame));
    t->state = TH_NEW;
    t->priority = 5;
    /* append to list */
    Thread **pp = &g_threads;
    while (*pp) pp = &(*pp)->next;
    *pp = t;
    return t;
}

static void freeThread(Thread *t) {
    free(t->stack);
    free(t->frames);
    free(t);
}

void thread_terminate(Thread *t) {
    t->state = TH_DEAD;
    t->fp = 0;
    if (t->jthread) monitor_notify_all_force(t->jthread);
}

int count_live_threads(void) {
    int n = 0;
    for (Thread *t = g_threads; t; t = t->next) if (t->state != TH_DEAD) n++;
    return n;
}

/* ---------------- monitors ---------------- */
static Monitor **monTable;
static int monCap, monCount;
static int *monFree;
static int monFreeCount;

Monitor *get_monitor(ref_t obj, int create) {
    uint32_t idx = O_LOCK(obj);
    if (idx) return monTable[idx - 1];
    if (!create) return NULL;
    int slot;
    if (monFreeCount) slot = monFree[--monFreeCount];
    else {
        if (monCount >= monCap) {
            monCap = monCap ? monCap * 2 : 256;
            monTable = realloc(monTable, monCap * sizeof(Monitor *));
            monFree = realloc(monFree, monCap * sizeof(int));
        }
        slot = monCount++;
        monTable[slot] = calloc(1, sizeof(Monitor));
    }
    Monitor *m = monTable[slot];
    memset(m, 0, sizeof *m);
    m->obj = obj;
    m->inUse = 1;
    O_LOCK(obj) = slot + 1;
    return m;
}

void monitors_sweep(void) {
    for (int i = 0; i < monCount; i++) {
        Monitor *m = monTable[i];
        if (!m->inUse) continue;
        if (!(O_SIZE(m->obj) & 2u)) { /* object unreachable */
            m->inUse = 0;
            monFree[monFreeCount++] = i;
        }
    }
}

int monitor_enter(Thread *t, ref_t obj) {
    Monitor *m = get_monitor(obj, 1);
    if (!m->owner) { m->owner = t; m->count = 1; return 1; }
    if (m->owner == t) { m->count++; return 1; }
    t->state = TH_BLOCKED;
    t->mon = m;
    return 0;
}

int monitor_exit(Thread *t, ref_t obj) {
    Monitor *m = get_monitor(obj, 0);
    if (!m || m->owner != t) return 0;
    if (--m->count == 0) m->owner = NULL;
    return 1;
}

static void removeWaiter(Monitor *m, Thread *t) {
    Thread **pp = &m->waitHead;
    while (*pp) {
        if (*pp == t) { *pp = t->waitNext; t->waitNext = NULL; return; }
        pp = &(*pp)->waitNext;
    }
}

int monitor_wait(Thread *t, ref_t obj, int64_t ms) {
    Monitor *m = get_monitor(obj, 0);
    if (!m || m->owner != t) return -1;
    t->savedCount = m->count;
    m->owner = NULL;
    m->count = 0;
    t->mon = m;
    t->state = TH_WAITING;
    t->wakeTime = ms > 0 ? vm_time_ms() + ms : 0;
    t->waitNext = NULL;
    Thread **pp = &m->waitHead;
    while (*pp) pp = &(*pp)->waitNext;
    *pp = t;
    return 0;
}

int monitor_notify(Thread *t, ref_t obj, int all) {
    Monitor *m = get_monitor(obj, 0);
    if (!m || m->owner != t) return -1;
    while (m->waitHead) {
        Thread *w = m->waitHead;
        m->waitHead = w->waitNext;
        w->waitNext = NULL;
        w->state = TH_REACQUIRE;
        if (!all) break;
    }
    return 0;
}

void monitor_notify_all_force(ref_t obj) {
    Monitor *m = get_monitor(obj, 0);
    if (!m) return;
    while (m->waitHead) {
        Thread *w = m->waitHead;
        m->waitHead = w->waitNext;
        w->waitNext = NULL;
        w->state = TH_REACQUIRE;
    }
}

/* interrupt support: called from Thread.interrupt native */
void thread_interrupt(Thread *t) {
    t->interrupted = 1;
    if (t->state == TH_SLEEPING || t->state == TH_WAITING) {
        if (t->state == TH_WAITING) { removeWaiter(t->mon, t); t->state = TH_REACQUIRE; }
        else t->state = TH_RUNNABLE;
        t->interrupted = 0;
        Thread *saved = g_current;
        g_current = t;
        throw_exception(t, "java/lang/InterruptedException", NULL);
        g_current = saved;
    }
}

/* ---------------- scheduler ---------------- */
static int64_t lastPoll;
int64_t g_perfGfxUs, g_perfPresentUs, g_perfGcUs;
uint32_t g_perfFrames;
static int64_t perfRunUs, perfIdleUs, perfStart;
int g_perfLog;
volatile int64_t g_perfAudioUs;   /* time the platform's audio thread spent mixing/decoding */

void scheduler_run(void) {
    Thread *last = NULL;
    while (!g_vmExit) {
        g_schedTicks++;
        int64_t now = vm_time_ms();
        if (now - lastPoll >= 5 || now < lastPoll) { plat_poll(); lastPoll = now; }
        int64_t nextWake = INT64_MAX;
        int live = 0;
        int evPending = g_eventPendingHook ? g_eventPendingHook() : 0;
        /* reap + update states */
        Thread **pp = &g_threads;
        while (*pp) {
            Thread *t = *pp;
            if (t->state == TH_DEAD) {
                *pp = t->next;
                if (last == t) last = NULL;
                if (g_current == t) g_current = NULL;
                freeThread(t);
                continue;
            }
            live++;
            switch (t->state) {
                case TH_SLEEPING:
                    if (now >= t->wakeTime) t->state = TH_RUNNABLE;
                    else if (t->wakeTime < nextWake) nextWake = t->wakeTime;
                    break;
                case TH_WAITING:
                    if (t->wakeTime && now >= t->wakeTime) { removeWaiter(t->mon, t); t->state = TH_REACQUIRE; }
                    else if (t->wakeTime && t->wakeTime < nextWake) nextWake = t->wakeTime;
                    break;
                case TH_EVENTWAIT:
                    if (evPending || (t->wakeTime && now >= t->wakeTime)) t->state = TH_RUNNABLE;
                    else if (t->wakeTime && t->wakeTime < nextWake) nextWake = t->wakeTime;
                    break;
                default: break;
            }
            if (t->state == TH_REACQUIRE && (!t->mon->owner || t->mon->owner == t)) {
                if (!t->mon->owner) { t->mon->owner = t; t->mon->count = t->savedCount; }
                else t->mon->count += t->savedCount;
                t->state = TH_RUNNABLE;
            }
            if (t->state == TH_BLOCKED && (!t->mon->owner || t->mon->owner == t)) t->state = TH_RUNNABLE;
            pp = &t->next;
        }
        if (live == 0) break;
        /* round robin pick */
        Thread *pick = NULL;
        Thread *start = last ? last->next : g_threads;
        for (Thread *t = start; t; t = t->next) if (t->state == TH_RUNNABLE) { pick = t; break; }
        if (!pick) for (Thread *t = g_threads; t && t != start; t = t->next) if (t->state == TH_RUNNABLE) { pick = t; break; }
        if (!pick) {
            int64_t wait = nextWake == INT64_MAX ? 5 : nextWake - now;
            if (wait > 5) wait = 5;
            if (wait > 0) { int64_t a = plat_time_us(); plat_idle((int)wait); perfIdleUs += plat_time_us() - a; }
            continue;
        }
        g_current = pick;
        last = pick;
        int64_t a = plat_time_us();
        interp_run(pick, SLICE_BUDGET);
        perfRunUs += plat_time_us() - a;
        if (!perfStart) perfStart = a;
        if (a - perfStart >= 10000000) {
            int64_t total = a - perfStart;
            int64_t gameUs = perfRunUs - g_perfGfxUs - g_perfPresentUs - g_perfGcUs;
            int64_t audioUs = g_perfAudioUs;
            if (audioUs > gameUs) audioUs = gameUs;   /* the audio thread preempts game slices */
            gameUs -= audioUs;
            if (g_perfLog) vm_log("perf: %.1f fps | game code %d%%, drawing %d%%, screen output %d%%, GC %d%%, sound %d%%, idle %d%%\n",
                   g_perfFrames * 1e6 / (double)total, (int)(gameUs * 100 / total), (int)(g_perfGfxUs * 100 / total),
                   (int)(g_perfPresentUs * 100 / total), (int)(g_perfGcUs * 100 / total), (int)(audioUs * 100 / total),
                   (int)(perfIdleUs * 100 / total));
            perfStart = a; perfRunUs = perfIdleUs = 0; g_perfGfxUs = g_perfPresentUs = g_perfGcUs = 0; g_perfFrames = 0;
            g_perfAudioUs = 0;
        }
    }
}

void threads_reset(void) {
    while (g_threads) { Thread *n = g_threads->next; freeThread(g_threads); g_threads = n; }
    g_current = NULL;
    nextThreadId = 1;
    for (int i = 0; i < monCount; i++) free(monTable[i]);
    free(monTable); free(monFree);
    monTable = NULL; monFree = NULL;
    monCap = monCount = monFreeCount = 0;
    lastPoll = 0;
    perfStart = perfRunUs = perfIdleUs = 0;
    g_perfGfxUs = g_perfPresentUs = g_perfGcUs = 0;
    g_perfFrames = 0;
    g_vmExit = 0;
    g_exitCode = 0;
}

static const char *stateName(int s) {
    static const char *n[] = { "new", "runnable", "sleeping", "waiting", "blocked", "reacquire", "eventwait", "dead" };
    return s >= 0 && s <= TH_DEAD ? n[s] : "?";
}

static int lineOf(Method *m, int pc) {
    int line = -1;
    for (int i = 0; i < m->nLines; i++) if (m->lines[i * 2] <= pc) line = m->lines[i * 2 + 1];
    return line;
}

/* Logs every thread with its state and top frames (diagnostics; may be called from a watchdog). */
void vm_dump_threads(const char *why) {
    int64_t now = vm_time_ms();
    vm_log("---- VM state (%s) ----\n", why);
    vm_log("heap free %u KB of %u KB, current thread %d\n", (unsigned)(heap_free_bytes() / 1024),
           (unsigned)(heap_total_bytes() / 1024), g_current ? g_current->id : -1);
    int n = 0;
    for (Thread *t = g_threads; t && n < 32; t = t->next, n++) {
        char extra[64] = "";
        if ((t->state == TH_SLEEPING || t->state == TH_WAITING || t->state == TH_EVENTWAIT) && t->wakeTime)
            snprintf(extra, sizeof extra, " (wakes in %d ms)", (int)(t->wakeTime - now));
        vm_log("thread %d: %s%s, %d frames\n", t->id, stateName(t->state), extra, t->fp);
        for (int f = t->fp - 1, k = 0; f >= 0 && k < 8; f--, k++) {
            Frame *fr = &t->frames[f];
            Method *m = fr->m;
            if (!m || !m->cls) continue;
            int pc = m->code && fr->pc ? (int)(fr->pc - m->code) : -1;
            vm_log("    at %s.%s (line %d, pc %d)\n", m->cls->name, m->name, lineOf(m, pc > 0 ? pc - 1 : 0), pc);
        }
    }
    vm_log("---- end of VM state ----\n");
}
