/*
 * include/asm-sparc64/cache.h
 */
#ifndef __ARCH_SPARC64_CACHE_H
#define __ARCH_SPARC64_CACHE_H

/* bytes per L1 cache line */
#define        L1_CACHE_SHIFT	5
#define        L1_CACHE_BYTES	32 /* Two 16-byte sub-blocks per line. */

#define        L1_CACHE_ALIGN(x)       (((x)+(L1_CACHE_BYTES-1))&~(L1_CACHE_BYTES-1))
#define		L1_CACHE_SHIFT_MAX 5	/* largest L1 which this arch supports */

#define        SMP_CACHE_BYTES_SHIFT	6
#define        SMP_CACHE_BYTES		(1 << SMP_CACHE_BYTES_SHIFT) /* L2 cache line size. */

#endif
