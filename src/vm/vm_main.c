/* VM bootstrap */
#include "vm.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

void natives_lang_post_init(void);
void interp_set_oom(ref_t r);
ref_t make_main_thread_object(Thread *t);

void vm_log(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    plat_log(buf);
}

void vm_fatal(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    plat_log("FATAL: ");
    plat_log(buf);
    plat_log("\n");
    exit(1);
}

int vm_init(uint8_t *bootJar, size_t bootLen, size_t heapBytes) {
    while (heap_init(heapBytes) != 0) {
        if (heapBytes <= 4 * 1024 * 1024) { vm_log("heap init failed\n"); return -1; }
        heapBytes -= 2 * 1024 * 1024;
    }
    vm_log("Java heap: %u KB\n", (unsigned)(heapBytes / 1024));
    static int nativesRegistered;
    if (!nativesRegistered) { natives_lang_init(); natives_midp_init(); nativesRegistered = 1; }
    if (vm_add_classpath_mem(bootJar, bootLen, 1) != 0) { vm_log("bad boot jar\n"); return -1; }
    classes_init();
    natives_lang_post_init();
    ref_t oom = make_exception(NULL, "java/lang/OutOfMemoryError", NULL);
    interp_set_oom(oom);
    return 0;
}

/* Runs static method cls.name(desc) with a single String arg (or none) in a new main thread. */
int vm_run_main(const char *clsName, const char *method, const char *desc, const char *strArg) {
    Thread *t = thread_create(0);
    g_current = t;
    t->jthread = make_main_thread_object(t);
    Class *c = load_class(clsName);
    if (!c) { vm_log("main class %s not found\n", clsName); return -1; }
    Method *m = find_method(c, method, desc);
    if (!m || !(m->access & ACC_STATIC)) { vm_log("method %s.%s%s not found\n", clsName, method, desc); return -1; }
    int32_t args[2] = { 0, 0 };
    if (m->argSlots == 1) {
        if (strcmp(desc, "([Ljava/lang/String;)V") == 0) {
            args[0] = new_obj_array(C_String, 0);
        } else if (strArg) {
            args[0] = new_string_utf8(strArg);
        }
    }
    thread_push_frame_for(t, m, args);
    t->state = TH_RUNNABLE;
    /* initialise the main class and its supers: push <clinit> frames so supers run first */
    Class *chain[64];
    int n = 0;
    for (Class *k = c; k && k->state != CLS_INITED && n < 64; k = k->super) chain[n++] = k;
    for (int i = 0; i < n; i++) {
        Class *k = chain[i];
        Method *clinit = NULL;
        for (int j = 0; j < k->nMethods; j++)
            if (strcmp(k->methods[j].name, "<clinit>") == 0) { clinit = &k->methods[j]; break; }
        if (!clinit) { k->state = CLS_INITED; continue; }
        k->state = CLS_INITIALIZING;
        k->initThread = t;
        extern int push_clinit_frame(Thread *t, Method *m, Class *c);
        push_clinit_frame(t, clinit, k);
    }
    scheduler_run();
    return g_exitCode;
}

/* Tears down everything so vm_init() can be called again (used to return to the game list). */
void vm_shutdown(void) {
    extern void classes_shutdown(void), heap_reset(void), interp_reset(void), strings_reset(void),
        threads_reset(void), natives_lang_reset(void), midp_reset(void);
    threads_reset();
    classes_shutdown();
    strings_reset();
    interp_reset();
    heap_reset();
    natives_lang_reset();
    midp_reset();
}
