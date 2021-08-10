// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/cpu.h>
#include <linux/cpumask.h>

#ifdef CONFIG_HOTPLUG_CPU

static DEFINE_MUTEX(pause_lock);

struct pause_cpu_state {
	int		ref_count;
};

static DEFINE_PER_CPU(struct pause_cpu_state, pause_state);

/* increment/decrement ref count for cpus in pause/resume mask */
static void update_ref_counts(struct cpumask *cpus, bool pause)
{
	int cpu;
	struct pause_cpu_state *pause_cpu_state;

	for_each_cpu(cpu, cpus) {
		pause_cpu_state = per_cpu_ptr(&pause_state, cpu);
		if (pause)
			pause_cpu_state->ref_count++;
		else {
			WARN_ON_ONCE(pause_cpu_state->ref_count == 0);
			pause_cpu_state->ref_count--;
		}
	}
}

static void update_pause_resume_cpus(struct cpumask *cpus)
{
	int cpu;
	struct pause_cpu_state *pause_cpu_state;

	for_each_cpu(cpu, cpus) {
		pause_cpu_state = per_cpu_ptr(&pause_state, cpu);
		if (pause_cpu_state->ref_count)
			cpumask_clear_cpu(cpu, cpus);
	}
}

/* cpus will be modified */
int walt_pause_cpus(struct cpumask *cpus)
{
	int ret = 0;
	cpumask_t requested_cpus;

	mutex_lock(&pause_lock);

	cpumask_copy(&requested_cpus, cpus);
	update_pause_resume_cpus(cpus);

	/*
	 * Add ref counts for all cpus in mask, but
	 * only actually pause online CPUs
	 */
	cpumask_and(cpus, cpus, cpu_online_mask);
	if (cpumask_empty(cpus)) {
		update_ref_counts(&requested_cpus, true);
		goto unlock;
	}

	ret = pause_cpus(cpus);
	if (ret < 0)
		pr_debug("pause_cpus failure ret=%d cpus=%*pbl\n", ret,
			 cpumask_pr_args(&requested_cpus));
	else
		update_ref_counts(&requested_cpus, true);
unlock:
	mutex_unlock(&pause_lock);

	return ret;
}
EXPORT_SYMBOL(walt_pause_cpus);

/* cpus will be modified */
int walt_resume_cpus(struct cpumask *cpus)
{
	int ret = 0;
	cpumask_t requested_cpus;

	mutex_lock(&pause_lock);
	cpumask_copy(&requested_cpus, cpus);
	update_ref_counts(&requested_cpus, false);
	update_pause_resume_cpus(cpus);

	/* only actually resume online CPUs */
	cpumask_and(cpus, cpus, cpu_online_mask);
	if (cpumask_empty(cpus))
		goto unlock;

	ret = resume_cpus(cpus);
	if (ret < 0) {
		pr_debug("resume_cpus failure ret=%d cpus=%*pbl\n", ret,
			 cpumask_pr_args(&requested_cpus));
		/* restore/increment ref counts in case of error */
		update_ref_counts(&requested_cpus, true);
	}
unlock:
	mutex_unlock(&pause_lock);

	return ret;
}
EXPORT_SYMBOL(walt_resume_cpus);

struct work_struct walt_pause_online_work;

/*
 * With refcounting and online/offline operations of the CPU
 * a recent and accurate value for the requested CPUs versus
 * ref-counted CPUs, must be made.
 *
 * When a CPU is onlined, this chain of events gets out of order.
 * The online workfn can be entered at the same time as the
 * walt_resume. If both are resuming the same set of CPUs
 * the call to walt_pause will decrement ref-counts and think that
 * the CPU is unpaused.  If the workfn has already found all the
 * ref-counts (and they were still set) it will re-pause
 * the CPUs thinking that is what the client intended.  This
 * leads to a conflict, because the client software is no longer
 * tracking these CPUs, and the state doesn't match what the client
 * intended.
 *
 * This case needs protection to maintain a valid state
 * of the device (where ref-counts == # of pause requests)
 * Use a mutex such that the values read at the start of walt_pause,
 * walt_resume, or walt_pause_online_workfn remain valid until the
 * operation is complete. A mutex must be used because pause_cpus
 * (and resume_cpus) cannot be called with a spinlock held, and
 * the operation is not complete
 * until those routines return.
 */
static void walt_pause_online_workfn(struct work_struct *work)
{
	struct pause_cpu_state *pause_cpu_state;
	cpumask_t re_pause_cpus;
	int cpu, ret = 0;

	mutex_lock(&pause_lock);

	cpumask_clear(&re_pause_cpus);

	/* search and test all online cpus */
	for_each_online_cpu(cpu) {
		pause_cpu_state = per_cpu_ptr(&pause_state, cpu);
		if (pause_cpu_state->ref_count)
			cpumask_set_cpu(cpu, &re_pause_cpus);
	}

	if (cpumask_empty(&re_pause_cpus))
		goto unlock;

	ret = pause_cpus(&re_pause_cpus);

unlock:
	mutex_unlock(&pause_lock);

	if (ret < 0)
		printk_deferred("pause_cpus during online failure ret=%d cpus=%*pb1\n", ret,
		       cpumask_pr_args(&re_pause_cpus));
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
		printk_deferred("failure to register cpuhp online state ret=%d\n", ret);
}
#endif /* CONFIG_HOTPLUG_CPU */
