// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/cpu.h>
#include <linux/cpumask.h>

#ifdef CONFIG_HOTPLUG_CPU

DEFINE_SPINLOCK(pause_lock);

struct pause_cpu_state {
	int		ref_count;
};

static DEFINE_PER_CPU(struct pause_cpu_state, pause_state);

/* increment ref count for cpus in passed mask */
static void inc_ref_counts(struct cpumask *cpus)
{
	int cpu;
	struct pause_cpu_state *pause_cpu_state;

	spin_lock(&pause_lock);
	for_each_cpu(cpu, cpus) {
		pause_cpu_state = per_cpu_ptr(&pause_state, cpu);
		if (pause_cpu_state->ref_count)
			cpumask_clear_cpu(cpu, cpus);
		pause_cpu_state->ref_count++;
	}
	spin_unlock(&pause_lock);
}

/*
 * decrement ref count for cpus in passed mask
 * updates the cpus to include only cpus ready to be unpaused
 */
static void dec_test_ref_counts(struct cpumask *cpus)
{
	int cpu;
	struct pause_cpu_state *pause_cpu_state;

	spin_lock(&pause_lock);
	for_each_cpu(cpu, cpus) {
		pause_cpu_state = per_cpu_ptr(&pause_state, cpu);
		WARN_ON_ONCE(pause_cpu_state->ref_count == 0);
		pause_cpu_state->ref_count--;
		if (pause_cpu_state->ref_count)
			cpumask_clear_cpu(cpu, cpus);
	}
	spin_unlock(&pause_lock);
}

/* cpus will be modified */
int walt_pause_cpus(struct cpumask *cpus)
{
	int ret;
	cpumask_t saved_cpus;

	cpumask_copy(&saved_cpus, cpus);

	/* prior to operation, track cpus requested to be paused */
	inc_ref_counts(cpus);
	ret = pause_cpus(cpus);
	if (ret < 0) {
		dec_test_ref_counts(&saved_cpus);
		pr_err("pause_cpus failure ret=%d cpus=%*pbl\n", ret,
		       cpumask_pr_args(&saved_cpus));
	}

	return ret;
}
EXPORT_SYMBOL(walt_pause_cpus);

/* cpus will be modified */
int walt_resume_cpus(struct cpumask *cpus)
{
	int ret;
	cpumask_t saved_cpus;

	cpumask_copy(&saved_cpus, cpus);

	dec_test_ref_counts(cpus);
	ret = resume_cpus(cpus);
	if (ret < 0) {
		inc_ref_counts(&saved_cpus);
		pr_err("resume_cpus failure ret=%d cpus=%*pbl\n", ret,
		       cpumask_pr_args(&saved_cpus));
	}

	return ret;
}
EXPORT_SYMBOL(walt_resume_cpus);

#endif /* CONFIG_HOTPLUG_CPU */
