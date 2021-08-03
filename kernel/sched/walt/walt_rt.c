// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#include <trace/hooks/sched.h>

#include "walt.h"
#include "trace.h"

static DEFINE_PER_CPU(cpumask_var_t, walt_local_cpu_mask);

static void walt_rt_energy_aware_wake_cpu(void *unused, struct task_struct *task,
				struct cpumask *lowest_mask, int ret, int *best_cpu)
{
	int cpu;
	unsigned long util, best_cpu_util = ULONG_MAX;
	unsigned long best_cpu_util_cum = ULONG_MAX;
	unsigned long util_cum;
	unsigned long tutil = task_util(task);
	unsigned int best_idle_exit_latency = UINT_MAX;
	unsigned int cpu_idle_exit_latency = UINT_MAX;
	bool boost_on_big = rt_boost_on_big();
	int cluster;
	int order_index = (boost_on_big && num_sched_clusters > 1) ? 1 : 0;
	bool best_cpu_lt = true;

	if (unlikely(walt_disabled))
		return;

	if (!ret)
		return; /* No targets found */

	rcu_read_lock();
	for (cluster = 0; cluster < num_sched_clusters; cluster++) {
		for_each_cpu_and(cpu, lowest_mask, &cpu_array[order_index][cluster]) {
			bool lt;

			trace_sched_cpu_util(cpu);

			if (!cpu_active(cpu))
				continue;

			if (sched_cpu_high_irqload(cpu))
				continue;

			if (__cpu_overutilized(cpu, tutil))
				continue;

			util = cpu_util(cpu);

			lt = (walt_low_latency_task(cpu_rq(cpu)->curr) ||
				walt_nr_rtg_high_prio(cpu));

			/*
			 * When the best is suitable and the current is not,
			 * skip it
			 */
			if (lt && !best_cpu_lt)
				continue;

			/*
			 * Either both are sutilable or unsuitable, load takes
			 * precedence.
			 */
			if (!(best_cpu_lt ^ lt) && (util > best_cpu_util))
				continue;

			/*
			 * If the previous CPU has same load, keep it as
			 * best_cpu.
			 */
			if (best_cpu_util == util && *best_cpu == task_cpu(task))
				continue;

			/*
			 * If candidate CPU is the previous CPU, select it.
			 * Otherwise, if its load is same with best_cpu and in
			 * a shallower C-state, select it.  If all above
			 * conditions are same, select the least cumulative
			 * window demand CPU.
			 */
			cpu_idle_exit_latency = walt_get_idle_exit_latency(cpu_rq(cpu));

			util_cum = cpu_util_cum(cpu);
			if (cpu != task_cpu(task) && best_cpu_util == util) {
				if (best_idle_exit_latency < cpu_idle_exit_latency)
					continue;

				if (best_idle_exit_latency == cpu_idle_exit_latency &&
						best_cpu_util_cum < util_cum)
					continue;
			}

			best_idle_exit_latency = cpu_idle_exit_latency;
			best_cpu_util_cum = util_cum;
			best_cpu_util = util;
			*best_cpu = cpu;
			best_cpu_lt = lt;
		}

		if (*best_cpu != -1)
			break;
	}

	rcu_read_unlock();
}

#ifdef CONFIG_UCLAMP_TASK
static inline bool walt_rt_task_fits_capacity(struct task_struct *p, int cpu)
{
	unsigned int min_cap;
	unsigned int max_cap;
	unsigned int cpu_cap;

	min_cap = uclamp_eff_value(p, UCLAMP_MIN);
	max_cap = uclamp_eff_value(p, UCLAMP_MAX);

	cpu_cap = capacity_orig_of(cpu);

	return cpu_cap >= min(min_cap, max_cap);
}
#else
static inline bool walt_rt_task_fits_capacity(struct task_struct *p, int cpu)
{
	return true;
}
#endif

/*
 * walt specific should_honor_rt_sync (see rt.c).  this will honor
 * the sync flag regardless of whether the current waker is cfs or rt
 */
static inline bool walt_should_honor_rt_sync(struct rq *rq, struct task_struct *p,
					     bool sync)
{
	return sync &&
		p->prio <= rq->rt.highest_prio.next &&
		rq->rt.rt_nr_running <= 2;
}

static void walt_select_task_rq_rt(void *unused, struct task_struct *task, int cpu,
					int sd_flag, int wake_flags, int *new_cpu)
{
	struct task_struct *curr;
	struct rq *rq, *this_cpu_rq;
	bool may_not_preempt;
	bool sync = !!(wake_flags && WF_SYNC);
	int ret, target = -1, this_cpu;
	struct cpumask *lowest_mask;

	if (unlikely(walt_disabled))
		return;

	/* For anything but wake ups, just return the task_cpu */
	if (sd_flag != SD_BALANCE_WAKE && sd_flag != SD_BALANCE_FORK)
		return;

	this_cpu = raw_smp_processor_id();
	this_cpu_rq = cpu_rq(this_cpu);

	/*
	 * Respect the sync flag as long as the task can run on this CPU.
	 */
	if (sysctl_sched_sync_hint_enable && cpu_active(this_cpu) &&
	    cpumask_test_cpu(this_cpu, task->cpus_ptr) &&
	    walt_should_honor_rt_sync(this_cpu_rq, task, sync)) {
		*new_cpu = this_cpu;
		return;
	}

	*new_cpu = cpu; /* previous CPU as back up */
	rq = cpu_rq(cpu);

	rcu_read_lock();
	curr = READ_ONCE(rq->curr); /* unlocked access */

	/*
	 * If the current task on @p's runqueue is a softirq task,
	 * it may run without preemption for a time that is
	 * ill-suited for a waiting RT task. Therefore, try to
	 * wake this RT task on another runqueue.
	 *
	 * Otherwise, just let it ride on the affined RQ and the
	 * post-schedule router will push the preempted task away
	 *
	 * This test is optimistic, if we get it wrong the load-balancer
	 * will have to sort it out.
	 *
	 * We take into account the capacity of the CPU to ensure it fits the
	 * requirement of the task - which is only important on heterogeneous
	 * systems like big.LITTLE.
	 */
	may_not_preempt = task_may_not_preempt(curr, cpu);

	lowest_mask = this_cpu_cpumask_var_ptr(walt_local_cpu_mask);

	/*
	 * If we're on asym system ensure we consider the different capacities
	 * of the CPUs when searching for the lowest_mask.
	 */
	ret = cpupri_find_fitness(&task_rq(task)->rd->cpupri, task,
				lowest_mask, walt_rt_task_fits_capacity);

	walt_rt_energy_aware_wake_cpu(NULL, task, lowest_mask, ret, &target);

	/*
	 * If cpu is non-preemptible, prefer remote cpu
	 * even if it's running a higher-prio task.
	 * Otherwise: Don't bother moving it if the destination CPU is
	 * not running a lower priority task.
	 */
	if (target != -1 &&
	    (may_not_preempt || task->prio < cpu_rq(target)->rt.highest_prio.curr))
		*new_cpu = target;

	rcu_read_unlock();
}

void walt_rt_init(void)
{
	unsigned int i;

	for_each_possible_cpu(i) {
		if(!(zalloc_cpumask_var_node(&per_cpu(walt_local_cpu_mask, i),
					GFP_KERNEL, cpu_to_node(i)))) {
			pr_err("walt_local_cpu_mask alloc failed for cpu%d\n", i);
			return;
		}
	}

	register_trace_android_rvh_select_task_rq_rt(walt_select_task_rq_rt, NULL);
	register_trace_android_rvh_find_lowest_rq(walt_rt_energy_aware_wake_cpu, NULL);
}
