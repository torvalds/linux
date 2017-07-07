#include <config.h>

#include "preempt.h"

#include "assume.h"
#include "locks.h"

/* Support NR_CPUS of at most 64 */
#define CPU_PREEMPTION_LOCKS_INIT0 LOCK_IMPL_INITIALIZER
#define CPU_PREEMPTION_LOCKS_INIT1 \
	CPU_PREEMPTION_LOCKS_INIT0, CPU_PREEMPTION_LOCKS_INIT0
#define CPU_PREEMPTION_LOCKS_INIT2 \
	CPU_PREEMPTION_LOCKS_INIT1, CPU_PREEMPTION_LOCKS_INIT1
#define CPU_PREEMPTION_LOCKS_INIT3 \
	CPU_PREEMPTION_LOCKS_INIT2, CPU_PREEMPTION_LOCKS_INIT2
#define CPU_PREEMPTION_LOCKS_INIT4 \
	CPU_PREEMPTION_LOCKS_INIT3, CPU_PREEMPTION_LOCKS_INIT3
#define CPU_PREEMPTION_LOCKS_INIT5 \
	CPU_PREEMPTION_LOCKS_INIT4, CPU_PREEMPTION_LOCKS_INIT4

/*
 * Simulate disabling preemption by locking a particular cpu. NR_CPUS
 * should be the actual number of cpus, not just the maximum.
 */
struct lock_impl cpu_preemption_locks[NR_CPUS] = {
	CPU_PREEMPTION_LOCKS_INIT0
#if (NR_CPUS - 1) & 1
	, CPU_PREEMPTION_LOCKS_INIT0
#endif
#if (NR_CPUS - 1) & 2
	, CPU_PREEMPTION_LOCKS_INIT1
#endif
#if (NR_CPUS - 1) & 4
	, CPU_PREEMPTION_LOCKS_INIT2
#endif
#if (NR_CPUS - 1) & 8
	, CPU_PREEMPTION_LOCKS_INIT3
#endif
#if (NR_CPUS - 1) & 16
	, CPU_PREEMPTION_LOCKS_INIT4
#endif
#if (NR_CPUS - 1) & 32
	, CPU_PREEMPTION_LOCKS_INIT5
#endif
};

#undef CPU_PREEMPTION_LOCKS_INIT0
#undef CPU_PREEMPTION_LOCKS_INIT1
#undef CPU_PREEMPTION_LOCKS_INIT2
#undef CPU_PREEMPTION_LOCKS_INIT3
#undef CPU_PREEMPTION_LOCKS_INIT4
#undef CPU_PREEMPTION_LOCKS_INIT5

__thread int thread_cpu_id;
__thread int preempt_disable_count;

void preempt_disable(void)
{
	BUG_ON(preempt_disable_count < 0 || preempt_disable_count == INT_MAX);

	if (preempt_disable_count++)
		return;

	thread_cpu_id = nondet_int();
	assume(thread_cpu_id >= 0);
	assume(thread_cpu_id < NR_CPUS);
	lock_impl_lock(&cpu_preemption_locks[thread_cpu_id]);
}

void preempt_enable(void)
{
	BUG_ON(preempt_disable_count < 1);

	if (--preempt_disable_count)
		return;

	lock_impl_unlock(&cpu_preemption_locks[thread_cpu_id]);
}
