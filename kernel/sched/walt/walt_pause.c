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

	/* add ref counts for all cpus in mask */
	inc_ref_counts(cpus);

	/* only actually pause online CPUs */
	cpumask_and(cpus, cpus, cpu_online_mask);

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

	/* remove ref counts for all cpus in mask */
	dec_test_ref_counts(cpus);

	/* only actually resume online CPUs */
	cpumask_and(cpus, cpus, cpu_online_mask);

	ret = resume_cpus(cpus);
	if (ret < 0) {
		inc_ref_counts(&saved_cpus);
		pr_err("resume_cpus failure ret=%d cpus=%*pbl\n", ret,
		       cpumask_pr_args(&saved_cpus));
	}

	return ret;
}
EXPORT_SYMBOL(walt_resume_cpus);

struct work_struct walt_pause_online_work;

/* workfn to perform re-pause operation by detecting
 * ref-counts and attempting to restore the state.
 * It must not adjust the ref-counts for each cpu, or
 * the actual paused state will no longer reflect client's
 * expectations.
 */
static void walt_pause_online_workfn(struct work_struct *work)
{
	struct pause_cpu_state *pause_cpu_state;
	cpumask_t re_pause_cpus;
	int cpu, ret = 0;

	cpumask_clear(&re_pause_cpus);

	/* search and test all online cpus */
	spin_lock(&pause_lock);
	for_each_online_cpu(cpu) {
		pause_cpu_state = per_cpu_ptr(&pause_state, cpu);
		if (pause_cpu_state->ref_count)
			cpumask_set_cpu(cpu, &re_pause_cpus);
	}
	spin_unlock(&pause_lock);

	if (cpumask_empty(&re_pause_cpus))
		return;

	/* will wait for existing hp operations to complete */
	ret = pause_cpus(&re_pause_cpus);
	if (ret < 0) {
		pr_err("pause_cpus during online failure ret=%d cpus=%*pb1\n", ret,
		       cpumask_pr_args(&re_pause_cpus));
	}
}

/* do not perform online work in hotplug context */
static int walt_pause_hp_online(unsigned int online_cpu)
{
	struct pause_cpu_state *pause_cpu_state;

	pause_cpu_state = per_cpu_ptr(&pause_state, online_cpu);
	if (pause_cpu_state->ref_count)
		schedule_work(&walt_pause_online_work);
	return 0;
}

void walt_pause_init(void)
{
	int ret;

	INIT_WORK(&walt_pause_online_work, walt_pause_online_workfn);

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "walt-pause/online",
				walt_pause_hp_online, NULL);

	if (ret < 0)
		pr_err("failure to register cpuhp online state ret=%d\n", ret);
}

#endif /* CONFIG_HOTPLUG_CPU */
