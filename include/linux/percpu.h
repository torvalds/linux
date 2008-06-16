#ifndef __LINUX_PERCPU_H
#define __LINUX_PERCPU_H

#include <linux/preempt.h>
#include <linux/slab.h> /* For kmalloc() */
#include <linux/smp.h>
#include <linux/cpumask.h>

#include <asm/percpu.h>

#ifdef CONFIG_SMP
#define DEFINE_PER_CPU(type, name)					\
	__attribute__((__section__(".data.percpu")))			\
	PER_CPU_ATTRIBUTES __typeof__(type) per_cpu__##name

#ifdef MODULE
#define SHARED_ALIGNED_SECTION ".data.percpu"
#else
#define SHARED_ALIGNED_SECTION ".data.percpu.shared_aligned"
#endif

#define DEFINE_PER_CPU_SHARED_ALIGNED(type, name)			\
	__attribute__((__section__(SHARED_ALIGNED_SECTION)))		\
	PER_CPU_ATTRIBUTES __typeof__(type) per_cpu__##name		\
	____cacheline_aligned_in_smp
#else
#define DEFINE_PER_CPU(type, name)					\
	PER_CPU_ATTRIBUTES __typeof__(type) per_cpu__##name

#define DEFINE_PER_CPU_SHARED_ALIGNED(type, name)		      \
	DEFINE_PER_CPU(type, name)
#endif

#define EXPORT_PER_CPU_SYMBOL(var) EXPORT_SYMBOL(per_cpu__##var)
#define EXPORT_PER_CPU_SYMBOL_GPL(var) EXPORT_SYMBOL_GPL(per_cpu__##var)

/* Enough to cover all DEFINE_PER_CPUs in kernel, including modules. */
#ifndef PERCPU_ENOUGH_ROOM
#ifdef CONFIG_MODULES
#define PERCPU_MODULE_RESERVE	8192
#else
#define PERCPU_MODULE_RESERVE	0
#endif

#define PERCPU_ENOUGH_ROOM						\
	(__per_cpu_end - __per_cpu_start + PERCPU_MODULE_RESERVE)
#endif	/* PERCPU_ENOUGH_ROOM */

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

struct percpu_data {
	void *ptrs[1];
};

#define __percpu_disguise(pdata) (struct percpu_data *)~(unsigned long)(pdata)
/* 
 * Use this to get to a cpu's version of the per-cpu object dynamically
 * allocated. Non-atomic access to the current CPU's version should
 * probably be combined with get_cpu()/put_cpu().
 */ 
#define percpu_ptr(ptr, cpu)                              \
({                                                        \
        struct percpu_data *__p = __percpu_disguise(ptr); \
        (__typeof__(ptr))__p->ptrs[(cpu)];	          \
})

extern void *percpu_populate(void *__pdata, size_t size, gfp_t gfp, int cpu);
extern void percpu_depopulate(void *__pdata, int cpu);
extern int __percpu_populate_mask(void *__pdata, size_t size, gfp_t gfp,
				  cpumask_t *mask);
extern void __percpu_depopulate_mask(void *__pdata, cpumask_t *mask);
extern void *__percpu_alloc_mask(size_t size, gfp_t gfp, cpumask_t *mask);
extern void percpu_free(void *__pdata);

#else /* CONFIG_SMP */

#define percpu_ptr(ptr, cpu) ({ (void)(cpu); (ptr); })

static inline void percpu_depopulate(void *__pdata, int cpu)
{
}

static inline void __percpu_depopulate_mask(void *__pdata, cpumask_t *mask)
{
}

static inline void *percpu_populate(void *__pdata, size_t size, gfp_t gfp,
				    int cpu)
{
	return percpu_ptr(__pdata, cpu);
}

static inline int __percpu_populate_mask(void *__pdata, size_t size, gfp_t gfp,
					 cpumask_t *mask)
{
	return 0;
}

static __always_inline void *__percpu_alloc_mask(size_t size, gfp_t gfp, cpumask_t *mask)
{
	return kzalloc(size, gfp);
}

static inline void percpu_free(void *__pdata)
{
	kfree(__pdata);
}

#endif /* CONFIG_SMP */

#define percpu_populate_mask(__pdata, size, gfp, mask) \
	__percpu_populate_mask((__pdata), (size), (gfp), &(mask))
#define percpu_depopulate_mask(__pdata, mask) \
	__percpu_depopulate_mask((__pdata), &(mask))
#define percpu_alloc_mask(size, gfp, mask) \
	__percpu_alloc_mask((size), (gfp), &(mask))

#define percpu_alloc(size, gfp) percpu_alloc_mask((size), (gfp), cpu_online_map)

/* (legacy) interface for use without CPU hotplug handling */

#define __alloc_percpu(size)	percpu_alloc_mask((size), GFP_KERNEL, \
						  cpu_possible_map)
#define alloc_percpu(type)	(type *)__alloc_percpu(sizeof(type))
#define free_percpu(ptr)	percpu_free((ptr))
#define per_cpu_ptr(ptr, cpu)	percpu_ptr((ptr), (cpu))

#endif /* __LINUX_PERCPU_H */
