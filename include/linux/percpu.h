/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PERCPU_H
#define __LINUX_PERCPU_H

#include <linux/alloc_tag.h>
#include <linux/mmdebug.h>
#include <linux/preempt.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/pfn.h>
#include <linux/init.h>
#include <linux/cleanup.h>
#include <linux/sched.h>

#include <asm/percpu.h>

/* enough to cover all DEFINE_PER_CPUs in modules */
#ifdef CONFIG_MODULES
#ifdef CONFIG_MEM_ALLOC_PROFILING
#define PERCPU_MODULE_RESERVE		(8 << 13)
#else
#define PERCPU_MODULE_RESERVE		(8 << 10)
#endif
#else
#define PERCPU_MODULE_RESERVE		0
#endif

/* minimum unit size, also is the maximum supported allocation size */
#define PCPU_MIN_UNIT_SIZE		PFN_ALIGN(32 << 10)

/* minimum allocation size and shift in bytes */
#define PCPU_MIN_ALLOC_SHIFT		2
#define PCPU_MIN_ALLOC_SIZE		(1 << PCPU_MIN_ALLOC_SHIFT)

/*
 * The PCPU_BITMAP_BLOCK_SIZE must be the same size as PAGE_SIZE as the
 * updating of hints is used to manage the nr_empty_pop_pages in both
 * the chunk and globally.
 */
#define PCPU_BITMAP_BLOCK_SIZE		PAGE_SIZE
#define PCPU_BITMAP_BLOCK_BITS		(PCPU_BITMAP_BLOCK_SIZE >>	\
					 PCPU_MIN_ALLOC_SHIFT)

#ifdef CONFIG_RANDOM_KMALLOC_CACHES
#define PERCPU_DYNAMIC_SIZE_SHIFT      12
#else
#define PERCPU_DYNAMIC_SIZE_SHIFT      10
#endif

/*
 * Percpu allocator can serve percpu allocations before slab is
 * initialized which allows slab to depend on the percpu allocator.
 * The following parameter decide how much resource to preallocate
 * for this.  Keep PERCPU_DYNAMIC_RESERVE equal to or larger than
 * PERCPU_DYNAMIC_EARLY_SIZE.
 */
#define PERCPU_DYNAMIC_EARLY_SIZE	(20 << PERCPU_DYNAMIC_SIZE_SHIFT)

/*
 * PERCPU_DYNAMIC_RESERVE indicates the amount of free area to piggy
 * back on the first chunk for dynamic percpu allocation if arch is
 * manually allocating and mapping it for faster access (as a part of
 * large page mapping for example).
 *
 * The following values give between one and two pages of free space
 * after typical minimal boot (2-way SMP, single disk and NIC) with
 * both defconfig and a distro config on x86_64 and 32.  More
 * intelligent way to determine this would be nice.
 */
#if BITS_PER_LONG > 32
#define PERCPU_DYNAMIC_RESERVE		(28 << PERCPU_DYNAMIC_SIZE_SHIFT)
#else
#define PERCPU_DYNAMIC_RESERVE		(20 << PERCPU_DYNAMIC_SIZE_SHIFT)
#endif

extern void *pcpu_base_addr;
extern const unsigned long *pcpu_unit_offsets;

struct pcpu_group_info {
	int			nr_units;	/* aligned # of units */
	unsigned long		base_offset;	/* base address offset */
	unsigned int		*cpu_map;	/* unit->cpu map, empty
						 * entries contain NR_CPUS */
};

struct pcpu_alloc_info {
	size_t			static_size;
	size_t			reserved_size;
	size_t			dyn_size;
	size_t			unit_size;
	size_t			atom_size;
	size_t			alloc_size;
	size_t			__ai_size;	/* internal, don't use */
	int			nr_groups;	/* 0 if grouping unnecessary */
	struct pcpu_group_info	groups[];
};

enum pcpu_fc {
	PCPU_FC_AUTO,
	PCPU_FC_EMBED,
	PCPU_FC_PAGE,

	PCPU_FC_NR,
};
extern const char * const pcpu_fc_names[PCPU_FC_NR];

extern enum pcpu_fc pcpu_chosen_fc;

typedef int (pcpu_fc_cpu_to_node_fn_t)(int cpu);
typedef int (pcpu_fc_cpu_distance_fn_t)(unsigned int from, unsigned int to);

extern struct pcpu_alloc_info * __init pcpu_alloc_alloc_info(int nr_groups,
							     int nr_units);
extern void __init pcpu_free_alloc_info(struct pcpu_alloc_info *ai);

extern void __init pcpu_setup_first_chunk(const struct pcpu_alloc_info *ai,
					 void *base_addr);

extern int __init pcpu_embed_first_chunk(size_t reserved_size, size_t dyn_size,
				size_t atom_size,
				pcpu_fc_cpu_distance_fn_t cpu_distance_fn,
				pcpu_fc_cpu_to_node_fn_t cpu_to_nd_fn);

#ifdef CONFIG_NEED_PER_CPU_PAGE_FIRST_CHUNK
void __init pcpu_populate_pte(unsigned long addr);
extern int __init pcpu_page_first_chunk(size_t reserved_size,
				pcpu_fc_cpu_to_node_fn_t cpu_to_nd_fn);
#endif

extern bool __is_kernel_percpu_address(unsigned long addr, unsigned long *can_addr);
extern bool is_kernel_percpu_address(unsigned long addr);

#if !defined(CONFIG_SMP) || !defined(CONFIG_HAVE_SETUP_PER_CPU_AREA)
extern void __init setup_per_cpu_areas(void);
#endif

extern void __percpu *pcpu_alloc_noprof(size_t size, size_t align, bool reserved,
				   gfp_t gfp) __alloc_size(1);
extern size_t pcpu_alloc_size(void __percpu *__pdata);

#define __alloc_percpu_gfp(_size, _align, _gfp)				\
	alloc_hooks(pcpu_alloc_noprof(_size, _align, false, _gfp))
#define __alloc_percpu(_size, _align)					\
	alloc_hooks(pcpu_alloc_noprof(_size, _align, false, GFP_KERNEL))
#define __alloc_reserved_percpu(_size, _align)				\
	alloc_hooks(pcpu_alloc_noprof(_size, _align, true, GFP_KERNEL))

#define alloc_percpu_gfp(type, gfp)					\
	(typeof(type) __percpu *)__alloc_percpu_gfp(sizeof(type),	\
						__alignof__(type), gfp)
#define alloc_percpu(type)						\
	(typeof(type) __percpu *)__alloc_percpu(sizeof(type),		\
						__alignof__(type))
#define alloc_percpu_noprof(type)					\
	((typeof(type) __percpu *)pcpu_alloc_noprof(sizeof(type),	\
					__alignof__(type), false, GFP_KERNEL))

extern void free_percpu(void __percpu *__pdata);

DEFINE_FREE(free_percpu, void __percpu *, free_percpu(_T))

extern phys_addr_t per_cpu_ptr_to_phys(void *addr);

extern unsigned long pcpu_nr_pages(void);

#endif /* __LINUX_PERCPU_H */
