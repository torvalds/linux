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

#include <linux/init.h>
#include <asm/cpu/cache.h>

#define L1_CACHE_BYTES		(1 << L1_CACHE_SHIFT)

#define __read_mostly __attribute__((__section__(".data.read_mostly")))

#ifndef __ASSEMBLY__
struct cache_info {
	unsigned int ways;		/* Number of cache ways */
	unsigned int sets;		/* Number of cache sets */
	unsigned int linesz;		/* Cache line size (bytes) */

	unsigned int way_size;		/* sets * line size */

	/*
	 * way_incr is the address offset for accessing the next way
	 * in memory mapped cache array ops.
	 */
	unsigned int way_incr;
	unsigned int entry_shift;
	unsigned int entry_mask;

	/*
	 * Compute a mask which selects the address bits which overlap between
	 * 1. those used to select the cache set during indexing
	 * 2. those in the physical page number.
	 */
	unsigned int alias_mask;

	unsigned int n_aliases;		/* Number of aliases */

	unsigned long flags;
};

int __init detect_cpu_and_cache_system(void);

#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* __ASM_SH_CACHE_H */
