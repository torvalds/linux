/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_CACHE_H
#define __LINUX_CACHE_H

#include <uapi/linux/kernel.h>
#include <asm/cache.h>

#ifndef L1_CACHE_ALIGN
#define L1_CACHE_ALIGN(x) __ALIGN_KERNEL(x, L1_CACHE_BYTES)
#endif

#ifndef SMP_CACHE_BYTES
#define SMP_CACHE_BYTES L1_CACHE_BYTES
#endif

/*
 * __read_mostly is used to keep rarely changing variables out of frequently
 * updated cachelines. Its use should be reserved for data that is used
 * frequently in hot paths. Performance traces can help decide when to use
 * this. You want __read_mostly data to be tightly packed, so that in the
 * best case multiple frequently read variables for a hot path will be next
 * to each other in order to reduce the number of cachelines needed to
 * execute a critical path. We should be mindful and selective of its use.
 * ie: if you're going to use it please supply a *good* justification in your
 * commit log
 */
#ifndef __read_mostly
#define __read_mostly
#endif

/*
 * __ro_after_init is used to mark things that are read-only after init (i.e.
 * after mark_rodata_ro() has been called). These are effectively read-only,
 * but may get written to during init, so can't live in .rodata (via "const").
 */
#ifndef __ro_after_init
#define __ro_after_init __section(".data..ro_after_init")
#endif

#ifndef ____cacheline_aligned
#define ____cacheline_aligned __attribute__((__aligned__(SMP_CACHE_BYTES)))
#endif

#ifndef ____cacheline_aligned_in_smp
#ifdef CONFIG_SMP
#define ____cacheline_aligned_in_smp ____cacheline_aligned
#else
#define ____cacheline_aligned_in_smp
#endif /* CONFIG_SMP */
#endif

#ifndef __cacheline_aligned
#define __cacheline_aligned					\
  __attribute__((__aligned__(SMP_CACHE_BYTES),			\
		 __section__(".data..cacheline_aligned")))
#endif /* __cacheline_aligned */

#ifndef __cacheline_aligned_in_smp
#ifdef CONFIG_SMP
#define __cacheline_aligned_in_smp __cacheline_aligned
#else
#define __cacheline_aligned_in_smp
#endif /* CONFIG_SMP */
#endif

/*
 * The maximum alignment needed for some critical structures
 * These could be inter-node cacheline sizes/L3 cacheline
 * size etc.  Define this in asm/cache.h for your arch
 */
#ifndef INTERNODE_CACHE_SHIFT
#define INTERNODE_CACHE_SHIFT L1_CACHE_SHIFT
#endif

#if !defined(____cacheline_internodealigned_in_smp)
#if defined(CONFIG_SMP)
#define ____cacheline_internodealigned_in_smp \
	__attribute__((__aligned__(1 << (INTERNODE_CACHE_SHIFT))))
#else
#define ____cacheline_internodealigned_in_smp
#endif
#endif

#ifndef CONFIG_ARCH_HAS_CACHE_LINE_SIZE
#define cache_line_size()	L1_CACHE_BYTES
#endif

/*
 * Helper to add padding within a struct to ensure data fall into separate
 * cachelines.
 */
#if defined(CONFIG_SMP)
struct cacheline_padding {
	char x[0];
} ____cacheline_internodealigned_in_smp;
#define CACHELINE_PADDING(name)		struct cacheline_padding name
#else
#define CACHELINE_PADDING(name)
#endif

#ifdef ARCH_DMA_MINALIGN
#define ARCH_HAS_DMA_MINALIGN
#else
#define ARCH_DMA_MINALIGN __alignof__(unsigned long long)
#endif

#endif /* __LINUX_CACHE_H */
