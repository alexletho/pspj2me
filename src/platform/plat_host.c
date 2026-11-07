/* Host (Linux) platform layer: headless, used for testing.
 * - frames are optionally dumped as PPM screenshots
 * - key input is scripted with --keys "t:ms:code,..." */
#define _POSIX_C_SOURCE 200809L
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

static int64_t startMs;
static const char *shotDir;
static int shotEvery = 0;
static int frameNo;
static int64_t lastShot;
static int64_t quitAt;
static uint32_t *lastFrame; static int lastW, lastH;
static int presentCount;

typedef struct { int64_t at; int type; int code; int x, y; } ScriptEv;
static ScriptEv script[512];
static int nScript, scriptPos;

static int64_t mono(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int64_t plat_time_ms(void) { return mono() - startMs; }
int64_t plat_time_us(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000; }
int64_t plat_wallclock_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void plat_idle(int ms) { if (ms > 0) { struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&ts, NULL); } }

void plat_log(const char *s) { fputs(s, stdout); fflush(stdout); }

const char *plat_data_dir(void) {
    static char dir[512];
    if (!dir[0]) {
        const char *d = getenv("PSPJ2ME_DATA");
        snprintf(dir, sizeof dir, "%s", d ? d : "./j2me_data");
        mkdir(dir, 0755);
    }
    return dir;
}

void plat_vibrate(int ms) { (void)ms; }

static void savePPM(const char *path, const uint32_t *px, int w, int h, int stride) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t c = px[y * stride + x];
            fputc((c >> 16) & 255, f); fputc((c >> 8) & 255, f); fputc(c & 255, f);
        }
    fclose(f);
}

void host_save_last_frame(const char *path) {
    if (lastFrame) savePPM(path, lastFrame, lastW, lastH, lastW);
}

void plat_present(const uint32_t *argb, int w, int h, int stride) {
    presentCount++;
    if (!lastFrame || lastW != w || lastH != h) {
        free(lastFrame);
        lastFrame = malloc((size_t)w * h * 4);
        lastW = w; lastH = h;
    }
    for (int y = 0; y < h; y++) memcpy(lastFrame + y * w, argb + y * stride, (size_t)w * 4);
    int64_t now = plat_time_ms();
    if (shotDir && shotEvery > 0 && now - lastShot >= shotEvery) {
        char p[600];
        snprintf(p, sizeof p, "%s/frame_%04d.ppm", shotDir, frameNo++);
        savePPM(p, argb, w, h, stride);
        lastShot = now;
    }
}

int host_present_count(void) { return presentCount; }

void plat_poll(void) {
    int64_t now = plat_time_ms();
    while (scriptPos < nScript && script[scriptPos].at <= now) {
        ScriptEv *e = &script[scriptPos++];
        if (e->type == 9) {
            char p[600];
            snprintf(p, sizeof p, "%s/shot_%lld.ppm", shotDir ? shotDir : ".", (long long)e->at);
            host_save_last_frame(p);
            printf("[host] screenshot %s\n", p);
        } else if (e->type >= 4) midp_post_pointer(e->type - 3, e->x, e->y);
        else midp_post_key(e->type, e->code);
    }
    if (quitAt && now >= quitAt) {
        printf("[host] quit timeout reached (%d frames presented)\n", presentCount);
        midp_request_exit();
        quitAt = 0;
    }
}

/* script format: "ms:key[,ms:key...]" -> press at ms, release 100ms later.
 * key may be a number (keycode) or names up/down/left/right/fire/soft1/soft2, or 'shot' */
void host_parse_keys(const char *spec) {
    char *s = strdup(spec), *save = NULL;
    for (char *tok = strtok_r(s, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        char *colon = strchr(tok, ':');
        if (!colon) continue;
        *colon = 0;
        int64_t at = atoll(tok);
        const char *k = colon + 1;
        int code;
        if (!strcmp(k, "shot")) { script[nScript].at = at; script[nScript].type = 9; nScript++; continue; }
        if (!strcmp(k, "up")) code = JK_UP;
        else if (!strcmp(k, "down")) code = JK_DOWN;
        else if (!strcmp(k, "left")) code = JK_LEFT;
        else if (!strcmp(k, "right")) code = JK_RIGHT;
        else if (!strcmp(k, "fire")) code = JK_FIRE;
        else if (!strcmp(k, "soft1")) code = JK_SOFT1;
        else if (!strcmp(k, "soft2")) code = JK_SOFT2;
        else if (strlen(k) == 1 && !(k[0] >= '0' && k[0] <= '9')) code = k[0];
        else if (k[0] == '#' && k[1]) code = atoi(k + 1);
        else code = (strlen(k) == 1) ? k[0] : atoi(k);
        if (nScript + 2 > 512) break;
        script[nScript++] = (ScriptEv){ at, KEY_PRESS, code, 0, 0 };
        script[nScript++] = (ScriptEv){ at + 100, KEY_RELEASE, code, 0, 0 };
    }
    free(s);
    /* sort by time (insertion sort) */
    for (int i = 1; i < nScript; i++) {
        ScriptEv e = script[i];
        int j = i - 1;
        while (j >= 0 && script[j].at > e.at) { script[j + 1] = script[j]; j--; }
        script[j + 1] = e;
    }
}

void host_init(const char *shots, int every, int64_t quitMs) {
    startMs = mono();
    nScript = scriptPos = 0;
    lastShot = 0;
    shotDir = shots;
    shotEvery = every;
    quitAt = quitMs;
    if (shotDir) mkdir(shotDir, 0755);
}

int plat_edit_text(const char *title, const char *initial, char *out, int outSize, int maxChars, int constraints) {
    (void)maxChars; (void)constraints;
    const char *env = getenv("PSPJ2ME_TEXT");
    snprintf(out, outSize, "%s", env ? env : initial);
    printf("[host] edit text '%s': '%s' -> '%s'\n", title, initial, out);
    return 1;
}
#include "../audio/mixer.h"
static int64_t audioClock;
/* host: run the mixer in "real time" without output so durations behave like on the device */
static void hostAudioTick(void) {
    int64_t now = plat_time_ms();
    if (!audioClock) audioClock = now;
    int frames = (int)((now - audioClock) * 44100 / 1000);
    if (frames <= 0) return;
    audioClock += (int64_t)frames * 1000 / 44100;
    static int16_t buf[4096 * 2];
    while (frames > 0) { int n = frames > 4096 ? 4096 : frames; mix_render(buf, n); frames -= n; }
}
int plat_audio_available(void) { return 1; }
void plat_audio_poll(void) { hostAudioTick(); }
void plat_overlay_active(int a) { (void)a; }

void *plat_heap_alloc(size_t bytes) { return malloc(bytes); }
void plat_heap_free(void *p) { free(p); }
