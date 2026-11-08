package javax.microedition.lcdui;

/* Phone makers used different key codes for the arrows, the centre key and the soft keys.
 * Inside PSPJ2ME keys use the Nokia numbering; this translates to and from the numbering of the
 * phone the game was built for (chosen per game: automatically or in the game's settings). */
final class KeyCodes {
    /* index: internal code -1..-8 (up, down, left, right, fire, soft1, soft2, clear) */
    private static final int[][] PROFILES = {
        { -1, -2, -3, -4, -5, -6, -7, -8 },           /* Nokia, Sony Ericsson, Samsung (and the default) */
        { -59, -60, -61, -62, -26, -1, -4, -12 },     /* Siemens */
        { -1, -6, -2, -5, -20, -21, -22, -8 },        /* Motorola */
        { -1, -2, -3, -4, -5, -202, -203, -204 },     /* LG */
    };
    private static int[] map;

    private static int[] map() {
        if (map == null) {
            int p = pspj2me.Native.keyProfile() - 1;   /* KP_NOKIA = 1 */
            map = PROFILES[p >= 0 && p < PROFILES.length ? p : 0];
        }
        return map;
    }

    /** internal code -> what the game expects */
    static int toGame(int code) {
        return code <= -1 && code >= -8 ? map()[-code - 1] : code;
    }

    /** a code the game uses -> internal code (0 if it isn't a phone key) */
    static int fromGame(int code) {
        if (code >= 0) return code;
        int[] m = map();
        for (int i = 0; i < m.length; i++) if (m[i] == code) return -(i + 1);
        return 0;
    }
}
