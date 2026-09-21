
#include "stegcode.h"
#include "shapes.h"
#include "protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t xs_next(uint32_t *x)
{
    *x ^= (*x << 13) & 0xFFFFFFFFu;
    *x ^= *x >> 17;
    *x ^= (*x << 5) & 0xFFFFFFFFu;
    return *x;
}

int zs_steg_decode(const uint64_t *loc, int nloc, uint32_t key,
                   uint8_t **out, size_t *outlen, int *shape,
                   char *err, size_t errsz)
{
    *out = NULL;
    if (err && errsz) err[0] = 0;
    if (nloc < ZS_SYNC_N + 6 + 2) {
        if (err) snprintf(err, errsz, "too few functions (%d)", nloc);
        return -1;
    }
    int pat[ZS_SYNC_N];
    zs_sync_pattern(key, pat);

    for (int i = 0; i + ZS_SYNC_N + 6 + 1 < nloc; i++) {
        int ok = 1;
        for (int j = 0; j < ZS_SYNC_N; j++) {
            uint64_t d = loc[i + j + 1] - loc[i + j];
            if (d != (uint64_t)pat[j]) { ok = 0; break; }
        }
        if (!ok) continue;

        uint64_t d0 = loc[i + 8 + 1] - loc[i + 8];
        uint64_t d1 = loc[i + 9 + 1] - loc[i + 9];
        uint64_t d2 = loc[i + 10 + 1] - loc[i + 10];
        uint64_t d3 = loc[i + 11 + 1] - loc[i + 11];
        uint64_t d4 = loc[i + 12 + 1] - loc[i + 12];
        uint64_t d5 = loc[i + 13 + 1] - loc[i + 13];
        if (d0 < 2 || d0 > 257 || d1 < 2 || d1 > 257 || d2 < 2 || d2 > 257 ||
            d3 < 2 || d3 > 257 || d4 < 2 || d4 > 257 || d5 < 2 || d5 > 257)
            continue;
        int shp = (int)d0 - 2;
        if (shp >= ZS_SHAPES) continue;
        size_t ctlen = (size_t)(d1 - 2) | ((size_t)(d2 - 2) << 8) |
                       ((size_t)(d3 - 2) << 16) | ((size_t)(d4 - 2) << 24);
        int csum = (int)d5 - 2;
        if (ctlen == 0 || ctlen > PAY_MAX_CT) continue;

        int ndata = nloc - i - 15;
        if (ndata < 1) continue;

        zs_huff_t huff;
        zs_huff_build(shp, &huff);
        uint8_t *ct = malloc(ctlen);
        if (!ct) continue;
        zs_bw_t bw;
        zs_bw_init(&bw, ct, ctlen);
        int done = 1;
        for (int j = 0; j < ndata; j++) {
            uint64_t d = loc[i + 14 + j + 1] - loc[i + 14 + j];
            if (d < 2 || d > 257) { done = 0; break; }
            zs_bw_symbol(&bw, &huff, (int)d);
            if (bw.nout >= bw.maxbits) break;
        }
        if (!done || bw.nout != bw.maxbits) {
            free(ct);
            continue;
        }

        int cs = 0;
        for (size_t j = 0; j < ctlen; j++) cs ^= ct[j];
        if (cs != csum) {
            free(ct);
            continue;
        }

        uint32_t x = key;
        for (size_t j = 0; j < ctlen; j++)
            ct[j] ^= (uint8_t)xs_next(&x);

        *out = ct;
        *outlen = ctlen;
        if (shape) *shape = shp;
        return 0;
    }
    if (err) snprintf(err, errsz, "decode failed");
    return -1;
}
