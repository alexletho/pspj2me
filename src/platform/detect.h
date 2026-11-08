#ifndef DETECT_H
#define DETECT_H
/* Works out, from a game's .jar (and .jad), which phone it was built for:
 * the screen size and the phone maker's key codes. J2ME games were published as one build per
 * phone family, so the file name, the manifest and the game's own images usually tell. */

/* phone key-code profiles (soft keys / arrows differ between makers) */
enum { KP_AUTO = 0, KP_NOKIA, KP_SIEMENS, KP_MOTOROLA, KP_LG, KP_COUNT };
extern const char *const kp_names[KP_COUNT];

typedef struct {
    int w, h;              /* screen size; 0 if nothing was found */
    char sizeFrom[80];     /* where it came from, for the log/settings ("file name", "manifest", ...) */
    int keys;              /* KP_NOKIA.. (never KP_AUTO) */
    char keysFrom[80];
} GameDetect;

/* fills `out`; never fails (unknown parts stay 0 / KP_NOKIA). */
void detect_game(const char *jarPath, GameDetect *out);

/* the parts on their own (used by the tests) */
int detect_size_from_name(const char *name, int *w, int *h, char *from, int fromLen);
int detect_keys_from_name(const char *name, char *from, int fromLen);
int detect_size_from_value(const char *v, int *w, int *h);

#endif
