#ifndef __LINUX_PERCPU_H
#define __LINUX_PERCPU_H

#include <linux/preempt.h>
#include <linux/slab.h> /* For kmalloc() */
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/pfn.h>

#include <asm/percpu.h>

#ifndef PER_CPU_BASE_SECTION
#ifdef CONFIG_SMP
#define PER_CPU_BASE_SECTION ".data.percpu"
#else
#define PER_CPU_BASE_SECTION ".data"
#endif
#endif

#ifdef CONFIG_SMP

#ifdef MODULE
#define PER_CPU_SHARED_ALIGNED_SECTION ""
#else
#define PER_CPU_SHARED_ALIGNED_SECTION ".shared_aligned"
#endif
#define PER_CPU_FIRST_SECTION ".first"

#else

#define PER_CPU_SHARED_ALIGNED_SECTION ""
#define PER_CPU_FIRST_SECTION ""

#endif

#define DEFINE_PER_CPU_SECTION(type, name, section)			\
	__attribute__((__section__(PER_CPU_BASE_SECTION section)))	\
	PER_CPU_ATTRIBUTES __typeof__(type) per_cpu__##name

#define DEFINE_PER_CPU(type, name)					\
	DEFINE_PER_CPU_SECTION(type, name, "")

#define DEFINE_PER_CPU_SHARED_ALIGNED(type, name)			\
	DEFINE_PER_CPU_SECTION(type, name, PER_CPU_SHARED_ALIGNED_SECTION) \
	____cacheline_aligned_in_smp

#define DEFINE_PER_CPU_PAGE_ALIGNED(type, name)				\
	DEFINE_PER_CPU_SECTION(type, name, ".page_aligned")

#define DEFINE_PER_CPU_FIRST(type, name)				\
	DEFINE_PER_CPU_SECTION(type, name, PER_CPU_FIRST_SECTION)

#define EXPORT_PER_CPU_SYMBOL(var) EXPORT_SYMBOL(per_cpu__##var)
#define EXPORT_PER_CPU_SYMBOL_GPL(var) EXPORT_SYMBOL_GPL(per_cpu__##var)

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

#ifdef CONFIG_HAVE_DYNAMIC_PER_CPU_AREA

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

typedef struct page * (*pcpu_get_page_fn_t)(unsigned int cpu, int pageno);
typedef void (*pcpu_populate_pte_fn_t)(unsigned long addr);

extern size_t __init pcpu_setup_first_chunk(pcpu_get_page_fn_t get_page_fn,
				size_t static_size, size_t reserved_size,
				ssize_t dyn_size, ssize_t unit_size,
				void *base_addr,
				pcpu_populate_pte_fn_t populate_pte_fn);

extern ssize_t __init pcpu_embed_first_chunk(
				size_t static_size, size_t reserved_size,
				ssize_t dyn_size, ssize_t unit_size);

/*
 * Use this to get to a cpu's version of the per-cpu object
 * dynamically allocated. Non-atomic access to the current CPU's
 * version should probably be combined with get_cpu()/put_cpu().
 */
#define per_cpu_ptr(ptr, cpu)	SHIFT_PERCPU_PTR((ptr), per_cpu_offset((cpu)))

extern void *__alloc_reserved_percpu(size_t size, size_t align);

#else /* CONFIG_HAVE_DYNAMIC_PER_CPU_AREA */

struct percpu_data {
	void *ptrs[1];
};

#define __percpu_disguise(pdata) (struct percpu_data *)~(unsigned long)(pdata)

#define per_cpu_ptr(ptr, cpu)						\
({									\
        struct percpu_data *__p = __percpu_disguise(ptr);		\
        (__typeof__(ptr))__p->ptrs[(cpu)];				\
})

#endif /* CONFIG_HAVE_DYNAMIC_PER_CPU_AREA */

extern void *__alloc_percpu(size_t size, size_t align);
extern void free_percpu(void *__pdata);

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

#endif /* CONFIG_SMP */

#define alloc_percpu(type)	(type *)__alloc_percpu(sizeof(type), \
						       __alignof__(type))

#endif /* __LINUX_PERCPU_H */
