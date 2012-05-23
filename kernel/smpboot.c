/*
 * Common SMP CPU bringup/teardown functions
 */
#include <linux/err.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/percpu.h>

#include "smpboot.h"

#ifdef CONFIG_GENERIC_SMP_IDLE_THREAD
/*
 * For the hotplug case we keep the task structs around and reuse
 * them.
 */
static DEFINE_PER_CPU(struct task_struct *, idle_threads);

struct task_struct * __cpuinit idle_thread_get(unsigned int cpu)
{
	struct task_struct *tsk = per_cpu(idle_threads, cpu);

	if (!tsk)
		return ERR_PTR(-ENOMEM);
	init_idle(tsk, cpu);
	return tsk;
}

void __init idle_thread_set_boot_cpu(void)
{
	per_cpu(idle_threads, smp_processor_id()) = current;
}

static inline void idle_init(unsigned int cpu)
{
	struct task_struct *tsk = per_cpu(idle_threads, cpu);

	if (!tsk) {
		tsk = fork_idle(cpu);
		if (IS_ERR(tsk))
			pr_err("SMP: fork_idle() failed for CPU %u\n", cpu);
		else
			per_cpu(idle_threads, cpu) = tsk;
	}
}

/**
 * idle_thread_init - Initialize the idle thread for a cpu
 * @cpu:	The cpu for which the idle thread should be initialized
 *
 * Creates the thread if it does not exist.
 */
void __init idle_threads_init(void)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		if (cpu != smp_processor_id())
			idle_init(cpu);
	}
}
#endif
