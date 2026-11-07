/* Game browser + per-game settings, drawn with the same software rasterizer. */
#include <pspkernel.h>
#include <pspctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include "psp_common.h"
#include "../platform.h"
#include "../../vm/zip.h"
#include "../../vm/vm.h"

typedef struct {
    char path[512];
    char file[128];
    char name[128];
    char vendor[96];
    char midletName[128];   /* MIDlet-Name exactly as in the manifest (empty if missing) */
    uint32_t *icon;   /* 32x32 ARGB or NULL */
} Game;

#define MAX_GAMES 256
static Game games[MAX_GAMES];
static int nGames;
static uint32_t *canvas;

static const int sizes[][2] = { { 128, 128 }, { 128, 160 }, { 176, 208 }, { 176, 220 }, { 208, 208 }, { 240, 320 },
                                { 320, 240 }, { 352, 416 }, { 360, 640 }, { 480, 272 }, { 400, 240 }, { 320, 272 }, { 360, 272 }, { 240, 272 }, { 480, 320 } };
#define NSIZES ((int)(sizeof(sizes) / sizeof(sizes[0])))
static const int keyChoices[] = { 0, JK_UP, JK_DOWN, JK_LEFT, JK_RIGHT, JK_FIRE, JK_SOFT1, JK_SOFT2, JK_CLEAR,
                                  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '*', '#' };
#define NKEYCHOICES ((int)(sizeof(keyChoices) / sizeof(keyChoices[0])))

static char *manifestValue(const char *mf, const char *key, char *out, int outLen) {
    size_t kl = strlen(key);
    const char *p = mf;
    while (p && *p) {
        if (strncmp(p, key, kl) == 0 && p[kl] == ':') {
            const char *v = p + kl + 1;
            while (*v == ' ') v++;
            int n = 0;
            while (v[n] && v[n] != '\r' && v[n] != '\n' && n < outLen - 1) { out[n] = v[n]; n++; }
            out[n] = 0;
            return out;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
    return NULL;
}

static uint32_t *scaleIcon(const uint32_t *src, int w, int h) {
    uint32_t *o = malloc(32 * 32 * 4);
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int sx = x * w / 32, sy = y * h / 32;
            o[y * 32 + x] = src[sy * w + sx];
        }
    return o;
}

static void loadGame(const char *path, const char *file) {
    if (nGames >= MAX_GAMES) return;
    for (int i = 0; i < nGames; i++) if (strcmp(games[i].path, path) == 0) return;
    Game *g = &games[nGames];
    memset(g, 0, sizeof *g);
    snprintf(g->path, sizeof g->path, "%s", path);
    snprintf(g->file, sizeof g->file, "%s", file);
    snprintf(g->name, sizeof g->name, "%s", file);
    Zip *z = zip_open_file(path);
    if (!z) return; /* not a valid jar: skip */
    ZipEntry *e = zip_find(z, "META-INF/MANIFEST.MF");
    if (e) {
        uint32_t len;
        char *mf = (char *)zip_read(z, e, &len);
        if (mf) {
            char v[256], icon[256] = "";
            if (manifestValue(mf, "MIDlet-Name", v, sizeof v)) { snprintf(g->name, sizeof g->name, "%s", v); snprintf(g->midletName, sizeof g->midletName, "%s", v); }
            if (manifestValue(mf, "MIDlet-Vendor", v, sizeof v)) snprintf(g->vendor, sizeof g->vendor, "%s", v);
            if (manifestValue(mf, "MIDlet-Icon", v, sizeof v)) snprintf(icon, sizeof icon, "%s", v);
            else if (manifestValue(mf, "MIDlet-1", v, sizeof v)) {
                char *c1 = strchr(v, ',');
                char *c2 = c1 ? strchr(c1 + 1, ',') : NULL;
                if (c1 && c2) {
                    *c2 = 0;
                    char *s = c1 + 1;
                    while (*s == ' ') s++;
                    snprintf(icon, sizeof icon, "%s", s);
                    char *end = icon + strlen(icon);
                    while (end > icon && end[-1] == ' ') *--end = 0;
                }
            }
            if (icon[0]) {
                ZipEntry *ie = zip_find(z, icon[0] == '/' ? icon + 1 : icon);
                if (ie) {
                    uint32_t il;
                    uint8_t *id = zip_read(z, ie, &il);
                    if (id) {
                        int w, h, a;
                        uint32_t *px = png_decode(id, il, &w, &h, &a);
                        if (px) { g->icon = scaleIcon(px, w, h); free(px); }
                        free(id);
                    }
                }
            }
            free(mf);
        }
    }
    zip_close(z);
    nGames++;
}

static void scanDir(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (e->d_name[0] == '.') continue; /* macOS "._name.jar" metadata files */
        if (l > 4 && (strcasecmp(e->d_name + l - 4, ".jar") == 0)) {
            char p[512];
            snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
            loadGame(p, e->d_name);
        }
    }
    closedir(d);
}

static int cmpGames(const void *a, const void *b) { return strcasecmp(((const Game *)a)->name, ((const Game *)b)->name); }

static unsigned prevB;
static unsigned readPressed(unsigned *held) {
    SceCtrlData pad;
    sceCtrlReadBufferPositive(&pad, 1);
    unsigned b = pad.Buttons;
    /* analog stick as d-pad in menus */
    if (pad.Ly < 40) b |= PSP_CTRL_UP;
    if (pad.Ly > 215) b |= PSP_CTRL_DOWN;
    unsigned p = b & ~prevB;
    prevB = b;
    if (held) *held = b;
    return p;
}

static void drawHeader(Surface *s, const char *title, const char *right) {
    gfx_fill_rect(s, 0, 0, SCR_W, SCR_H, 0xff15191f);
    gfx_fill_rect(s, 0, 0, SCR_W, 24, 0xff203a70);
    ui_text(s, 8, 4, title, 0xffffffff, 1, 1);
    if (right) ui_text(s, SCR_W - 8 - ui_text_width(right, 0, 0), 6, right, 0xffb0c4e8, 0, 0);
}

static void drawFooter(Surface *s, const char *hint) {
    gfx_fill_rect(s, 0, SCR_H - 20, SCR_W, 20, 0xff202020);
    ui_text(s, 8, SCR_H - 17, hint, 0xffd0d0d0, 0, 0);
}


void midp_rms_dir_for(const char *name, const char *vendor, char *out, int len);

static void removeDir(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    char p[700];
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        remove(p);
    }
    closedir(d);
    rmdir(dir);
}

/* Deletes the jar (+ jad, settings, optionally saves). */
static void deleteGame(Game *g, int withSaves) {
    char p[700];
    vm_log("deleting game %s%s\n", g->path, withSaves ? " (with saves)" : "");
    remove(g->path);
    snprintf(p, sizeof p, "%s", g->path);
    char *dot = strrchr(p, '.');
    if (dot) { strcpy(dot, ".jad"); remove(p); strcpy(dot, ".JAD"); remove(p); }
    char bn[160];
    snprintf(bn, sizeof bn, "%s", g->file);
    dot = strrchr(bn, '.');
    if (dot) *dot = 0;
    snprintf(p, sizeof p, "%s/%s.ini", plat_data_dir(), bn);
    remove(p);
    if (withSaves) {
        midp_rms_dir_for(g->midletName, g->vendor, p, sizeof p);
        removeDir(p);
    }
    free(g->icon);
}

/* returns 0 = cancel, 1 = delete game, 2 = delete game and saves */
static int confirmDelete(Surface *s, Game *g) {
    prevB = 0xffffffff;
    for (;;) {
        unsigned p = readPressed(NULL);
        if (p & PSP_CTRL_CIRCLE) return 0;
        if (p & PSP_CTRL_CROSS) return 1;
        if (p & PSP_CTRL_TRIANGLE) return 2;
        int w = 400, h = 120, x = (SCR_W - w) / 2, y = (SCR_H - h) / 2;
        gfx_fill_rect(s, x, y, w, h, 0xff2a1a1a);
        gfx_rect(s, x, y, w - 1, h - 1, 0xffd05050, 0);
        ui_text(s, x + 14, y + 10, "Delete this game from the memory card?", 0xffffffff, 1, 1);
        ui_text(s, x + 14, y + 34, g->name, 0xffffd060, 1, 1);
        ui_text(s, x + 14, y + 54, g->file, 0xffb0b0b0, 0, 0);
        ui_text(s, x + 14, y + 78, "X: delete game (keep saves)", 0xffffffff, 0, 0);
        ui_text(s, x + 14, y + 94, "Triangle: delete game and its saves      O: cancel", 0xffffffff, 0, 0);
        video_draw_launcher(canvas);
    }
}

static int settingsScreen(Game *g) {
    char cfg[700], bn[160];
    snprintf(bn, sizeof bn, "%s", g->file);
    char *dot = strrchr(bn, '.');
    if (dot) *dot = 0;
    snprintf(cfg, sizeof cfg, "%s/%s.ini", plat_data_dir(), bn);
    Settings st = g_settings;
    settings_load(&st, cfg);
    int sel = 0, top = 0;
    enum { S_SIZE = 0, S_SPEED, S_SHOULDERS, S_SCALE, S_FILTER, S_FPS, S_VSYNC, S_KEYS };
    int nItems = S_KEYS + B_COUNT + 1; /* + reset */
    Surface s;
    ui_surface(&s, canvas);
    for (;;) {
        unsigned held;
        unsigned p = readPressed(&held);
        if (p & PSP_CTRL_CIRCLE) { settings_save(&st, cfg); return 0; }
        if (p & PSP_CTRL_TRIANGLE) return 0;   /* close without saving */
        if (p & PSP_CTRL_UP) sel = (sel + nItems - 1) % nItems;
        if (p & PSP_CTRL_DOWN) sel = (sel + 1) % nItems;
        int dir = (p & PSP_CTRL_RIGHT) ? 1 : (p & PSP_CTRL_LEFT) ? -1 : 0;
        if ((p & PSP_CTRL_CROSS) && !dir) dir = 1;
        if (dir) {
            if (sel == S_SIZE) {
                int cur = 0;
                for (int i = 0; i < NSIZES; i++) if (sizes[i][0] == st.screenW && sizes[i][1] == st.screenH) cur = i;
                cur = (cur + NSIZES + dir) % NSIZES;
                st.screenW = sizes[cur][0]; st.screenH = sizes[cur][1];
            } else if (sel == S_SCALE) st.scale = (st.scale + SCALE_COUNT + dir) % SCALE_COUNT;
            else if (sel == S_FILTER) st.filter ^= 1;
            else if (sel == S_FPS) st.showFps ^= 1;
            else if (sel == S_VSYNC) st.vsync ^= 1;
            else if (sel == S_SHOULDERS) st.shoulderCombos ^= 1;
            else if (sel == S_SPEED) {
                static const int steps[] = { 100, 75, 50, 35 };
                int cur = 0;
                for (int i = 0; i < 4; i++) if (steps[i] == st.sleepScale) cur = i;
                st.sleepScale = steps[(cur + 4 + dir) % 4];
            }
            else if (sel >= S_KEYS && sel < S_KEYS + B_COUNT) {
                int b = sel - S_KEYS, cur = 0;
                for (int i = 0; i < NKEYCHOICES; i++) if (keyChoices[i] == st.keys[b]) cur = i;
                cur = (cur + NKEYCHOICES + dir) % NKEYCHOICES;
                st.keys[b] = keyChoices[cur];
            } else if (sel == nItems - 1 && (p & PSP_CTRL_CROSS)) {
                Settings d;
                settings_defaults(&d);
                memcpy(st.keys, d.keys, sizeof d.keys);
            }
        }
        if (sel < top) top = sel;
        if (sel >= top + 10) top = sel - 9;
        drawHeader(&s, "Game settings", g->name);
        char line[160];
        for (int i = top; i < nItems && i < top + 10; i++) {
            int y = 32 + (i - top) * 21;
            if (i == sel) gfx_fill_rect(&s, 6, y - 2, SCR_W - 12, 20, 0xff3060c0);
            if (i == S_SIZE) snprintf(line, sizeof line, "Phone screen size:  < %d x %d >", st.screenW, st.screenH);
            else if (i == S_SCALE) snprintf(line, sizeof line, "Display mode:  < %s >", scale_name(st.scale));
            else if (i == S_FILTER) snprintf(line, sizeof line, "Smooth scaling:  %s", st.filter ? "On" : "Off");
            else if (i == S_FPS) snprintf(line, sizeof line, "Show FPS:  %s", st.showFps ? "On" : "Off");
            else if (i == S_VSYNC) snprintf(line, sizeof line, "VSync:  %s", st.vsync ? "On" : "Off");
            else if (i == S_SHOULDERS) snprintf(line, sizeof line, "Shoulder combos (hold L/R for 1 3 7 9 Fire Clear):  %s",
                                                 st.shoulderCombos ? "On" : "Off");
            else if (i == S_SPEED) snprintf(line, sizeof line, "Speed-up (shorter game sleeps):  < %s >",
                                             st.sleepScale >= 100 ? "Off" : st.sleepScale == 75 ? "x1.3" : st.sleepScale == 50 ? "x2" : "x3");
            else if (i < S_KEYS + B_COUNT) snprintf(line, sizeof line, "Button %s  ->  < %s >", button_name(i - S_KEYS), key_name(st.keys[i - S_KEYS]));
            else snprintf(line, sizeof line, "Reset key bindings to defaults");
            ui_text(&s, 14, y, line, 0xffffffff, 1, 0);
        }
        drawFooter(&s, "Left/Right: change    O: save and close    Triangle: close without saving");
        video_draw_launcher(canvas);
    }
}

int launcher_run(char *outJar, int outLen) {
    if (!canvas) canvas = malloc(SCR_W * SCR_H * 4);
    for (int i = 0; i < nGames; i++) free(games[i].icon);
    nGames = 0;
    int firstFrame = 1;
    char dir[600];
    snprintf(dir, sizeof dir, "%s/games", g_baseDir);
    mkdir(dir, 0777);
    scanDir(dir);
    /* J2ME/ at the root of the same device we were started from (ms0: memory stick,
       ef0: PSP Go internal storage), plus the memory stick on a PSP Go with a card inserted */
    char dev[16] = "ms0:";
    const char *colon = strchr(g_baseDir, ':');
    if (colon && colon - g_baseDir < 12) snprintf(dev, sizeof dev, "%.*s", (int)(colon - g_baseDir + 1), g_baseDir);
    char extra[64];
    snprintf(extra, sizeof extra, "%s/J2ME", dev);
    scanDir(extra);
    if (strcmp(dev, "ms0:") != 0) { scanDir("ms0:/J2ME"); scanDir("ms0:/PSP/GAME/PSPJ2ME/games"); }
    qsort(games, nGames, sizeof(Game), cmpGames);
    int sel = 0, top = 0;
    char lastPath[600];
    snprintf(lastPath, sizeof lastPath, "%s/last_game.txt", plat_data_dir());
    {
        FILE *lf = fopen(lastPath, "r");
        char last[512] = "";
        if (lf) { if (fgets(last, sizeof last, lf)) last[strcspn(last, "\r\n")] = 0; fclose(lf); }
        for (int i = 0; i < nGames; i++) if (last[0] && !strcmp(games[i].path, last)) { sel = i; top = sel > 3 ? sel - 3 : 0; }
    }
    const int rows = 6, rowH = 38;
    Surface s;
    ui_surface(&s, canvas);
    prevB = 0xffffffff;
    for (;;) {
        unsigned p = readPressed(NULL);
        if (nGames > 0) {
            if (p & PSP_CTRL_UP) sel = (sel + nGames - 1) % nGames;
            if (p & PSP_CTRL_DOWN) sel = (sel + 1) % nGames;
            if (p & PSP_CTRL_LEFT) sel = sel - rows < 0 ? 0 : sel - rows;
            if (p & PSP_CTRL_RIGHT) sel = sel + rows >= nGames ? nGames - 1 : sel + rows;
            if (p & PSP_CTRL_CROSS) {
                snprintf(outJar, outLen, "%s", games[sel].path);
                FILE *lf = fopen(lastPath, "w");
                if (lf) { fprintf(lf, "%s\n", games[sel].path); fclose(lf); }
                return 0;
            }
            if (p & PSP_CTRL_TRIANGLE) { settingsScreen(&games[sel]); prevB = 0xffffffff; continue; }
            if (p & PSP_CTRL_SELECT) {
                Settings st = g_settings;
                char cfg[700], bn[160];
                snprintf(bn, sizeof bn, "%s", games[sel].file);
                char *dot = strrchr(bn, '.');
                if (dot) *dot = 0;
                snprintf(cfg, sizeof cfg, "%s/%s.ini", plat_data_dir(), bn);
                settings_load(&st, cfg);
                prevB = 0xffffffff;
                for (;;) {
                    if (readPressed(NULL)) break;
                    draw_controls(&s, &st, games[sel].name);
                    video_draw_launcher(canvas);
                }
                prevB = 0xffffffff;
                continue;
            }
            if (p & PSP_CTRL_SQUARE) {
                int r = confirmDelete(&s, &games[sel]);
                if (r) {
                    deleteGame(&games[sel], r == 2);
                    memmove(&games[sel], &games[sel + 1], (nGames - sel - 1) * sizeof(Game));
                    nGames--;
                    if (sel >= nGames) sel = nGames > 0 ? nGames - 1 : 0;
                }
                prevB = 0xffffffff;
                continue;
            }
        }
        if (sel < top) top = sel;
        if (sel >= top + rows) top = sel - rows + 1;
        char count[64];
        snprintf(count, sizeof count, "%d game%s", nGames, nGames == 1 ? "" : "s");
        drawHeader(&s, "PSPJ2ME " PSPJ2ME_VERSION "  -  J2ME games", count);
        if (nGames == 0) {
            ui_text(&s, 20, 50, "No games found.", 0xffffffff, 2, 1);
            ui_text(&s, 20, 80, "Copy your .jar files (and .jad files, if you have them) to:", 0xffd0d0d0, 1, 0);
            ui_text(&s, 30, 102, dir, 0xffffd060, 1, 1);
            ui_text(&s, 20, 128, "or to  ms0:/J2ME/  and start PSPJ2ME again.", 0xffd0d0d0, 1, 0);
        }
        for (int i = top; i < nGames && i < top + rows; i++) {
            int y = 30 + (i - top) * rowH;
            Game *g = &games[i];
            if (i == sel) gfx_fill_round_rect(&s, 6, y, SCR_W - 12, rowH - 2, 8, 8, 0xff3060c0);
            else gfx_fill_round_rect(&s, 6, y, SCR_W - 12, rowH - 2, 8, 8, 0xff222833);
            if (g->icon) gfx_blit(&s, g->icon, 32, 0, 0, 32, 32, 0, 12, y + 2, 1);
            else { gfx_fill_rect(&s, 12, y + 2, 32, 32, 0xff505868); ui_text(&s, 19, y + 10, "J2", 0xffffffff, 1, 1); }
            ui_text(&s, 54, y + 2, g->name, 0xffffffff, 1, 1);
            char sub[300];
            snprintf(sub, sizeof sub, "%s%s%s", g->vendor, g->vendor[0] ? "  -  " : "", g->file);
            ui_text(&s, 54, y + 19, sub, i == sel ? 0xffd8e4ff : 0xff9098a8, 0, 0);
        }
        if (nGames > rows) {
            int h = (SCR_H - 54) * rows / nGames, yy = 28 + (SCR_H - 54 - h) * top / (nGames - rows);
            gfx_fill_rect(&s, SCR_W - 4, yy, 3, h, 0xff8090b0);
        }
        drawFooter(&s, "X: play   Triangle: settings   Square: delete   SELECT: controls   HOME: quit");
        video_draw_launcher(canvas);
        if (firstFrame) { firstFrame = 0; vm_log("launcher: %d game(s) found\n", nGames); sceIoDevctl("kemulator:", 0x20, NULL, 0, NULL, 0); }
        if (!outJar) { /* test mode: dump the launcher screen and stop */
            char p[700];
            snprintf(p, sizeof p, "%s/shot_launcher.ppm", g_baseDir);
            FILE *f = fopen(p, "wb");
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
                for (int i = 0; i < SCR_W * SCR_H; i++) { unsigned char c[3] = { canvas[i] >> 16, canvas[i] >> 8, canvas[i] }; fwrite(c, 1, 3, f); }
                fclose(f);
            }
            return 0;
        }
    }
}
