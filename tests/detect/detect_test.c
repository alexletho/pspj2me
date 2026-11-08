/* Unit test for src/platform/detect.c: screen size and phone key profile detection. */
#include "detect.h"
#include <stdio.h>
#include <string.h>

static int fails;
static void size(const char *name, int ew, int eh) {
    int w = 0, h = 0;
    char from[80] = "";
    detect_size_from_name(name, &w, &h, from, sizeof from);
    int ok = w == ew && h == eh;
    if (!ok) fails++;
    printf("%s size %-40s -> %dx%d (%s)\n", ok ? "ok  " : "FAIL", name, w, h, from);
}
static void keys(const char *name, int expect) {
    char from[80] = "";
    int k = detect_keys_from_name(name, from, sizeof from);
    if (k != expect) fails++;
    printf("%s keys %-40s -> %d (%s)\n", k == expect ? "ok  " : "FAIL", name, k, from);
}
static void game(const char *dir, const char *jar, int ew, int eh, int ekeys) {
    char p[512];
    snprintf(p, sizeof p, "%s/%s", dir, jar);
    GameDetect g;
    detect_game(p, &g);
    int ok = g.w == ew && g.h == eh && g.keys == ekeys;
    if (!ok) fails++;
    printf("%s jar  %-40s -> %dx%d (%s), keys %d (%s)\n", ok ? "ok  " : "FAIL", jar, g.w, g.h, g.sizeFrom, g.keys, g.keysFrom);
}

int main(int argc, char **argv) {
    size("Asphalt4_240x320.jar", 240, 320);
    size("games/Doom_RPG_176X208_Nokia.jar", 176, 208);
    size("Tetris_128*160.jar", 128, 160);
    size("Prince_320_240_E71.jar", 320, 240);
    size("NFS_N73.jar", 240, 320);
    size("game_k750i.jar", 176, 220);
    size("Bounce_v3.jar", 0, 0);            /* a version number is not a Motorola V3 */
    size("Game2009_100.jar", 0, 0);
    size("plain.jar", 0, 0);
    keys("Gangstar_Siemens_S65.jar", KP_SIEMENS);
    keys("Tetris_SIE.jar", KP_SIEMENS);
    keys("Asphalt_moto_razr.jar", KP_MOTOROLA);
    keys("Game_LG_KG800.jar", KP_LG);
    keys("Game_SE_K800.jar", KP_NOKIA);
    keys("Game_Samsung_D900.jar", KP_NOKIA);
    keys("Bounce_v3.jar", KP_AUTO);
    keys("plain.jar", KP_AUTO);
    const char *dir = argc > 1 ? argv[1] : "build/detect";
    game(dir, "plain.jar", 0, 0, KP_NOKIA);
    game(dir, "Manifest.jar", 176, 208, KP_NOKIA);
    game(dir, "Jadsize.jar", 128, 160, KP_NOKIA);
    game(dir, "Images.jar", 240, 320, KP_NOKIA);
    game(dir, "ApiOne.jar", 0, 0, KP_SIEMENS);
    game(dir, "ApiTwo.jar", 0, 0, KP_MOTOROLA);
    game(dir, "ApiBoth.jar", 0, 0, KP_NOKIA);   /* refers to Siemens and Nokia: decides at run time */
    printf(fails ? "detect test: %d FAILED\n" : "all detect tests passed\n", fails);
    return fails != 0;
}
