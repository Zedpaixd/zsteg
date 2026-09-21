
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "../src/protocol.h"
#include "../src/shapes.h"
#include "../src/stegcode.h"

static uint32_t xs_next(uint32_t *x)
{
    *x ^= (*x << 13) & 0xFFFFFFFFu;
    *x ^= *x >> 17;
    *x ^= (*x << 5) & 0xFFFFFFFFu;
    return *x & 0xFFFFFFFFu;
}

static void keystream(uint32_t key, uint8_t *out, size_t n)
{
    uint32_t x = key & 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        x = xs_next(&x);
        out[i] = (uint8_t)(x & 0xFF);
    }
}

static uint8_t *encrypt(const uint8_t *in, size_t n, uint32_t key, size_t *outn)
{
    uint8_t *ks = malloc(n ? n : 1);
    uint8_t *out = malloc(n ? n : 1);
    if (!ks || !out) { free(ks); free(out); return NULL; }
    keystream(key, ks, n);
    for (size_t i = 0; i < n; i++)
        out[i] = in[i] ^ ks[i];
    free(ks);
    *outn = n;
    return out;
}

static int zinflate(const uint8_t *src, size_t slen, uint8_t **out, size_t *outlen)
{
    if (slen > UINT_MAX)
        return -1;
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    if (inflateInit2(&zs, -15) != Z_OK)
        return -1;
    size_t cap = slen * 4 + 4096;
    if (cap > PAY_MAX_PLEN) cap = PAY_MAX_PLEN;
    uint8_t *buf = malloc(cap);
    if (!buf) { inflateEnd(&zs); return -1; }
    zs.next_in = (Bytef *)src;
    zs.avail_in = (uInt)slen;
    int r;
    for (;;) {
        if (zs.avail_out == 0) {
            if (zs.total_out >= PAY_MAX_PLEN) { free(buf); inflateEnd(&zs); return -1; }
            cap *= 2;
            if (cap > PAY_MAX_PLEN) cap = PAY_MAX_PLEN;
            uint8_t *n = realloc(buf, cap);
            if (!n) { free(buf); inflateEnd(&zs); return -1; }
            buf = n;
            zs.next_out = buf + zs.total_out;
            zs.avail_out = (uInt)(cap - zs.total_out);
        }
        r = inflate(&zs, Z_NO_FLUSH);
        if (r == Z_STREAM_END) break;
        if (r != Z_OK) { free(buf); inflateEnd(&zs); return -1; }
    }
    *out = buf;
    *outlen = zs.total_out;
    inflateEnd(&zs);
    return 0;
}

static const uint8_t nop_tab[15][15] = {
    { 0x90 },
    { 0x66, 0x90 },
    { 0x0f, 0x1f, 0x00 },
    { 0x0f, 0x1f, 0x40, 0x00 },
    { 0x0f, 0x1f, 0x44, 0x00, 0x00 },
    { 0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00 },
    { 0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00 },
    { 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x66, 0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x66, 0x66, 0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x66, 0x66, 0x66, 0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 },
};

typedef struct { const char *s; int len; int w; unsigned imm; } ctmpl_t;

static const ctmpl_t CODE_LOADS[] = {
    { "movl %edi, %eax",           2, 10, 0 },
    { "movl %esi, %eax",           2,  7, 0 },
    { "movl %edx, %eax",           2,  4, 0 },
    { "movl %ecx, %eax",           2,  3, 0 },
    { "xorl %eax, %eax",           2,  5, 0 },
    { "leal (%rdi,%rdi), %eax",    3,  6, 0 },
    { "leal (%rdi,%rsi), %eax",    3,  3, 0 },
    { "leal (%rsi,%rsi), %eax",    3,  2, 0 },
    { "leal (%rdx,%rdx), %eax",    3,  2, 0 },
    { "leal (%rdi,%rdi,4), %eax",  3,  2, 0 },
    { "leal (%rdi,%rdi,8), %eax",  3,  1, 0 },
};

static const ctmpl_t CODE_OPS[] = {
    { "addl %edi, %eax",           2, 10, 0 },
    { "addl %esi, %eax",           2,  7, 0 },
    { "addl %edx, %eax",           2,  4, 0 },
    { "addl %ecx, %eax",           2,  3, 0 },
    { "subl %edi, %eax",           2,  4, 0 },
    { "subl %esi, %eax",           2,  3, 0 },
    { "subl %edx, %eax",           2,  2, 0 },
    { "xorl %edi, %eax",           2,  5, 0 },
    { "xorl %esi, %eax",           2,  4, 0 },
    { "xorl %edx, %eax",           2,  2, 0 },
    { "andl %edi, %eax",           2,  3, 0 },
    { "orl %edi, %eax",            2,  2, 0 },
    { "cmpl %edi, %eax",           2,  3, 0 },
    { "cmpl %esi, %eax",           2,  2, 0 },
    { "testl %eax, %eax",          2,  4, 0 },
    { "negl %eax",                 2,  3, 0 },
    { "notl %eax",                 2,  2, 0 },
    { "shll $1, %eax",             2,  3, 0 },
    { "shrl $1, %eax",             2,  2, 0 },
    { "sarl $1, %eax",             2,  3, 0 },
    { "roll $1, %eax",             2,  1, 0 },
    { "rorl $1, %eax",             2,  1, 0 },
    { "shll %cl, %eax",            2,  2, 0 },
    { "sarl %cl, %eax",            2,  1, 0 },
    { "movl %eax, %ecx",           2,  2, 0 },
    { "movl %eax, %edx",           2,  1, 0 },
    { "incl %eax",                 2,  2, 0 },
    { "decl %eax",                 2,  2, 0 },
    { "nop",                       1,  3, 0 },
    { "cwtl",                      1,  1, 0 },
    { "cltd",                      1,  1, 0 },
    { "imull %edi, %eax",          3,  5, 0 },
    { "imull %esi, %eax",          3,  3, 0 },
    { "imull %edx, %eax",          3,  2, 0 },
    { "imull $3, %eax",            3,  2, 0 },
    { "imull $5, %eax",            3,  1, 0 },
    { "imull $7, %eax",            3,  2, 0 },
    { "imull $3, %edi, %eax",      3,  2, 0 },
    { "imull $7, %edi, %eax",      3,  1, 0 },
    { "shll $2, %eax",             3,  3, 0 },
    { "shrl $2, %eax",             3,  2, 0 },
    { "sarl $2, %eax",             3,  2, 0 },
    { "shll $3, %eax",             3,  1, 0 },
    { "sarl $3, %eax",             3,  1, 0 },
    { "movzbl %al, %eax",          3,  2, 0 },
    { "addl $%u, %eax",            3,  3, 8 },
    { "subl $%u, %eax",            3,  2, 8 },
    { "xorl $%u, %eax",            3,  3, 8 },
    { "andl $%u, %eax",            3,  3, 8 },
    { "cmpl $%u, %eax",            3,  2, 8 },
    { "leal %u(%rdi,%rdi), %eax",  4,  2, 8 },
    { "leal %u(%rdi,%rdi,4), %eax",4,  1, 8 },
    { "leal %u(%rdi,%rsi), %eax",  4,  1, 8 },
    { "movl $%u, %eax",            5,  1, 32 },
    { "xorl $%u, %eax",            5,  2, 32 },
    { "andl $%u, %eax",            5,  3, 32 },
    { "addl $%u, %eax",            5,  1, 32 },
    { "subl $%u, %eax",            5,  1, 32 },
    { "cmpl $%u, %eax",            5,  1, 32 },
    { "setne %al\n\tmovzbl %al, %eax", 6, 2, 0 },
    { "sete %al\n\tmovzbl %al, %eax",  6, 2, 0 },
    { "setl %al\n\tmovzbl %al, %eax",  6, 1, 0 },
    { "setg %al\n\tmovzbl %al, %eax",  6, 1, 0 },
    { "setle %al\n\tmovzbl %al, %eax", 6, 1, 0 },

    { "testl %eax, %eax\n\tjne 1f\n\tincl %eax\n1:", 6, 2, 0 },
    { "testl %eax, %eax\n\tjs 1f\n\tnegl %eax\n1:", 6, 1, 0 },
    { "testl %edi, %edi\n\tje 1f\n\txorl %edi, %eax\n1:", 6, 1, 0 },
    { "cmpl $0, %eax\n\tje 1f\n\tdecl %eax\n1:", 7, 2, 0 },
    { "cmpl %edi, %eax\n\tje 1f\n\tleal (%rdi,%rdi), %eax\n1:", 7, 1, 0 },
    { "cmpl $1, %eax\n\tjle 1f\n\tsarl $1, %eax\n1:", 7, 1, 0 },
};

static const uint32_t CODE_CONST32[] = {
    0x9E3779B9u, 0x85EBCA6Bu, 0xC2B2AE35u, 0x27D4EB2Fu, 0x165667B1u,
    0x2545F491u, 0x3C6EF372u, 0x5BD1E995u, 0x811C9DC5u, 0x01000193u,
    0x7FEB352Du, 0x846CA68Bu, 0x6A09E667u, 0xBB67AE85u, 0x9E3779B1u,
};
static const uint32_t CODE_CONST8[] = {
    1, 2, 3, 4, 5, 7, 8, 15, 16, 31, 63, 127, 0x3F, 0x7F, 0x0F,

};

static void emit_ctmpl(FILE *out, const ctmpl_t *t, uint32_t *rng)
{

    char buf[160];
    const char *mark = strstr(t->s, "%u");
    if (!mark) {
        fprintf(out, "\t%s\n", t->s);
        return;
    }
    uint32_t c;
    if (t->imm == 32)
        c = CODE_CONST32[xs_next(rng) %
            (sizeof CODE_CONST32 / sizeof *CODE_CONST32)];
    else
        c = CODE_CONST8[xs_next(rng) %
           (sizeof CODE_CONST8 / sizeof *CODE_CONST8)];
    size_t pre = (size_t)(mark - t->s);
    memcpy(buf, t->s, pre);
    int dn = sprintf(buf + pre, "%u", c);
    strcpy(buf + pre + dn, mark + 2);
    fprintf(out, "\t%s\n", buf);
}

static const ctmpl_t *pick_ctmpl(const ctmpl_t *tab, size_t n, uint32_t *rng,
                                 int maxlen)
{
    size_t total = 0;
    for (size_t i = 0; i < n; i++) total += (size_t)tab[i].w;
    for (int tries = 0; tries < 128; tries++) {
        size_t u = xs_next(rng) % total;
        for (size_t i = 0; i < n; i++) {
            if (u < (size_t)tab[i].w)
                return tab[i].len <= maxlen ? &tab[i] : NULL;
            u -= (size_t)tab[i].w;
        }
    }
    return NULL;
}

static const ctmpl_t *pick_len(const ctmpl_t *tab, size_t n, int want,
                               uint32_t *rng)
{
    size_t total = 0;
    for (size_t i = 0; i < n; i++)
        if (tab[i].len == want) total += (size_t)tab[i].w;
    if (!total) return NULL;
    size_t u = xs_next(rng) % total;
    for (size_t i = 0; i < n; i++) {
        if (tab[i].len != want) continue;
        if (u < (size_t)tab[i].w) return &tab[i];
        u -= (size_t)tab[i].w;
    }
    return NULL;
}

static void emit_code_body(FILE *out, int len, uint32_t seed)
{
    uint32_t rng = seed ? seed : 0x6A2B3C4Du;
    int rem = len;
    if (rem <= 0) return;
    if (rem == 1) {
        fprintf(out, "\tnop\n");
        return;
    }

    const ctmpl_t *t = NULL;
    for (int tries = 0; tries < 32 && !t; tries++) {
        t = pick_ctmpl(CODE_LOADS, sizeof CODE_LOADS / sizeof *CODE_LOADS,
                       &rng, rem);
    }
    if (!t) t = &CODE_LOADS[0];
    emit_ctmpl(out, t, &rng);
    rem -= t->len;

    size_t nops = sizeof CODE_OPS / sizeof *CODE_OPS;
    while (rem > 6) {
        t = NULL;
        for (int tries = 0; tries < 48 && !t; tries++)
            t = pick_ctmpl(CODE_OPS, nops, &rng, rem);
        if (!t) break;
        emit_ctmpl(out, t, &rng);
        rem -= t->len;
    }

    while (rem > 0) {
        t = pick_len(CODE_OPS, nops, rem, &rng);
        if (!t)
            break;
        emit_ctmpl(out, t, &rng);
        rem -= t->len;
    }
}

static void emit_filler(FILE *out, int len, uint32_t seed, const char *mode)
{
    if (mode && strcmp(mode, "zero") == 0) {
        fprintf(out, "\t.zero %d\n", len);
        return;
    }
    if (!mode || strcmp(mode, "code") == 0) {
        emit_code_body(out, len, seed);
        return;
    }

    uint32_t rng = seed ? seed : 1;
    uint8_t bytes[512];
    int nb = 0, rem = len;
    while (rem > 0) {
        rng = xs_next(&rng);
        int c = 1 + (int)(rng % 15u);
        if (c > rem) c = rem;
        for (int i = 0; i < c; i++)
            bytes[nb++] = nop_tab[c - 1][i];
        rem -= c;
    }
    for (int i = 0; i < nb; i += 12) {
        fprintf(out, "\t.byte ");
        int e = i + 12 < nb ? i + 12 : nb;
        for (int j = i; j < e; j++)
            fprintf(out, "%s0x%02x", j > i ? "," : "", bytes[j]);
        fprintf(out, "\n");
    }
}

static void emit_slot(FILE *out, int v, int idx, const char *mode, int endbr)
{
    fprintf(out, "\t.cfi_startproc\n");
    if (endbr) {
        fprintf(out, "\tendbr64\n");
        emit_filler(out, v - 5, 0x9E3779B9u * (uint32_t)(idx + 1), mode);
    } else {
        emit_filler(out, v - 1, 0x9E3779B9u * (uint32_t)(idx + 1), mode);
    }
    fprintf(out, "\tret\n\t.cfi_endproc\n");
}

static char **read_lines(const char *path, size_t *n_out)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char **lines = NULL;
    size_t n = 0, cap = 0;
    char buf[65536];
    while (fgets(buf, sizeof buf, f)) {
        if (n == cap) {
            cap = cap ? cap * 2 : 1024;
            lines = realloc(lines, cap * sizeof *lines);
        }
        lines[n] = strdup(buf);
        n++;
    }
    fclose(f);
    *n_out = n;
    return lines;
}

static int is_align(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return strncmp(s, ".p2align", 8) == 0 || strncmp(s, ".balign", 7) == 0 ||
           strncmp(s, ".align", 6) == 0;
}

static int is_tail_dir(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return strncmp(s, ".section", 8) == 0 || strncmp(s, ".ident", 6) == 0;
}

static int cmd_embed(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: zstool embed <carrier.s> <out.s> "
                        "[--payload FILE] [--key 0x..] [--shape natural|lean]\n"
                        "[--fill code|nop|zero] [--run inmemory|infile]\n");
        return 2;
    }
    const char *infile = argv[0], *outfile = argv[1];
    const char *payload_path = "payload/bin/payload.bin";
    const char *fill = "code";
    const char *run = "inmemory";
    int shape = ZS_SHAPE_NATURAL;
    uint32_t key = PAY_KEY;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--payload") && i + 1 < argc)
            payload_path = argv[++i];
        else if (!strcmp(argv[i], "--fill") && i + 1 < argc) {
            const char *v = argv[++i];
            if (strcmp(v, "code") && strcmp(v, "nop") && strcmp(v, "zero")) {
                fprintf(stderr, "error: --fill must be code, nop or zero\n");
                return 2;
            }
            fill = v;
        }
        else if (!strcmp(argv[i], "--run") && i + 1 < argc)
            run = argv[++i];
        else if (!strcmp(argv[i], "--shape") && i + 1 < argc) {
            if (!strcmp(argv[i + 1], "lean")) shape = ZS_SHAPE_LEAN;
            else if (strcmp(argv[i + 1], "natural") != 0) {
                fprintf(stderr, "error: --shape must be natural or lean\n");
                return 2;
            }
            i++;
        }
        else if (!strcmp(argv[i], "--key") && i + 1 < argc) {
            char *end = NULL;
            unsigned long v = strtoul(argv[++i], &end, 0);
            if (!end || *end || v > 0xFFFFFFFFul) {
                fprintf(stderr, "error: --key must be a 32-bit number "
                                "(e.g. 0xC0FFEE42)\n");
                return 2;
            }
            key = (uint32_t)v;
        }
    }
    if (key == 0) {
        fprintf(stderr, "error: --key must be non-zero (0 degenerates the "
                        "keystream to plaintext)\n");
        return 2;
    }
    int run_file = 0;
    if (!strcmp(run, "infile")) run_file = 1;
    else if (strcmp(run, "inmemory") != 0) {
        fprintf(stderr, "error: --run must be 'inmemory' or 'infile'\n");
        return 2;
    }

    if (key == PAY_KEY) {
        FILE *ph = fopen("src/protocol.h", "r");
        if (ph) {
            char line[128];
            while (fgets(line, sizeof line, ph))
                if (sscanf(line, "#define PAY_KEY 0x%x", &key) == 1) break;
            fclose(ph);
        }
    }

    FILE *pf = fopen(payload_path, "rb");
    if (!pf) { fprintf(stderr, "error: cannot open payload %s\n", payload_path); return 1; }
    fseek(pf, 0, SEEK_END);
    long raw_len = ftell(pf);
    rewind(pf);
    if (raw_len <= 0) { fprintf(stderr, "error: empty payload file\n"); return 1; }
    uint8_t *raw = malloc((size_t)raw_len);
    if (fread(raw, 1, (size_t)raw_len, pf) != (size_t)raw_len) return 1;
    fclose(pf);

    if ((unsigned long)raw_len > UINT_MAX) return 1;
    uLongf comp_len = compressBound((uLong)raw_len);
    uint8_t *comp = malloc(comp_len);
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    if (deflateInit2(&zs, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return 1;
    zs.next_in = (Bytef *)raw;
    zs.avail_in = (uInt)raw_len;
    zs.next_out = comp;
    zs.avail_out = (uInt)comp_len;
    int zr = deflate(&zs, Z_FINISH);
    comp_len = zs.total_out;
    deflateEnd(&zs);
    free(raw);
    if (zr != Z_STREAM_END) return 1;

    size_t ct_len;
    uint8_t *ct = encrypt(comp, comp_len, key, &ct_len);
    free(comp);
    if (!ct) return 1;
    if (ct_len > PAY_MAX_CT) {
        fprintf(stderr, "error: payload too large after compression+encryption "
                        "(%zu B > runtime cap %u B)\n", ct_len, PAY_MAX_CT);
        return 1;
    }
    uint8_t csum = 0;
    for (size_t i = 0; i < ct_len; i++) csum ^= ct[i];

    zs_huff_t huff;
    zs_huff_build(shape, &huff);
    int min_k = 24;
    for (int i = 0; i < 256; i++)
        if (huff.k[i] && huff.k[i] < min_k) min_k = huff.k[i];
    size_t max_syms = (ct_len * 8) / (size_t)min_k + 16;
    uint16_t *syms = malloc(max_syms * 2);
    if (!syms) return 1;
    zs_br_t br;
    zs_br_init(&br, ct, ct_len);
    size_t nsyms = 0;
    while (br.nconsumed < ct_len * 8) {
        int len = zs_br_symbol(&br, &huff);
        if (!len || nsyms >= max_syms) break;
        syms[nsyms++] = (uint16_t)len;
    }
    if (nsyms == 0 || br.nconsumed < ct_len * 8) {
        fprintf(stderr, "error: ciphertext did not fully encode "
                        "(symbol cap reached; internal bug, to be addressed later)\n");
        free(syms);
        free(ct);
        return 1;
    }

    int pat[ZS_SYNC_N];
    zs_sync_pattern(key, pat);
    size_t nruns = ZS_SYNC_N + 6 + nsyms + 1;
    if (nruns + 64 > (1u << 20)) {
        fprintf(stderr, "error: payload too large for the slot-block "
                        "decoder window (~%.0f KB compressed max)\n",
                (double)((1u << 20) * 5.0 / 8.0) / 1024.0);
        free(syms);
        free(ct);
        return 1;
    }
    int *runs = malloc(nruns * sizeof *runs);
    size_t r = 0;
    for (int i = 0; i < ZS_SYNC_N; i++) runs[r++] = pat[i];
    runs[r++] = shape + 2;
    for (int i = 0; i < 4; i++) runs[r++] = (int)((ct_len >> (8 * i)) & 0xFF) + 2;
    runs[r++] = csum + 2;
    for (size_t i = 0; i < nsyms; i++) runs[r++] = syms[i];
    runs[r++] = 16;

    size_t nlines;
    char **lines = read_lines(infile, &nlines);
    if (!lines) { fprintf(stderr, "error: cannot open carrier %s\n", infile); return 1; }

    size_t sec = nlines;
    for (size_t i = 0; i < nlines; i++)
        if (is_tail_dir(lines[i])) { sec = i; break; }
    size_t j = sec;
    while (j > 0 && (!lines[j - 1][0] || lines[j - 1][0] == '\n' || is_align(lines[j - 1])))
        j--;

    size_t K = 128 + (size_t)(key % 129u);
    if (K > nsyms) K = nsyms;
    int zero_fill = fill && !strcmp(fill, "zero");
    int *picked = malloc((size_t)(nsyms ? nsyms : 1) * sizeof *picked);
    for (size_t i = 0; i < nsyms; i++) picked[i] = -1;
    if (!zero_fill) {
        for (size_t k = 0; k < K; k++) {
            size_t di = (size_t)((uint64_t)k * nsyms / K);
            if (di < nsyms) picked[di] = (int)k;
        }
    }
    FILE *out = fopen(outfile, "w");
    if (!out) return 1;
    for (size_t i = 0; i < j; i++) fputs(lines[i], out);
    size_t data0 = ZS_SYNC_N + 6;
    for (size_t i = 0; i < nruns; i++) {
        int is_data = i >= data0 && i < data0 + nsyms;
        if (is_data && picked[i - data0] >= 0)
            fprintf(out, ".Lgst_rs_%d:\n", picked[i - data0]);
        emit_slot(out, runs[i], (int)i, fill, is_data);
    }

    fprintf(out, "\t.section .data.rel.ro,\"aw\",@progbits\n");
    fprintf(out, "\t.p2align 3\n");
    fprintf(out, "\t.globl gst_slot_table\n");
    fprintf(out, "gst_slot_table:\n");
    if (!zero_fill)
        for (size_t k = 0; k < K; k++)
            fprintf(out, "\t.quad .Lgst_rs_%zu\n", k);
    fprintf(out, "\t.quad 0\n");
    for (size_t i = sec; i < nlines; i++) fputs(lines[i], out);
    fclose(out);
    free(picked);
    for (size_t i = 0; i < nlines; i++) free(lines[i]);
    free(lines);

    uint64_t code_bytes = 0;
    for (size_t i = 0; i < nruns; i++) code_bytes += (uint64_t)runs[i];
    double ent = 0;
    for (int l = 2; l <= 257; l++) {
        int k = huff.k[l - 2];
        if (k > 0) ent += (double)k * (1.0 / (double)(1u << k));
    }
    printf("payload     : %s (%ld B)\n", payload_path, raw_len);
    printf("compressed  : %zu B (%.1f%%)\n", comp_len, 100.0 * comp_len / (double)raw_len);
    printf("shape       : %s (H~%.2f b/slot)\n", shape == ZS_SHAPE_NATURAL ? "natural" : "lean", ent);
    printf("fill        : %s (code | nop | zero)\n", fill);
    printf("run         : %s\n", run);
    printf("key         : 0x%08x\n", key);
    printf("ciphertext  : %zu B, csum 0x%02x\n", ct_len, csum);
    printf("slots       : %zu (sync %d + header 6 + data %zu + term)\n",
           nruns, ZS_SYNC_N, nsyms);
    printf("reachable   : %zu slots in dispatch table (cover calls them)\n",
           zero_fill ? 0 : K);
    printf("slot code   : ~%llu B\n", (unsigned long long)code_bytes);
    printf("eh_frame    : ~%llu B (FDE + .eh_frame_hdr table, %zu entries)\n",
           (unsigned long long)(nruns * 24), nruns + 64);
    printf("written     : %s\n", outfile);
    free(runs);
    free(syms);

    FILE *rm = fopen("src/runmode.h", "w");
    if (!rm) {
        fprintf(stderr, "error: cannot write src/runmode.h: %s\n", strerror(errno));
        fprintf(stderr, "       the build needs a writable source tree\n");
        return 1;
    }
    fprintf(rm, "#ifndef ZSTEG_RUNMODE_H\n#define ZSTEG_RUNMODE_H\n");
    fprintf(rm, "#define PAY_RUN_FILE %d\n", run_file ? 1 : 0);
    fprintf(rm, "#endif\n");
    if (fclose(rm) != 0) {
        fprintf(stderr, "error: cannot flush src/runmode.h: %s\n", strerror(errno));
        return 1;
    }
    FILE *pm = fopen("src/protocol.h", "w");
    if (!pm) {
        fprintf(stderr, "error: cannot write src/protocol.h: %s\n", strerror(errno));
        fprintf(stderr, "       the build needs a writable source tree\n");
        return 1;
    }
    fprintf(pm,
            "#ifndef ZSTEG_PROTOCOL_H\n#define ZSTEG_PROTOCOL_H\n"
            "#define PAY_KEY        0x%08xu\n"
            "#define PAY_MAX_CT     (1u << 28)\n"
            "#define PAY_MAX_PLEN   (256u << 20)\n"
            "#endif\n",
            key);
    if (fclose(pm) != 0) {
        fprintf(stderr, "error: cannot flush src/protocol.h: %s\n", strerror(errno));
        return 1;
    }
    free(ct);
    return 0;
}

static int64_t pe_read(const uint8_t **pp, unsigned enc,
                       const uint8_t *hdr_file, uint64_t hdr_vaddr,
                       const uint8_t *end)
{
    const uint8_t *p = *pp;
    const uint8_t *field = p;
    uint64_t v = 0;
    int sz = 0, sgn = 0;
    switch (enc & 0x0f) {
    case 0x00: sz = 8; break;
    case 0x02: sz = 2; break;
    case 0x03: sz = 4; break;
    case 0x04: sz = 8; break;
    case 0x0a: sz = 2; sgn = 1; break;
    case 0x0b: sz = 4; sgn = 1; break;
    case 0x0c: sz = 8; sgn = 1; break;
    default: return -1;
    }
    if (end && (size_t)(end - p) < (size_t)sz)
        return -1;
    if (sgn) {
        int64_t t = 0;
        if (sz == 2) { int16_t s; memcpy(&s, p, 2); t = s; }
        else if (sz == 4) { int32_t s; memcpy(&s, p, 4); t = s; }
        else { memcpy(&t, p, 8); }
        v = (uint64_t)t;
    } else {
        memcpy(&v, p, (size_t)sz);
    }
    p += sz;
    uint64_t fld = hdr_vaddr + (uint64_t)(field - hdr_file);
    switch (enc & 0x70) {
    case 0x10: v += fld; break;
    case 0x30: v += hdr_vaddr; break;
    }
    *pp = p;
    return (int64_t)v;
}

static uint8_t *decode_from_file(const uint8_t *img, size_t img_len,
                                 uint32_t key, size_t *out_len)
{
    if (img_len < 64 || memcmp(img, "\x7f" "ELF", 4) != 0)
        return NULL;
    uint64_t e_phoff = *(uint64_t *)(img + 32);
    uint16_t e_phentsize = *(uint16_t *)(img + 54);
    uint16_t e_phnum = *(uint16_t *)(img + 56);
    if (e_phentsize < 56 || e_phoff > img_len ||
        (uint64_t)e_phnum > (img_len - e_phoff) / e_phentsize)
        return NULL;

    const uint8_t *hdr = NULL;
    uint64_t hdr_vaddr = 0;
    size_t hdr_len = 0;
    for (int i = 0; i < e_phnum; i++) {
        const uint8_t *ph = img + e_phoff + (size_t)i * e_phentsize;
        uint32_t p_type = *(uint32_t *)ph;
        if (p_type == 0x6474e550u) {
            uint64_t poff = *(uint64_t *)(ph + 8);
            uint64_t psz  = *(uint64_t *)(ph + 32);
            if (poff > img_len || psz > img_len - poff || psz < 8)
                return NULL;
            hdr = img + poff;
            hdr_vaddr = *(uint64_t *)(ph + 16);
            hdr_len = (size_t)psz;
            break;
        }
    }
    if (!hdr) return NULL;
    const uint8_t *end = hdr + hdr_len;

    const uint8_t *p = hdr + 4;
    if (pe_read(&p, hdr[1], hdr, hdr_vaddr, end) < 0) return NULL;
    int64_t cnt = pe_read(&p, hdr[2], hdr, hdr_vaddr, end);
    if (cnt <= 0 || cnt > 1 << 20) return NULL;
    int n = (int)cnt;
    uint64_t *locs = malloc((size_t)n * 8);
    if (!locs) return NULL;
    for (int i = 0; i < n; i++) {
        int64_t loc = pe_read(&p, hdr[3], hdr, hdr_vaddr, end);
        if (loc < 0) { free(locs); return NULL; }
        locs[i] = (uint64_t)loc;
        if (pe_read(&p, hdr[3], hdr, hdr_vaddr, end) < 0) { free(locs); return NULL; }
    }

    uint8_t *defl = NULL;
    size_t dlen = 0;
    char err[128];
    uint8_t *result = NULL;
    if (zs_steg_decode(locs, n, key, &defl, &dlen, NULL, err, sizeof err) == 0) {
        size_t plen = 0;
        if (zinflate(defl, dlen, &result, &plen) == 0)
            *out_len = plen;
        free(defl);
    }
    free(locs);
    return result;
}

static int cmd_verify(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "usage: zstool verify <binary> [--payload FILE] [--key 0x..]\n");
        return 2;
    }
    const char *bin = argv[0];
    const char *payload_path = "payload/bin/payload.bin";

    uint32_t key = PAY_KEY;
    {
        FILE *ph = fopen("src/protocol.h", "r");
        if (ph) {
            char line[128];
            while (fgets(line, sizeof line, ph))
                if (sscanf(line, "#define PAY_KEY 0x%x", &key) == 1) break;
            fclose(ph);
        }
    }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--payload") && i + 1 < argc)
            payload_path = argv[++i];
        else if (!strcmp(argv[i], "--key") && i + 1 < argc)
            key = (uint32_t)strtoul(argv[++i], NULL, 0);
    }
    if (key == 0) {
        fprintf(stderr, "error: --key must be non-zero\n");
        return 2;
    }
    FILE *f = fopen(bin, "rb");
    if (!f) { fprintf(stderr, "error: cannot open %s\n", bin); return 1; }
    fseek(f, 0, SEEK_END);
    long flen = ftell(f);
    rewind(f);
    uint8_t *img = malloc((size_t)flen);
    if (fread(img, 1, (size_t)flen, f) != (size_t)flen) return 1;
    fclose(f);

    size_t plen = 0;
    uint8_t *payload = decode_from_file(img, (size_t)flen, key, &plen);
    free(img);
    if (!payload) {
        fprintf(stderr, "payload not found / checksum or decompress failed "
                        "(wrong --key?)\n");
        return 1;
    }
    FILE *pf = fopen(payload_path, "rb");
    if (!pf) { fprintf(stderr, "cannot open %s\n", payload_path); return 1; }
    fseek(pf, 0, SEEK_END);
    long olen = ftell(pf);
    rewind(pf);
    uint8_t *orig = malloc((size_t)olen);
    if (fread(orig, 1, (size_t)olen, pf) != (size_t)olen) return 1;
    fclose(pf);

    int match = plen == (size_t)olen && memcmp(payload, orig, plen) == 0;
    printf("%s: recovered %zu-byte payload %s%s (%ld bytes)\n",
           match ? "MATCH" : "MISMATCH", plen,
           match ? "==" : "!=", payload_path, olen);
    free(orig);
    free(payload);
    return match ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: zstool <embed|verify> ...\n");
        return 2;
    }
    if (!strcmp(argv[1], "embed"))
        return cmd_embed(argc - 2, argv + 2);
    if (!strcmp(argv[1], "verify"))
        return cmd_verify(argc - 2, argv + 2);
    fprintf(stderr, "unknown command: %s\n", argv[1]);
    return 2;
}
