
#ifndef ZSTEG_STEGCODE_H
#define ZSTEG_STEGCODE_H

#include <stddef.h>
#include <stdint.h>

int zs_steg_decode(const uint64_t *loc, int nloc, uint32_t key,
                   uint8_t **out, size_t *outlen, int *shape,
                   char *err, size_t errsz);

#endif
