#ifndef __LINUX_CACHE_H
#define __LINUX_CACHE_H

#include <linux/kernel.h>
#include <linux/config.h>
#include <asm/cache.h>

#ifndef L1_CACHE_ALIGN
#define L1_CACHE_ALIGN(x) ALIGN(x, L1_CACHE_BYTES)
#endif

#ifndef SMP_CACHE_BYTES
#define SMP_CACHE_BYTES L1_CACHE_BYTES
#endif

#if defined(CONFIG_X86) || defined(CONFIG_SPARC64)
#define __read_mostly __attribute__((__section__(".data.read_mostly")))
#else
#define __read_mostly
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
		 __section__(".data.cacheline_aligned")))
#endif /* __cacheline_aligned */

#ifndef __cacheline_aligned_in_smp
#ifdef CONFIG_SMP
#define __cacheline_aligned_in_smp __cacheline_aligned
#else
#define __cacheline_aligned_in_smp
#endif /* CONFIG_SMP */
#endif

#if !defined(____cacheline_maxaligned_in_smp)
#if defined(CONFIG_SMP)
#define ____cacheline_maxaligned_in_smp \
	__attribute__((__aligned__(1 << (L1_CACHE_SHIFT_MAX))))
#else
#define ____cacheline_maxaligned_in_smp
#endif
#endif

#endif /* __LINUX_CACHE_H */
