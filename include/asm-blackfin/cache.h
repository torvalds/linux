/*
 * include/asm-blackfin/cache.h
 */
#ifndef __ARCH_BLACKFIN_CACHE_H
#define __ARCH_BLACKFIN_CACHE_H

/*
 * Bytes per L1 cache line
 * Blackfin loads 32 bytes for cache
 */
#define L1_CACHE_SHIFT	5
#define L1_CACHE_BYTES	(1 << L1_CACHE_SHIFT)
#define SMP_CACHE_BYTES	L1_CACHE_BYTES

/*
 * Put cacheline_aliged data to L1 data memory
 */
#ifdef CONFIG_CACHELINE_ALIGNED_L1
#define __cacheline_aligned				\
	  __attribute__((__aligned__(L1_CACHE_BYTES),	\
		__section__(".data_l1.cacheline_aligned")))
#endif

/*
 * largest L1 which this arch supports
 */
#define L1_CACHE_SHIFT_MAX	5

#endif
