#ifndef CARRIER_H
#define CARRIER_H

#include <stddef.h>
#include <stdint.h>

int      hu_parse_hex(const char *s, uint8_t *out, size_t max);
uint16_t hu_crc16(const uint8_t *d, size_t n);
uint32_t hu_fnv1a32(const uint8_t *d, size_t n);
int      hu_popcount8(uint8_t b);
void     hu_u32_hex(uint32_t v, char *o);

uint32_t hu_crc32(const uint8_t *d, size_t n);
uint64_t hu_fnv1a64(const uint8_t *d, size_t n);

unsigned hu_selftest(void);
uint64_t hu_sink_value(void);

int hu_file_checksums(const char *path, char *out, size_t outsz);

const char *hu_usage_text(void);
const char *hu_version_text(void);

#endif
