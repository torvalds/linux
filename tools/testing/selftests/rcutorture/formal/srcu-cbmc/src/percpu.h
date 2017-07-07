#ifndef PERCPU_H
#define PERCPU_H

#include <stddef.h>
#include "bug_on.h"
#include "preempt.h"

#define __percpu

/* Maximum size of any percpu data. */
#define PERCPU_OFFSET (4 * sizeof(long))

/* Ignore alignment, as CBMC doesn't care about false sharing. */
#define alloc_percpu(type) __alloc_percpu(sizeof(type), 1)

static inline void *__alloc_percpu(size_t size, size_t align)
{
	BUG();
	return NULL;
}

static inline void free_percpu(void *ptr)
{
	BUG();
}

#define per_cpu_ptr(ptr, cpu) \
	((typeof(ptr)) ((char *) (ptr) + PERCPU_OFFSET * cpu))

#define __this_cpu_inc(pcp) __this_cpu_add(pcp, 1)
#define __this_cpu_dec(pcp) __this_cpu_sub(pcp, 1)
#define __this_cpu_sub(pcp, n) __this_cpu_add(pcp, -(typeof(pcp)) (n))

#define this_cpu_inc(pcp) this_cpu_add(pcp, 1)
#define this_cpu_dec(pcp) this_cpu_sub(pcp, 1)
#define this_cpu_sub(pcp, n) this_cpu_add(pcp, -(typeof(pcp)) (n))

/* Make CBMC use atomics to work around bug. */
#ifdef RUN
#define THIS_CPU_ADD_HELPER(ptr, x) (*(ptr) += (x))
#else
/*
 * Split the atomic into a read and a write so that it has the least
 * possible ordering.
 */
#define THIS_CPU_ADD_HELPER(ptr, x) \
	do { \
		typeof(ptr) this_cpu_add_helper_ptr = (ptr); \
		typeof(ptr) this_cpu_add_helper_x = (x); \
		typeof(*ptr) this_cpu_add_helper_temp; \
		__CPROVER_atomic_begin(); \
		this_cpu_add_helper_temp = *(this_cpu_add_helper_ptr); \
		__CPROVER_atomic_end(); \
		this_cpu_add_helper_temp += this_cpu_add_helper_x; \
		__CPROVER_atomic_begin(); \
		*(this_cpu_add_helper_ptr) = this_cpu_add_helper_temp; \
		__CPROVER_atomic_end(); \
	} while (0)
#endif

/*
 * For some reason CBMC needs an atomic operation even though this is percpu
 * data.
 */
#define __this_cpu_add(pcp, n) \
	do { \
		BUG_ON(preemptible()); \
		THIS_CPU_ADD_HELPER(per_cpu_ptr(&(pcp), thread_cpu_id), \
				    (typeof(pcp)) (n)); \
	} while (0)

#define this_cpu_add(pcp, n) \
	do { \
		int this_cpu_add_impl_cpu = get_cpu(); \
		THIS_CPU_ADD_HELPER(per_cpu_ptr(&(pcp), this_cpu_add_impl_cpu), \
				    (typeof(pcp)) (n)); \
		put_cpu(); \
	} while (0)

/*
 * This will cause a compiler warning because of the cast from char[][] to
 * type*. This will cause a compile time error if type is too big.
 */
#define DEFINE_PER_CPU(type, name) \
	char name[NR_CPUS][PERCPU_OFFSET]; \
	typedef char percpu_too_big_##name \
		[sizeof(type) > PERCPU_OFFSET ? -1 : 1]

#define for_each_possible_cpu(cpu) \
	for ((cpu) = 0; (cpu) < NR_CPUS; ++(cpu))

#endif
