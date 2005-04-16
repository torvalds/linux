#ifndef _ASM_IA64_CACHE_H
#define _ASM_IA64_CACHE_H

#include <linux/config.h>

/*
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

/* Bytes per L1 (data) cache line.  */
#define L1_CACHE_SHIFT		CONFIG_IA64_L1_CACHE_SHIFT
#define L1_CACHE_BYTES		(1 << L1_CACHE_SHIFT)

#define L1_CACHE_SHIFT_MAX 7	/* largest L1 which this arch supports */

#ifdef CONFIG_SMP
# define SMP_CACHE_SHIFT	L1_CACHE_SHIFT
# define SMP_CACHE_BYTES	L1_CACHE_BYTES
#else
  /*
   * The "aligned" directive can only _increase_ alignment, so this is
   * safe and provides an easy way to avoid wasting space on a
   * uni-processor:
   */
# define SMP_CACHE_SHIFT	3
# define SMP_CACHE_BYTES	(1 << 3)
#endif

#endif /* _ASM_IA64_CACHE_H */
