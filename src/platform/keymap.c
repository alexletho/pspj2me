/* PSP button -> phone key mapping with "shoulder shift" combos.
 *
 * Base layer: every input sends keys[input].
 * Shoulder shift (when enabled): L and R don't send a key when pressed. While one is held,
 * D-pad Up/Down and X/O send the combo keys below. If L/R is released without having been
 * used for a combo, its own key (keys[B_L] / keys[B_R], 1 and 3 by default) is tapped. */
#include "keymap.h"
#include "platform.h"
#include <string.h>

const int km_comboButtons[KM_COMBO_KEYS] = { B_UP, B_DOWN, B_CROSS, B_CIRCLE };
const int km_comboL[KM_COMBO_KEYS] = { '1', '7', JK_FIRE, JK_CLEAR };
const int km_comboR[KM_COMBO_KEYS] = { '3', '9', JK_FIRE, JK_CLEAR };

int keymap_combo_code(int shoulderIsR, int button) {
    for (int i = 0; i < KM_COMBO_KEYS; i++)
        if (km_comboButtons[i] == button) return shoulderIsR ? km_comboR[i] : km_comboL[i];
    return 0;
}

static void tap(KeyMap *km, int code, int64_t now, KeyPostFn post) {
    if (!code) return;
    post(KEY_PRESS, code);
    for (int i = 0; i < 8; i++)
        if (!km->pend[i].code) { km->pend[i].code = code; km->pend[i].at = now + 80; return; }
    post(KEY_RELEASE, code); /* no free slot: release immediately */
}

void keymap_update(KeyMap *km, const int *keys, int shoulders, const int *state, const int *suppressPress,
                   int64_t now, KeyPostFn post) {
    /* scheduled releases of tapped keys */
    for (int i = 0; i < 8; i++)
        if (km->pend[i].code && now >= km->pend[i].at) { post(KEY_RELEASE, km->pend[i].code); km->pend[i].code = 0; }

    int st[B_COUNT];
    memcpy(st, state, sizeof st);
    if (shoulders) {
        /* L / R act as shift buttons */
        if (st[B_L] && !km->lDown) { km->lDown = 1; km->lUsed = 0; }
        if (!st[B_L] && km->lDown) { km->lDown = 0; if (!km->lUsed) tap(km, keys[B_L], now, post); }
        if (st[B_R] && !km->rDown) { km->rDown = 1; km->rUsed = 0; }
        if (!st[B_R] && km->rDown) { km->rDown = 0; if (!km->rUsed) tap(km, keys[B_R], now, post); }
        /* a shoulder that was a plain key before combos were enabled must not stay stuck */
        st[B_L] = st[B_R] = 0;
    } else {
        km->lDown = km->rDown = 0;
    }
    for (int i = 0; i < B_COUNT; i++) {
        if (st[i] && !km->heldCode[i]) {
            if (suppressPress && suppressPress[i]) continue;
            int code = keys[i];
            if (shoulders && (km->lDown || km->rDown)) {
                int c = keymap_combo_code(!km->lDown, i);   /* L wins if both are held */
                if (c) code = c;
                /* any button pressed while a shoulder is held cancels that shoulder's tap */
                if (km->lDown) km->lUsed = 1;
                if (km->rDown) km->rUsed = 1;
            }
            if (code) { km->heldCode[i] = code; post(KEY_PRESS, code); }
        } else if (!st[i] && km->heldCode[i]) {
            post(KEY_RELEASE, km->heldCode[i]);
            km->heldCode[i] = 0;
        }
    }
}

void keymap_release_all(KeyMap *km, KeyPostFn post) {
    for (int i = 0; i < B_COUNT; i++)
        if (km->heldCode[i]) { post(KEY_RELEASE, km->heldCode[i]); km->heldCode[i] = 0; }
    for (int i = 0; i < 8; i++)
        if (km->pend[i].code) { post(KEY_RELEASE, km->pend[i].code); km->pend[i].code = 0; }
    /* a shoulder held across a menu must not tap its key afterwards */
    km->lUsed = km->rUsed = 1;
}
