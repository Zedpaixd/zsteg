# zSteg

A program that hides a payload inside **the layout of its own code** and recovers
it at runtime, from the binary itself.

The payload does not live in a data section, in a string table, in padding, or in
appended bytes. It lives in the **address deltas between consecutive functions**,
readable through the binary's own `.eh_frame_hdr` unwind table. Every function
length in the slot block is a symbol of a chosen distribution, so the file's
function-size histogram looks like an ordinary compiled binary rather than a
payload container.

## Sample example:
```
$ make
$ ./out/zsteg 1a2b3c
payload: real ELF running (pid=4102 uid=1000 argc=1 argv0=hexkit)
payload: PATH=/usr/bin:/bin
payload: heap works
worker exited with code 42
selftest: 183 micro-functions ok (sink 4e0c38b0f2a1d9c4)
hex utils: 4 bytes  crc16=bf68  fnv1a=045d4bb3  popcount=24
```
---
The last three lines are the program carrying on as the utility it pretends to
be; the first four are the payload, running from a decrypted image that never
existed on disk as an executable file.

## How it works

### 1. The channel

The compiler emits the carrier's functions as assembly, and each function is a
`.cfi` region, so the linker writes one FDE per function into `.eh_frame` and one
entry per FDE into `.eh_frame_hdr`:

```
 .eh_frame_hdr:  [ version | encodings | eh_frame_ptr | fde_count ]
                 [ initial_location_0 | fde_ptr_0 ]     <- sorted by address
                 [ initial_location_1 | fde_ptr_1 ]
                 ...
```

The **difference between two consecutive `initial_location` values is the length
of the function between them**. That is the channel: `len(fn_i) = loc[i+1] - loc[i]`.

To write data, the encoder appends functions whose lengths *are* the data:

```
 [ sync: 8 slots ]           lengths = a fixed per-key pattern, in range
 [ header: 6 slots ]         shape id, ciphertext length (4 bytes), checksum
 [ data: N slots ]           one symbol per slot; the sequence of symbols IS
                             the ciphertext bit stream
 [ terminator: 1 slot ]      never read; it exists so the last data slot has a
                             measurable delta
```

Decoding is the inverse: read the deltas, find the sync pattern, read the header,
then walk the data slots. Nothing inspects instruction bytes -- the slot bodies are
decoration and can be any valid code.

### 2. The code

Each slot length `L` (5..257 bytes for data, 2..257 for the literal header) is a
symbol of a **complete prefix code**. The code lengths `k(L)` are chosen so that
`p(L) = 2^-k(L)` matches a target function-size distribution, and the canonical
codewords are built from the `k` table alone (no entropy table travels with the
file, making it so that the decoder rebuilds it in O(256)).

Two shapes ship:

| shape | H | E[L] | character |
|---|---|---|---|
| `natural` (default) | ~5.75 bits/slot | ~25 B | smooth exponential decay, matches real builds |
| `lean` | ~7.1 bits/slot | ~57 B | flatter tail, fewer and fatter functions, ~20% less `eh_frame` overhead |

Because the ciphertext is uniform, **encoding it through this codec reproduces
the target distribution exactly** in the emitted slot lengths. That is the whole
trick: the channel's shape is the cover's shape.

### 3. The payload

```
payload  --deflate-->  --xor(xorshift, per-build key)-->  ciphertext bits
         --> canonical symbols  --> slot lengths  --> .eh_frame_hdr deltas
```

At runtime the dropper analyzes **its own loaded image**:

1. locate its ELF base, read `.eh_frame_hdr` (bounds-checked: `p_filesz`, never
   `p_memsz`; every read is range-checked; entry counts are capped),
2. find the sync pattern derived from the compiled-in key,
3. read the header, gather the data symbols, encode them back to bits,
4. verify the checksum, decrypt, inflate (puff), and check for the ELF magic,
5. execute the recovered image:
   - `RUN=inmemory` (default): `memfd_create` + `fexecve`, so it runs from
     memory with no executable file ever on disk,
   - `RUN=infile`: write to a temporary file, `execve`, unlink afterwards.

Execution happens in a forked child: a bad payload cannot take the carrier down,
and the parent reports the child's exit code.

### 4. The cover

The slots are not inert filler:

* every slot body is a real function body (ALU work, a few branches, `ret`),
  generated from templates that vary with the shape,
* the binary declares CET, so slots begin with `endbr64` like the rest of the
  code,
* 234 slots are reachable in the current build: `main` calls them through a dispatch
  folds their results into a checksum, so the cover behaves like the hex utility
  it claims to be (`./out/zsteg --help`),
* slot lengths follow a smooth distribution; there are no 2-byte stubs and no
  spikes at round numbers.

## Requirements

| need | why |
|---|---|
| Linux x86-64 | the carrier is emitted as x86-64 assembly; the runtime uses `memfd_create`/`execveat` and a Linux ELF loader |
| `gcc` or `clang` | builds the carrier, the tool and the runtime |
| `musl-gcc` | the payload is a **static musl** binary; the loader cannot initialise glibc's IFUNC/TLS startup |
| zlib development headers | only the host-side embed tool uses zlib; the runtime carries its own inflate |
| binutils (`strip`, `readelf`), coreutils | build and self-checks |

```bash
sudo apt install build-essential musl-tools zlib1g-dev binutils
# or equivalent for others
```

The build refuses to start without them and tells you which package to install.

## Build

```bash
make                 # payload -> tool -> carrier -> embedded slots -> out/zsteg
make check           # build, then run every self-check
make clean           # drop build products (keeps src/protocol.h, the live key)
make distclean       # also drop generated headers and the payload
make help            # targets and every variable
```

Useful variables (all optional, all overridable on the command line):

```bash
make KEY=0xC0FFEE42            # per-build key: the tree remembers it until re-embedded
make SHAPE=lean                # natural (default) or lean
make RUN=infile                # drop-to-file execution instead of in-memory
make CC=clang                  # compiler for carrier + runtime
make MUSL_CC=musl-gcc          # compiler for the payload
make PAYLOAD=/path/to/prog.elf # embed your own program (see below)
make OUT=build                 # write artifacts elsewhere
```

An example build (this repository, default settings):

```
payload     : payload/bin/payload.bin (38296 B)
compressed  : 17870 B (46.7%)
shape       : natural (H~5.75 b/slot)
slots       : 24885 (sync 8 + header 6 + data 24870 + term)
reachable   : 234 slots in dispatch table (cover calls them)
slot code   : ~616388 B
eh_frame    : ~597240 B (FDE + .eh_frame_hdr table, 24949 entries)
out/zsteg   : 1362136 B, stripped
```

### What `make check` verifies

| check | what it proves |
|---|---|
| payload runs in memory, reports code 42 | the loader works end to end |
| the payload's own output appears | it is a real process, not a stub |
| the carrier's slots run | the cover is a working program |
| `zstool verify` | byte-exact recovery through the deltas |
| no payload plaintext in the dropper | the file carries no readable payload |
| wrong key rejected | the key actually gates recovery |
| corrupted carrier rejected cleanly | no crash on damaged input, exit 1 |
| oversized payload refused | the encoder fails loudly instead of emitting a broken block |
| a substituted payload reports its own code | any program works, nothing is payload-specific |
| drop-to-file variant | the `RUN=infile` path drops an executable ELF and runs it |

### What the build catches (and how loudly)

The build is deliberately paranoid, because every one of these has bitten before thanks to my goldfish memory. I assume you can benefit from this too though:

* missing platform (not Linux x86-64), missing or unusable `CC`, missing
  `musl-gcc`, missing zlib headers, missing `strip`/`readelf`,
* a compiler that rejects `-fno-reorder-functions`/`-fasynchronous-unwind-tables`
  (the wire format needs unwind tables), or that cannot emit assembly,
* missing source files (checked at parse time so you get one clear line, not a
  cascade of "no rule to make target"),
* a read-only source tree (the embed step writes two generated headers),
* a payload that is not an ELF, is dynamically linked, or is a **glibc** static
  build (detected before linking, with the install hint),
* a changed `CC`/`MUSL_CC`/flag set: stamps force the affected artifacts to
  rebuild, so a stale payload or carrier can never be reused silently,
* a changed `KEY`/`SHAPE`/`RUN`: the embed step re-runs, and the generated
  headers are rewritten with the values actually used,
* stale build products: `carrier.s`, `carrier_pad.s`, generated headers, objects
  and the tool are removed by `make clean`.

## Using your own payload

Any **static musl** program works; nothing in the carrier depends on what the
payload is:

```bash
musl-gcc -static -O2 -s -o myprog.elf myprog.c
make PAYLOAD=/tmp/myprog.elf
./out/zsteg                 # runs your program from memory
```

The payload contract is its exit code: the carrier reports it and the parent
propagates failure. The bundled `payload/payload.c` prints a few proof lines
and returns 42; replace it (or point `PAYLOAD` at a prebuilt ELF) and nothing else
changes. The build validates your binary (it is an ELF, static, not glibc) and refuses
with the exact reason if it does not qualify.

Keep an eye on size: the channel's capacity is bounded by the carrier block and
the decoder's FDE window. The encoder fails loudly (`payload too large`) rather
than emitting a block that cannot be decoded, so a too-large payload is a build
error, not a silent truncation.

## Wire format reference

```
sync        8 slots    lengths = zs_sync_pattern(key), each 34..94
header      6 slots    [0] shape id + 2
                       [1..4] ciphertext length, little-endian bytes, each + 2
                       [5] xor checksum over the ciphertext, + 2
data        N slots    one canonical codeword per slot length
terminator  1 slot     unread; gives the last data slot a measurable delta
```

* codewords: canonical prefix code built from `zs_ktab_<shape>`; `k==0` marks an
  unused length and is skipped by every codec loop (this is where a naive
  "length = base[depth] + rank" decoder breaks and produces a table with a Kraft
  sum below 1; the decoder walks the per-depth length list instead),
* the sync pattern is derived from the key, so a wrong key fails at the first
  step, and the header checksum catches the rest,
* `zs_steg_decode()` is shared by the host tool and the runtime, so the two can
  never drift,
* the decoder tolerates real cover functions *after* the slot block: it stops at
  the ciphertext bit budget rather than assuming the block runs to the last FDE.

## Layout

```
main.c                  the dropper's main(): fork, analyze self, report, then
                        behave like the hex utility it carries
src/carrier.c           the cover program whose functions become slots; 183 are
                        called through the dispatch table and hashed
src/selfsteg.c          self-analysis: parse own ELF + .eh_frame_hdr, recover,
                        decrypt, inflate, execute
src/elfrun.c            the in-memory ELF loader (segments, argv/env, entry)
src/stegcode.c          the shared codec (symbols -> bits, bits -> symbols)
src/shapes.h            the dyadic distributions and the canonical code tables
vendor/puff.c           inflate for the runtime (zlib licence, Mark Adler)
tools/zstool.c          host tool: embed + verify, plus generated-header writing
payload/payload.c       the demo payload (static musl, returns 42)
src/protocol.h          generated by the embed step (key + limits)
src/runmode.h           generated by the embed step (in-memory vs drop-to-file)
out/                    build artifacts (git-ignored)
```

## Licence

MIT