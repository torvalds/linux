/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_CACHE_H
#define __LINUX_CACHE_H

#include <uapi/linux/kernel.h>
#include <vdso/cache.h>
#include <asm/cache.h>

#ifndef L1_CACHE_ALIGN
#define L1_CACHE_ALIGN(x) __ALIGN_KERNEL(x, L1_CACHE_BYTES)
#endif

/**
 * SMP_CACHE_ALIGN - align a value to the L2 cacheline size
 * @x: value to align
 *
 * On some architectures, L2 ("SMP") CL size is bigger than L1, and sometimes,
 * this needs to be accounted.
 *
 * Return: aligned value.
 */
#ifndef SMP_CACHE_ALIGN
#define SMP_CACHE_ALIGN(x)	ALIGN(x, SMP_CACHE_BYTES)
#endif

/*
 * ``__aligned_largest`` aligns a field to the value most optimal for the
 * target architecture to perform memory operations. Get the actual value
 * to be able to use it anywhere else.
 */
#ifndef __LARGEST_ALIGN
#define __LARGEST_ALIGN		sizeof(struct { long x; } __aligned_largest)
#endif

#ifndef LARGEST_ALIGN
#define LARGEST_ALIGN(x)	ALIGN(x, __LARGEST_ALIGN)
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

#ifndef __cacheline_group_begin
#define __cacheline_group_begin(GROUP) \
	__u8 __cacheline_group_begin__##GROUP[0]
#endif

#ifndef __cacheline_group_end
#define __cacheline_group_end(GROUP) \
	__u8 __cacheline_group_end__##GROUP[0]
#endif

/**
 * __cacheline_group_begin_aligned - declare an aligned group start
 * @GROUP: name of the group
 * @...: optional group alignment
 *
 * The following block inside a struct:
 *
 *	__cacheline_group_begin_aligned(grp);
 *	field a;
 *	field b;
 *	__cacheline_group_end_aligned(grp);
 *
 * will always be aligned to either the specified alignment or
 * ``SMP_CACHE_BYTES``.
 */
#define __cacheline_group_begin_aligned(GROUP, ...)		\
	__cacheline_group_begin(GROUP)				\
	__aligned((__VA_ARGS__ + 0) ? : SMP_CACHE_BYTES)

/**
 * __cacheline_group_end_aligned - declare an aligned group end
 * @GROUP: name of the group
 * @...: optional alignment (same as was in __cacheline_group_begin_aligned())
 *
 * Note that the end marker is aligned to sizeof(long) to allow more precise
 * size assertion. It also declares a padding at the end to avoid next field
 * falling into this cacheline.
 */
#define __cacheline_group_end_aligned(GROUP, ...)		\
	__cacheline_group_end(GROUP) __aligned(sizeof(long));	\
	struct { } __cacheline_group_pad__##GROUP		\
	__aligned((__VA_ARGS__ + 0) ? : SMP_CACHE_BYTES)

#ifndef CACHELINE_ASSERT_GROUP_MEMBER
#define CACHELINE_ASSERT_GROUP_MEMBER(TYPE, GROUP, MEMBER) \
	BUILD_BUG_ON(!(offsetof(TYPE, MEMBER) >= \
		       offsetofend(TYPE, __cacheline_group_begin__##GROUP) && \
		       offsetofend(TYPE, MEMBER) <= \
		       offsetof(TYPE, __cacheline_group_end__##GROUP)))
#endif

#ifndef CACHELINE_ASSERT_GROUP_SIZE
#define CACHELINE_ASSERT_GROUP_SIZE(TYPE, GROUP, SIZE) \
	BUILD_BUG_ON(offsetof(TYPE, __cacheline_group_end__##GROUP) - \
		     offsetofend(TYPE, __cacheline_group_begin__##GROUP) > \
		     SIZE)
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
