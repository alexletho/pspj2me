/* AMR narrowband files ("#!AMR\n", RFC 4867 storage format) -> 8 kHz mono PCM, using the
 * opencore-amr decoder in third_party/opencore-amr (Apache License 2.0). */
#include "amr.h"
#include <stdlib.h>
#include <string.h>
#include "sp_dec.h"
#include "amrdecode.h"
#include "pvgsmamrdecoderinterface.h"

#define AMR_MAX_SECONDS 600

int16_t *amr_decode(const uint8_t *d, int len, int *outFrames) {
    /* speech bytes after the 1-byte header, by frame type (3GPP TS 26.101) */
    static const int sizes[16] = { 12, 13, 15, 17, 19, 20, 26, 31, 5, 6, 5, 5, 0, 0, 0, 0 };
    if (len < 7 || memcmp(d, "#!AMR\n", 6) != 0) return NULL;   /* AMR-WB ("#!AMR-WB\n") and multichannel: not handled */
    int maxFrames = AMR_MAX_SECONDS * 50;
    /* count frames first so the output is allocated once */
    int n = 0;
    for (int pos = 6; pos < len && n < maxFrames; n++) {
        int size = sizes[(d[pos] >> 3) & 15];
        if (pos + 1 + size > len) break;
        pos += 1 + size;
    }
    if (n == 0) return NULL;
    int16_t *pcm = malloc((size_t)n * 160 * sizeof(int16_t));
    if (!pcm) return NULL;
    void *st = NULL;
    if (GSMInitDecode(&st, (Word8 *)"PSPJ2ME") != 0 || !st) { free(pcm); return NULL; }
    int pos = 6;
    for (int i = 0; i < n; i++) {
        int ft = (d[pos] >> 3) & 15, size = sizes[ft];
        UWord8 frame[32];                      /* the decoder may read a little past the frame: pad with zeros */
        memset(frame, 0, sizeof frame);
        memcpy(frame, d + pos + 1, size);
        if (ft >= 12 && ft <= 14) ft = AMR_NO_DATA;   /* reserved types */
        AMRDecode(st, (enum Frame_Type_3GPP)ft, frame, pcm + i * 160, MIME_IETF);
        pos += 1 + size;
    }
    GSMDecodeFrameExit(&st);
    *outFrames = n * 160;
    return pcm;
}
