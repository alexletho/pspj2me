/* Host test runner */
#include "vm/vm.h"
#include "platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int vm_init(uint8_t *bootJar, size_t bootLen, size_t heapBytes);
int vm_run_main(const char *clsName, const char *method, const char *desc, const char *strArg);
void host_init(const char *shots, int every, int64_t quitMs);
void host_parse_keys(const char *spec);
void host_save_last_frame(const char *path);
int midp_launch(const char *jarPath, const char *midletClass);

static uint8_t *readFile(const char *p, size_t *len) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n);
    if (fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f);
    *len = n;
    return b;
}

int main(int argc, char **argv) {
    const char *boot = "build/boot.jar", *mainCls = NULL, *shots = NULL, *keys = NULL, *app = NULL, *midlet = NULL, *final = NULL;
    int every = 0, repeat = 1;
    long quit = 0;
    size_t heap = 24 * 1024 * 1024;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--boot") && i + 1 < argc) boot = argv[++i];
        else if (!strcmp(argv[i], "--main") && i + 1 < argc) mainCls = argv[++i];
        else if (!strcmp(argv[i], "--midlet") && i + 1 < argc) midlet = argv[++i];
        else if (!strcmp(argv[i], "--shots") && i + 1 < argc) shots = argv[++i];
        else if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--quit") && i + 1 < argc) quit = atol(argv[++i]);
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc) keys = argv[++i];
        else if (!strcmp(argv[i], "--heap") && i + 1 < argc) heap = (size_t)atol(argv[++i]) * 1024 * 1024;
        else if (!strcmp(argv[i], "--final") && i + 1 < argc) final = argv[++i];
        else if (!strcmp(argv[i], "--repeat") && i + 1 < argc) repeat = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--phone") && i + 1 < argc) { extern void midp_set_key_profile(int); midp_set_key_profile(atoi(argv[++i])); }
        else app = argv[i];
    }
    size_t bootLen;
    uint8_t *bootData = readFile(boot, &bootLen);
    if (!bootData) { fprintf(stderr, "cannot read boot jar %s\n", boot); return 1; }
    int rc = 0;
    extern void vm_shutdown(void);
    for (int run = 0; run < repeat; run++) {
        host_init(shots, every, quit);
        if (keys) host_parse_keys(keys);
        if (vm_init(bootData, bootLen, heap) != 0) return 1;
        if (mainCls) {
            if (app && vm_add_classpath_file(app) != 0) { fprintf(stderr, "cannot open %s\n", app); return 1; }
            char cls[256];
            snprintf(cls, sizeof cls, "%s", mainCls);
            for (char *p = cls; *p; p++) if (*p == '.') *p = '/';
            rc = vm_run_main(cls, "main", "([Ljava/lang/String;)V", NULL);
        } else {
            if (!app) { fprintf(stderr, "usage: j2me_host [opts] app.jar\n"); return 1; }
            rc = midp_launch(app, midlet);
        }
        if (repeat > 1) {
            vm_shutdown();
            FILE *f = fopen("/proc/self/statm", "r");
            long pages = 0, rss = 0;
            if (f) { if (fscanf(f, "%ld %ld", &pages, &rss) != 2) rss = 0; fclose(f); }
            printf("[host] run %d done rc=%d rss=%ld KB\n", run + 1, rc, rss * 4);
        }
    }
    if (final) host_save_last_frame(final);
    if (repeat > 1) free(bootData);   /* the VM was shut down: nothing references it any more */
    return rc;
}
