/* Misc low level processor primitives */
#ifndef _LINUX_PROCESSOR_H
#define _LINUX_PROCESSOR_H

#include <asm/processor.h>

/*
 * spin_begin is used before beginning a busy-wait loop, and must be paired
 * with spin_end when the loop is exited. spin_cpu_relax must be called
 * within the loop.
 *
 * The loop body should be as small and fast as possible, on the order of
 * tens of instructions/cycles as a guide. It should and avoid calling
 * cpu_relax, or any "spin" or sleep type of primitive including nested uses
 * of these primitives. It should not lock or take any other resource.
 * Violations of these guidelies will not cause a bug, but may cause sub
 * optimal performance.
 *
 * These loops are optimized to be used where wait times are expected to be
 * less than the cost of a context switch (and associated overhead).
 *
 * Detection of resource owner and decision to spin or sleep or guest-yield
 * (e.g., spin lock holder vcpu preempted, or mutex owner not on CPU) can be
 * tested within the loop body.
 */
#ifndef spin_begin
#define spin_begin()
#endif

#ifndef spin_cpu_relax
#define spin_cpu_relax() cpu_relax()
#endif

/*
 * spin_cpu_yield may be called to yield (undirected) to the hypervisor if
 * necessary. This should be used if the wait is expected to take longer
 * than context switch overhead, but we can't sleep or do a directed yield.
 */
#ifndef spin_cpu_yield
#define spin_cpu_yield() cpu_relax_yield()
#endif

#ifndef spin_end
#define spin_end()
#endif

/*
 * spin_until_cond can be used to wait for a condition to become true. It
 * may be expected that the first iteration will true in the common case
 * (no spinning), so that callers should not require a first "likely" test
 * for the uncontended case before using this primitive.
 *
 * Usage and implementation guidelines are the same as for the spin_begin
 * primitives, above.
 */
#ifndef spin_until_cond
#define spin_until_cond(cond)					\
do {								\
	if (unlikely(!(cond))) {				\
		spin_begin();					\
		do {						\
			spin_cpu_relax();			\
		} while (!(cond));				\
		spin_end();					\
	}							\
} while (0)

#endif

#endif /* _LINUX_PROCESSOR_H */
