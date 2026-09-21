
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <link.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/wait.h>
#include <unistd.h>

#include "elfrun.h"

#define PAGE(x) ((x) & ~((uintptr_t)sysconf(_SC_PAGESIZE) - 1))

static int load_segments(const uint8_t *img, size_t len,
                         const ElfW(Ehdr) *eh, uintptr_t base, uintptr_t *out_entry,
                         uintptr_t *out_span_lo, size_t *out_span_len)
{
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    const ElfW(Phdr) *ph = (const ElfW(Phdr) *)(img + eh->e_phoff);
    (void)base;

    uintptr_t span_lo = UINTPTR_MAX, span_hi = 0;
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        if (ph[i].p_filesz > ph[i].p_memsz) return -1;
        if (ph[i].p_vaddr > UINTPTR_MAX - ph[i].p_memsz) return -1;
        if (ph[i].p_offset > len || ph[i].p_filesz > len - ph[i].p_offset)
            return -1;
        uintptr_t vstart = PAGE(ph[i].p_vaddr);
        uintptr_t vend = ph[i].p_vaddr + ph[i].p_memsz;
        vend = (vend + page - 1) & ~(page - 1);
        if (vstart < span_lo) span_lo = vstart;
        if (vend > span_hi) span_hi = vend;
    }
    if (span_hi <= span_lo) return -1;
    size_t span_len = span_hi - span_lo;

    void *map = mmap((void *)span_lo, span_len, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (map == MAP_FAILED)
        return -1;

    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        if (ph[i].p_filesz == 0) continue;
        uintptr_t dst = ph[i].p_vaddr;
        memcpy((void *)dst, img + ph[i].p_offset, ph[i].p_filesz);
    }

    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        uintptr_t vstart = PAGE(ph[i].p_vaddr);
        size_t off_in = ph[i].p_vaddr - vstart;
        size_t mlen = (off_in + ph[i].p_memsz + page - 1) & ~(page - 1);
        int prot = 0;
        if (ph[i].p_flags & PF_R) prot |= PROT_READ;
        if (ph[i].p_flags & PF_W) prot |= PROT_WRITE;
        if (ph[i].p_flags & PF_X) prot |= PROT_EXEC;
        if (mprotect((void *)vstart, mlen, prot) != 0) { munmap(map, span_len); return -1; }
    }

    int entry_ok = 0;
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_LOAD && (ph[i].p_flags & PF_X) &&
            eh->e_entry >= ph[i].p_vaddr &&
            eh->e_entry < ph[i].p_vaddr + ph[i].p_memsz) {
            entry_ok = 1;
            break;
        }
    }
    if (!entry_ok) { munmap(map, span_len); return -1; }

    *out_entry = eh->e_entry;
    *out_span_lo = span_lo;
    *out_span_len = span_len;
    return 0;
}

typedef struct { uint64_t a_type, a_val; } auxv_t;

static uintptr_t phdr_vaddr(const ElfW(Ehdr) *eh)
{
    const ElfW(Phdr) *ph = (const ElfW(Phdr) *)((const uint8_t *)eh + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_LOAD &&
            ph[i].p_offset <= eh->e_phoff &&
            eh->e_phoff < ph[i].p_offset + ph[i].p_filesz)
            return ph[i].p_vaddr + (eh->e_phoff - ph[i].p_offset);
    }
    return eh->e_phoff;
}

static int build_stack(uintptr_t entry, const ElfW(Ehdr) *eh, uintptr_t base,
                       uintptr_t *out_sp, uintptr_t *out_argv0)
{
    (void)base;
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    const char *argv0 = "hexkit";
    const char *path  = "PATH=/usr/bin:/bin";

    uint64_t rand16[2];
    if (getrandom(rand16, sizeof rand16, 0) != (ssize_t)sizeof rand16) {
        rand16[0] = 0x6A2B3C4D5E6F7081ull;
        rand16[1] = 0x1029384756ABCDEFull;
    }

    auxv_t auxv[] = {
        { AT_PHDR,   (uint64_t)phdr_vaddr(eh) },
        { AT_PHENT,  eh->e_phentsize },
        { AT_PHNUM,  eh->e_phnum },
        { AT_PAGESZ, page },
        { AT_BASE,   0 },
        { AT_FLAGS,  0 },
        { AT_ENTRY,  entry },
        { AT_UID,    getuid() }, { AT_EUID, geteuid() },
        { AT_GID,    getgid() }, { AT_EGID, getegid() },
        { AT_SECURE, 0 },
        { AT_HWCAP,  getauxval(AT_HWCAP) },
        { AT_HWCAP2, getauxval(AT_HWCAP2) },
        { AT_CLKTCK, (uint64_t)sysconf(_SC_CLK_TCK) },
        { AT_RANDOM, (uint64_t)(uintptr_t)rand16 },
        { AT_NULL,   0 },
    };
    size_t naux = sizeof auxv / sizeof auxv[0];

    size_t slen = strlen(argv0) + 1 + strlen(path) + 1;
    size_t bufsz = 8 + 2 * 8 + 2 * 8 + naux * 16 + 16 + slen + 16;
    bufsz = (bufsz + 15) & ~(size_t)15;

    size_t stksz = 8u << 20;
    void *stk = mmap(NULL, stksz + 4096, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stk == MAP_FAILED)
        return -1;
    if (mprotect(stk, 4096, PROT_NONE) != 0) {
        munmap(stk, stksz + 4096);
        return -1;
    }

    uint8_t *buf = (uint8_t *)stk + stksz + 4096 - bufsz;
    memset(buf, 0, bufsz);

    uint8_t *sp = buf;
    uint64_t *q;

    q = (uint64_t *)sp; *q = 1; sp += 8;

    uint64_t *argv0_slot = (uint64_t *)sp; sp += 8;
    uint64_t *argv_end  = (uint64_t *)sp; *argv_end = 0; sp += 8;

    uint64_t *env0_slot = (uint64_t *)sp; sp += 8;
    uint64_t *env_end  = (uint64_t *)sp; *env_end = 0; sp += 8;

    for (size_t i = 0; i < naux; i++) {
        q = (uint64_t *)sp; *q = auxv[i].a_type; sp += 8;
        q = (uint64_t *)sp; *q = auxv[i].a_val;  sp += 8;
    }

    memcpy(sp, rand16, 16); sp += 16;

    char *s = (char *)sp;
    memcpy(s, path, strlen(path) + 1);
    uintptr_t path_addr = (uintptr_t)s; s += strlen(path) + 1;
    memcpy(s, argv0, strlen(argv0) + 1);
    uintptr_t a0_addr = (uintptr_t)s;

    *argv0_slot = a0_addr;
    *env0_slot  = path_addr;
    *out_sp = (uintptr_t)buf;
    *out_argv0 = a0_addr;
    return 0;
}

int elf_run(const void *img, size_t len)
{
    if (!img || len < sizeof(ElfW(Ehdr)))
        return -1;
    const ElfW(Ehdr) *eh = (const ElfW(Ehdr) *)img;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0)
        return -1;

    if (eh->e_ident[EI_CLASS] != ELFCLASS64 ||
        eh->e_ident[EI_DATA] != ELFDATA2LSB ||
        eh->e_machine != EM_X86_64)
        return -1;
    if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN)
        return -1;
    if (eh->e_phentsize < (ElfW(Half))sizeof(ElfW(Phdr)))
        return -1;
    if (eh->e_phoff > len || (size_t)eh->e_phnum > (len - eh->e_phoff) / eh->e_phentsize)
        return -1;

    const ElfW(Phdr) *ph = (const ElfW(Phdr) *)(img + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_INTERP)
            return -1;
    }
    if (eh->e_entry == 0)
        return -1;

    uintptr_t base = 0;
    uintptr_t entry = 0;
    uintptr_t span_lo = 0;
    size_t span_len = 0;
    if (load_segments((const uint8_t *)img, len, eh, base, &entry,
                      &span_lo, &span_len) != 0)
        return -1;

    uintptr_t sp = 0, a0 = 0;
    if (build_stack(entry, eh, base, &sp, &a0) != 0) {
        munmap((void *)span_lo, span_len);
        return -1;
    }
    (void)a0;

    __asm__ volatile("movq %0, %%rsp\n\tjmp *%1" : : "r"(sp), "r"(entry) : "memory");
    __builtin_unreachable();
}
