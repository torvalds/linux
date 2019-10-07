// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Housekeeping management. Manage the targets for routine code that can run on
 *  any CPU: unbound workqueues, timers, kthreads and any offloadable work.
 *
 * Copyright (C) 2017 Red Hat, Inc., Frederic Weisbecker
 * Copyright (C) 2017-2018 SUSE, Frederic Weisbecker
 *
 */
#include "sched.h"

DEFINE_STATIC_KEY_FALSE(housekeeping_overridden);
EXPORT_SYMBOL_GPL(housekeeping_overridden);
static cpumask_var_t housekeeping_mask;
static unsigned int housekeeping_flags;

bool housekeeping_enabled(enum hk_flags flags)
{
	return !!(housekeeping_flags & flags);
}
EXPORT_SYMBOL_GPL(housekeeping_enabled);

int housekeeping_any_cpu(enum hk_flags flags)
{
	int cpu;

	if (static_branch_unlikely(&housekeeping_overridden)) {
		if (housekeeping_flags & flags) {
			cpu = sched_numa_find_closest(housekeeping_mask, smp_processor_id());
			if (cpu < nr_cpu_ids)
				return cpu;

			return cpumask_any_and(housekeeping_mask, cpu_online_mask);
		}
	}
	return smp_processor_id();
}
EXPORT_SYMBOL_GPL(housekeeping_any_cpu);

const struct cpumask *housekeeping_cpumask(enum hk_flags flags)
{
	if (static_branch_unlikely(&housekeeping_overridden))
		if (housekeeping_flags & flags)
			return housekeeping_mask;
	return cpu_possible_mask;
}
EXPORT_SYMBOL_GPL(housekeeping_cpumask);

void housekeeping_affine(struct task_struct *t, enum hk_flags flags)
{
	if (static_branch_unlikely(&housekeeping_overridden))
		if (housekeeping_flags & flags)
			set_cpus_allowed_ptr(t, housekeeping_mask);
}
EXPORT_SYMBOL_GPL(housekeeping_affine);

bool housekeeping_test_cpu(int cpu, enum hk_flags flags)
{
	if (static_branch_unlikely(&housekeeping_overridden))
		if (housekeeping_flags & flags)
			return cpumask_test_cpu(cpu, housekeeping_mask);
	return true;
}
EXPORT_SYMBOL_GPL(housekeeping_test_cpu);

void __init housekeeping_init(void)
{
	if (!housekeeping_flags)
		return;

	static_branch_enable(&housekeeping_overridden);

	if (housekeeping_flags & HK_FLAG_TICK)
		sched_tick_offload_init();

	/* We need at least one CPU to handle housekeeping work */
	WARN_ON_ONCE(cpumask_empty(housekeeping_mask));
}

static int __init housekeeping_setup(char *str, enum hk_flags flags)
{
	cpumask_var_t non_housekeeping_mask;
	cpumask_var_t tmp;
	int err;

	alloc_bootmem_cpumask_var(&non_housekeeping_mask);
	err = cpulist_parse(str, non_housekeeping_mask);
	if (err < 0 || cpumask_last(non_housekeeping_mask) >= nr_cpu_ids) {
		pr_warn("Housekeeping: nohz_full= or isolcpus= incorrect CPU range\n");
		free_bootmem_cpumask_var(non_housekeeping_mask);
		return 0;
	}

	alloc_bootmem_cpumask_var(&tmp);
	if (!housekeeping_flags) {
		alloc_bootmem_cpumask_var(&housekeeping_mask);
		cpumask_andnot(housekeeping_mask,
			       cpu_possible_mask, non_housekeeping_mask);

		cpumask_andnot(tmp, cpu_present_mask, non_housekeeping_mask);
		if (cpumask_empty(tmp)) {
			pr_warn("Housekeeping: must include one present CPU, "
				"using boot CPU:%d\n", smp_processor_id());
			__cpumask_set_cpu(smp_processor_id(), housekeeping_mask);
			__cpumask_clear_cpu(smp_processor_id(), non_housekeeping_mask);
		}
	} else {
		cpumask_andnot(tmp, cpu_present_mask, non_housekeeping_mask);
		if (cpumask_empty(tmp))
			__cpumask_clear_cpu(smp_processor_id(), non_housekeeping_mask);
		cpumask_andnot(tmp, cpu_possible_mask, non_housekeeping_mask);
		if (!cpumask_equal(tmp, housekeeping_mask)) {
			pr_warn("Housekeeping: nohz_full= must match isolcpus=\n");
			free_bootmem_cpumask_var(tmp);
			free_bootmem_cpumask_var(non_housekeeping_mask);
			return 0;
		}
	}
	free_bootmem_cpumask_var(tmp);

	if ((flags & HK_FLAG_TICK) && !(housekeeping_flags & HK_FLAG_TICK)) {
		if (IS_ENABLED(CONFIG_NO_HZ_FULL)) {
			tick_nohz_full_setup(non_housekeeping_mask);
		} else {
			pr_warn("Housekeeping: nohz unsupported."
				" Build with CONFIG_NO_HZ_FULL\n");
			free_bootmem_cpumask_var(non_housekeeping_mask);
			return 0;
		}
	}

	housekeeping_flags |= flags;

	free_bootmem_cpumask_var(non_housekeeping_mask);

	return 1;
}

static int __init housekeeping_nohz_full_setup(char *str)
{
	unsigned int flags;

	flags = HK_FLAG_TICK | HK_FLAG_WQ | HK_FLAG_TIMER | HK_FLAG_RCU | HK_FLAG_MISC;

	return housekeeping_setup(str, flags);
}
__setup("nohz_full=", housekeeping_nohz_full_setup);

static int __init housekeeping_isolcpus_setup(char *str)
{
	unsigned int flags = 0;

	while (isalpha(*str)) {
		if (!strncmp(str, "nohz,", 5)) {
			str += 5;
			flags |= HK_FLAG_TICK;
			continue;
		}

		if (!strncmp(str, "domain,", 7)) {
			str += 7;
			flags |= HK_FLAG_DOMAIN;
			continue;
		}

		pr_warn("isolcpus: Error, unknown flag\n");
		return 0;
	}

	/* Default behaviour for isolcpus without flags */
	if (!flags)
		flags |= HK_FLAG_DOMAIN;

	return housekeeping_setup(str, flags);
}
__setup("isolcpus=", housekeeping_isolcpus_setup);
