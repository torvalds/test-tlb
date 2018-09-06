Quick memory latency and TLB test program.

NOTE! This is a quick hack, and the code itself has some hardcoded
constants in it that you should look at and possibly change to match
your machine. 

      #define PAGE_SIZE 4096

      #define FREQ 3.9

.. and then later down..

      // Hugepage size
      #define HUGEPAGE (2*1024*1024)

if you don't change the FREQ define (it's in gigahertz) the tests still
work, and the absolute time values in nanoseconds are correct, but the
CPU cycle estimation will obviously be completely off.

In addition to the tweakables in the code itself, there are other
hardcoded values in the Makefile, where the "make run" script will be
testing memory sizes ranging from 4kB to 256MB, and it uses a hardcoded
stride of 64 bytes.

To give meaningful numbers, the stride should generally be at least the
size of your cacheline, although particularly in cases with multiple
different cacheline sizes, you might just use different strides and see
how it affects the scores. 

Note that the "stride" is still used when doing the random list case,
but since the end result will be chasing a random chain it won't be a
"stride" in memory as much as just the granularity of the chain entries. 
You can use odd stride values to see how unaligned loads change the
picture, for example. 

Also note that the actual memory sizes used for testing will depend on
how much cache you have, but also on how much memory you have.  To
actually get a largepage (when using the "-H" flag), not only does your
architecture need to support it, you also have to have enough free
memory that the OS will give you hugepage allocations in the first
place. 

So if you are running on some s390 with a 384MB L4 cache, you should
increase the largest memory area size to at least 1G, but you should
also increase the stride to 256 to match the cache line size. 

Also not that the use of MADV_HUGEPAGE is obviously Linux-specific, but
the use of madvise() means that it is *advisory* rather than some hard
requirement, and depending on your situation, you may not actually see
the hugepage case at all.

For example MADV_HUGEPAGE obviously depends on your kernel being built
to support it, and not all architectures support large pages at all. 
You can still do the non-hugepage tests, of course, but then you'll not
have the baseline that a bigger page size will get you. 


Finally, there are a couple of gotchas you need to be aware of:


 * each timing test is run for just one second, and there is no noise
   reduction code.  If the machine is busy, that will obviously affect
   the result.  But even more commonly, other effects will also affect
   the reported results, particularly the exact pattern of
   randomization, and the virtual to physical mapping of the underlying
   memory allocation. 

   So the timings are "fairly stable", but if you want to really explore
   the latencies you needed to run the test multiple times, to get
   different virtual-to-physical mappings, and to get different list
   randomization. 


 * the hugetlb case helps avoid TLB misses, but it has another less
   obvious secondary effect: it makes the memory area be contiguous in
   physical RAM in much bigger chunks.  That in turn affects the caching
   in the normal data caches on a very fundamental level, since you will
   not see cacheline associativity conflicts within such a contiguous
   physical mapping. 

   In particular, the hugepage case will sometimes look much better than
   the normal page size case when you start to get closer to the cache
   size.  This is particularly noticeable in lower-associativity caches. 

   If you have a large direct-mapped L4, for example, you'll start to
   see a *lot* of cache misses long before you are really close to the
   L4 size, simply because your cache is effectively only covering a
   much smaller area. 

   The effect is noticeable even with something like the 4-way L2 in
   modern intel cores.  The L2 may be 256kB in size, but depending on
   the exact virtual-to-physical memory allocation, you might be missing
   quite a bit long before that, and indeed see higher latencies already
   with just a 128kB memory area.

   In contrast, if you run a hugepage test (using as 2MB page on x86),
   the contiguous memory allocation means that your 256kB area will be
   cached in its entirety. 

   See above on "run the tests several times" to see these kinds of
   patterns.  A lot of memory latency testers try to run for long times
   to get added precision, but that's pointless: the variation comes not
   from how long the benchmark is run, but from underlying allocation
   pattern differences. 


Finally, I've made the license be GPLv2 (which is basically my default
license), but this is a quick hack and if you have some reason to want
to use this where another license would be preferable, email me and we
can discuss the issue.  I will probably accommodate other alternatives in
the very unlikely case that somebody actually cares. 

                 Linus
