# EXPLAIN.md — What is this repository?

This is **`test-tlb`**, a small, single-file benchmark written by Linus
Torvalds to measure **memory access latency** and infer the behavior of the
**TLB (Translation Lookaside Buffer)** and the CPU cache hierarchy on a given
machine.

The original README is signed *"Linus"* and the code carries Linus's
characteristic style: a quick, pragmatic hack, full of hardcoded constants,
with the important caveats explained in plain English at the end.

---

## The problem Linus is trying to solve

Reading a pointer from memory is fast if the data is in cache and slow if it
is not. The *location* of a byte in physical memory is decided partly by the
virtual-to-physical page mapping done by the OS and the TLB. Linus wanted a
simple tool that answers questions like:

- What is the latency of a cache hit at each level (L1, L2, L3...)?
- At what working-set size does the memory latency jump? That jump reveals
  cache sizes and TLB coverage.
- How does the page size affect things? Specifically, does using **huge pages
  (2MB on x86)** avoid TLB misses?
- How does the *randomness* of a memory access pattern change the picture
  compared to a sequential one?

---

## How the program works

`test-tlb` allocates a region of memory, fills it so that it forms a chain of
pointers (each slot points to the next one at a fixed `stride`), and then
follows that chain over and over for a fixed amount of time (about 1 second),
counting how many hops it performs.

Because every access depends on the previous one, the memory pipeline cannot
speculate ahead — this is a classic **pointer-chasing latency test**. The
measured quantity is the average nanoseconds per hop.

### The key pieces of `test-tlb.c`

- `create_map()` (`test-tlb.c:174`) — `mmap()`s the region, and when the `-H`
  flag is given, aligns it to a huge page boundary and calls
  `madvise(MADV_HUGEPAGE)` so the kernel may back it with 2MB pages. Without
  `-H`, it uses `madvise(MADV_NOHUGEPAGE)` to force plain 4KB pages (this
  was added because of s390 behavior where transparent hugepages were on by
  default — see the comment at `test-tlb.c:215`). With plain pages it
  re-`mmap()`s over the old allocation (using `MAP_FIXED`) on every run so
  that each iteration gets a *different* physical mapping, which changes the
  cache-associativity behavior.

- `warmup()` (`test-tlb.c:57`) — one pass over the chain before timing, so
  the measurement does not include the cost of allocating/touching the pages
  (writeback activity from creating the map).

- `do_test()` (`test-tlb.c:70`) — the actual timed loop. It uses a `SIGALRM`
  timer (the timing interval is chosen based on the warmup so the fast cases
  take about 1 second and there is always at least 0.2s of granularity) and
  returns the average **nanoseconds per access**.

- `randomize_map()` (`test-tlb.c:138`) — turns the linear chain into a
  **random circular linked list** of offsets (Fisher–Yates shuffle), so with
  the `-r` flag the accesses jump all over the mapped region, defeating both
  the data cache and the TLB prefetchers.

- `get_num()` (`test-tlb.c:110`) — parses sizes with `k`/`M`/`G` suffixes
  (`4k`, `2M`, `128M`, ...).

- `main()` (`test-tlb.c:234`) — parses `-H` (huge pages) and `-r` (random
  list) flags plus `<size> <stride>`, then runs the test **5 times** and keeps
  the **minimum**, printing the result in ns and an estimated cycle count
  (`~cycles`) using the hardcoded `FREQ` (in GHz) from `test-tlb.c:19`.

### The `Makefile`

- `make` builds the binary (`gcc -g -Wall -O test-tlb.c -o test-tlb -lm`).
- `make run` is the real workhorse: it runs all four configurations
  (huge pages, plain pages, × random) over memory sizes from `4k` up to
  `256M`, all with a fixed stride of 64 bytes.
- `make odd-case` runs a single test with a huge **prime-number stride**
  (`15485863`) to exercise a deliberately unaligned / odd access pattern.

---

## Why it matters / what you learn

Running `make run` produces a table of latency vs. working-set size. Reading
it tells you:

1. **Cache sizes** — where the latency curve steps up, that's where a cache
   level (or TLB reach) runs out.
2. **TLB behavior** — with 4KB pages, a 2MB working set needs 512 TLB
   entries, which overflows the TLB on most cores, causing the latency to
   jump. With 2MB huge pages (`-H`), the same 2MB area needs only **one** TLB
   entry, so the latency stays flat — a very visible demonstration of why
   huge pages help.
3. **Cache associativity side-effects** — as the README explains, huge pages
   also make the physical allocation more contiguous, which removes cacheline
   associativity conflicts. So `-H` can look better than plain pages even
   *before* TLB misses become the issue, especially on low-associativity
   caches (the README discusses a direct-mapped L4 and Intel's 4-way L2).

---

## The gotchas Linus warns about (from the README)

- Each test runs for only ~1 second with **no noise reduction**. The results
  are "fairly stable", but to really explore latencies you should run the
  tests several times to get different virtual-to-physical mappings and
  different list randomizations.
- The timings are heavily influenced by the underlying physical allocation,
  so the *variation* is not reduced by running longer — it comes from
  allocation pattern differences, not measurement time.
- Huge pages are **advisory** (`madvise`), Linux-specific, and depend on
  kernel support and on having enough free memory to actually get huge page
  allocations.
- Hardcoded values you probably need to change for your machine:
  - `PAGE_SIZE`, `FREQ` (in GHz — cycle estimates are wrong if not set),
    and `HUGEPAGE` in `test-tlb.c`
  - the size list and stride in the `Makefile` (stride should be at least
    your cacheline size)

---

## License

GPLv2 (Linus's default license). The README notes it is a quick hack and that
he would be open to discussing a different license if anyone actually cared.

---

*This document was generated from the repository's own README and source
comments to explain what Linus does with this project.*
