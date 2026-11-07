#ifndef AMR_H
#define AMR_H
#include <stdint.h>
/* decodes a whole .amr (narrowband) file to 8 kHz mono; malloc'ed, NULL if not AMR-NB */
int16_t *amr_decode(const uint8_t *data, int len, int *outFrames);
#endif
