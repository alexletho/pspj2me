#ifndef ZIP_H
#define ZIP_H
#include <stdint.h>
#include <stddef.h>

typedef struct ZipEntry {
    char *name;
    uint32_t method;
    uint32_t compSize;
    uint32_t size;
    uint32_t localOffset;
    struct ZipEntry *next; /* hash chain */
} ZipEntry;

typedef struct Zip {
    uint8_t *data;      /* whole archive in memory, or NULL for a lazy (file-backed) zip */
    void *file;         /* FILE * of a lazy zip */
    size_t len;
    int ownsData;
    int nEntries;
    ZipEntry *entries;
    ZipEntry **buckets;
    int nBuckets;
} Zip;

/* Opens a zip from a memory buffer. If takeOwnership, the buffer is free()d on close. */
Zip *zip_open_mem(uint8_t *data, size_t len, int takeOwnership);
Zip *zip_open_file(const char *path);
/* Reads only the central directory; entries are read from the file on demand.
   Cheap way to look at a few entries (manifest, icon) of a big jar. */
Zip *zip_open_file_lazy(const char *path);
void zip_close(Zip *z);
ZipEntry *zip_find(Zip *z, const char *name);
/* Returns malloc'ed buffer with uncompressed data (with an extra 0 byte appended). */
uint8_t *zip_read(Zip *z, ZipEntry *e, uint32_t *outLen);

/* raw deflate decoder; returns 0 on success */
int inflate_raw(const uint8_t *src, size_t srcLen, uint8_t *dst, size_t dstLen, size_t *outLen);
/* zlib wrapper (2-byte header) */
int inflate_zlib(const uint8_t *src, size_t srcLen, uint8_t *dst, size_t dstLen, size_t *outLen);

#endif
