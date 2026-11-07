/* PSP platform layer: video (GU), input, overlay menu, OSK text input, logging, entry point. */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspctrl.h>
#include <psprtc.h>
#include <psputility.h>
#include <psppower.h>
#include <psploadexec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <sys/stat.h>
#include "psp_common.h"
#include "../platform.h"
#include "../../vm/vm.h"

PSP_MODULE_INFO("PSPJ2ME", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(512);
PSP_HEAP_SIZE_KB(10 * 1024);  /* libc heap; the Java heap is allocated from the kernel (see plat_heap_alloc) */

extern const unsigned char g_bootJar[];
extern const unsigned int g_bootJarSize;
int vm_init(uint8_t *bootJar, size_t bootLen, size_t heapBytes);
int midp_launch(const char *jarPath, const char *midletClass);
int midp_load_descriptor(const char *jarPath);

Settings g_settings;
static int menuOpen, showControls;
static void drawMenu(void);
char g_baseDir[512];
static char dataDir[600];
static char gameCfgPath[700];
static FILE *logFile;
static char selfPath[512];

/* ---------------- logging / time ---------------- */
#define LOG_LIMIT (256 * 1024)
static long logBytes;
void plat_log(const char *s) {
    printf("%s", s);
    if (!logFile) return;
    if (logBytes > LOG_LIMIT) return;
    logBytes += (long)strlen(s);
    fputs(s, logFile);
    if (logBytes > LOG_LIMIT) fputs("\n[log size limit reached: further messages are not written]\n", logFile);
    fflush(logFile);
}

int64_t plat_time_ms(void) { return (int64_t)(sceKernelGetSystemTimeWide() / 1000); }
int64_t plat_time_us(void) { return (int64_t)sceKernelGetSystemTimeWide(); }

int64_t plat_wallclock_ms(void) {
    u64 tick = 0;
    sceRtcGetCurrentTick(&tick);   /* microseconds since 0001-01-01 UTC */
    return (int64_t)((tick - 62135596800000000ULL) / 1000ULL);
}

void plat_idle(int ms) { if (ms > 0) sceKernelDelayThread(ms * 1000); }
const char *plat_data_dir(void) { return dataDir; }
void plat_vibrate(int ms) { (void)ms; }

/* ---------------- Java heap memory ---------------- */
static SceUID heapUid = -1;
void *plat_heap_alloc(size_t bytes) {
    SceUID uid = sceKernelAllocPartitionMemory(2, "javaheap", PSP_SMEM_Low, bytes, NULL);
    if (uid < 0) return NULL;
    heapUid = uid;
    return sceKernelGetBlockHeadAddr(uid);
}
void plat_heap_free(void *p) {
    (void)p;
    if (heapUid >= 0) { sceKernelFreePartitionMemory(heapUid); heapUid = -1; }
}

/* ---------------- exit callback ---------------- */
static int exitCallback(int a1, int a2, void *common) {
    (void)a1; (void)a2; (void)common;
    /* don't touch files here: the main thread may be inside stdio and hold its lock */
    extern volatile int g_audioMute;
    g_audioMute = 1;                  /* fade out cleanly instead of cutting audio mid-buffer */
    sceKernelDelayThread(60 * 1000);
    sceKernelExitGame();
    return 0;
}

/* ---------------- watchdog ---------------- */
extern volatile uint32_t g_schedTicks;
extern uint32_t g_framesPresented;
extern int64_t g_lastPresentMs;
void vm_dump_threads(const char *why);
static volatile int vmRunning;
static int watchdogThread(SceSize args, void *argp) {
    (void)args; (void)argp;
    uint32_t lastTicks = 0;
    int stuckSecs = 0, reportedStuck = 0, reportedNoFrames = 0;
    for (;;) {
        sceKernelDelayThread(1000 * 1000);
        if (!vmRunning) { stuckSecs = 0; reportedStuck = reportedNoFrames = 0; continue; }
        uint32_t t = g_schedTicks;
        stuckSecs = (t == lastTicks) ? stuckSecs + 1 : 0;
        lastTicks = t;
        if (stuckSecs == 5 && !reportedStuck) {
            reportedStuck = 1;
            vm_log("WATCHDOG: the VM has not run its scheduler for 5 s (stuck inside native code or a system call)\n");
            vm_dump_threads("watchdog: VM stuck");
        }
        if (!reportedNoFrames && plat_time_ms() - g_lastPresentMs > 10000) {
            reportedNoFrames = 1;
            vm_log("WATCHDOG: nothing drawn for 10 s (%u frames so far)\n", (unsigned)g_framesPresented);
            vm_dump_threads("watchdog: no frames");
        }
        if (plat_time_ms() - g_lastPresentMs < 1000) reportedNoFrames = 0;
    }
    return 0;
}
static int callbackThread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int cbid = sceKernelCreateCallback("Exit Callback", exitCallback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}
static void setupCallbacks(void) {
    int thid = sceKernelCreateThread("update_thread", callbackThread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);
    int wd = sceKernelCreateThread("watchdog", watchdogThread, 0x18, 0x4000, PSP_THREAD_ATTR_USER, 0);
    if (wd >= 0) sceKernelStartThread(wd, 0, 0);
}

/* ---------------- settings ---------------- */
void settings_defaults(Settings *s) {
    memset(s, 0, sizeof *s);
    s->screenW = 240; s->screenH = 320;
    s->scale = SCALE_FIT;
    s->filter = 1;
    s->vsync = 0;
    s->cpu333 = 1;
    s->fullCanvas = 1;
    s->sleepScale = 100;
    s->shoulderCombos = 1;
    /* D-pad = 2/4/6/8, stick = arrows, SELECT/START = soft keys, X=5, O=0, Square=*, Triangle=#, L=1, R=3 */
    s->keys[B_UP] = '2'; s->keys[B_DOWN] = '8'; s->keys[B_LEFT] = '4'; s->keys[B_RIGHT] = '6';
    s->keys[B_CROSS] = '5'; s->keys[B_CIRCLE] = '0'; s->keys[B_SQUARE] = '*'; s->keys[B_TRIANGLE] = '#';
    s->keys[B_L] = '1'; s->keys[B_R] = '3'; s->keys[B_SELECT] = JK_SOFT1; s->keys[B_START] = JK_SOFT2;
    s->keys[B_ANALOG_UP] = JK_UP; s->keys[B_ANALOG_DOWN] = JK_DOWN; s->keys[B_ANALOG_LEFT] = JK_LEFT; s->keys[B_ANALOG_RIGHT] = JK_RIGHT;
}

static const char *btnKeys[B_COUNT] = { "up", "down", "left", "right", "cross", "circle", "square", "triangle", "l", "r", "select", "start",
                                        "analog_up", "analog_down", "analog_left", "analog_right" };
static const char *btnNames[B_COUNT] = { "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Cross", "Circle", "Square", "Triangle", "L", "R", "Select", "Start",
                                         "Stick up", "Stick down", "Stick left", "Stick right" };
const char *button_name(int b) { return btnNames[b]; }

const char *scale_name(int s) {
    static const char *n[SCALE_COUNT] = { "Fit (black bars)", "Stretch" };
    return s >= 0 && s < SCALE_COUNT ? n[s] : "?";
}

const char *key_name(int code) {
    static char buf[16];
    switch (code) {
        case JK_UP: return "Up"; case JK_DOWN: return "Down"; case JK_LEFT: return "Left"; case JK_RIGHT: return "Right";
        case JK_FIRE: return "Fire"; case JK_SOFT1: return "Soft L"; case JK_SOFT2: return "Soft R"; case JK_CLEAR: return "Clear";
        case 0: return "-";
    }
    if (code >= 32 && code < 127) { snprintf(buf, sizeof buf, "%c", code); return buf; }
    snprintf(buf, sizeof buf, "%d", code);
    return buf;
}

void settings_load(Settings *s, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    int keys[B_COUNT], haveKeys = 0, keyVer = 0;
    memcpy(keys, s->keys, sizeof keys);
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = line, *v = eq + 1;
        v[strcspn(v, "\r\n")] = 0;
        if (!strcmp(k, "screen")) sscanf(v, "%dx%d", &s->screenW, &s->screenH);
        else if (!strcmp(k, "scale")) s->scale = atoi(v);
        else if (!strcmp(k, "filter")) s->filter = atoi(v);
        else if (!strcmp(k, "fps")) s->showFps = atoi(v);
        else if (!strcmp(k, "vsync")) s->vsync = atoi(v);
        else if (!strcmp(k, "cpu333")) s->cpu333 = atoi(v);
        else if (!strcmp(k, "fullcanvas")) s->fullCanvas = atoi(v);
        else if (!strcmp(k, "sleepscale")) s->sleepScale = atoi(v);
        else if (!strcmp(k, "shoulders")) s->shoulderCombos = atoi(v);
        else if (!strcmp(k, "keyver")) keyVer = atoi(v);
        else if (!strncmp(k, "key.", 4)) {
            int code = atoi(v);
            /* only codes the settings screen can produce: none, JK_* or a keypad key */
            int valid = (code >= JK_CLEAR && code <= 0) || (code >= '0' && code <= '9') || code == '*' || code == '#';
            for (int b = 0; b < B_COUNT; b++) if (valid && !strcmp(k + 4, btnKeys[b])) { keys[b] = code; haveKeys = 1; }
        }
    }
    fclose(f);
    /* key layouts saved by older versions (different defaults) are ignored */
    if (haveKeys && keyVer == 2) memcpy(s->keys, keys, sizeof keys);
    if (s->scale >= SCALE_COUNT) s->scale = SCALE_FIT;
    if (s->screenW < 64 || s->screenW > 512) s->screenW = 240;
    if (s->screenH < 64 || s->screenH > 512) s->screenH = 320;
    if (s->scale < 0 || s->scale >= SCALE_COUNT) s->scale = SCALE_FIT;  /* removed modes (rotate, 1:1) -> Fit */
    s->filter = !!s->filter; s->showFps = !!s->showFps; s->vsync = !!s->vsync;
    s->cpu333 = !!s->cpu333; s->shoulderCombos = !!s->shoulderCombos;
    if (s->sleepScale < 10 || s->sleepScale > 100) s->sleepScale = 100;
}

void settings_save(const Settings *s, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "keyver=2\nscreen=%dx%d\nscale=%d\nfilter=%d\nfps=%d\nvsync=%d\ncpu333=%d\nfullcanvas=%d\nsleepscale=%d\nshoulders=%d\n", s->screenW, s->screenH, s->scale, s->filter, s->showFps, s->vsync, s->cpu333, s->fullCanvas, s->sleepScale, s->shoulderCombos);
    for (int b = 0; b < B_COUNT; b++) fprintf(f, "key.%s=%d\n", btnKeys[b], s->keys[b]);
    fclose(f);
}

/* ---------------- video ---------------- */
#define BUF_WIDTH 512
static unsigned int __attribute__((aligned(16))) gulist[0x10000];
static uint32_t *gameTex;      /* 512x272 ABGR, launcher screen */
static uint16_t *gameTex16;    /* 512x512 RGB565, swizzled: the game screen */
static uint32_t *overlayTex;   /* 512x272 ABGR */
static uint32_t *overlayArgb;  /* 480x272 ARGB working buffer */
static int texW = 240, texH = 320;
static int overlayVisible;
static int frameCount, fps;
static int64_t fpsTime;
static int fpsShown = -1;

typedef struct { float u, v; float x, y, z; } Vtx;

void video_init(void) {
    gameTex = memalign(16, 512 * 512 * 4);
    gameTex16 = memalign(16, 512 * 512 * 2);
    memset(gameTex16, 0, 512 * 512 * 2);
    overlayTex = memalign(16, 512 * 272 * 4);
    overlayArgb = memalign(16, SCR_W * SCR_H * 4);
    memset(gameTex, 0, 512 * 512 * 4);
    memset(overlayTex, 0, 512 * 272 * 4);
    memset(overlayArgb, 0, SCR_W * SCR_H * 4);

    void *fbp0 = (void *)0;
    void *fbp1 = (void *)(BUF_WIDTH * SCR_H * 4);
    sceGuInit();
    sceGuStart(GU_DIRECT, gulist);
    sceGuDrawBuffer(GU_PSM_8888, fbp0, BUF_WIDTH);
    sceGuDispBuffer(SCR_W, SCR_H, fbp1, BUF_WIDTH);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuShadeModel(GU_SMOOTH);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

static inline uint32_t argb2abgr(uint32_t p) {
    uint32_t b = __builtin_bswap32(p);   /* BBGGRRAA */
    return (b >> 8) | (b << 24);         /* AABBGGRR */
}

/* GPU work is not waited for right after submitting a frame: the next frame (or the input poll)
   completes it, so the CPU keeps running the game while the GPU scales the picture. */
static int gpuPending;
static int64_t presConvUs, presGpuUs, presStatT0;
static int presFrames;
static void gpuFinish(void) {
    if (!gpuPending) return;
    int64_t a = sceKernelGetSystemTimeWide();
    sceGuSync(0, 0);
    if (g_settings.vsync) sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
    gpuPending = 0;
    presGpuUs += sceKernelGetSystemTimeWide() - a;
}

void video_upload_game(const uint32_t *argb, int w, int h, int stride) {
    if (w > 512) w = 512;
    if (h > 512) h = 512;
    texW = w; texH = h;
    gpuFinish();   /* the GPU must be done reading the texture before we overwrite it */
    int64_t a = sceKernelGetSystemTimeWide();
    /* ARGB8888 -> RGB565 (PSP order: R in the low bits), written in the GPU's swizzled layout
       (blocks of 8x8 pixels for 16-bit textures), which the GPU reads much faster. */
    for (int y = 0; y < h; y++) {
        const uint32_t *src = argb + y * stride;
        uint16_t *rowBase = gameTex16 + (y >> 3) * (512 * 8) + (y & 7) * 8;
        int x = 0;
#define TO565(p) ((((p) >> 19) & 0x1f) | (((p) >> 5) & 0x7e0) | (((p) << 8) & 0xf800))
        for (; x + 8 <= w; x += 8) {
            uint32_t *d = (uint32_t *)(rowBase + (x >> 3) * 64);   /* two pixels per store */
            const uint32_t *sp = src + x;
            d[0] = TO565(sp[0]) | (TO565(sp[1]) << 16);
            d[1] = TO565(sp[2]) | (TO565(sp[3]) << 16);
            d[2] = TO565(sp[4]) | (TO565(sp[5]) << 16);
            d[3] = TO565(sp[6]) | (TO565(sp[7]) << 16);
        }
        for (; x < w; x++) {
            uint32_t p = src[x];
            rowBase[(x >> 3) * 64 + (x & 7)] = (uint16_t)(((p >> 19) & 0x1f) | ((p >> 5) & 0x7e0) | ((p << 8) & 0xf800));
        }
    }
    sceKernelDcacheWritebackRange(gameTex16, 512 * ((h + 7) & ~7) * 2);
    presConvUs += sceKernelGetSystemTimeWide() - a;
    if (++presFrames >= 120) {
        int64_t now = sceKernelGetSystemTimeWide();
        if (presStatT0 && g_settings.showFps)
            vm_log("screen: convert %.1f ms/frame, gpu+swap %.1f ms/frame (vsync %s, display %s)\n",
                   presConvUs / 1000.0 / presFrames, presGpuUs / 1000.0 / presFrames, g_settings.vsync ? "on" : "off", scale_name(g_settings.scale));
        presStatT0 = now; presConvUs = presGpuUs = 0; presFrames = 0;
    }
}

uint32_t *video_overlay_buffer(void) { return overlayArgb; }

void video_overlay_changed(int visible) {
    overlayVisible = visible;
    if (!visible) {
        /* wipe the whole layer: the FPS counter reuses it and only redraws its own corner */
        memset(overlayTex, 0, 512 * 272 * 4);
        sceKernelDcacheWritebackRange(overlayTex, 512 * 272 * 4);
        fpsShown = -1;
        return;
    }
    for (int y = 0; y < SCR_H; y++) {
        const uint32_t *s = overlayArgb + y * SCR_W;
        uint32_t *d = overlayTex + y * 512;
        for (int x = 0; x < SCR_W; x++) d[x] = argb2abgr(s[x]);
    }
    sceKernelDcacheWritebackRange(overlayTex, 512 * 272 * 4);
}

/* draw texture region (tw x th) into dest rect, optionally rotated */
static void drawTexFmt(const void *tex, int psm, int swizzle, int tbw, int texPow2W, int texPow2H, int tw, int th,
                    float dx, float dy, float dw, float dh, int rot, int linear) {
    sceGuTexMode(psm, 0, 0, swizzle);
    sceGuTexImage(0, texPow2W, texPow2H, tbw, tex);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(linear ? GU_LINEAR : GU_NEAREST, linear ? GU_LINEAR : GU_NEAREST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexFlush();
    if (rot == 0) {
        /* vertical strips keep the texture cache happy */
        int strips = (tw + 63) / 64;
        Vtx *v = sceGuGetMemory(strips * 2 * sizeof(Vtx));
        for (int i = 0; i < strips; i++) {
            float u0 = i * 64, u1 = (i + 1) * 64 > tw ? tw : (i + 1) * 64;
            v[i * 2].u = u0; v[i * 2].v = 0;
            v[i * 2].x = dx + dw * u0 / tw; v[i * 2].y = dy; v[i * 2].z = 0;
            v[i * 2 + 1].u = u1; v[i * 2 + 1].v = th;
            v[i * 2 + 1].x = dx + dw * u1 / tw; v[i * 2 + 1].y = dy + dh; v[i * 2 + 1].z = 0;
        }
        sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D, strips * 2, 0, v);
    } else {
        Vtx *v = sceGuGetMemory(4 * sizeof(Vtx));
        /* screen corners: TL, TR, BL, BR */
        float xs[4] = { dx, dx + dw, dx, dx + dw }, ys[4] = { dy, dy, dy + dh, dy + dh };
        float us[4], vs[4];
        if (rot == 1) { /* clockwise: texture top -> screen right */
            us[0] = 0; vs[0] = th; us[1] = 0; vs[1] = 0; us[2] = tw; vs[2] = th; us[3] = tw; vs[3] = 0;
        } else {        /* counter-clockwise: texture top -> screen left */
            us[0] = tw; vs[0] = 0; us[1] = tw; vs[1] = th; us[2] = 0; vs[2] = 0; us[3] = 0; vs[3] = th;
        }
        for (int i = 0; i < 4; i++) { v[i].u = us[i]; v[i].v = vs[i]; v[i].x = xs[i]; v[i].y = ys[i]; v[i].z = 0; }
        sceGuDrawArray(GU_TRIANGLE_STRIP, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 4, 0, v);
    }
}

static void drawTex(uint32_t *tex, int tbw, int texPow2W, int texPow2H, int tw, int th,
                    float dx, float dy, float dw, float dh, int rot, int linear) {
    drawTexFmt(tex, GU_PSM_8888, 0, tbw, texPow2W, texPow2H, tw, th, dx, dy, dw, dh, rot, linear);
}
static void drawGameTex(float dx, float dy, float dw, float dh, int linear) {
    int pw = 8, ph = 8;
    while (pw < texW) pw <<= 1;
    while (ph < texH) ph <<= 1;
    drawTexFmt(gameTex16, GU_PSM_5650, 1, 512, pw, ph, texW, texH, dx, dy, dw, dh, 0, linear);
}

static void gameRect(float *x, float *y, float *w, float *h, int *rot) {
    int gw = texW, gh = texH;
    *rot = 0;
    switch (g_settings.scale) {
        case SCALE_STRETCH: *w = SCR_W; *h = SCR_H; break;
        default: {
            float s = (float)SCR_H / gh;
            if (gw * s > SCR_W) s = (float)SCR_W / gw;
            *w = gw * s; *h = gh * s;
        }
    }
    *x = (SCR_W - *w) / 2;
    *y = (SCR_H - *h) / 2;
}

static void fpsOverlay(void);

void video_frame(void) {
    gpuFinish();
    frameCount++;
    int64_t now = plat_time_ms();
    if (now - fpsTime >= 1000) { fps = frameCount; frameCount = 0; fpsTime = now; }
    if (g_settings.showFps && fps != fpsShown && !overlayVisible) fpsOverlay();

    sceGuStart(GU_DIRECT, gulist);
    sceGuClearColor(0xff000000);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    sceGuDisable(GU_BLEND);
    float x, y, w, h;
    int rot;
    gameRect(&x, &y, &w, &h, &rot);
    int pw = 1, ph = 1;
    while (pw < texW) pw <<= 1;
    while (ph < texH) ph <<= 1;
    int linear = g_settings.filter && !(w == texW && h == texH);
    (void)pw; (void)ph; (void)rot;
    drawGameTex(x, y, w, h, linear);
    if (overlayVisible || (g_settings.showFps && fpsShown >= 0)) {
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
        drawTex(overlayTex, 512, 512, 512, SCR_W, SCR_H, 0, 0, SCR_W, SCR_H, 0, 0);
        sceGuDisable(GU_BLEND);
    }
    sceGuFinish();
    gpuPending = 1;
}

void video_draw_launcher(const uint32_t *argb) {
    gpuFinish();
    for (int y = 0; y < SCR_H; y++) {
        const uint32_t *s = argb + y * SCR_W;
        uint32_t *d = gameTex + y * 512;
        for (int x = 0; x < SCR_W; x++) d[x] = argb2abgr(s[x]) | 0xff000000u;
    }
    sceKernelDcacheWritebackRange(gameTex, 512 * SCR_H * 4);
    sceGuStart(GU_DIRECT, gulist);
    sceGuClearColor(0xff000000);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    drawTex(gameTex, 512, 512, 512, SCR_W, SCR_H, 0, 0, SCR_W, SCR_H, 0, 0);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

/* For utility dialogs (OSK): draws the last game frame, leaves the GU list open for the dialog. */
void video_begin_dialog_frame(void) {
    gpuFinish();
    sceGuStart(GU_DIRECT, gulist);
    sceGuClearColor(0xff000000);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    float x, y, w, h;
    int rot;
    gameRect(&x, &y, &w, &h, &rot);
    int pw = 1, ph = 1;
    while (pw < texW) pw <<= 1;
    while (ph < texH) ph <<= 1;
    (void)pw; (void)ph; (void)rot;
    drawGameTex(x, y, w, h, 1);
    sceGuFinish();
    sceGuSync(0, 0);
}
void video_end_dialog_frame(void) {
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

/* ---------------- ui helpers ---------------- */
void ui_surface(Surface *s, uint32_t *px) {
    s->px = px; s->w = s->stride = SCR_W; s->h = SCR_H;
    s->cx1 = 0; s->cy1 = 0; s->cx2 = SCR_W; s->cy2 = SCR_H;
}

static int utf8to16(const char *in, uint16_t *out, int max) {
    int n = 0;
    const unsigned char *p = (const unsigned char *)in;
    while (*p && n < max) {
        uint32_t c = *p++;
        if (c >= 0xc0 && c < 0xe0 && *p) { c = ((c & 0x1f) << 6) | (*p++ & 0x3f); }
        else if (c >= 0xe0 && c < 0xf0 && p[0] && p[1]) { c = ((c & 0x0f) << 12) | ((p[0] & 0x3f) << 6) | (p[1] & 0x3f); p += 2; }
        out[n++] = (uint16_t)c;
    }
    return n;
}

void ui_text(Surface *s, int x, int y, const char *utf8, uint32_t argb, int size, int bold) {
    uint16_t buf[256];
    int n = utf8to16(utf8, buf, 256);
    gfx_draw_text(s, gfx_font(size, bold), buf, n, x, y, argb, 0);
}

int ui_text_width(const char *utf8, int size, int bold) {
    uint16_t buf[256];
    int n = utf8to16(utf8, buf, 256);
    return gfx_text_width(gfx_font(size, bold), buf, n);
}

/* ---------------- controls panel ---------------- */
void draw_controls(Surface *s, const Settings *st, const char *title) {
    int w = 460, h = 260, x = (SCR_W - w) / 2, y = (SCR_H - h) / 2;
    gfx_fill_rect(s, x, y, w, h, 0xf0182030);
    gfx_rect(s, x, y, w - 1, h - 1, 0xff6080c0, 0);
    ui_text(s, x + 12, y + 5, title, 0xffffffff, 1, 1);
    static const int order[] = { B_ANALOG_UP, B_ANALOG_DOWN, B_ANALOG_LEFT, B_ANALOG_RIGHT, B_UP, B_DOWN, B_LEFT, B_RIGHT,
                                 B_CROSS, B_CIRCLE, B_SQUARE, B_TRIANGLE, B_L, B_R, B_SELECT, B_START };
    int n = (int)(sizeof order / sizeof order[0]);
    char line[128];
    for (int i = 0; i < n; i++) {
        int col = i / 8, row = i % 8;
        int lx = x + 14 + col * 224, ly = y + 26 + row * 17;
        const char *bn = button_name(order[i]);
        ui_text(s, lx, ly, bn, 0xffb0c4e8, 0, 0);
        if (st->shoulderCombos && (order[i] == B_L || order[i] == B_R)) snprintf(line, sizeof line, "shift (hold)");
        else snprintf(line, sizeof line, "%s", key_name(st->keys[order[i]]));
        ui_text(s, lx + 96, ly, line, 0xffffffff, 0, 1);
    }
    int cy = y + 26 + 8 * 17 + 6;
    gfx_line(s, x + 10, cy - 3, x + w - 11, cy - 3, 0xff405070, 0);
    if (st->shoulderCombos) {
        /* key_name() returns a shared buffer: copy each name before formatting */
        char kn[2][KM_COMBO_KEYS][16], l1[96], l2[96];
        for (int k = 0; k < KM_COMBO_KEYS; k++) {
            snprintf(kn[0][k], 16, "%s", key_name(km_comboL[k]));
            snprintf(kn[1][k], 16, "%s", key_name(km_comboR[k]));
        }
        snprintf(l1, sizeof l1, "Hold L:  D-pad up = %s   D-pad down = %s   X = %s   O = %s", kn[0][0], kn[0][1], kn[0][2], kn[0][3]);
        snprintf(l2, sizeof l2, "Hold R:  D-pad up = %s   D-pad down = %s   X = %s   O = %s", kn[1][0], kn[1][1], kn[1][2], kn[1][3]);
        ui_text(s, x + 14, cy, l1, 0xffffffff, 0, 0);
        ui_text(s, x + 14, cy + 16, l2, 0xffffffff, 0, 0);
    } else {
        ui_text(s, x + 14, cy, "Shoulder combos are off for this game: L and R are plain keys.", 0xffffffff, 0, 0);
    }
    ui_text(s, x + 14, y + h - 38, "START + SELECT together: PSPJ2ME menu (send any key, display, exit)", 0xffffd060, 0, 0);
    ui_text(s, x + 14, y + h - 21, "Change keys: Triangle on the game in the game list.  Press any button to close.", 0xffb0b0b0, 0, 0);
}

/* ---------------- in-game overlay menu ---------------- */
static int menuSel;
static int keypadSel;
static int pausedForOverlay;
static int javaOverlay;
static const int keypadCodes[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9', '*', '0', '#', JK_SOFT1, JK_SOFT2, JK_FIRE, JK_CLEAR };
#define NKEYPAD ((int)(sizeof(keypadCodes) / sizeof(keypadCodes[0])))
static int userExit;   /* the player chose "Exit game" */
enum { MI_RESUME = 0, MI_CONTROLS, MI_KEYPAD, MI_SCALE, MI_FILTER, MI_FPS, MI_VSYNC, MI_EXIT, MI_COUNT };
static int pendingRelease, pendingReleaseAt;

void plat_overlay_active(int active) { javaOverlay = active; }

static void fpsOverlay(void) {
    memset(overlayArgb, 0, SCR_W * SCR_H * 4);
    Surface s;
    ui_surface(&s, overlayArgb);
    char b[32];
    snprintf(b, sizeof b, "%d fps", fps);
    gfx_fill_rect(&s, 0, 0, 52, 16, 0x80000000);
    ui_text(&s, 3, 1, b, 0xffffff00, 0, 1);
    fpsShown = fps;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 52; x++) overlayTex[y * 512 + x] = argb2abgr(overlayArgb[y * SCR_W + x]);
    sceKernelDcacheWritebackRange(overlayTex, 512 * 16 * 4);
}

static void drawMenu(void) {
    memset(overlayArgb, 0, SCR_W * SCR_H * 4);
    Surface s;
    ui_surface(&s, overlayArgb);
    int mw = 300, mh = 30 + MI_COUNT * 22 + 8, mx = (SCR_W - mw) / 2, my = (SCR_H - mh) / 2;
    gfx_fill_rect(&s, 0, 0, SCR_W, SCR_H, 0x80000000);
    if (showControls) { draw_controls(&s, &g_settings, "Controls"); video_overlay_changed(1); return; }
    gfx_fill_rect(&s, mx, my, mw, mh, 0xf0202838);
    gfx_rect(&s, mx, my, mw - 1, mh - 1, 0xff6080c0, 0);
    ui_text(&s, mx + 10, my + 6, "PSPJ2ME menu", 0xffffffff, 1, 1);
    char line[128];
    for (int i = 0; i < MI_COUNT; i++) {
        int y = my + 30 + i * 22;
        if (i == menuSel) gfx_fill_rect(&s, mx + 4, y - 2, mw - 8, 21, 0xff3060c0);
        switch (i) {
            case MI_RESUME: snprintf(line, sizeof line, "Resume"); break;
            case MI_CONTROLS: snprintf(line, sizeof line, "Controls (button list)"); break;
            case MI_KEYPAD: snprintf(line, sizeof line, "Send key:  < %s >   (X sends)", key_name(keypadCodes[keypadSel])); break;
            case MI_SCALE: snprintf(line, sizeof line, "Display:  < %s >", scale_name(g_settings.scale)); break;
            case MI_FILTER: snprintf(line, sizeof line, "Smooth filter:  %s", g_settings.filter ? "On" : "Off"); break;
            case MI_FPS: snprintf(line, sizeof line, "Show FPS:  %s", g_settings.showFps ? "On" : "Off"); break;
            case MI_VSYNC: snprintf(line, sizeof line, "VSync:  %s", g_settings.vsync ? "On" : "Off"); break;
            case MI_EXIT: snprintf(line, sizeof line, "Exit game"); break;
        }
        ui_text(&s, mx + 12, y, line, 0xffffffff, 1, 0);
    }
    video_overlay_changed(1);
}

static void closeMenu(void) {
    menuOpen = 0;
    showControls = 0;
    video_overlay_changed(0);
    fpsShown = -1;
    video_frame();   /* redraw now: the game may not draw again by itself */
    if (gameCfgPath[0]) settings_save(&g_settings, gameCfgPath);
    if (pausedForOverlay) { midp_post_system(13); pausedForOverlay = 0; }
}

static void menuInput(unsigned pressed) {
    if (showControls) { showControls = 0; drawMenu(); video_frame(); return; }
    if (pressed & PSP_CTRL_CIRCLE) { closeMenu(); return; }
    if (pressed & PSP_CTRL_UP) menuSel = (menuSel + MI_COUNT - 1) % MI_COUNT;
    if (pressed & PSP_CTRL_DOWN) menuSel = (menuSel + 1) % MI_COUNT;
    int dir = (pressed & PSP_CTRL_RIGHT) ? 1 : (pressed & PSP_CTRL_LEFT) ? -1 : 0;
    if (dir) {
        if (menuSel == MI_KEYPAD) keypadSel = (keypadSel + NKEYPAD + dir) % NKEYPAD;
        else if (menuSel == MI_SCALE) g_settings.scale = (g_settings.scale + SCALE_COUNT + dir) % SCALE_COUNT;
        else if (menuSel == MI_FILTER) g_settings.filter ^= 1;
        else if (menuSel == MI_FPS) g_settings.showFps ^= 1;
        else if (menuSel == MI_VSYNC) g_settings.vsync ^= 1;
    }
    if (pressed & PSP_CTRL_CROSS) {
        switch (menuSel) {
            case MI_RESUME: closeMenu(); return;
            case MI_CONTROLS: showControls = 1; break;
            case MI_KEYPAD:
                closeMenu();
                midp_post_key(KEY_PRESS, keypadCodes[keypadSel]);
                pendingRelease = keypadCodes[keypadSel];
                pendingReleaseAt = (int)plat_time_ms() + 120;
                return;
            case MI_FILTER: g_settings.filter ^= 1; break;
            case MI_FPS: g_settings.showFps ^= 1; break;
            case MI_VSYNC: g_settings.vsync ^= 1; break;
            case MI_SCALE: g_settings.scale = (g_settings.scale + 1) % SCALE_COUNT; break;
            case MI_EXIT:
                closeMenu();
                userExit = 1;
                midp_request_exit();
                return;
        }
    }
    drawMenu();
    video_frame();
}


/* ---------------- developer test mode (autostart.txt) ----------------
 * jar=<path>            start this game directly (skip the launcher)
 * launcher=1            only show the launcher, screenshot it and quit
 * thenlauncher=1        after the game, show the launcher, screenshot it and quit
 * quit=<ms>             exit after ms
 * shots=<ms>,<ms>,...   save shot_<ms>.ppm of the last frame
 * keys=<ms>:<code>,...  scripted key presses (code: number or up/down/left/right/fire/soft1/soft2/start) */
typedef struct { int at, code; int start; } TKey;
static TKey tkeys[128];
static int nTKeys, tkeyPos;
static int tShots[32], nTShots, tShotPos;
static int tQuit, tQuitOrig;
static int64_t tStart;
static int testMode;
static char tMain[128];
static int tRelaunch;
static int tThenLauncher;
static int tControls;
static char tJar[600];
static uint32_t *lastFrame;
static int lastW, lastH;

static void savePPM(const char *path, const uint32_t *px, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    unsigned char *row = malloc(w * 3);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) { uint32_t c = px[y * w + x]; row[x * 3] = c >> 16; row[x * 3 + 1] = c >> 8; row[x * 3 + 2] = c; }
        fwrite(row, 1, w * 3, f);
    }
    free(row);
    fclose(f);
}

static int parseKeyName(const char *k) {
    if (!strcmp(k, "up")) return JK_UP;
    if (!strcmp(k, "down")) return JK_DOWN;
    if (!strcmp(k, "left")) return JK_LEFT;
    if (!strcmp(k, "right")) return JK_RIGHT;
    if (!strcmp(k, "fire")) return JK_FIRE;
    if (!strcmp(k, "soft1")) return JK_SOFT1;
    if (!strcmp(k, "soft2")) return JK_SOFT2;
    if (strlen(k) == 1) return k[0];
    return atoi(k);
}

static int loadTestConfig(char *jar, int jarLen, int *launcherOnly) {
    char path[600];
    snprintf(path, sizeof path, "%s/autostart.txt", g_baseDir);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[1024];
    *launcherOnly = 0;
    jar[0] = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *v = eq + 1;
        if (!strcmp(line, "jar")) snprintf(jar, jarLen, "%s", v);
        else if (!strcmp(line, "launcher")) *launcherOnly = atoi(v);
        else if (!strcmp(line, "main")) snprintf(tMain, sizeof tMain, "%s", v);
        else if (!strcmp(line, "relaunch")) tRelaunch = atoi(v);
        else if (!strcmp(line, "thenlauncher")) tThenLauncher = atoi(v);
        else if (!strcmp(line, "controls")) tControls = atoi(v);
        else if (!strcmp(line, "quit")) tQuit = tQuitOrig = atoi(v);
        else if (!strcmp(line, "shots")) {
            for (char *t = strtok(v, ","); t && nTShots < 32; t = strtok(NULL, ",")) tShots[nTShots++] = atoi(t);
        } else if (!strcmp(line, "keys")) {
            for (char *t = strtok(v, ","); t && nTKeys < 128; t = strtok(NULL, ",")) {
                char *c = strchr(t, ':');
                if (!c) continue;
                *c = 0;
                tkeys[nTKeys].at = atoi(t);
                tkeys[nTKeys].start = !strcmp(c + 1, "start");
                tkeys[nTKeys].code = parseKeyName(c + 1);
                nTKeys++;
            }
        }
    }
    fclose(f);
    testMode = 1;
    return 1;
}

static void testTick(void) {
    if (!testMode) return;
    int t = (int)(plat_time_ms() - tStart);
    while (tkeyPos < nTKeys && tkeys[tkeyPos].at <= t) {
        TKey *k = &tkeys[tkeyPos++];
        vm_log("[test] key %d at %d ms\n", k->code, t);
        midp_post_key(KEY_PRESS, k->code);
        midp_post_key(KEY_RELEASE, k->code);
    }
    if (tControls && tShotPos < nTShots && tShots[tShotPos] <= t && !menuOpen) {
        menuOpen = 1; showControls = 1; drawMenu(); video_frame();
    }
    while (tShotPos < nTShots && tShots[tShotPos] <= t) {
        char p[700];
        snprintf(p, sizeof p, "%s/shot_%d.ppm", g_baseDir, tShots[tShotPos]);
        if (lastFrame) { savePPM(p, lastFrame, lastW, lastH); vm_log("[test] saved %s (%dx%d)\n", p, lastW, lastH); }
        gpuFinish();   /* show the last submitted frame before capturing */
        sceIoDevctl("kemulator:", 0x20, NULL, 0, NULL, 0); /* PPSSPP: capture the real display */
        tShotPos++;
    }
    if (tQuit && t >= tQuit) { vm_log("[test] quit after %d ms\n", t); tQuit = 0; userExit = 1; midp_request_exit(); }
}

/* ---------------- input ---------------- */
static unsigned prevButtons;
static KeyMap g_km;
static void postKey(int type, int code) { midp_post_key(type, code); }

static const unsigned btnMask[B_COUNT] = {
    PSP_CTRL_UP, PSP_CTRL_DOWN, PSP_CTRL_LEFT, PSP_CTRL_RIGHT, PSP_CTRL_CROSS, PSP_CTRL_CIRCLE, PSP_CTRL_SQUARE,
    PSP_CTRL_TRIANGLE, PSP_CTRL_LTRIGGER, PSP_CTRL_RTRIGGER, PSP_CTRL_SELECT, PSP_CTRL_START, 0, 0, 0, 0 };

static void releaseAll(void) { keymap_release_all(&g_km, postKey); }

#define COMBO (PSP_CTRL_START | PSP_CTRL_SELECT)
static int64_t softPendingSince;   /* START/SELECT press waiting to see if it becomes the menu combo */
static int comboLatched;

void plat_poll(void) {
    gpuFinish();
    testTick();
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(&pad, 1);
    unsigned b = pad.Buttons;
    unsigned pressed = b & ~prevButtons;
    prevButtons = b;
    int64_t now = plat_time_ms();

    if (pendingRelease && (int)now >= pendingReleaseAt) { midp_post_key(KEY_RELEASE, pendingRelease); pendingRelease = 0; }

    if (menuOpen) {
        if ((b & COMBO) == COMBO) { if (!comboLatched) { comboLatched = 1; closeMenu(); } return; }
        comboLatched = 0;
        if (pressed) menuInput(pressed);
        return;
    }
    /* START + SELECT together opens the PSPJ2ME menu */
    if ((b & COMBO) == COMBO) {
        if (!comboLatched) {
            comboLatched = 1;
            softPendingSince = 0;
            releaseAll();
            menuOpen = 1;
            menuSel = 0;
            drawMenu();
            video_frame();
        }
        return;
    }
    if (comboLatched) { if (!(b & COMBO)) comboLatched = 0; else return; }
    /* hold back a lone START/SELECT briefly so the combo doesn't leak a soft-key press */
    if (pressed & COMBO) softPendingSince = now;
    int holdSoft = softPendingSince && now - softPendingSince < 100 && (b & COMBO);
    if (!(b & COMBO)) softPendingSince = 0;

    int state[B_COUNT];
    for (int i = 0; i < B_COUNT; i++) state[i] = btnMask[i] ? (b & btnMask[i]) != 0 : 0;
    state[B_ANALOG_UP] = pad.Ly < 40;
    state[B_ANALOG_DOWN] = pad.Ly > 215;
    state[B_ANALOG_LEFT] = pad.Lx < 40;
    state[B_ANALOG_RIGHT] = pad.Lx > 215;
    int suppress[B_COUNT] = { 0 };
    suppress[B_START] = suppress[B_SELECT] = holdSoft;
    keymap_update(&g_km, g_settings.keys, g_settings.shoulderCombos, state, suppress, now, postKey);
}

void plat_present(const uint32_t *argb, int w, int h, int stride) {

    if (testMode) {
        if (!lastFrame || lastW != w || lastH != h) { free(lastFrame); lastFrame = malloc(w * h * 4); lastW = w; lastH = h; }
        for (int y = 0; y < h; y++) memcpy(lastFrame + y * w, argb + y * stride, w * 4);
    }
    video_upload_game(argb, w, h, stride);
    if (menuOpen) drawMenu();
    video_frame();
}

/* ---------------- OSK ---------------- */
static int utf16len(const uint16_t *s) { int n = 0; while (s[n]) n++; return n; }

int plat_edit_text(const char *title, const char *initial, char *out, int outSize, int maxChars, int constraints) {
    static uint16_t desc[128], intext[1024], outtext[1024];
    memset(desc, 0, sizeof desc); memset(intext, 0, sizeof intext); memset(outtext, 0, sizeof outtext);
    utf8to16(title, desc, 127);
    utf8to16(initial, intext, 1023);
    if (maxChars <= 0 || maxChars > 1023) maxChars = 1023;
    SceUtilityOskData data;
    memset(&data, 0, sizeof data);
    data.language = PSP_UTILITY_OSK_LANGUAGE_DEFAULT;
    data.lines = 1;
    data.unk_24 = 1;
    int c = constraints & 0xffff;
    data.inputtype = (c == 2 || c == 3 || c == 5) ? PSP_UTILITY_OSK_INPUTTYPE_LATIN_DIGIT : PSP_UTILITY_OSK_INPUTTYPE_ALL;
    data.desc = desc;
    data.intext = intext;
    data.outtextlength = 1024;
    data.outtextlimit = maxChars;
    data.outtext = outtext;
    SceUtilityOskParams params;
    memset(&params, 0, sizeof params);
    params.base.size = sizeof params;
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &params.base.language);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_UNKNOWN, &params.base.buttonSwap);
    params.base.graphicsThread = 17;
    params.base.accessThread = 19;
    params.base.fontThread = 18;
    params.base.soundThread = 16;
    params.datacount = 1;
    params.data = &data;
    releaseAll();
    if (sceUtilityOskInitStart(&params) < 0) return 0;
    int done = 0;
    while (!done) {
        video_begin_dialog_frame();
        switch (sceUtilityOskGetStatus()) {
            case PSP_UTILITY_DIALOG_INIT: break;
            case PSP_UTILITY_DIALOG_VISIBLE: sceUtilityOskUpdate(1); break;
            case PSP_UTILITY_DIALOG_QUIT: sceUtilityOskShutdownStart(); break;
            case PSP_UTILITY_DIALOG_NONE: done = 1; break;
            default: break;
        }
        video_end_dialog_frame();
    }
    prevButtons = 0xffffffff; /* ignore buttons still held from the dialog */
    if (data.result == PSP_UTILITY_OSK_RESULT_CANCELLED) return 0;
    /* utf16 -> utf8 */
    int n = utf16len(outtext), o = 0;
    for (int i = 0; i < n && o < outSize - 4; i++) {
        uint16_t ch = outtext[i];
        if (ch < 0x80) out[o++] = (char)ch;
        else if (ch < 0x800) { out[o++] = 0xc0 | (ch >> 6); out[o++] = 0x80 | (ch & 0x3f); }
        else { out[o++] = 0xe0 | (ch >> 12); out[o++] = 0x80 | ((ch >> 6) & 0x3f); out[o++] = 0x80 | (ch & 0x3f); }
    }
    out[o] = 0;
    return 1;
}

/* ---------------- main ---------------- */
static void baseName(const char *path, char *out, int len) {
    const char *s = strrchr(path, '/');
    snprintf(out, len, "%s", s ? s + 1 : path);
    char *dot = strrchr(out, '.');
    if (dot) *dot = 0;
}

int main(int argc, char **argv) {
    setupCallbacks();
    if (argc > 0 && argv[0]) {
        snprintf(selfPath, sizeof selfPath, "%s", argv[0]);
        snprintf(g_baseDir, sizeof g_baseDir, "%s", argv[0]);
        char *slash = strrchr(g_baseDir, '/');
        if (slash) *slash = 0;
    } else snprintf(g_baseDir, sizeof g_baseDir, "ms0:/PSP/GAME/PSPJ2ME");
    snprintf(dataDir, sizeof dataDir, "%s/data", g_baseDir);
    mkdir(dataDir, 0777);
    char logPath[600];
    snprintf(logPath, sizeof logPath, "%s/pspj2me.log", g_baseDir);
    /* a fresh log each start; the previous run's log is kept as pspj2me.prev.log */
    {
        char prevPath[600];
        snprintf(prevPath, sizeof prevPath, "%s/pspj2me.prev.log", g_baseDir);
        remove(prevPath);
        rename(logPath, prevPath);
        logFile = fopen(logPath, "w");
        if (logFile) {
            ScePspDateTime now;
            sceRtcGetCurrentClockLocalTime(&now);
            fprintf(logFile, "PSPJ2ME " PSPJ2ME_VERSION " log %04d-%02d-%02d %02d:%02d:%02d\n",
                    now.year, now.month, now.day, now.hour, now.minute, now.second);
            fflush(logFile);
        }
    }

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    char globalCfg[600];
    snprintf(globalCfg, sizeof globalCfg, "%s/settings.ini", dataDir);
    settings_defaults(&g_settings);
    settings_load(&g_settings, globalCfg);
    if (g_settings.cpu333) scePowerSetClockFrequency(333, 333, 166);
    vm_log("CPU clock %d MHz, bus %d MHz\n", scePowerGetCpuClockFrequency(), scePowerGetBusClockFrequency());

    video_init();
    audio_init();

    char jar[600];
    int launcherOnly = 0;
    tStart = plat_time_ms();
    jar[0] = 0;
    if (loadTestConfig(jar, sizeof jar, &launcherOnly) && launcherOnly) {
        launcher_run(NULL, 0);
        sceKernelExitGame();
        return 0;
    }
    extern void vm_shutdown(void);
    extern void mix_stop_all(void);
    snprintf(tJar, sizeof tJar, "%s", jar);
    for (;;) {
        /* fresh settings for every launch */
        settings_defaults(&g_settings);
        settings_load(&g_settings, globalCfg);
        gameCfgPath[0] = 0;
        if (!jar[0] && launcher_run(jar, sizeof jar) != 0) break;
        char bn[256];
        baseName(jar, bn, sizeof bn);
        snprintf(gameCfgPath, sizeof gameCfgPath, "%s/%s.ini", dataDir, bn);
        settings_load(&g_settings, gameCfgPath);
        midp_set_screen_size(g_settings.screenW, g_settings.screenH);
        extern void midp_set_full_canvas(int);
        midp_set_full_canvas(1);
        extern int g_sleepScale;
        if (g_settings.sleepScale < 10 || g_settings.sleepScale > 100) g_settings.sleepScale = 100;
        g_sleepScale = g_settings.sleepScale;
        vm_log("settings: screen %dx%d, display %s, filter %s, vsync %s, speed-up sleep %d%%, shoulder combos %s\n",
               g_settings.screenW, g_settings.screenH, scale_name(g_settings.scale), g_settings.filter ? "on" : "off",
               g_settings.vsync ? "on" : "off", g_settings.sleepScale, g_settings.shoulderCombos ? "on" : "off");
        extern int g_perfLog;
        g_perfLog = g_settings.showFps;

        /* Java heap: everything the kernel has left, minus a reserve for kernel threads/stacks */
        size_t kfree = sceKernelMaxFreeMemSize();
        size_t heap = kfree > 4 * 1024 * 1024 ? kfree - 1536 * 1024 : kfree / 2;
        if (heap > 48 * 1024 * 1024) heap = 48 * 1024 * 1024;
        vm_log("PSPJ2ME starting %s\nmemory: kernel free %u KB, total free %u KB, requesting Java heap %u KB\n", jar,
               (unsigned)(kfree / 1024), (unsigned)(sceKernelTotalFreeMemSize() / 1024), (unsigned)(heap / 1024));
        if (vm_init((uint8_t *)g_bootJar, g_bootJarSize, heap) != 0) {
            vm_log("VM init failed\n");
            vm_shutdown();
            jar[0] = 0;
            if (testMode) break;
            continue;
        }
        int rc;
        if (tMain[0]) {
            extern int vm_run_main(const char *, const char *, const char *, const char *);
            vm_add_classpath_file(jar);
            rc = vm_run_main(tMain, "main", "([Ljava/lang/String;)V", NULL);
        } else {
            extern int64_t g_lastPresentMs;
            extern uint32_t g_framesPresented;
            g_lastPresentMs = plat_time_ms();
            g_framesPresented = 0;
            vmRunning = 1;
            rc = midp_launch(jar, NULL);
        }
        vmRunning = 0;
        vm_log("MIDlet finished (rc=%d), returning to the game list\n", rc);
        {
            /* tell the player why they are back in the list if the game never got going */
            extern uint32_t g_framesPresented;
            if (rc == 1)
                launcher_set_notice(bn, "This file is not a J2ME game the emulator can start (damaged .jar, or no MIDlet in its manifest).");
            else if (g_framesPresented == 0 && !userExit)
                launcher_set_notice(bn, "The game stopped before showing anything. It may need an API PSPJ2ME does not support yet.");
            userExit = 0;
        }
        mix_stop_all();
        vm_shutdown();
        /* reset in-game UI/input state */
        menuOpen = 0;
        javaOverlay = 0;
        pendingRelease = 0;
        memset(&g_km, 0, sizeof g_km);
        prevButtons = 0xffffffff;
        comboLatched = 0;
        softPendingSince = 0;
        video_overlay_changed(0);
        fpsShown = -1;
        jar[0] = 0;
        if (testMode) {
            if (tRelaunch-- <= 0) {
                if (tThenLauncher) launcher_run(NULL, 0);
                break;
            }
            snprintf(jar, sizeof jar, "%s", tJar);
            tStart = plat_time_ms();
            tkeyPos = 0; tShotPos = 0;
            tQuit = tQuitOrig;
        }
    }
    audio_shutdown();
    if (logFile) { fclose(logFile); logFile = NULL; }
    sceKernelExitGame();
    return 0;
}
