#ifndef _ARCH_X86_CACHE_H
#define _ARCH_X86_CACHE_H

/* L1 cache line size */
#define L1_CACHE_SHIFT	(CONFIG_X86_L1_CACHE_SHIFT)
#define L1_CACHE_BYTES	(1 << L1_CACHE_SHIFT)

#define __read_mostly __attribute__((__section__(".data.read_mostly")))

#ifdef CONFIG_X86_VSMP
/* vSMP Internode cacheline shift */
#define INTERNODE_CACHE_SHIFT (12)
#ifdef CONFIG_SMP
#define __cacheline_aligned_in_smp					\
	__attribute__((__aligned__(1 << (INTERNODE_CACHE_SHIFT))))	\
	__attribute__((__section__(".data.page_aligned")))
#endif
#endif

#endif
