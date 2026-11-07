#ifndef KEYMAP_H
#define KEYMAP_H
/* PSP button -> phone key mapping, including the "shoulder shift" combos.
 * Portable (no PSP headers) so it can be unit-tested on the host. */
#include <stdint.h>

/* PSP inputs (index into the key table) */
enum { B_UP = 0, B_DOWN, B_LEFT, B_RIGHT, B_CROSS, B_CIRCLE, B_SQUARE, B_TRIANGLE, B_L, B_R, B_SELECT, B_START,
       B_ANALOG_UP, B_ANALOG_DOWN, B_ANALOG_LEFT, B_ANALOG_RIGHT, B_COUNT };

/* Shoulder shift: while L (or R) is held, these buttons send another key. */
#define KM_COMBO_KEYS 4
extern const int km_comboButtons[KM_COMBO_KEYS];          /* B_UP, B_DOWN, B_CROSS, B_CIRCLE */
extern const int km_comboL[KM_COMBO_KEYS], km_comboR[KM_COMBO_KEYS];

typedef struct {
    int heldCode[B_COUNT];   /* key code currently held down for each input (0 = none) */
    int lDown, rDown;        /* shoulder held (shift mode) */
    int lUsed, rUsed;        /* shoulder was used for a combo while held */
    struct { int code; int64_t at; } pend[8];   /* scheduled key releases (taps) */
} KeyMap;

typedef void (*KeyPostFn)(int type, int code);   /* type: 1 press, 2 release */

/* state[i] = 1 if input i is down. suppressPress[i] = 1 to delay a new press of input i.
 * keys[] = per-input key codes (L/R entries are their tap keys when shoulders is on). */
void keymap_update(KeyMap *km, const int *keys, int shoulders, const int *state, const int *suppressPress,
                   int64_t nowMs, KeyPostFn post);
void keymap_release_all(KeyMap *km, KeyPostFn post);
/* key sent for input `button` while the given shoulder is held; 0 if it isn't a combo */
int keymap_combo_code(int shoulderIsR, int button);

#endif
