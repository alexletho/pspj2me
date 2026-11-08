/* Natives for MIDP (lcdui, rms, media glue) and the MIDlet launcher. */
#include "../vm/vm.h"
#include "../vm/zip.h"
#include "../audio/mixer.h"
#include "../gfx/gfx.h"
#include "../platform/platform.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

int vm_run_main(const char *clsName, const char *method, const char *desc, const char *strArg);
extern int (*g_eventPendingHook)(void);
extern int64_t g_perfGfxUs, g_perfPresentUs;
extern uint32_t g_perfFrames;

/* ---------------- event queue ---------------- */
#define EVQ 256
typedef struct { int type, a, b; } Ev;
static Ev evq[EVQ];
static int evHead, evTail;
static volatile int evWake;
static int64_t exitDeadline;
static int scrW = 240, scrH = 320;
static int fullCanvas;

typedef struct { int code; int64_t next; int repeating; } Held;
static Held held[8];

static void push_ev(int type, int a, int b) {
    int n = (evTail + 1) % EVQ;
    if (n == evHead) return; /* full: drop */
    evq[evTail].type = type; evq[evTail].a = a; evq[evTail].b = b;
    evTail = n;
}

void midp_post_key(int type, int code) {
    int64_t now = vm_time_ms();
    if (type == KEY_PRESS) {
        for (int i = 0; i < 8; i++) if (held[i].code == code) held[i].code = 0;
        for (int i = 0; i < 8; i++) if (!held[i].code) { held[i].code = code; held[i].next = now + 450; held[i].repeating = 0; break; }
    } else if (type == KEY_RELEASE) {
        for (int i = 0; i < 8; i++) if (held[i].code == code) held[i].code = 0;
    }
    push_ev(type, code, 0);
}
void midp_post_pointer(int type, int x, int y) { push_ev(3 + type, x, y); }
void midp_post_system(int type) { push_ev(type, 0, 0); }
void midp_request_exit(void) {
    push_ev(11, 0, 0);
    if (!exitDeadline) exitDeadline = vm_time_ms() + 3000;
}
void midp_set_screen_size(int w, int h) { scrW = w; scrH = h; }
void midp_set_full_canvas(int on) { fullCanvas = on; }
static int keyProfile = 1;
/* the phone the game was built for: key codes, and microedition.platform for non-Nokia builds */
void midp_set_key_profile(int p) {
    extern void lang_set_platform(const char *name);
    static const char *const platforms[] = { "PSPJ2ME", "PSPJ2ME", "SIE-S65/50", "motorazrV3", "LG-KG800" };
    keyProfile = p >= 1 && p <= 4 ? p : 1;
    lang_set_platform(platforms[keyProfile]);
}

static int eventsPending(void) {
    int64_t now = vm_time_ms();
    for (int i = 0; i < 8; i++) {
        if (held[i].code && now >= held[i].next) {
            push_ev(KEY_REPEAT, held[i].code, 0);
            held[i].next = now + 90;
        }
    }
    if (exitDeadline && now > exitDeadline) { vm_log("exit timeout: forcing VM exit\n"); g_vmExit = 1; }
    return evHead != evTail || evWake;
}

/* ---------------- field offsets ---------------- */
static uint32_t G_buf, G_bw, G_bh, G_tx, G_ty, G_cx1, G_cy1, G_cx2, G_cy2, G_color, G_font, G_stroke;
static uint32_t F_sizeIdx, F_style;
static uint32_t I_pixels, I_width, I_height, I_mutable, I_alpha;
static int offsetsReady;

static uint32_t off(const char *cls, const char *f) {
    Class *c = load_class(cls);
    if (!c) vm_fatal("missing class %s", cls);
    Field *fl = find_field_by_name(c, f);
    if (!fl) vm_fatal("missing field %s.%s", cls, f);
    return fl->offset;
}

static void initOffsets(void) {
    if (offsetsReady) return;
    const char *G = "javax/microedition/lcdui/Graphics";
    G_buf = off(G, "buf"); G_bw = off(G, "bw"); G_bh = off(G, "bh"); G_tx = off(G, "tx"); G_ty = off(G, "ty");
    G_cx1 = off(G, "clipX1"); G_cy1 = off(G, "clipY1"); G_cx2 = off(G, "clipX2"); G_cy2 = off(G, "clipY2");
    G_color = off(G, "color"); G_font = off(G, "font"); G_stroke = off(G, "stroke");
    F_sizeIdx = off("javax/microedition/lcdui/Font", "sizeIdx");
    F_style = off("javax/microedition/lcdui/Font", "style");
    const char *I = "javax/microedition/lcdui/Image";
    I_pixels = off(I, "pixels"); I_width = off(I, "width"); I_height = off(I, "height");
    I_mutable = off(I, "mutable"); I_alpha = off(I, "alpha");
    offsetsReady = 1;
}

#define NPE(t) do { throw_exception(t, "java/lang/NullPointerException", NULL); return NAT_EXC; } while (0)
#define OOM(t) do { throw_exception(t, "java/lang/OutOfMemoryError", NULL); return NAT_EXC; } while (0)

static int surfaceOf(ref_t g, Surface *s) {
    initOffsets();
    ref_t buf = F_REF(g, G_buf);
    if (!buf) return 0;
    s->px = (uint32_t *)A_INT(buf);
    s->w = s->stride = F_INT(g, G_bw);
    s->h = F_INT(g, G_bh);
    s->cx1 = F_INT(g, G_cx1); s->cy1 = F_INT(g, G_cy1);
    s->cx2 = F_INT(g, G_cx2); s->cy2 = F_INT(g, G_cy2);
    if (s->cx1 < 0) s->cx1 = 0;
    if (s->cy1 < 0) s->cy1 = 0;
    if (s->cx2 > s->w) s->cx2 = s->w;
    if (s->cy2 > s->h) s->cy2 = s->h;
    return 1;
}

#define GSETUP() Surface S; ref_t g = (ref_t)a[0]; if (!surfaceOf(g, &S)) return NAT_OK; \
    int tx = F_INT(g, G_tx), ty = F_INT(g, G_ty); uint32_t col = (uint32_t)F_INT(g, G_color); int dotted = F_INT(g, G_stroke) == 1; (void)dotted; (void)col

static int G_drawLine(Thread *t, int32_t *a) { GSETUP(); gfx_line(&S, a[1] + tx, a[2] + ty, a[3] + tx, a[4] + ty, col, dotted); return NAT_OK; }
static int G_fillRect(Thread *t, int32_t *a) { GSETUP(); gfx_fill_rect(&S, a[1] + tx, a[2] + ty, a[3], a[4], col); return NAT_OK; }
static int G_drawRect(Thread *t, int32_t *a) { GSETUP(); gfx_rect(&S, a[1] + tx, a[2] + ty, a[3], a[4], col, dotted); return NAT_OK; }
static int G_drawRoundRect(Thread *t, int32_t *a) { GSETUP(); gfx_draw_round_rect(&S, a[1] + tx, a[2] + ty, a[3], a[4], a[5], a[6], col, dotted); return NAT_OK; }
static int G_fillRoundRect(Thread *t, int32_t *a) { GSETUP(); gfx_fill_round_rect(&S, a[1] + tx, a[2] + ty, a[3], a[4], a[5], a[6], col); return NAT_OK; }
static int G_fillArc(Thread *t, int32_t *a) { GSETUP(); gfx_fill_arc(&S, a[1] + tx, a[2] + ty, a[3], a[4], a[5], a[6], col); return NAT_OK; }
static int G_drawArc(Thread *t, int32_t *a) { GSETUP(); gfx_draw_arc(&S, a[1] + tx, a[2] + ty, a[3], a[4], a[5], a[6], col, dotted); return NAT_OK; }
static int G_fillTriangle(Thread *t, int32_t *a) {
    GSETUP();
    gfx_fill_triangle(&S, a[1] + tx, a[2] + ty, a[3] + tx, a[4] + ty, a[5] + tx, a[6] + ty, col);
    return NAT_OK;
}

static const BitmapFont *fontOf(ref_t font, int *style) {
    int sizeIdx = 1, st = 0;
    if (font) { sizeIdx = F_INT(font, F_sizeIdx); st = F_INT(font, F_style); }
    if (style) *style = st;
    return gfx_font(sizeIdx, st & 1);
}

/* drawText0(String s, char[] c, int off, int len, int x, int y, int anchor) */
static int G_drawText(Thread *t, int32_t *a) {
    GSETUP();
    ref_t str = (ref_t)a[1], chars = (ref_t)a[2];
    int offs = a[3], len = a[4], x = a[5], y = a[6], anchor = a[7];
    const uint16_t *p;
    if (str) p = string_chars(str) + offs;
    else if (chars) p = A_CHAR(chars) + offs;
    else NPE(t);
    int style;
    const BitmapFont *f = fontOf(F_REF(g, G_font), &style);
    if (anchor == 0) anchor = 16 | 4;
    if (anchor & (1 | 8)) {
        int w = gfx_text_width(f, p, len);
        if (anchor & 1) x -= w / 2; else x -= w;
    }
    if (anchor & 64) y -= f->ascent;
    else if (anchor & 32) y -= f->height;
    else if (anchor & 2) y -= f->height / 2;
    gfx_draw_text(&S, f, p, len, x + tx, y + ty, col, style & (FSTYLE_ITALIC | FSTYLE_UNDERLINED));
    return NAT_OK;
}

/* drawRegion0(Image src, xs, ys, w, h, transform, xd, yd, anchor) */
static int G_drawRegion(Thread *t, int32_t *a) {
    GSETUP();
    ref_t img = (ref_t)a[1];
    if (!img) NPE(t);
    ref_t px = F_REF(img, I_pixels);
    if (!px) return NAT_OK;
    int iw = F_INT(img, I_width);
    int xs = a[2], ys = a[3], w = a[4], h = a[5], tr = a[6], xd = a[7], yd = a[8], anchor = a[9];
    int dw = tr >= 4 ? h : w, dh = tr >= 4 ? w : h;
    if (anchor == 0) anchor = 16 | 4;
    if (anchor & 1) xd -= dw / 2; else if (anchor & 8) xd -= dw;
    if (anchor & 2) yd -= dh / 2; else if (anchor & 32) yd -= dh;
    gfx_blit(&S, (const uint32_t *)A_INT(px), iw, xs, ys, w, h, tr, xd + tx, yd + ty, F_INT(img, I_alpha) != 0);
    return NAT_OK;
}

/* drawRGB(int[] rgb, offset, scan, x, y, w, h, processAlpha) */
static int G_drawRGB(Thread *t, int32_t *a) {
    GSETUP();
    ref_t rgb = (ref_t)a[1];
    if (!rgb) NPE(t);
    int offs = a[2], scan = a[3], x = a[4], y = a[5], w = a[6], h = a[7], pa = a[8];
    if (w <= 0 || h <= 0) return NAT_OK;
    int64_t first = offs, last = offs + (int64_t)(h - 1) * scan + w - 1;
    int64_t lo = first < last ? first : last, hi = first < last ? last : first;
    if (offs < 0 || lo < 0 || hi >= A_LEN(rgb) || (offs + (int64_t)(h - 1) * scan) < 0) {
        throw_exception(t, "java/lang/ArrayIndexOutOfBoundsException", NULL); return NAT_EXC;
    }
    gfx_draw_rgb(&S, A_INT(rgb), offs, scan, x + tx, y + ty, w, h, pa);
    return NAT_OK;
}

static int G_fillPolygon(Thread *t, int32_t *a) {
    GSETUP();
    ref_t xs = (ref_t)a[1], ys = (ref_t)a[3];
    int xo = a[2], yo = a[4], n = a[5];
    if (!xs || !ys) NPE(t);
    if (n <= 0) return NAT_OK;
    if (xo < 0 || yo < 0 || xo + n > A_LEN(xs) || yo + n > A_LEN(ys)) { throw_exception(t, "java/lang/ArrayIndexOutOfBoundsException", NULL); return NAT_EXC; }
    int *px = malloc(n * sizeof(int)), *py = malloc(n * sizeof(int));
    for (int i = 0; i < n; i++) { px[i] = A_INT(xs)[xo + i] + tx; py[i] = A_INT(ys)[yo + i] + ty; }
    gfx_fill_polygon(&S, px, py, n, col);
    free(px); free(py);
    return NAT_OK;
}


/* timing wrappers for the profiler */
static int T_G_drawLine(Thread *t, int32_t *a) { int64_t s0 = plat_time_us(); int r = G_drawLine(t, a); g_perfGfxUs += plat_time_us() - s0; return r; }
static int T_G_fillRect(Thread *t, int32_t *a) { int64_t s0 = plat_time_us(); int r = G_fillRect(t, a); g_perfGfxUs += plat_time_us() - s0; return r; }
static int T_G_drawRect(Thread *t, int32_t *a) { int64_t s0 = plat_time_us(); int r = G_drawRect(t, a); g_perfGfxUs += plat_time_us() - s0; return r; }
static int T_G_drawRoundRect(Thread *t, int32_t *a) { int64_t s0 = plat_time_us(); int r = G_drawRoundRect(t, a); g_perfGfxUs += plat_time_us() - s0; return r; }
static int T_G_fillRoundRect(Thread *t, int32_t *a) { int64_t s0 = plat_time_us(); int r = G_fillRoundRect(t, a); g_perfGfxUs += plat_time_us() - s0; return r; }
static int T_G_fillArc(Thread *t, int32_t *a) { int64_t s0 = plat_time_us(); int r = G_fillArc(t, a); g_perfGfxUs += plat_time_us() - s0; return r; }
static int T_G_drawArc(Thread *t, int32_t *a) { int64_t s0 = plat_time_us(); int r = G_drawArc(t, a); g_perfGfxUs += plat_time_us() - s0; return r; }
static int T_G_fillTriangle(Thread *t, int32_t *a) { int64_t s0 = plat_time_us(); int r = G_fillTriangle(t, a); g_perfGfxUs += plat_time_us() - s0; return r; }
static int T_G_drawText(Thread *t, int32_t *a) { int64_t s0 = plat_time_us(); int r = G_drawText(t, a); g_perfGfxUs += plat_time_us() - s0; return r; }
static int T_G_drawRegion(Thread *t, int32_t *a) { int64_t s0 = plat_time_us(); int r = G_drawRegion(t, a); g_perfGfxUs += plat_time_us() - s0; return r; }
static int T_G_drawRGB(Thread *t, int32_t *a) { int64_t s0 = plat_time_us(); int r = G_drawRGB(t, a); g_perfGfxUs += plat_time_us() - s0; return r; }
static int T_G_fillPolygon(Thread *t, int32_t *a) { int64_t s0 = plat_time_us(); int r = G_fillPolygon(t, a); g_perfGfxUs += plat_time_us() - s0; return r; }

/* ---------------- Font ---------------- */
static int Font_height(Thread *t, int32_t *a) { (void)t; a[0] = gfx_font(a[0], 0)->height; return NAT_OK; }
static int Font_baseline(Thread *t, int32_t *a) { (void)t; a[0] = gfx_font(a[0], 0)->ascent; return NAT_OK; }
static int Font_charWidth(Thread *t, int32_t *a) {
    (void)t;
    uint16_t c = (uint16_t)a[2];
    a[0] = gfx_char_width(gfx_font(a[0], a[1] & 1), c);
    return NAT_OK;
}
static int Font_charsWidth(Thread *t, int32_t *a) {
    ref_t arr = (ref_t)a[2];
    if (!arr) NPE(t);
    a[0] = gfx_text_width(gfx_font(a[0], a[1] & 1), A_CHAR(arr) + a[3], a[4]);
    return NAT_OK;
}
static int Font_stringWidth(Thread *t, int32_t *a) {
    ref_t s = (ref_t)a[2];
    if (!s) NPE(t);
    a[0] = gfx_text_width(gfx_font(a[0], a[1] & 1), string_chars(s) + a[3], a[4]);
    return NAT_OK;
}

/* ---------------- Image ---------------- */
static int Image_loadResource(Thread *t, int32_t *a) {
    ref_t n = (ref_t)a[0];
    if (!n) NPE(t);
    char name[512];
    string_to_utf8(n, name, sizeof name);
    uint32_t len;
    uint8_t *data = vm_read_resource(name, &len);
    if (!data) { a[0] = 0; return NAT_OK; }
    ref_t arr = new_prim_array('B', len);
    if (!arr) { free(data); OOM(t); }
    memcpy(O_DATA(arr), data, len);
    free(data);
    a[0] = arr;
    return NAT_OK;
}

static int Image_decode(Thread *t, int32_t *a) {
    initOffsets();
    ref_t self = (ref_t)a[0], data = (ref_t)a[1];
    int offs = a[2], len = a[3];
    if (!data) NPE(t);
    const uint8_t *p = (const uint8_t *)A_BYTE(data) + offs;
    int w, h, alpha = 0;
    uint32_t *px = png_decode(p, len, &w, &h, &alpha);
    if (!px) { px = jpeg_decode(p, len, &w, &h); alpha = 0; }
    if (!px) { a[0] = 0; return NAT_OK; }
    ref_t arr = new_prim_array('I', w * h);
    if (!arr) { free(px); OOM(t); }
    memcpy(A_INT(arr), px, (size_t)w * h * 4);
    free(px);
    F_REF(self, I_pixels) = arr;
    F_INT(self, I_width) = w;
    F_INT(self, I_height) = h;
    F_INT(self, I_alpha) = alpha;
    F_INT(self, I_mutable) = 0;
    a[0] = 1;
    return NAT_OK;
}

static int Image_fill(Thread *t, int32_t *a) {
    ref_t px = (ref_t)a[0];
    if (!px) NPE(t);
    int32_t v = a[1], n = A_LEN(px);
    int32_t *p = A_INT(px);
    for (int32_t i = 0; i < n; i++) p[i] = v;
    return NAT_OK;
}

static int Image_blit(Thread *t, int32_t *a) {
    ref_t dst = (ref_t)a[0], src = (ref_t)a[2];
    if (!dst || !src) NPE(t);
    Surface S;
    S.px = (uint32_t *)A_INT(dst);
    S.w = S.stride = a[1];
    S.h = A_LEN(dst) / a[1];
    S.cx1 = 0; S.cy1 = 0; S.cx2 = S.w; S.cy2 = S.h;
    gfx_blit(&S, (const uint32_t *)A_INT(src), a[3], a[4], a[5], a[6], a[7], a[8], 0, 0, 0);
    return NAT_OK;
}

static int Image_fixAlpha(Thread *t, int32_t *a) {
    ref_t px = (ref_t)a[0];
    if (!px) NPE(t);
    int n = A_LEN(px);
    uint32_t *p = (uint32_t *)A_INT(px);
    int any = 0;
    if (!a[1]) { for (int i = 0; i < n; i++) p[i] |= 0xff000000u; }
    else for (int i = 0; i < n; i++) if ((p[i] >> 24) != 255) { any = 1; break; }
    a[0] = any;
    return NAT_OK;
}

/* ---------------- Native ---------------- */
static int N_pollEvent(Thread *t, int32_t *a) {
    ref_t out = (ref_t)a[0];
    if (!out) NPE(t);
    if (evHead == evTail) { a[0] = 0; return NAT_OK; }
    Ev e = evq[evHead];
    evHead = (evHead + 1) % EVQ;
    A_INT(out)[0] = e.type;
    A_INT(out)[1] = e.a;
    A_INT(out)[2] = e.b;
    a[0] = 1;
    return NAT_OK;
}
static int N_waitEvent(Thread *t, int32_t *a) {
    if (evHead != evTail) return NAT_OK;
    if (evWake) { evWake = 0; return NAT_OK; }
    t->state = TH_EVENTWAIT;
    t->wakeTime = a[0] > 0 ? vm_time_ms() + a[0] : 0;
    return NAT_YIELD;
}
static int N_wake(Thread *t, int32_t *a) { (void)t; (void)a; evWake = 1; return NAT_OK; }
uint32_t g_framesPresented;
int64_t g_lastPresentMs;
static int64_t launchMs;
static int64_t nextPresentMs;
static int N_present(Thread *t, int32_t *a) {
    ref_t px = (ref_t)a[0];
    if (!px) NPE(t);
    /* cap at ~60 fps like a phone LCD: the drawing thread sleeps (other threads keep running) */
    int64_t now = vm_time_ms();
    if (now < nextPresentMs && nextPresentMs - now <= 17) {
        t->state = TH_SLEEPING;
        t->wakeTime = nextPresentMs;
        return NAT_RETRY;
    }
    nextPresentMs = (now < nextPresentMs + 17 ? nextPresentMs : now) + 16;
    if (nextPresentMs < now) nextPresentMs = now + 16;
    int w = a[1], h = a[2];
    if (w * h > A_LEN(px)) return NAT_OK;
    if (!g_framesPresented) vm_log("first frame shown %d ms after launch (%dx%d)\n", (int)(vm_time_ms() - launchMs), w, h);
    g_framesPresented++;
    g_lastPresentMs = vm_time_ms();
    g_perfFrames++;
    int64_t t0 = plat_time_us();
    plat_present((const uint32_t *)A_INT(px), w, h, w);
    g_perfPresentUs += plat_time_us() - t0;
    return NAT_OK;
}
static int N_screenW(Thread *t, int32_t *a) { (void)t; a[0] = scrW; return NAT_OK; }
static int N_screenH(Thread *t, int32_t *a) { (void)t; a[0] = scrH; return NAT_OK; }
static int N_forceFull(Thread *t, int32_t *a) { (void)t; a[0] = fullCanvas; return NAT_OK; }
static int N_keyProfile(Thread *t, int32_t *a) { (void)t; a[0] = keyProfile; return NAT_OK; }
static int N_exit(Thread *t, int32_t *a) { (void)t; (void)a; g_vmExit = 1; return NAT_YIELD; }
static int N_vibrate(Thread *t, int32_t *a) { (void)t; plat_vibrate(a[0]); return NAT_OK; }
static int N_overlay(Thread *t, int32_t *a) { (void)t; plat_overlay_active(a[0]); return NAT_OK; }

/* app properties (manifest + jad) */
typedef struct { char *k, *v; } KV;
static KV *props;
static int nProps, capProps;

static void setProp(const char *k, const char *v) {
    for (int i = 0; i < nProps; i++) if (strcmp(props[i].k, k) == 0) { free(props[i].v); props[i].v = strdup(v); return; }
    if (nProps == capProps) { capProps = capProps ? capProps * 2 : 32; props = realloc(props, capProps * sizeof(KV)); }
    props[nProps].k = strdup(k);
    props[nProps].v = strdup(v);
    nProps++;
}

static const char *getProp(const char *k) {
    for (int i = 0; i < nProps; i++) if (strcmp(props[i].k, k) == 0) return props[i].v;
    return NULL;
}

static void trim(char *s) {
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
}

static void parseProps(const char *text, int len) {
    char *buf = malloc(len + 2);
    /* join continuation lines (manifest: lines starting with a space) */
    int o = 0;
    for (int i = 0; i < len; i++) {
        if (text[i] == '\r') continue;
        if (text[i] == '\n' && i + 1 < len && text[i + 1] == ' ') { i++; continue; }
        buf[o++] = text[i];
    }
    buf[o] = 0;
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *c = strchr(line, ':');
        if (!c) continue;
        *c = 0;
        char *k = line, *v = c + 1;
        while (*k == ' ' || *k == '\t' || (unsigned char)*k == 0xef || (unsigned char)*k == 0xbb || (unsigned char)*k == 0xbf) k++;
        while (*v == ' ' || *v == '\t') v++;
        trim(k); trim(v);
        if (*k) setProp(k, v);
    }
    free(buf);
}

const char *midp_get_property(const char *k) { return getProp(k); }

static int N_getAppProperty(Thread *t, int32_t *a) {
    ref_t k = (ref_t)a[0];
    if (!k) NPE(t);
    char key[256];
    string_to_utf8(k, key, sizeof key);
    const char *v = getProp(key);
    a[0] = v ? new_string_utf8(v) : 0;
    return NAT_OK;
}

/* RMS */
static char rmsDir[600];

static void mkdirs(const char *path) {
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { if (p[-1] != ':') { *p = 0; mkdir(tmp, 0777); *p = '/'; } }
    }
    mkdir(tmp, 0777);
}

static void encodeName(const char *in, char *out, int outLen) {
    int o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o < outLen - 4; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-') out[o++] = *p;
        else o += snprintf(out + o, outLen - o, "%%%02X", *p);
    }
    out[o] = 0;
}

static void decodeName(const char *in, char *out, int outLen) {
    int o = 0;
    for (const char *p = in; *p && o < outLen - 1; p++) {
        if (*p == '%' && p[1] && p[2]) {
            char hex[3] = { p[1], p[2], 0 };
            out[o++] = (char)strtol(hex, NULL, 16);
            p += 2;
        } else out[o++] = *p;
    }
    out[o] = 0;
}

/* Record store files: <rms dir>/<encoded name>.rmsdb
 * The extension is longer than 3 characters on purpose: FAT keeps a short "8.3" name like
 * "score.rms" without a long-name entry, and the PSP then lists it as "SCORE.RMS" (case lost),
 * so listRecordStores() could not report it. Version 1.0-1.5 files (.rms) are still read and
 * moved to the new name on first use. */
#define RMS_EXT ".rmsdb"
#define RMS_OLD_EXT ".rms"
static void rmsPathExt(ref_t name, char *path, int len, const char *ext) {
    char n[256], enc[800];
    string_to_utf8(name, n, sizeof n);
    encodeName(n, enc, sizeof enc);
    snprintf(path, len, "%s/%s%s", rmsDir, enc, ext);
}
static void rmsPath(ref_t name, char *path, int len) { rmsPathExt(name, path, len, RMS_EXT); }

static int N_rmsRead(Thread *t, int32_t *a) {
    if (!a[0]) NPE(t);
    char path[1200];
    rmsPath((ref_t)a[0], path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) {
        /* power lost between "remove old" and "rename new" in N_rmsWrite: the .tmp is complete */
        char tmp[1210];
        snprintf(tmp, sizeof tmp, "%s.tmp", path);
        if (rename(tmp, path) == 0) f = fopen(path, "rb");
    }
    if (!f) {
        /* saved by an older version: move it to the new name */
        char old[1200];
        rmsPathExt((ref_t)a[0], old, sizeof old, RMS_OLD_EXT);
        if (rename(old, path) == 0) { vm_log("rms: moved %s to the new file name\n", old); f = fopen(path, "rb"); }
    }
    if (!f) { a[0] = 0; return NAT_OK; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > 16 * 1024 * 1024) { fclose(f); a[0] = 0; return NAT_OK; }
    ref_t arr = new_prim_array('B', (int32_t)n);
    if (!arr) { fclose(f); OOM(t); }
    if (n > 0 && fread(O_DATA(arr), 1, n, f) != (size_t)n) { fclose(f); a[0] = 0; return NAT_OK; }
    fclose(f);
    a[0] = arr;
    return NAT_OK;
}
static int N_rmsWrite(Thread *t, int32_t *a) {
    if (!a[0] || !a[1]) NPE(t);
    char path[1200], tmp[1210];
    rmsPath((ref_t)a[0], path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    ref_t data = (ref_t)a[1];
    FILE *f = fopen(tmp, "wb");
    if (!f) { mkdirs(rmsDir); f = fopen(tmp, "wb"); }
    if (!f) { a[0] = 0; return NAT_OK; }
    size_t n = A_LEN(data);
    int ok = fwrite(O_DATA(data), 1, n, f) == n;
    ok &= fclose(f) == 0;
    if (ok) {
        remove(path);
        ok = rename(tmp, path) == 0;
        if (ok) { char old[1200]; rmsPathExt((ref_t)a[0], old, sizeof old, RMS_OLD_EXT); remove(old); }
    } else remove(tmp);   /* memory stick full or write-protected: don't leave a partial file */
    a[0] = ok;
    return NAT_OK;
}
static int N_rmsDelete(Thread *t, int32_t *a) {
    if (!a[0]) NPE(t);
    char path[1200], old[1200];
    rmsPath((ref_t)a[0], path, sizeof path);
    rmsPathExt((ref_t)a[0], old, sizeof old, RMS_OLD_EXT);
    int r1 = remove(path) == 0, r2 = remove(old) == 0;
    a[0] = r1 || r2;
    return NAT_OK;
}
static int endsWithCI(const char *s, size_t l, const char *ext) {
    size_t el = strlen(ext);
    return l > el && strcasecmp(s + l - el, ext) == 0;
}
static int N_rmsList(Thread *t, int32_t *a) {
    char names[64][256];
    int n = 0;
    DIR *d = opendir(rmsDir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && n < 64) {
            size_t l = strlen(e->d_name);
            size_t el = endsWithCI(e->d_name, l, RMS_EXT) ? strlen(RMS_EXT) : endsWithCI(e->d_name, l, RMS_OLD_EXT) ? strlen(RMS_OLD_EXT) : 0;
            if (!el) continue;
            char enc[256], dec[256];
            snprintf(enc, sizeof enc, "%.*s", (int)(l - el), e->d_name);
            decodeName(enc, dec, sizeof dec);
            int dup = 0;   /* the same store under the old and the new name */
            for (int i = 0; i < n; i++) if (!strcmp(names[i], dec)) dup = 1;
            if (!dup) snprintf(names[n++], 256, "%s", dec);
        }
        closedir(d);
    }
    ref_t arr = new_obj_array(C_String, n);
    if (!arr) OOM(t);
    PUSH_ROOT(t, arr);
    for (int i = 0; i < n; i++) {
        ref_t s = new_string_utf8(names[i]);
        if (!s) { POP_ROOTS(t, 1); OOM(t); }
        A_REF(arr)[i] = s;
    }
    POP_ROOTS(t, 1);
    a[0] = arr;
    return NAT_OK;
}

static int N_editText(Thread *t, int32_t *a) {
    char title[256], init[4096], out[4096];
    string_to_utf8((ref_t)a[0], title, sizeof title);
    string_to_utf8((ref_t)a[1], init, sizeof init);
    if (plat_edit_text(title, init, out, sizeof out, a[2], a[3])) {
        a[0] = new_string_utf8(out);
        if (!a[0]) OOM(t);
    } else a[0] = 0;
    return NAT_OK;
}

/* ---- sound (MMAPI players, Nokia Sound); see src/audio/mixer.h ---- */
static int N_soundLoad(Thread *t, int32_t *a) {
    ref_t d = (ref_t)a[0];
    if (!d) NPE(t);
    plat_audio_poll();
    a[0] = plat_audio_available() ? mix_load((const uint8_t *)O_DATA(d), A_LEN(d), a[1] == 1) : -1;
    if (a[0] <= 0) vm_log("sound: unsupported or damaged audio data (%d bytes)\n", A_LEN(d));
    return NAT_OK;
}
static int N_soundBeep(Thread *t, int32_t *a) { (void)t; a[0] = plat_audio_available() ? mix_load_beep(a[0], a[1]) : -1; return NAT_OK; }
static int N_soundMidiDevice(Thread *t, int32_t *a) { (void)t; a[0] = plat_audio_available() ? mix_load_midi_device() : -1; return NAT_OK; }
static int N_soundStart(Thread *t, int32_t *a) { (void)t; plat_audio_poll(); mix_start(a[0]); return NAT_OK; }
static int N_soundPause(Thread *t, int32_t *a) { (void)t; plat_audio_poll(); mix_pause(a[0]); return NAT_OK; }
static int N_soundFree(Thread *t, int32_t *a) { (void)t; mix_free(a[0]); return NAT_OK; }
static int N_soundState(Thread *t, int32_t *a) {
    (void)t;
    plat_audio_poll();
    a[0] = mix_valid(a[0]) ? mix_state(a[0]) : -1;
    return NAT_OK;
}
static int N_soundLoops(Thread *t, int32_t *a) { (void)t; mix_set_loops(a[0], a[1]); return NAT_OK; }
static int N_soundLoopsDone(Thread *t, int32_t *a) { (void)t; plat_audio_poll(); a[0] = mix_loops_done(a[0]); return NAT_OK; }
static int N_soundTime(Thread *t, int32_t *a) { (void)t; plat_audio_poll(); slot_put_long(a, mix_time_us(a[0])); return NAT_OK; }
static int N_soundDuration(Thread *t, int32_t *a) { (void)t; slot_put_long(a, mix_duration_us(a[0])); return NAT_OK; }
static int N_soundSeek(Thread *t, int32_t *a) { (void)t; slot_put_long(a, mix_seek_us(a[0], slot_get_long(a + 1))); return NAT_OK; }
static int N_soundStopAt(Thread *t, int32_t *a) { (void)t; mix_stop_at(a[0], slot_get_long(a + 1)); return NAT_OK; }
static int N_soundVolume(Thread *t, int32_t *a) { (void)t; mix_volume(a[0], a[1]); return NAT_OK; }
static int N_soundRate(Thread *t, int32_t *a) { (void)t; mix_rate(a[0], a[1]); return NAT_OK; }
static int N_soundPitch(Thread *t, int32_t *a) { (void)t; mix_pitch(a[0], a[1]); return NAT_OK; }
static int N_soundMidi(Thread *t, int32_t *a) { (void)t; mix_midi_event(a[0], a[1], a[2], a[3]); return NAT_OK; }
static int N_soundMidiQuery(Thread *t, int32_t *a) { (void)t; a[0] = mix_midi_query(a[0], a[1], a[2]); return NAT_OK; }
static int N_tone(Thread *t, int32_t *a) { (void)t; if (plat_audio_available()) mix_tone(a[0], a[1], a[2]); return NAT_OK; }
static int N_reportProblem(Thread *t, int32_t *a) {
    (void)t;
    vm_problem_exception((ref_t)a[0], a[1]);
    return NAT_OK;
}

void natives_midp_init(void) {
#define R(c, n, d, f) vm_register_native(c, n, d, f)
    const char *G = "javax/microedition/lcdui/Graphics";
    R(G, "drawLine", NULL, T_G_drawLine);
    R(G, "fillRect", NULL, T_G_fillRect);
    R(G, "drawRect", NULL, T_G_drawRect);
    R(G, "drawRoundRect", NULL, T_G_drawRoundRect);
    R(G, "fillRoundRect", NULL, T_G_fillRoundRect);
    R(G, "fillArc", NULL, T_G_fillArc);
    R(G, "drawArc", NULL, T_G_drawArc);
    R(G, "fillTriangle", NULL, T_G_fillTriangle);
    R(G, "drawText0", NULL, T_G_drawText);
    R(G, "drawRegion0", NULL, T_G_drawRegion);
    R(G, "drawRGB", NULL, T_G_drawRGB);
    R(G, "fillPolygon0", NULL, T_G_fillPolygon);
    const char *F = "javax/microedition/lcdui/Font";
    R(F, "height0", NULL, Font_height);
    R(F, "baseline0", NULL, Font_baseline);
    R(F, "charWidth0", NULL, Font_charWidth);
    R(F, "charsWidth0", NULL, Font_charsWidth);
    R(F, "stringWidth0", NULL, Font_stringWidth);
    const char *I = "javax/microedition/lcdui/Image";
    R(I, "loadResource", NULL, Image_loadResource);
    R(I, "decode0", NULL, Image_decode);
    R(I, "fill", NULL, Image_fill);
    R(I, "blit0", NULL, Image_blit);
    R(I, "fixAlpha0", NULL, Image_fixAlpha);
    const char *N = "pspj2me/Native";
    R(N, "pollEvent", NULL, N_pollEvent);
    R(N, "waitEvent", NULL, N_waitEvent);
    R(N, "wakeEventLoop", NULL, N_wake);
    R(N, "present", NULL, N_present);
    R(N, "screenWidth", NULL, N_screenW);
    R(N, "screenHeight", NULL, N_screenH);
    R(N, "forceFullScreen", NULL, N_forceFull);
    R(N, "keyProfile", NULL, N_keyProfile);
    R(N, "exit", NULL, N_exit);
    R(N, "getAppProperty", NULL, N_getAppProperty);
    R(N, "rmsRead", NULL, N_rmsRead);
    R(N, "rmsWrite", NULL, N_rmsWrite);
    R(N, "rmsDelete", NULL, N_rmsDelete);
    R(N, "rmsList", NULL, N_rmsList);
    R(N, "editText", NULL, N_editText);
    R(N, "vibrate", NULL, N_vibrate);
    R(N, "setOverlayActive", NULL, N_overlay);
    R(N, "soundLoad", NULL, N_soundLoad);
    R(N, "soundBeep", NULL, N_soundBeep);
    R(N, "soundMidiDevice", NULL, N_soundMidiDevice);
    R(N, "soundStart", NULL, N_soundStart);
    R(N, "soundPause", NULL, N_soundPause);
    R(N, "soundFree", NULL, N_soundFree);
    R(N, "soundState", NULL, N_soundState);
    R(N, "soundLoops", NULL, N_soundLoops);
    R(N, "soundLoopsDone", NULL, N_soundLoopsDone);
    R(N, "soundTime", NULL, N_soundTime);
    R(N, "soundDuration", NULL, N_soundDuration);
    R(N, "soundSeek", NULL, N_soundSeek);
    R(N, "soundStopAt", NULL, N_soundStopAt);
    R(N, "soundVolume", NULL, N_soundVolume);
    R(N, "soundRate", NULL, N_soundRate);
    R(N, "soundPitch", NULL, N_soundPitch);
    R(N, "soundMidi", NULL, N_soundMidi);
    R(N, "soundMidiQuery", NULL, N_soundMidiQuery);
    R(N, "tone", NULL, N_tone);
    R(N, "reportProblem", NULL, N_reportProblem);
#undef R
    g_eventPendingHook = eventsPending;
}

/* Reads manifest (+ sibling .jad) of a jar into the property table. Returns 0 on success. */
int midp_load_descriptor(const char *jarPath) {
    Zip *z = zip_open_file_lazy(jarPath);
    if (!z) return -1;
    ZipEntry *e = zip_find(z, "META-INF/MANIFEST.MF");
    if (e) {
        uint32_t len;
        uint8_t *m = zip_read(z, e, &len);
        if (m) { parseProps((const char *)m, len); free(m); }
    }
    zip_close(z);
    char jad[600];
    snprintf(jad, sizeof jad, "%s", jarPath);
    char *dot = strrchr(jad, '.');
    if (dot) {
        strcpy(dot, ".jad");
        FILE *f = fopen(jad, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *b = malloc(n + 1);
            if (fread(b, 1, n, f) == (size_t)n) parseProps(b, (int)n);
            free(b);
            fclose(f);
        }
    }
    return 0;
}

/* Finds the class of MIDlet-<index> ("Name, Icon, Class"). */
int midp_midlet_class(int index, char *out, int outLen) {
    char key[32];
    snprintf(key, sizeof key, "MIDlet-%d", index);
    const char *v = getProp(key);
    if (!v) return -1;
    const char *c1 = strchr(v, ',');
    const char *c2 = c1 ? strchr(c1 + 1, ',') : NULL;
    if (!c2) return -1;
    c2++;
    while (*c2 == ' ') c2++;
    snprintf(out, outLen, "%s", c2);
    trim(out);
    return 0;
}

int midp_launch(const char *jarPath, const char *midletClass) {
    if (vm_add_classpath_file(jarPath) != 0) {
        vm_log("cannot open jar %s\n", jarPath);
        vm_problem(PROB_FATAL, "This file is damaged or is not a .jar archive (or is too big to load).");
        return 1;
    }
    midp_load_descriptor(jarPath);
    char cls[256];
    if (midletClass) snprintf(cls, sizeof cls, "%s", midletClass);
    else if (midp_midlet_class(1, cls, sizeof cls) != 0) {
        vm_log("no MIDlet-1 in manifest\n");
        vm_problem(PROB_FATAL, "This .jar is not a J2ME game: its manifest names no MIDlet to start.");
        return 1;
    }
    const char *name = getProp("MIDlet-Name");
    const char *vendor = getProp("MIDlet-Vendor");
    char suite[300], enc[600];
    snprintf(suite, sizeof suite, "%s_%s", name ? name : "unknown", vendor ? vendor : "unknown");
    encodeName(suite, enc, sizeof enc);
    snprintf(rmsDir, sizeof rmsDir, "%s/rms/%s", plat_data_dir(), enc);
    mkdirs(rmsDir);
    g_framesPresented = 0;
    launchMs = g_lastPresentMs = vm_time_ms();
    vm_log("Launching MIDlet %s (%s) screen %dx%d\n", cls, name ? name : "?", scrW, scrH);
    return vm_run_main("pspj2me/Launcher", "main", "(Ljava/lang/String;)V", cls);
}

void midp_reset(void) {
    evHead = evTail = 0;
    evWake = 0;
    nextPresentMs = 0;
    exitDeadline = 0;
    memset(held, 0, sizeof held);
    offsetsReady = 0;
    for (int i = 0; i < nProps; i++) { free(props[i].k); free(props[i].v); }
    free(props);
    props = NULL;
    nProps = capProps = 0;
    rmsDir[0] = 0;
}

/* Save-data (RMS) directory for a MIDlet suite; shared with the launcher for deleting saves. */
void midp_rms_dir_for(const char *name, const char *vendor, char *out, int len) {
    char suite[300], enc[600];
    snprintf(suite, sizeof suite, "%s_%s", name && *name ? name : "unknown", vendor && *vendor ? vendor : "unknown");
    encodeName(suite, enc, sizeof enc);
    snprintf(out, len, "%s/rms/%s", plat_data_dir(), enc);
}
