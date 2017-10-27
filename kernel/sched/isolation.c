/*
 *  Housekeeping management. Manage the targets for routine code that can run on
 *  any CPU: unbound workqueues, timers, kthreads and any offloadable work.
 *
 * Copyright (C) 2017 Red Hat, Inc., Frederic Weisbecker
 *
 */

#include <linux/sched/isolation.h>
#include <linux/tick.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/static_key.h>

DEFINE_STATIC_KEY_FALSE(housekeeping_overriden);
EXPORT_SYMBOL_GPL(housekeeping_overriden);
static cpumask_var_t housekeeping_mask;

int housekeeping_any_cpu(void)
{
	if (static_branch_unlikely(&housekeeping_overriden))
		return cpumask_any_and(housekeeping_mask, cpu_online_mask);

	return smp_processor_id();
}
EXPORT_SYMBOL_GPL(housekeeping_any_cpu);

const struct cpumask *housekeeping_cpumask(void)
{
	if (static_branch_unlikely(&housekeeping_overriden))
		return housekeeping_mask;

	return cpu_possible_mask;
}
EXPORT_SYMBOL_GPL(housekeeping_cpumask);

void housekeeping_affine(struct task_struct *t)
{
	if (static_branch_unlikely(&housekeeping_overriden))
		set_cpus_allowed_ptr(t, housekeeping_mask);
}
EXPORT_SYMBOL_GPL(housekeeping_affine);

bool housekeeping_test_cpu(int cpu)
{
	if (static_branch_unlikely(&housekeeping_overriden))
		return cpumask_test_cpu(cpu, housekeeping_mask);

	return true;
}
EXPORT_SYMBOL_GPL(housekeeping_test_cpu);

void __init housekeeping_init(void)
{
	if (!tick_nohz_full_enabled())
		return;

	if (!alloc_cpumask_var(&housekeeping_mask, GFP_KERNEL)) {
		WARN(1, "NO_HZ: Can't allocate not-full dynticks cpumask\n");
		cpumask_clear(tick_nohz_full_mask);
		tick_nohz_full_running = false;
		return;
	}

	cpumask_andnot(housekeeping_mask,
		       cpu_possible_mask, tick_nohz_full_mask);

	static_branch_enable(&housekeeping_overriden);

	/* We need at least one CPU to handle housekeeping work */
	WARN_ON_ONCE(cpumask_empty(housekeeping_mask));
}
