/* Unit test for the PSP button mapping (shoulder shift combos). */
#include "../../src/platform/keymap.h"
#include "../../src/platform/platform.h"
#include <stdio.h>
#include <string.h>

static char log_[1024];
static void post(int type, int code) {
    char b[32];
    if (code >= 32 && code < 127) snprintf(b, sizeof b, "%c%c ", type == 1 ? '+' : '-', code);
    else snprintf(b, sizeof b, "%c%d ", type == 1 ? '+' : '-', code);
    strcat(log_, b);
}

static int keys[B_COUNT];
static KeyMap km;
static int st[B_COUNT];
static int64_t t;
static int fails;

static void step(void) { keymap_update(&km, keys, 1, st, NULL, t, post); t += 10; }
static void stepNoShoulders(void) { keymap_update(&km, keys, 0, st, NULL, t, post); t += 10; }
static void expect(const char *name, const char *want) {
    if (strcmp(log_, want) != 0) { printf("FAIL %s: got [%s] want [%s]\n", name, log_, want); fails++; }
    else printf("ok   %s: %s\n", name, log_);
    log_[0] = 0;
}
static void reset(void) { memset(&km, 0, sizeof km); memset(st, 0, sizeof st); log_[0] = 0; t = 0; }

int main(void) {
    keys[B_UP] = '2'; keys[B_DOWN] = '8'; keys[B_LEFT] = '4'; keys[B_RIGHT] = '6';
    keys[B_CROSS] = '5'; keys[B_CIRCLE] = '0'; keys[B_SQUARE] = '*'; keys[B_TRIANGLE] = '#';
    keys[B_L] = '1'; keys[B_R] = '3'; keys[B_SELECT] = JK_SOFT1; keys[B_START] = JK_SOFT2;
    keys[B_ANALOG_UP] = JK_UP; keys[B_ANALOG_DOWN] = JK_DOWN; keys[B_ANALOG_LEFT] = JK_LEFT; keys[B_ANALOG_RIGHT] = JK_RIGHT;

    reset(); st[B_UP] = 1; step(); st[B_UP] = 0; step();
    expect("D-pad up = 2", "+2 -2 ");

    reset(); st[B_L] = 1; step(); st[B_L] = 0; step(); t += 100; step();
    expect("L alone sends nothing", "");

    reset(); st[B_R] = 1; step(); st[B_R] = 0; step(); t += 100; step();
    expect("R alone sends nothing", "");

    reset(); st[B_L] = 1; step(); st[B_UP] = 1; step(); st[B_UP] = 0; step(); st[B_L] = 0; step(); t += 100; step();
    expect("L+Up = 1", "+1 -1 ");

    reset(); st[B_L] = 1; step(); st[B_DOWN] = 1; step(); st[B_DOWN] = 0; st[B_L] = 0; step(); t += 100; step();
    expect("L+Down = 7", "+7 -7 ");

    reset(); st[B_R] = 1; step(); st[B_UP] = 1; step(); st[B_UP] = 0; step(); st[B_DOWN] = 1; step(); st[B_DOWN] = 0; st[B_R] = 0; step(); t += 100; step();
    expect("R+Up = 3, R+Down = 9", "+3 -3 +9 -9 ");

    reset(); st[B_L] = 1; step(); st[B_CROSS] = 1; step(); st[B_CROSS] = 0; step(); st[B_CIRCLE] = 1; step(); st[B_CIRCLE] = 0; st[B_L] = 0; step(); t += 100; step();
    expect("L+X = Fire, L+O = Clear", "+-5 --5 +-8 --8 ");

    reset(); st[B_R] = 1; step(); st[B_CROSS] = 1; step(); st[B_CROSS] = 0; st[B_R] = 0; step(); t += 100; step();
    expect("R+X = Fire", "+-5 --5 ");

    reset(); st[B_L] = 1; step(); st[B_LEFT] = 1; step(); st[B_LEFT] = 0; step(); st[B_L] = 0; step(); t += 100; step();
    expect("L+Left = 4 (no combo)", "+4 -4 ");

    reset(); st[B_L] = 1; step(); st[B_UP] = 1; step(); st[B_L] = 0; step(); st[B_UP] = 0; step(); t += 100; step();
    expect("release L before Up: 1 released correctly", "+1 -1 ");

    reset(); st[B_UP] = 1; step(); st[B_L] = 1; step(); st[B_UP] = 0; step(); st[B_L] = 0; step(); t += 100; step();
    expect("Up held before L stays 2", "+2 -2 ");

    reset(); st[B_L] = 1; st[B_R] = 1; step(); st[B_UP] = 1; step(); st[B_UP] = 0; st[B_L] = 0; st[B_R] = 0; step(); t += 100; step();
    expect("L+R+Up: L wins = 1", "+1 -1 ");

    reset(); st[B_L] = 1; step(); st[B_ANALOG_LEFT] = 1; step(); st[B_ANALOG_LEFT] = 0; st[B_L] = 0; step(); t += 100; step();
    expect("L + stick left = arrow (no combo)", "+-3 --3 ");

    reset(); st[B_L] = 1; stepNoShoulders(); st[B_L] = 0; stepNoShoulders();
    expect("combos off: L = 1 immediately", "+1 -1 ");

    reset(); st[B_L] = 1; step(); st[B_UP] = 1; step(); keymap_release_all(&km, post); expect("menu opens: held 1 is released", "+1 -1 ");
    st[B_UP] = 0; st[B_L] = 0; step(); t += 100; step();
    expect("nothing extra after the menu", "");

    printf(fails ? "%d FAILED\n" : "all keymap tests passed\n", fails);
    return fails != 0;
}
