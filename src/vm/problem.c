/* What went wrong in this run, in words a player understands.
 *
 * The VM and the MIDP layer report problems here; the platform shows the most serious one
 * (in the game list after the game ends, or over a game that stopped after a crash).
 * The technical details (stack traces) still go to the log. */
#include "vm.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char probText[300];
static int probSev;
static int64_t probMs;

void vm_problem_reset(void) { probText[0] = 0; probSev = 0; probMs = 0; }
const char *vm_problem_text(void) { return probText; }
int vm_problem_severity(void) { return probSev; }
int64_t vm_problem_time(void) { return probMs; }

/* keeps the first message of the highest severity */
void vm_problem(int sev, const char *fmt, ...) {
    if (sev <= probSev) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(probText, sizeof probText, fmt, ap);
    va_end(ap);
    probSev = sev;
    probMs = vm_time_ms();
    vm_log("PROBLEM: %s\n", probText);
}

/* APIs of other phones and optional JSRs PSPJ2ME does not implement */
static const struct { const char *prefix, *what; } apis[] = {
    { "javax/microedition/m3g/", "3D graphics (M3G, JSR-184)" },
    { "com/mascotcapsule/", "MascotCapsule 3D graphics" },
    { "javax/microedition/m2g/", "SVG graphics (JSR-226)" },
    { "javax/bluetooth/", "Bluetooth" },
    { "javax/obex/", "Bluetooth file exchange (OBEX)" },
    { "javax/microedition/location/", "location / GPS (JSR-179)" },
    { "javax/microedition/sensor/", "motion sensors (JSR-256)" },
    { "javax/microedition/amms/", "advanced multimedia (JSR-234)" },
    { "javax/microedition/payment/", "in-app payments (JSR-229)" },
    { "javax/microedition/pim/", "the phone's contacts and calendar (JSR-75)" },
    { "javax/microedition/content/", "content handlers (JSR-211)" },
    { "javax/microedition/sip/", "SIP calls (JSR-180)" },
    { "javax/microedition/media/", "a sound/video feature of MMAPI" },
    { "com/siemens/", "Siemens phone extensions" },
    { "com/samsung/", "Samsung phone extensions" },
    { "com/motorola/", "Motorola phone extensions" },
    { "com/sonyericsson/", "Sony Ericsson phone extensions" },
    { "com/sprintpcs/", "Sprint phone extensions" },
    { "com/vodafone/", "Vodafone phone extensions" },
    { "com/nttdocomo/", "DoJa (i-mode) phone extensions" },
    { "com/jblend/", "JBlend phone extensions" },
    { "com/kddi/", "KDDI phone extensions" },
    { "com/lge/", "LG phone extensions" },
    { "mmpp/", "LG phone extensions" },
    { "com/nokia/", "Nokia phone extensions" },
    { "java/", "standard Java" },
    { "javax/", "standard J2ME" },
};

static void dotted(const char *in, char *out, int len) {
    int i = 0;
    for (; in[i] && i < len - 1; i++) out[i] = in[i] == '/' ? '.' : in[i];
    out[i] = 0;
}

/* explains a missing class (internal name, a/b/C) */
static void explainMissingClass(const char *cls, char *out, int len) {
    char d[200];
    dotted(cls, d, sizeof d);
    for (size_t i = 0; i < sizeof apis / sizeof apis[0]; i++) {
        if (strncmp(cls, apis[i].prefix, strlen(apis[i].prefix)) == 0) {
            if (apis[i].prefix[0] == 'j' && (!strcmp(apis[i].prefix, "java/") || !strcmp(apis[i].prefix, "javax/")))
                snprintf(out, len, "The game uses %s, which PSPJ2ME does not have yet. Please report this game.", d);
            else
                snprintf(out, len, "This game needs %s, which PSPJ2ME does not support.", apis[i].what);
            return;
        }
    }
    snprintf(out, len, "Part of the game (%s) is missing from the .jar. The file may be damaged or incomplete.", d);
}

/* short name of an exception class: java/lang/NullPointerException -> NullPointerException */
static const char *simpleName(const char *cls) {
    const char *s = strrchr(cls, '/');
    return s ? s + 1 : cls;
}

/* phase: 0 = a game thread died, 1 = while creating the MIDlet, 2 = in startApp */
void vm_problem_exception(ref_t ex, int phase) {
    if (!ex) return;
    Class *c = CLASS_OF(ex);
    char msg[200] = "", text[300];
    ref_t m = F_REF(ex, g_thrMessageOff);
    if (m) string_to_utf8(m, msg, sizeof msg);
    const char *n = c->name;
    int sev = phase ? PROB_START : PROB_CRASH;
    if (!strcmp(n, "java/lang/OutOfMemoryError"))
        snprintf(text, sizeof text, "The game ran out of memory (%u KB available). It may need a phone with more memory.",
                 (unsigned)(heap_total_bytes() / 1024));
    else if (!strcmp(n, "java/lang/StackOverflowError"))
        snprintf(text, sizeof text, "The game's code went into endless recursion (stack overflow).");
    else if ((!strcmp(n, "java/lang/NoClassDefFoundError") || !strcmp(n, "java/lang/ClassNotFoundException")) && msg[0]) {
        char cls[200];
        snprintf(cls, sizeof cls, "%s", msg);
        for (char *p = cls; *p; p++) if (*p == '.') *p = '/';
        if (phase == 1 && !strcmp(n, "java/lang/ClassNotFoundException")) {
            char d[200];
            dotted(cls, d, sizeof d);
            snprintf(text, sizeof text, "The game's main class %s is missing from the .jar. The file may be damaged.", d);
        } else explainMissingClass(cls, text, sizeof text);
        sev = PROB_MISSING;
    } else if (!strcmp(n, "java/lang/NoSuchMethodError") || !strcmp(n, "java/lang/NoSuchFieldError") ||
               !strcmp(n, "java/lang/AbstractMethodError") || (!strcmp(n, "java/lang/Error") && !strcmp(msg, "UnsatisfiedLinkError"))) {
        char cls[200], d[200];
        snprintf(cls, sizeof cls, "%s", msg);
        char *dot = strchr(cls, '.');
        if (dot) *dot = 0;
        int known = 0;
        for (size_t i = 0; i < sizeof apis / sizeof apis[0]; i++)
            if (strncmp(cls, apis[i].prefix, strlen(apis[i].prefix)) == 0) known = 1;
        dotted(msg, d, sizeof d);
        if (known) snprintf(text, sizeof text, "The game uses a feature PSPJ2ME does not have yet (%s). Please report this game.", d);
        else snprintf(text, sizeof text, "The game's files don't fit together (%s). The .jar may be damaged.", d);
        sev = PROB_MISSING;
    } else if (phase == 1)
        snprintf(text, sizeof text, "The game crashed while loading (%s%s%s).", simpleName(n), msg[0] ? ": " : "", msg);
    else if (phase == 2)
        snprintf(text, sizeof text, "The game crashed while starting (%s%s%s).", simpleName(n), msg[0] ? ": " : "", msg);
    else
        snprintf(text, sizeof text, "The game crashed (%s%s%s).", simpleName(n), msg[0] ? ": " : "", msg);
    vm_problem(sev, "%s", text);
}
