// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <trace/hooks/sched.h>

#include "walt.h"
#include "trace.h"

static DEFINE_PER_CPU(cpumask_var_t, walt_local_cpu_mask);
DEFINE_PER_CPU(u64, rt_task_arrival_time) = 0;
static bool long_running_rt_task_trace_rgstrd;

static void rt_task_arrival_marker(void *unused, bool preempt,
	struct task_struct *prev, struct task_struct *next,
	unsigned int prev_state)
{
	unsigned int cpu = raw_smp_processor_id();

	if (next->policy == SCHED_FIFO && next != cpu_rq(cpu)->stop)
		per_cpu(rt_task_arrival_time, cpu) = rq_clock_task(this_rq());
	else
		per_cpu(rt_task_arrival_time, cpu) = 0;
}

static void long_running_rt_task_notifier(void *unused, struct rq *rq)
{
	struct task_struct *curr = rq->curr;
	unsigned int cpu = raw_smp_processor_id();

	if (!sysctl_sched_long_running_rt_task_ms)
		return;

	if (!per_cpu(rt_task_arrival_time, cpu))
		return;

	if (per_cpu(rt_task_arrival_time, cpu) && curr->policy != SCHED_FIFO) {
		/*
		 * It is possible that the scheduling policy for the current
		 * task might get changed after task arrival time stamp is
		 * noted during sched_switch of RT task. To avoid such false
		 * positives, reset arrival time stamp.
		 */
		per_cpu(rt_task_arrival_time, cpu) = 0;
		return;
	}

	/*
	 * Since we are called from the main tick, rq clock task must have
	 * been updated very recently. Use it directly, instead of
	 * update_rq_clock_task() to avoid warnings.
	 */
	if (rq->clock_task -
		per_cpu(rt_task_arrival_time, cpu)
			> sysctl_sched_long_running_rt_task_ms * MSEC_TO_NSEC) {
		printk_deferred("RT task %s (%d) runtime > %u now=%llu task arrival time=%llu runtime=%llu\n",
				curr->comm, curr->pid,
				sysctl_sched_long_running_rt_task_ms * MSEC_TO_NSEC,
				rq->clock_task,
				per_cpu(rt_task_arrival_time, cpu),
				rq->clock_task -
				per_cpu(rt_task_arrival_time, cpu));
		BUG();
	}
}

int sched_long_running_rt_task_ms_handler(struct ctl_table *table, int write,
				       void __user *buffer, size_t *lenp,
				       loff_t *ppos)
{
	int ret;
	static DEFINE_MUTEX(mutex);

	mutex_lock(&mutex);

	ret = proc_douintvec_minmax(table, write, buffer, lenp, ppos);

	if (sysctl_sched_long_running_rt_task_ms > 0 &&
			sysctl_sched_long_running_rt_task_ms < 800)
		sysctl_sched_long_running_rt_task_ms = 800;

	if (write && !long_running_rt_task_trace_rgstrd) {
		register_trace_sched_switch(rt_task_arrival_marker, NULL);
		register_trace_android_vh_scheduler_tick(long_running_rt_task_notifier, NULL);
		long_running_rt_task_trace_rgstrd = true;
	}

	mutex_unlock(&mutex);

	return ret;
}

static void walt_rt_energy_aware_wake_cpu(struct task_struct *task, struct cpumask *lowest_mask,
					  int ret, int *best_cpu)
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
	int end_index = 0;
	bool best_cpu_lt = true;

	if (unlikely(walt_disabled))
		return;

	if (!ret)
		return; /* No targets found */

	rcu_read_lock();

	if (num_sched_clusters > 3 && order_index == 0)
		end_index = 1;

	for (cluster = 0; cluster < num_sched_clusters; cluster++) {
		for_each_cpu_and(cpu, lowest_mask, &cpu_array[order_index][cluster]) {
			bool lt;

			trace_sched_cpu_util(cpu, lowest_mask);

			if (!cpu_active(cpu))
				continue;

			if (cpu_halted(cpu))
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
		if (cluster < end_index) {
			if (*best_cpu == -1 || !available_idle_cpu(*best_cpu))
				continue;
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

enum rt_fastpaths {
	NONE = 0,
	NON_WAKEUP,
	SYNC_WAKEUP,
	CLUSTER_PACKING_FASTPATH,
};


static void walt_select_task_rq_rt(void *unused, struct task_struct *task, int cpu,
					int sd_flag, int wake_flags, int *new_cpu)
{
	struct task_struct *curr;
	struct rq *rq, *this_cpu_rq;
	bool may_not_preempt;
	bool sync = !!(wake_flags & WF_SYNC);
	int ret, target = -1, this_cpu;
	struct cpumask *lowest_mask = NULL;
	int packing_cpu = -1;
	int fastpath = NONE;
	struct cpumask lowest_mask_reduced = { CPU_BITS_NONE };
	struct walt_task_struct *wts;

	if (unlikely(walt_disabled))
		return;

	/* For anything but wake ups, just return the task_cpu */
	if (sd_flag != SD_BALANCE_WAKE && sd_flag != SD_BALANCE_FORK) {
		fastpath = NON_WAKEUP;
		goto out;
	}

	this_cpu = raw_smp_processor_id();
	this_cpu_rq = cpu_rq(this_cpu);
	wts = (struct walt_task_struct *) task->android_vendor_data1;

	/*
	 * Respect the sync flag as long as the task can run on this CPU.
	 */
	if (sysctl_sched_sync_hint_enable && cpu_active(this_cpu) && !cpu_halted(this_cpu) &&
	    cpumask_test_cpu(this_cpu, task->cpus_ptr) &&
	    cpumask_test_cpu(this_cpu, &wts->reduce_mask) &&
	    walt_should_honor_rt_sync(this_cpu_rq, task, sync)) {
		fastpath = SYNC_WAKEUP;
		*new_cpu = this_cpu;
		goto out;
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
	may_not_preempt = cpu_busy_with_softirqs(cpu);

	lowest_mask = this_cpu_cpumask_var_ptr(walt_local_cpu_mask);

	/*
	 * If we're on asym system ensure we consider the different capacities
	 * of the CPUs when searching for the lowest_mask.
	 */
	ret = cpupri_find_fitness(&task_rq(task)->rd->cpupri, task,
				lowest_mask, walt_rt_task_fits_capacity);

	if (cpumask_test_cpu(0, &wts->reduce_mask))
		packing_cpu = walt_find_and_choose_cluster_packing_cpu(0, task);
	if (packing_cpu >= 0) {
		fastpath = CLUSTER_PACKING_FASTPATH;
		*new_cpu = packing_cpu;
		goto unlock;
	}

	cpumask_and(&lowest_mask_reduced, lowest_mask, &wts->reduce_mask);
	if (!cpumask_empty(&lowest_mask_reduced))
		walt_rt_energy_aware_wake_cpu(task, &lowest_mask_reduced, ret, &target);
	if (target == -1)
		walt_rt_energy_aware_wake_cpu(task, lowest_mask, ret, &target);

	/*
	 * If cpu is non-preemptible, prefer remote cpu
	 * even if it's running a higher-prio task.
	 * Otherwise: Don't bother moving it if the destination CPU is
	 * not running a lower priority task.
	 */
	if (target != -1 &&
	    (may_not_preempt || task->prio < cpu_rq(target)->rt.highest_prio.curr))
		*new_cpu = target;

	/* if backup or chosen cpu is halted, pick something else */
	if (cpu_halted(*new_cpu)) {
		cpumask_t non_halted;

		/* choose the lowest-order, unhalted, allowed CPU */
		cpumask_andnot(&non_halted, task->cpus_ptr, cpu_halt_mask);
		target = cpumask_first(&non_halted);
		if (target < nr_cpu_ids)
			*new_cpu = target;
	}
unlock:
	rcu_read_unlock();
out:
	trace_sched_select_task_rt(task, fastpath, *new_cpu, lowest_mask);
}


static void walt_rt_find_lowest_rq(void *unused, struct task_struct *task,
				   struct cpumask *lowest_mask, int ret, int *best_cpu)

{
	int packing_cpu = -1;
	int fastpath = 0;
	struct walt_task_struct *wts;
	struct cpumask lowest_mask_reduced = { CPU_BITS_NONE };

	if (unlikely(walt_disabled))
		return;

	wts = (struct walt_task_struct *) task->android_vendor_data1;

	if (cpumask_test_cpu(0, &wts->reduce_mask))
		packing_cpu = walt_find_and_choose_cluster_packing_cpu(0, task);
	if (packing_cpu >= 0) {
		*best_cpu = packing_cpu;
		fastpath = CLUSTER_PACKING_FASTPATH;
		goto out;
	}

	cpumask_and(&lowest_mask_reduced, lowest_mask, &wts->reduce_mask);
	if (!cpumask_empty(&lowest_mask_reduced))
		walt_rt_energy_aware_wake_cpu(task, &lowest_mask_reduced, ret, best_cpu);
	if (*best_cpu == -1)
		walt_rt_energy_aware_wake_cpu(task, lowest_mask, ret, best_cpu);

	/*
	 * Walt was not able to find a non-halted best cpu. Ensure that
	 * find_lowest_rq doesn't use a halted cpu going forward, but
	 * does a best effort itself to find a good CPU.
	 */
	if (*best_cpu == -1)
		cpumask_andnot(lowest_mask, lowest_mask, cpu_halt_mask);
out:
	trace_sched_rt_find_lowest_rq(task, fastpath, *best_cpu, lowest_mask);
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
	register_trace_android_rvh_find_lowest_rq(walt_rt_find_lowest_rq, NULL);
}
