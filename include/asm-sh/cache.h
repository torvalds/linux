/* $Id: cache.h,v 1.6 2004/03/11 18:08:05 lethal Exp $
 *
 * include/asm-sh/cache.h
 *
 * Copyright 1999 (C) Niibe Yutaka
 * Copyright 2002, 2003 (C) Paul Mundt
 */
#ifndef __ASM_SH_CACHE_H
#define __ASM_SH_CACHE_H
#ifdef __KERNEL__

#include <asm/cpu/cache.h>
#include <asm/cpu/cacheflush.h>

#define SH_CACHE_VALID		1
#define SH_CACHE_UPDATED	2
#define SH_CACHE_COMBINED	4
#define SH_CACHE_ASSOC		8

#define L1_CACHE_BYTES		(1 << L1_CACHE_SHIFT)
#define SMP_CACHE_BYTES		L1_CACHE_BYTES

#define L1_CACHE_ALIGN(x)	(((x)+(L1_CACHE_BYTES-1))&~(L1_CACHE_BYTES-1))

#define L1_CACHE_SHIFT_MAX 	5	/* largest L1 which this arch supports */

struct cache_info {
	unsigned int ways;
	unsigned int sets;
	unsigned int linesz;

	unsigned int way_incr;

	unsigned int entry_shift;
	unsigned int entry_mask;

	unsigned long flags;
};

/* Flush (write-back only) a region (smaller than a page) */
extern void __flush_wback_region(void *start, int size);
/* Flush (write-back & invalidate) a region (smaller than a page) */
extern void __flush_purge_region(void *start, int size);
/* Flush (invalidate only) a region (smaller than a page) */
extern void __flush_invalidate_region(void *start, int size);

#endif /* __KERNEL__ */
#endif /* __ASM_SH_CACHE_H */
