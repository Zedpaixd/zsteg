
#ifndef ZSTEG_SHAPES_H
#define ZSTEG_SHAPES_H

#include <stdint.h>
#include <string.h>

#define ZS_SHAPE_NATURAL 0
#define ZS_SHAPE_LEAN    1
#define ZS_SHAPES        2
#define ZS_MAXK          24

#if defined(__GNUC__)
#define ZS_UNUSED __attribute__((unused))
#else
#define ZS_UNUSED
#endif

static const uint8_t zs_ktab_natural[256] = {
    0, 0, 0, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11,
    11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
    14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17,
    17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19,
    19, 19, 19, 19, 19, 19, 19, 20, 20, 20, 20, 20, 20, 20, 20, 20,
    20, 20, 20, 20, 20, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
    21, 21, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
};

static const uint8_t zs_ktab_lean[256] = {
    0, 0, 0, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
};

static const uint8_t *const zs_ktabs[ZS_SHAPES] = {
    zs_ktab_natural, zs_ktab_lean,
};

typedef struct {
    const uint8_t *k;
    int maxk;
    uint32_t first[ZS_MAXK + 1];
    uint16_t cnt[ZS_MAXK + 1];
    uint16_t base[ZS_MAXK + 1];
    uint32_t code[256];
} zs_huff_t;

static void zs_huff_build(int shape, zs_huff_t *h)
{
    memset(h, 0, sizeof *h);
    h->k = zs_ktabs[shape];
    int cnt[ZS_MAXK + 1];
    memset(cnt, 0, sizeof cnt);
    for (int i = 0; i < 256; i++)
        if (h->k[i]) cnt[h->k[i]]++;
    int maxk = 0;
    for (int d = 1; d <= ZS_MAXK; d++)
        if (cnt[d]) maxk = d;
    h->maxk = maxk;
    uint32_t first = 0;
    for (int d = 1; d <= maxk; d++) {
        h->first[d] = first;
        h->cnt[d] = (uint16_t)cnt[d];
        first = (first + (uint32_t)cnt[d]) << 1;
    }
    for (int d = 1; d <= maxk; d++) {
        for (int i = 0; i < 256; i++) {
            if (h->k[i] == d) { h->base[d] = (uint16_t)(i + 2); break; }
        }
    }
    int rank[256];
    memset(rank, 0, sizeof rank);
    for (int i = 0; i < 256; i++) {
        if (!h->k[i]) continue;
        for (int j = 0; j < i; j++)
            if (h->k[j] == h->k[i]) rank[i]++;
    }
    for (int i = 0; i < 256; i++) {
        if (!h->k[i]) continue;
        int d = h->k[i];
        h->code[i] = h->first[d] + (uint32_t)rank[i];
    }
}

typedef struct {
    uint8_t *out;
    size_t maxbits;
    uint64_t buf;
    int nb;
    size_t nout;
} zs_bw_t;

static ZS_UNUSED void zs_bw_init(zs_bw_t *w, uint8_t *out, size_t maxbytes)
{
    w->out = out;
    w->maxbits = maxbytes * 8;
    w->buf = 0;
    w->nb = 0;
    w->nout = 0;
}

static ZS_UNUSED int zs_bw_symbol(zs_bw_t *w, const zs_huff_t *h, int length)
{
    if (w->nout >= w->maxbits) return 0;
    int idx = length - 2;
    int k = h->k[idx];
    uint32_t c = h->code[idx];
    for (int i = k - 1; i >= 0; i--) {
        if (w->nout >= w->maxbits) break;
        w->buf |= (uint64_t)((c >> i) & 1u) << (63 - w->nb);
        w->nb++;
        w->nout++;
        if (w->nb == 8) {
            w->out[(w->nout / 8) - 1] = (uint8_t)(w->buf >> 56);
            w->buf <<= 8;
            w->nb = 0;
        }
    }
    return 1;
}

typedef struct {
    const uint8_t *in;
    size_t inlen;
    size_t pos;
    uint64_t buf;
    int nb;
    size_t nconsumed;
} zs_br_t;

static ZS_UNUSED void zs_br_init(zs_br_t *r, const uint8_t *in, size_t inlen)
{
    r->in = in;
    r->inlen = inlen;
    r->pos = 0;
    r->buf = 0;
    r->nb = 0;
    r->nconsumed = 0;
}

static void zs_br_refill(zs_br_t *r)
{
    while (r->nb < ZS_MAXK) {
        uint8_t b = r->pos < r->inlen ? r->in[r->pos++] : 0;
        r->buf |= (uint64_t)b << (56 - r->nb);
        r->nb += 8;
    }
}

static ZS_UNUSED int zs_br_symbol(zs_br_t *r, const zs_huff_t *h)
{
    zs_br_refill(r);
    for (int d = 1; d <= h->maxk; d++) {
        uint32_t p = (uint32_t)(r->buf >> (64 - d));
        if (h->cnt[d] && p >= h->first[d] &&
            p - h->first[d] < h->cnt[d]) {

            int rr = (int)(p - h->first[d]);
            int length = 2;
            for (int l = 2; l <= 257; l++) {
                if (h->k[l - 2] == d) {
                    if (rr == 0) { length = l; break; }
                    rr--;
                }
            }
            r->buf <<= d;
            r->nb -= d;
            r->nconsumed += (size_t)d;
            if (length < 2 || length > 257) return 0;
            return length;
        }
    }
    return 0;
}

#define ZS_SYNC_N 8

static void zs_sync_pattern(uint32_t key, int pat[ZS_SYNC_N])
{
    uint32_t x = key ? key : 0x6A2B3C4Du;
    for (int i = 0; i < ZS_SYNC_N; i++) {
        x ^= (x << 13) & 0xFFFFFFFFu;
        x ^= x >> 17;
        x ^= (x << 5) & 0xFFFFFFFFu;
        pat[i] = 34 + (int)(x % 61u);
    }
}

#endif
