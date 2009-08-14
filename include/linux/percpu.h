#ifndef __LINUX_PERCPU_H
#define __LINUX_PERCPU_H

#include <linux/preempt.h>
#include <linux/slab.h> /* For kmalloc() */
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/pfn.h>

#include <asm/percpu.h>

/* enough to cover all DEFINE_PER_CPUs in modules */
#ifdef CONFIG_MODULES
#define PERCPU_MODULE_RESERVE		(8 << 10)
#else
#define PERCPU_MODULE_RESERVE		0
#endif

#ifndef PERCPU_ENOUGH_ROOM
#define PERCPU_ENOUGH_ROOM						\
	(ALIGN(__per_cpu_end - __per_cpu_start, SMP_CACHE_BYTES) +	\
	 PERCPU_MODULE_RESERVE)
#endif

/*
 * Must be an lvalue. Since @var must be a simple identifier,
 * we force a syntax error here if it isn't.
 */
#define get_cpu_var(var) (*({				\
	extern int simple_identifier_##var(void);	\
	preempt_disable();				\
	&__get_cpu_var(var); }))
#define put_cpu_var(var) preempt_enable()

#ifdef CONFIG_SMP

#ifndef CONFIG_HAVE_LEGACY_PER_CPU_AREA

/* minimum unit size, also is the maximum supported allocation size */
#define PCPU_MIN_UNIT_SIZE		PFN_ALIGN(64 << 10)

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
#define PERCPU_DYNAMIC_RESERVE		(20 << 10)
#else
#define PERCPU_DYNAMIC_RESERVE		(12 << 10)
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
extern const char *pcpu_fc_names[PCPU_FC_NR];

extern enum pcpu_fc pcpu_chosen_fc;

typedef void * (*pcpu_fc_alloc_fn_t)(unsigned int cpu, size_t size,
				     size_t align);
typedef void (*pcpu_fc_free_fn_t)(void *ptr, size_t size);
typedef void (*pcpu_fc_populate_pte_fn_t)(unsigned long addr);
typedef int (pcpu_fc_cpu_distance_fn_t)(unsigned int from, unsigned int to);

extern struct pcpu_alloc_info * __init pcpu_alloc_alloc_info(int nr_groups,
							     int nr_units);
extern void __init pcpu_free_alloc_info(struct pcpu_alloc_info *ai);

extern struct pcpu_alloc_info * __init pcpu_build_alloc_info(
				size_t reserved_size, ssize_t dyn_size,
				size_t atom_size,
				pcpu_fc_cpu_distance_fn_t cpu_distance_fn);

extern int __init pcpu_setup_first_chunk(const struct pcpu_alloc_info *ai,
					 void *base_addr);

#ifdef CONFIG_NEED_PER_CPU_EMBED_FIRST_CHUNK
extern int __init pcpu_embed_first_chunk(size_t reserved_size, ssize_t dyn_size,
				size_t atom_size,
				pcpu_fc_cpu_distance_fn_t cpu_distance_fn,
				pcpu_fc_alloc_fn_t alloc_fn,
				pcpu_fc_free_fn_t free_fn);
#endif

#ifdef CONFIG_NEED_PER_CPU_PAGE_FIRST_CHUNK
extern int __init pcpu_page_first_chunk(size_t reserved_size,
				pcpu_fc_alloc_fn_t alloc_fn,
				pcpu_fc_free_fn_t free_fn,
				pcpu_fc_populate_pte_fn_t populate_pte_fn);
#endif

/*
 * Use this to get to a cpu's version of the per-cpu object
 * dynamically allocated. Non-atomic access to the current CPU's
 * version should probably be combined with get_cpu()/put_cpu().
 */
#define per_cpu_ptr(ptr, cpu)	SHIFT_PERCPU_PTR((ptr), per_cpu_offset((cpu)))

extern void *__alloc_reserved_percpu(size_t size, size_t align);

#else /* CONFIG_HAVE_LEGACY_PER_CPU_AREA */

struct percpu_data {
	void *ptrs[1];
};

/* pointer disguising messes up the kmemleak objects tracking */
#ifndef CONFIG_DEBUG_KMEMLEAK
#define __percpu_disguise(pdata) (struct percpu_data *)~(unsigned long)(pdata)
#else
#define __percpu_disguise(pdata) (struct percpu_data *)(pdata)
#endif

#define per_cpu_ptr(ptr, cpu)						\
({									\
        struct percpu_data *__p = __percpu_disguise(ptr);		\
        (__typeof__(ptr))__p->ptrs[(cpu)];				\
})

#endif /* CONFIG_HAVE_LEGACY_PER_CPU_AREA */

extern void *__alloc_percpu(size_t size, size_t align);
extern void free_percpu(void *__pdata);

#ifndef CONFIG_HAVE_SETUP_PER_CPU_AREA
extern void __init setup_per_cpu_areas(void);
#endif

#else /* CONFIG_SMP */

#define per_cpu_ptr(ptr, cpu) ({ (void)(cpu); (ptr); })

static inline void *__alloc_percpu(size_t size, size_t align)
{
	/*
	 * Can't easily make larger alignment work with kmalloc.  WARN
	 * on it.  Larger alignment should only be used for module
	 * percpu sections on SMP for which this path isn't used.
	 */
	WARN_ON_ONCE(align > SMP_CACHE_BYTES);
	return kzalloc(size, GFP_KERNEL);
}

static inline void free_percpu(void *p)
{
	kfree(p);
}

static inline void __init setup_per_cpu_areas(void) { }

static inline void *pcpu_lpage_remapped(void *kaddr)
{
	return NULL;
}

#endif /* CONFIG_SMP */

#define alloc_percpu(type)	(type *)__alloc_percpu(sizeof(type), \
						       __alignof__(type))

/*
 * Optional methods for optimized non-lvalue per-cpu variable access.
 *
 * @var can be a percpu variable or a field of it and its size should
 * equal char, int or long.  percpu_read() evaluates to a lvalue and
 * all others to void.
 *
 * These operations are guaranteed to be atomic w.r.t. preemption.
 * The generic versions use plain get/put_cpu_var().  Archs are
 * encouraged to implement single-instruction alternatives which don't
 * require preemption protection.
 */
#ifndef percpu_read
# define percpu_read(var)						\
  ({									\
	typeof(per_cpu_var(var)) __tmp_var__;				\
	__tmp_var__ = get_cpu_var(var);					\
	put_cpu_var(var);						\
	__tmp_var__;							\
  })
#endif

#define __percpu_generic_to_op(var, val, op)				\
do {									\
	get_cpu_var(var) op val;					\
	put_cpu_var(var);						\
} while (0)

#ifndef percpu_write
# define percpu_write(var, val)		__percpu_generic_to_op(var, (val), =)
#endif

#ifndef percpu_add
# define percpu_add(var, val)		__percpu_generic_to_op(var, (val), +=)
#endif

#ifndef percpu_sub
# define percpu_sub(var, val)		__percpu_generic_to_op(var, (val), -=)
#endif

#ifndef percpu_and
# define percpu_and(var, val)		__percpu_generic_to_op(var, (val), &=)
#endif

#ifndef percpu_or
# define percpu_or(var, val)		__percpu_generic_to_op(var, (val), |=)
#endif

#ifndef percpu_xor
# define percpu_xor(var, val)		__percpu_generic_to_op(var, (val), ^=)
#endif

#endif /* __LINUX_PERCPU_H */
