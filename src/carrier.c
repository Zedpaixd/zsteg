
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "carrier.h"

uint8_t  hu_hexval(char c)         { return (uint8_t)((c <= '9') ? (c - '0') : ((c | 0x20) - 'a' + 10)); }
int      hu_ishex(char c)          { return (c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'f'); }
uint8_t  hu_nibble_hi(uint8_t b)   { return b >> 4; }
uint8_t  hu_nibble_lo(uint8_t b)   { return b & 0x0F; }
uint8_t  hu_hex_u8(const char *s)  { return (uint8_t)((hu_hexval(s[0]) << 4) | hu_hexval(s[1])); }
uint16_t hu_hex_u16(const char *s) { return (uint16_t)((hu_hex_u8(s) << 8) | hu_hex_u8(s + 2)); }
uint32_t hu_hex_u32(const char *s) { return ((uint32_t)hu_hex_u16(s) << 16) | hu_hex_u16(s + 4); }
char     hu_hexchar(uint8_t v)     { return (v < 10) ? (char)('0' + v) : (char)('a' + (v - 10)); }
void     hu_u8_hex(uint8_t b, char *o)      { o[0] = hu_hexchar(hu_nibble_hi(b)); o[1] = hu_hexchar(hu_nibble_lo(b)); o[2] = '\0'; }
void     hu_u16_hex(uint16_t v, char *o)    { hu_u8_hex((uint8_t)(v >> 8), o); hu_u8_hex((uint8_t)v, o + 2); }
void     hu_u32_hex(uint32_t v, char *o)    { hu_u16_hex((uint16_t)(v >> 16), o); hu_u16_hex((uint16_t)v, o + 4); }
int      hu_parse_hex(const char *s, uint8_t *out, size_t max) { size_t n = 0; while (hu_ishex(s[0]) && hu_ishex(s[1]) && n < max) { out[n++] = hu_hex_u8(s); s += 2; } return (int)n; }

int hu_isdigit(char c) { return c >= '0' && c <= '9'; }
int hu_isupper(char c) { return c >= 'A' && c <= 'Z'; }
int hu_islower(char c) { return c >= 'a' && c <= 'z'; }
char hu_toupper(char c){ return hu_islower(c) ? (char)(c - 32) : c; }
char hu_tolower(char c){ return hu_isupper(c) ? (char)(c + 32) : c; }

size_t hu_strnlen(const char *s, size_t max) { size_t n = 0; while (n < max && s[n]) n++; return n; }
int    hu_streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
int    hu_prefix_eq(const char *s, const char *p) { while (*p) { if (*s++ != *p++) return 0; } return 1; }
char * hu_trim(char *s) { while (*s == ' ' || *s == '\t') s++; return s; }

uint8_t  hu_reverse8(uint8_t b)  { b = (uint8_t)(((b >> 1) & 0x55) | ((b << 1) & 0xAA)); b = (uint8_t)(((b >> 2) & 0x33) | ((b << 2) & 0xCC)); return (uint8_t)(((b >> 4) | (b << 4)) & 0xFF); }
uint16_t hu_reverse16(uint16_t v){ return (uint16_t)((hu_reverse8((uint8_t)v) << 8) | hu_reverse8((uint8_t)(v >> 8))); }
uint32_t hu_reverse32(uint32_t v){ return ((uint32_t)hu_reverse16((uint16_t)v) << 16) | hu_reverse16((uint16_t)(v >> 16)); }
uint8_t  hu_rol8(uint8_t b, unsigned n)  { n &= 7; return (uint8_t)((b << n) | (b >> (8 - n))); }
uint16_t hu_rol16(uint16_t v, unsigned n){ n &= 15; return (uint16_t)((v << n) | (v >> (16 - n))); }
uint32_t hu_rol32(uint32_t v, unsigned n){ n &= 31; return (v << n) | (v >> (32 - n)); }
uint8_t  hu_ror8(uint8_t b, unsigned n)  { return hu_rol8(b, (unsigned)(8 - (n & 7))); }
uint16_t hu_ror16(uint16_t v, unsigned n){ return hu_rol16(v, (unsigned)(16 - (n & 15))); }
uint32_t hu_ror32(uint32_t v, unsigned n){ return hu_rol32(v, (unsigned)(32 - (n & 31))); }
uint16_t hu_bswap16(uint16_t v)  { return (uint16_t)((v >> 8) | (v << 8)); }
uint32_t hu_bswap32(uint32_t v)  { return (uint32_t)(hu_bswap16((uint16_t)v) << 16) | hu_bswap16((uint16_t)(v >> 16)); }
uint64_t hu_bswap64(uint64_t v)  { return ((uint64_t)hu_bswap32((uint32_t)v) << 32) | hu_bswap32((uint32_t)(v >> 32)); }
int      hu_clz32(uint32_t v)    { int n = 0; if (!v) return 32; if (!(v & 0xFFFF0000u)) { n += 16; v <<= 16; } if (!(v & 0xFF000000u)) { n += 8; v <<= 8; } if (!(v & 0xF0000000u)) { n += 4; v <<= 4; } if (!(v & 0xC0000000u)) { n += 2; v <<= 2; } if (!(v & 0x80000000u)) n++; return n; }
int      hu_ctz32(uint32_t v)    { return v ? (31 - hu_clz32(v & (uint32_t)-(int64_t)v)) : 32; }
int      hu_popcount8(uint8_t b) { b = (uint8_t)(b - ((b >> 1) & 0x55)); b = (uint8_t)((b & 0x33) + ((b >> 2) & 0x33)); return (int)((b + (b >> 4)) & 0x0F); }
int      hu_popcount16(uint16_t v) { return hu_popcount8((uint8_t)v) + hu_popcount8((uint8_t)(v >> 8)); }
int      hu_popcount32(uint32_t v) { return hu_popcount16((uint16_t)v) + hu_popcount16((uint16_t)(v >> 16)); }
int      hu_popcount64(uint64_t v) { return hu_popcount32((uint32_t)v) + hu_popcount32((uint32_t)(v >> 32)); }

uint8_t  hu_crc8(const uint8_t *d, size_t n) { uint8_t crc = 0; for (size_t i = 0; i < n; i++) { crc ^= d[i]; for (int j = 0; j < 8; j++) crc = (uint8_t)((crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1)); } return crc; }
uint16_t hu_crc16(const uint8_t *d, size_t n) { uint16_t crc = 0xFFFF; for (size_t i = 0; i < n; i++) { crc ^= (uint16_t)d[i] << 8; for (int j = 0; j < 8; j++) crc = (uint16_t)((crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1)); } return (uint16_t)~crc; }
uint32_t hu_crc32(const uint8_t *d, size_t n) { uint32_t crc = 0xFFFFFFFF; for (size_t i = 0; i < n; i++) { crc ^= d[i]; for (int j = 0; j < 8; j++) crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int64_t)(crc & 1)); } return ~crc; }
uint32_t hu_fnv1a32(const uint8_t *d, size_t n) { uint32_t h = 0x811C9DC5u; for (size_t i = 0; i < n; i++) { h ^= d[i]; h *= 0x01000193u; } return h; }
uint64_t hu_fnv1a64(const uint8_t *d, size_t n) { uint64_t h = 0xCBF29CE484222325ull; for (size_t i = 0; i < n; i++) { h ^= d[i]; h *= 0x100000001B3ull; } return h; }
uint8_t  hu_checksum8(const uint8_t *d, size_t n) { uint8_t s = 0; for (size_t i = 0; i < n; i++) s = (uint8_t)(s + d[i]); return (uint8_t)(0x100 - s); }
uint16_t hu_checksum16(const uint8_t *d, size_t n) { uint32_t s = 0; for (size_t i = 0; i < n; i++) s += d[i]; return (uint16_t)((s & 0xFFFF) + (s >> 16)); }
uint32_t hu_sum32(const uint8_t *d, size_t n) { uint32_t s = 0; for (size_t i = 0; i < n; i++) s += d[i]; return s; }

uint8_t  hu_xor8(const uint8_t *d, size_t n) { uint8_t x = 0; for (size_t i = 0; i < n; i++) x ^= d[i]; return x; }
void     hu_memxor(uint8_t *d, const uint8_t *k, size_t n) { for (size_t i = 0; i < n; i++) d[i] ^= k[i % 16]; }
int      hu_find_byte(const uint8_t *d, size_t n, uint8_t b) { for (size_t i = 0; i < n; i++) if (d[i] == b) return (int)i; return -1; }
int      hu_count_byte(const uint8_t *d, size_t n, uint8_t b) { int c = 0; for (size_t i = 0; i < n; i++) if (d[i] == b) c++; return c; }
uint8_t  hu_max_u8(const uint8_t *d, size_t n) { uint8_t m = 0; for (size_t i = 0; i < n; i++) if (d[i] > m) m = d[i]; return m; }
uint8_t  hu_min_u8(const uint8_t *d, size_t n) { uint8_t m = 0xFF; for (size_t i = 0; i < n; i++) if (d[i] < m) m = d[i]; return m; }
uint8_t  hu_avg_u8(const uint8_t *d, size_t n) { return n ? (uint8_t)(hu_sum32(d, n) / (uint32_t)n) : 0; }
uint8_t  hu_clamp_u8(uint8_t v, uint8_t lo, uint8_t hi) { return (v < lo) ? lo : (v > hi ? hi : v); }
int      hu_parity8(uint8_t b) { return hu_popcount8(b) & 1; }
uint8_t  hu_gray8(uint8_t b)   { return (uint8_t)(b ^ (b >> 1)); }
uint8_t  hu_ungray8(uint8_t g) { uint8_t b = 0; while (g) { b ^= g; g >>= 1; } return b; }

size_t hu_count_upper(const char *s) { size_t n = 0; for (; *s; s++) if (hu_isupper(*s)) n++; return n; }
size_t hu_count_lower(const char *s) { size_t n = 0; for (; *s; s++) if (hu_islower(*s)) n++; return n; }
size_t hu_count_digits(const char *s) { size_t n = 0; for (; *s; s++) if (hu_isdigit(*s)) n++; return n; }
int    hu_has_suffix(const char *s, const char *suf) { size_t ls = hu_strnlen(s, 1024), lf = hu_strnlen(suf, 128); if (lf > ls) return 0; return hu_streq(s + ls - lf, suf); }
size_t hu_index_of(const char *s, char c) { for (size_t i = 0; s[i]; i++) if (s[i] == c) return i; return (size_t)-1; }
size_t hu_last_index_of(const char *s, char c) { size_t r = (size_t)-1; for (size_t i = 0; s[i]; i++) if (s[i] == c) r = i; return r; }
int    hu_isalnum(char c) { return hu_isdigit(c) || hu_isupper(c) || hu_islower(c); }
int    hu_isspace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
int    hu_isprint(char c) { return c >= 0x20 && c <= 0x7E; }
int    hu_isctrl(char c)  { return c < 0x20 || c == 0x7F; }

uint64_t hu_rol64(uint64_t v, unsigned n) { n &= 63; return n ? ((v << n) | (v >> (64 - n))) : v; }
uint64_t hu_ror64(uint64_t v, unsigned n) { return hu_rol64(v, (unsigned)(64 - (n & 63))); }
uint64_t hu_reverse64(uint64_t v) { return ((uint64_t)hu_reverse32((uint32_t)v) << 32) | hu_reverse32((uint32_t)(v >> 32)); }
int      hu_clz64(uint64_t v) { if (!v) return 64; int n = 0; if (!(v >> 32)) { n += 32; v <<= 32; } return n + hu_clz32((uint32_t)v); }
int      hu_ctz64(uint64_t v) { return v ? (63 - hu_clz64(v & (uint64_t)-(int64_t)v)) : 64; }
uint64_t hu_bit_set(uint64_t v, unsigned i) { return v | (1ull << (i & 63)); }
uint64_t hu_bit_clear(uint64_t v, unsigned i) { return v & ~(1ull << (i & 63)); }
uint64_t hu_bit_toggle(uint64_t v, unsigned i) { return v ^ (1ull << (i & 63)); }
int      hu_bit_test(uint64_t v, unsigned i) { return (int)((v >> (i & 63)) & 1); }
uint64_t hu_mask_low(unsigned n) { return n >= 64 ? ~0ull : ((1ull << n) - 1); }
uint64_t hu_mask_high(unsigned n) { return n ? hu_mask_low(n) << (64 - n) : 0; }

int      hu_abs32(int v) { return v < 0 ? -v : v; }
int64_t  hu_abs64(int64_t v) { return v < 0 ? -v : v; }
uint16_t hu_min16(uint16_t a, uint16_t b) { return a < b ? a : b; }
uint16_t hu_max16(uint16_t a, uint16_t b) { return a > b ? a : b; }
uint32_t hu_min32(uint32_t a, uint32_t b) { return a < b ? a : b; }
uint32_t hu_max32(uint32_t a, uint32_t b) { return a > b ? a : b; }
int      hu_clamp32(int v, int lo, int hi) { if (v < lo) return lo; if (v > hi) return hi; return v; }
uint32_t hu_avg32(uint32_t a, uint32_t b) { return a / 2 + b / 2 + ((a & 1u) & (b & 1u)); }
int      hu_is_pow2(uint64_t v) { return v && !(v & (v - 1)); }
uint32_t hu_pow2_floor32(uint32_t v) { if (!v) return 0; return 1u << (31 - hu_clz32(v)); }
uint32_t hu_pow2_ceil32(uint32_t v) { if (!v || hu_is_pow2(v)) return v; return hu_pow2_floor32(v) << 1; }
int      hu_log2_32(uint32_t v) { return v ? (31 - hu_clz32(v)) : -1; }
uint32_t hu_gcd32(uint32_t a, uint32_t b) { while (b) { uint32_t t = a % b; a = b; b = t; } return a; }
uint32_t hu_lcm32(uint32_t a, uint32_t b) { return b ? (a / hu_gcd32(a, b)) * b : 0; }

uint8_t  hu_swap_nibbles(uint8_t b) { return (uint8_t)((b << 4) | (b >> 4)); }
uint8_t  hu_bcd2bin(uint8_t b) { return (uint8_t)(((b >> 4) & 0x0F) * 10 + (b & 0x0F)); }
uint8_t  hu_bin2bcd(uint8_t b) { return (uint8_t)(((b / 10) << 4) | (b % 10)); }
uint8_t  hu_bcd_add(uint8_t a, uint8_t b) { return hu_bin2bcd((uint8_t)(hu_bcd2bin(a) + hu_bcd2bin(b))); }

uint32_t hu_djb2(const uint8_t *d, size_t n) { uint32_t h = 5381; for (size_t i = 0; i < n; i++) h = h * 33 + d[i]; return h; }
uint32_t hu_sdbm(const uint8_t *d, size_t n) { uint32_t h = 0; for (size_t i = 0; i < n; i++) h = d[i] + (h << 6) + (h << 16) - h; return h; }
uint32_t hu_jenkins(const uint8_t *d, size_t n) { uint32_t h = 0; for (size_t i = 0; i < n; i++) { h += d[i]; h += h << 10; h ^= h >> 6; } h += h << 3; h ^= h >> 11; h += h << 15; return h; }
uint16_t hu_fletcher16(const uint8_t *d, size_t n) { uint16_t a = 0, b = 0; for (size_t i = 0; i < n; i++) { a = (uint16_t)(a + d[i]); b = (uint16_t)(b + a); } return (uint16_t)((b << 8) | a); }
uint32_t hu_adler32(const uint8_t *d, size_t n) { uint32_t a = 1, b = 0; for (size_t i = 0; i < n; i++) { a = (a + d[i]) % 65521; b = (b + a) % 65521; } return (b << 16) | a; }

void hu_u8_dec(uint8_t v, char *o) { char t[4]; int i = 0; do { t[i++] = (char)('0' + v % 10); v = (uint8_t)(v / 10); } while (v); int j = 0; while (i) o[j++] = t[--i]; o[j] = '\0'; }
void hu_u16_dec(uint16_t v, char *o) { char t[6]; int i = 0; do { t[i++] = (char)('0' + v % 10); v = (uint16_t)(v / 10); } while (v); int j = 0; while (i) o[j++] = t[--i]; o[j] = '\0'; }
void hu_u32_dec(uint32_t v, char *o) { char t[11]; int i = 0; do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v); int j = 0; while (i) o[j++] = t[--i]; o[j] = '\0'; }
size_t hu_bin_str8(uint8_t b, char *o) { for (int i = 7; i >= 0; i--) o[7 - i] = (char)('0' + ((b >> i) & 1)); o[8] = '\0'; return 8; }
size_t hu_bin_str16(uint16_t v, char *o) { for (int i = 15; i >= 0; i--) o[15 - i] = (char)('0' + ((v >> i) & 1)); o[16] = '\0'; return 16; }
size_t hu_oct_str32(uint32_t v, char *o) { char t[12]; int i = 0; do { t[i++] = (char)('0' + (v & 7)); v >>= 3; } while (v); int j = 0; while (i) o[j++] = t[--i]; o[j] = '\0'; return (size_t)j; }

int      hu_cmp_u8(uint8_t a, uint8_t b) { return (a > b) - (a < b); }
int      hu_cmp_u16(uint16_t a, uint16_t b) { return (a > b) - (a < b); }
int      hu_cmp_u32(uint32_t a, uint32_t b) { return (a > b) - (a < b); }
uint8_t  hu_select_u8(int c, uint8_t a, uint8_t b) { return c ? a : b; }
uint16_t hu_select_u16(int c, uint16_t a, uint16_t b) { return c ? a : b; }
uint32_t hu_select_u32(int c, uint32_t a, uint32_t b) { return c ? a : b; }
int8_t   hu_sign_ext8(uint8_t b) { return (int8_t)b; }
int16_t  hu_sign_ext16(uint16_t v) { return (int16_t)v; }
uint16_t hu_merge16(uint16_t hi, uint16_t lo) { return (uint16_t)((hi << 8) | (lo & 0xFF)); }
uint32_t hu_merge8(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | d; }

extern uint32_t (*const gst_slot_table[])(uint32_t);

static volatile uint64_t hu_sink;

unsigned hu_selftest(void)
{
    uint64_t sink = 0x6A09E667F3BCC909ull;
    unsigned n = 0;
    for (unsigned i = 0; gst_slot_table[i]; i++) {
        uint32_t r = gst_slot_table[i](0x9E3779B9u ^ (uint32_t)i);
        sink ^= (uint64_t)r * (uint64_t)(i + 1);
        sink = hu_rol64(sink, 13) ^ (sink >> 7);
        n++;
    }
    hu_sink = sink;
    return n;
}

uint64_t hu_sink_value(void) { return hu_sink; }

int hu_file_checksums(const char *path, char *out, size_t outsz)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    if (n < 0 || n > (1L << 23)) { fclose(f); return -1; }
    uint8_t *buf = malloc((size_t)(n ? n : 1));
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    char hex[20];
    hu_u32_hex(hu_crc32(buf, (size_t)n), hex);
    int m = snprintf(out, outsz, "file %s: %ld bytes  crc32=%s  fnv1a64=%016llx",
                     path, n, hex,
                     (unsigned long long)hu_fnv1a64(buf, (size_t)n));
    free(buf);
    return m;
}

const char *hu_usage_text(void)
{
    return
        "hexkit 1.2.0 :---: hex & checksum utility\n"
        "\n"
        "usage: hexkit [options] [hexstring]\n"
        "\n"
        "  hexstring       hex bytes to analyze (default: deadbeef)\n"
        "  -s, --selftest  registry self-test only\n"
        "  -f, --file PATH checksum a file (crc32 + fnv1a64)\n"
        "  -v, --version   print version\n"
        "  -h, --help      show this help\n"
        "\n"
        "every run executes the built-in micro-function registry\n"
        "self-test first, then the requested hex analysis.\n";
}

const char *hu_version_text(void)
{
    return "hexkit 1.2.0";
}
