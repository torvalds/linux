// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/seq_file.h>
#include <trace/hooks/sched.h>
#include <trace/hooks/binder.h>

#include "walt.h"
#include "trace.h"
#include <../../../drivers/android/binder_internal.h>
#include "../../../drivers/android/binder_trace.h"

static void create_util_to_cost_pd(struct em_perf_domain *pd)
{
	int util, cpu = cpumask_first(to_cpumask(pd->cpus));
	unsigned long fmax;
	unsigned long scale_cpu;
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu);
	struct walt_sched_cluster *cluster = wrq->cluster;

	fmax = (u64)pd->table[pd->nr_perf_states - 1].frequency;
	scale_cpu = arch_scale_cpu_capacity(cpu);

	for (util = 0; util < 1024; util++) {
		int j;

		int f = (fmax * util) / scale_cpu;
		struct em_perf_state *ps = &pd->table[0];

		for (j = 0; j < pd->nr_perf_states; j++) {
			ps = &pd->table[j];
			if (ps->frequency >= f)
				break;
		}
		cluster->util_to_cost[util] = ps->cost;
	}
}

void create_util_to_cost(void)
{
	struct perf_domain *pd;
	struct root_domain *rd = cpu_rq(smp_processor_id())->rd;

	rcu_read_lock();
	pd = rcu_dereference(rd->pd);
	for (; pd; pd = pd->next)
		create_util_to_cost_pd(pd->em_pd);
	rcu_read_unlock();
}

DECLARE_PER_CPU(unsigned long, gov_last_util);

/* Migration margins */
unsigned int sched_capacity_margin_up[WALT_NR_CPUS] = {
			[0 ... WALT_NR_CPUS-1] = 1078 /* ~5% margin */
};
unsigned int sched_capacity_margin_down[WALT_NR_CPUS] = {
			[0 ... WALT_NR_CPUS-1] = 1205 /* ~15% margin */
};

/* Migration margins for topapp */
unsigned int sched_capacity_margin_early_up[WALT_NR_CPUS] = {
			[0 ... WALT_NR_CPUS-1] = 1078 /* ~5% margin */
};
unsigned int sched_capacity_margin_early_down[WALT_NR_CPUS] = {
			[0 ... WALT_NR_CPUS-1] = 1205 /* ~15% margin */
};

static inline bool
bias_to_this_cpu(struct task_struct *p, int cpu, int start_cpu)
{
	bool base_test = cpumask_test_cpu(cpu, p->cpus_ptr) &&
						cpu_active(cpu);

	bool start_cap_test = !check_for_higher_capacity(start_cpu, cpu);

	return base_test && start_cap_test;
}

static inline bool task_demand_fits(struct task_struct *p, int dst_cpu)
{
	if (is_max_possible_cluster_cpu(dst_cpu))
		return true;

	if (!task_in_related_thread_group(p) && p->prio >= 124 &&
			!is_min_possible_cluster_cpu(dst_cpu) &&
			!is_max_possible_cluster_cpu(dst_cpu)) {
		/* a non topapp low prio task fits on gold */
		return true;
	}
	return task_fits_capacity(p, dst_cpu);
}

struct find_best_target_env {
	bool	is_rtg;
	int	need_idle;
	int	fastpath;
	int	start_cpu;
	int	order_index;
	int	end_index;
	bool	strict_max;
	int	skip_cpu;
	u64	prs[8];
};

/*
 * cpu_util_without: compute cpu utilization without any contributions from *p
 * @cpu: the CPU which utilization is requested
 * @p: the task which utilization should be discounted
 *
 * The utilization of a CPU is defined by the utilization of tasks currently
 * enqueued on that CPU as well as tasks which are currently sleeping after an
 * execution on that CPU.
 *
 * This method returns the utilization of the specified CPU by discounting the
 * utilization of the specified task, whenever the task is currently
 * contributing to the CPU utilization.
 */
static unsigned long cpu_util_without(int cpu, struct task_struct *p)
{
	unsigned int util;

	/*
	 * WALT does not decay idle tasks in the same manner
	 * as PELT, so it makes little sense to subtract task
	 * utilization from cpu utilization. Instead just use
	 * cpu_util for this case.
	 */
	if (likely(READ_ONCE(p->__state) == TASK_WAKING))
		return cpu_util(cpu);

	/* Task has no contribution or is new */
	if (cpu != task_cpu(p) || !READ_ONCE(p->se.avg.last_update_time))
		return cpu_util(cpu);

	util = max_t(long, cpu_util(cpu) - task_util(p), 0);

	/*
	 * Utilization (estimated) can exceed the CPU capacity, thus let's
	 * clamp to the maximum CPU capacity to ensure consistency with
	 * the cpu_util call.
	 */
	return min_t(unsigned long, util, capacity_orig_of(cpu));
}

static inline bool walt_task_skip_min_cpu(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	return (sched_boost_type != CONSERVATIVE_BOOST) &&
		walt_get_rtg_status(p) && (wts->unfilter ||
		walt_pipeline_low_latency_task(p));
}

static inline bool walt_is_many_wakeup(int sibling_count_hint)
{
	return sibling_count_hint >= sysctl_sched_many_wakeup_threshold;
}

static inline bool walt_target_ok(int target_cpu, int order_index)
{
	return !((order_index != num_sched_clusters - 1) &&
		 (cpumask_weight(&cpu_array[order_index][0]) == 1) &&
		 (target_cpu == cpumask_first(&cpu_array[order_index][0])));
}

#define MIN_UTIL_FOR_ENERGY_EVAL	52
static void walt_get_indicies(struct task_struct *p, int *order_index,
		int *end_index, int per_task_boost, bool is_uclamp_boosted,
		bool *energy_eval_needed)
{
	*order_index = 0;
	*end_index = 0;

	if (num_sched_clusters <= 1)
		return;

	if (per_task_boost > TASK_BOOST_ON_MID) {
		*order_index = num_sched_clusters - 1;
		*energy_eval_needed = false;
		return;
	}

	if (is_full_throttle_boost()) {
		*energy_eval_needed = false;
		*order_index = num_sched_clusters - 1;
		*end_index = num_sched_clusters - 2;

		for (; *end_index >= 0; (*end_index)--)
			if (task_demand_fits(p,
					cpumask_first(&cpu_array[*order_index][*end_index])))
				break;
		return;
	}

	if (is_uclamp_boosted || per_task_boost ||
		task_boost_policy(p) == SCHED_BOOST_ON_BIG ||
		walt_task_skip_min_cpu(p)) {
		*energy_eval_needed = false;
		*order_index = 1;
		*end_index = max(0, num_sched_clusters - 3);

		if (sysctl_sched_asymcap_boost) {
			(*end_index)++;
			return;
		}
	}

	for (; *order_index < num_sched_clusters - 1; (*order_index)++) {
		if (task_demand_fits(p, cpumask_first(&cpu_array[*order_index][0])))
			break;
	}

	if (*order_index == 0 &&
			(task_util(p) >= MIN_UTIL_FOR_ENERGY_EVAL) &&
			!(p->in_iowait && task_in_related_thread_group(p)) &&
			!walt_get_rtg_status(p) &&
			!(sched_boost_type == CONSERVATIVE_BOOST && task_sched_boost(p)) &&
			!sysctl_sched_suppress_region2
		)
		*end_index = 1;

	if (p->in_iowait && task_in_related_thread_group(p))
		*energy_eval_needed = false;
}

enum fastpaths {
	NONE = 0,
	SYNC_WAKEUP,
	PREV_CPU_FASTPATH,
	CLUSTER_PACKING_FASTPATH,
	PIPELINE_FASTPATH,
};

static inline bool is_complex_sibling_idle(int cpu)
{
	if (cpu_l2_sibling[cpu] != -1)
		return available_idle_cpu(cpu_l2_sibling[cpu]);
	return false;
}

static inline bool walt_should_reject_fbt_cpu(struct walt_rq *wrq, struct task_struct *p,
						int cpu, int order_index,
						struct find_best_target_env *fbt_env)
{
	if (!cpu_active(cpu))
		return true;

	if (cpu_halted(cpu))
		return true;

	if (order_index != 0 && cpu_partial_halted(cpu))
		return true;

	/*
	 * This CPU is the target of an active migration that's
	 * yet to complete. Avoid placing another task on it.
	 */
	if (is_reserved(cpu))
		return true;

	if (sched_cpu_high_irqload(cpu))
		return true;

	if (fbt_env->skip_cpu == cpu)
		return true;

	if (wrq->num_mvp_tasks > 0 && per_task_boost(p) != TASK_BOOST_STRICT_MAX)
		return true;

	return false;
}

bool select_prev_cpu_fastpath(int prev_cpu, int start_cpu, int order_index,
		struct task_struct *p)
{
	struct walt_rq *prev_wrq = &per_cpu(walt_rq, prev_cpu);
	struct walt_rq *start_wrq = &per_cpu(walt_rq, start_cpu);
	bool valid_part_haltable_prev_cpu = false, valid_prev_cpu = false;

	if (!cpu_active(prev_cpu))
		return false;

	if (!available_idle_cpu(prev_cpu))
		return false;

	if (!cpumask_test_cpu(prev_cpu, p->cpus_ptr))
		return false;

	if (cpu_halted(prev_cpu))
		return false;

	if (is_reserved(prev_cpu))
		return false;

	valid_part_haltable_prev_cpu = cpumask_test_cpu(prev_cpu, &part_haltable_cpus) &&
					((order_index == 0 && cpu_partial_halted(prev_cpu)) ||
					 (order_index == 1 && !cpu_partial_halted(prev_cpu)));
	valid_prev_cpu = (prev_wrq->cluster->id == start_wrq->cluster->id);

	if (!(valid_part_haltable_prev_cpu || valid_prev_cpu))
		return false;

	return true;
}

#define DIRE_STRAITS_PREV_NR_LIMIT 10
static void walt_find_best_target(struct sched_domain *sd,
					cpumask_t *candidates,
					struct task_struct *p,
					struct find_best_target_env *fbt_env,
					bool *force_energy_eval)
{
	unsigned long min_task_util = uclamp_task_util(p);
	long target_max_spare_cap = 0;
	unsigned long best_idle_cuml_util = ULONG_MAX;
	unsigned int min_exit_latency = UINT_MAX;
	int i, start_cpu;
	long spare_wake_cap, most_spare_wake_cap = 0;
	int most_spare_cap_cpu = -1;
	int least_nr_cpu = -1;
	unsigned int cpu_rq_runnable_cnt = UINT_MAX;
	int prev_cpu = task_cpu(p);
	int order_index = fbt_env->order_index, end_index = fbt_env->end_index;
	int stop_index = INT_MAX;
	int cluster;
	unsigned int target_nr_rtg_high_prio = UINT_MAX;
	bool rtg_high_prio_task = task_rtg_high_prio(p);
	cpumask_t visit_cpus;
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;
	int packing_cpu, cpu;
	unsigned int search_sibling_cluster = 0;
	bool visited_clusters[MAX_CLUSTERS] = {[0 ... (MAX_CLUSTERS-1)] = false};

	/* Find start CPU based on boost value */
	start_cpu = fbt_env->start_cpu;

	/*
	 * For higher capacity worth I/O tasks, stop the search
	 * at the end of higher capacity cluster(s).
	 */
	if (order_index > 0 && wts->iowaited) {
		stop_index = num_sched_clusters - 2;
		most_spare_wake_cap = LONG_MIN;
	}

	if (fbt_env->strict_max) {
		stop_index = 0;
		most_spare_wake_cap = LONG_MIN;
	}

	/* fast path for packing_cpu */
	packing_cpu = walt_find_and_choose_cluster_packing_cpu(start_cpu, p);
	if (packing_cpu >= 0) {
		fbt_env->fastpath = CLUSTER_PACKING_FASTPATH;
		cpumask_set_cpu(packing_cpu, candidates);
		visited_clusters[cpu_cluster(packing_cpu)->id] = true;
		goto out;
	}

	/* fast path for prev_cpu */
	if (select_prev_cpu_fastpath(prev_cpu, start_cpu, order_index, p)) {
		fbt_env->fastpath = PREV_CPU_FASTPATH;
		cpumask_set_cpu(prev_cpu, candidates);
		visited_clusters[cpu_cluster(prev_cpu)->id] = true;
		goto out;
	}

/* retry for sibling cluster */
retry:
	for (cluster = 0; cluster < num_sched_clusters; cluster++) {
		int best_idle_cpu_cluster = -1;
		int target_cpu_cluster = -1;
		int this_complex_idle = 0;
		int best_complex_idle = 0;
		int cluster_id;

		target_max_spare_cap = 0;
		min_exit_latency = INT_MAX;
		best_idle_cuml_util = ULONG_MAX;

		if (search_sibling_cluster) {
			if (!(search_sibling_cluster & BIT(cluster)))
				continue;
			cluster_id = cluster;
			cpumask_and(&visit_cpus, p->cpus_ptr, &sched_cluster[cluster]->cpus);
		} else {
			cpumask_and(&visit_cpus, p->cpus_ptr, &cpu_array[order_index][cluster]);
			cluster_id = cpu_cluster(
					cpumask_first(&cpu_array[order_index][cluster]))->id;
		}

		if (visited_clusters[cluster_id])
			continue;

		visited_clusters[cluster_id] = true;

		for_each_cpu(i, &visit_cpus) {
			unsigned long capacity_orig = capacity_orig_of(i);
			unsigned long wake_cpu_util, new_cpu_util, new_util_cuml;
			long spare_cap;
			unsigned int idle_exit_latency = UINT_MAX;
			struct walt_rq *wrq = &per_cpu(walt_rq, i);

			trace_sched_cpu_util(i, NULL);
			/* record the prss as we visit cpus in a cluster */
			fbt_env->prs[i] = wrq->prev_runnable_sum + wrq->grp_time.prev_runnable_sum;

			if (walt_should_reject_fbt_cpu(wrq, p, i, order_index, fbt_env))
				continue;

			/*
			 * p's blocked utilization is still accounted for on prev_cpu
			 * so prev_cpu will receive a negative bias due to the double
			 * accounting. However, the blocked utilization may be zero.
			 */
			wake_cpu_util = cpu_util_without(i, p);
			spare_wake_cap = capacity_orig - wake_cpu_util;

			if (spare_wake_cap > most_spare_wake_cap) {
				most_spare_wake_cap = spare_wake_cap;
				most_spare_cap_cpu = i;
			}

			/*
			 * Keep track of runnables for each CPU, if none of the
			 * CPUs have spare capacity then use CPU with less
			 * number of runnables.
			 */
			if (cpu_rq(i)->nr_running < cpu_rq_runnable_cnt) {
				cpu_rq_runnable_cnt = cpu_rq(i)->nr_running;
				least_nr_cpu = i;
			}

			/*
			 * Ensure minimum capacity to grant the required boost.
			 * The target CPU can be already at a capacity level higher
			 * than the one required to boost the task.
			 */
			new_cpu_util = wake_cpu_util + min_task_util;
			if (new_cpu_util > capacity_orig)
				continue;

			/*
			 * Find an optimal backup IDLE CPU for non latency
			 * sensitive tasks.
			 *
			 * Looking for:
			 * - favoring shallowest idle states
			 *   i.e. avoid to wakeup deep-idle CPUs
			 *
			 * The following code path is used by non latency
			 * sensitive tasks if IDLE CPUs are available. If at
			 * least one of such CPUs are available it sets the
			 * best_idle_cpu to the most suitable idle CPU to be
			 * selected.
			 *
			 * If idle CPUs are available, favour these CPUs to
			 * improve performances by spreading tasks.
			 * Indeed, the energy_diff() computed by the caller
			 * will take care to ensure the minimization of energy
			 * consumptions without affecting performance.
			 */
			if (available_idle_cpu(i)) {
				idle_exit_latency = walt_get_idle_exit_latency(cpu_rq(i));

				this_complex_idle = is_complex_sibling_idle(i) ? 1 : 0;

				if (this_complex_idle < best_complex_idle)
					continue;
				/*
				 * Prefer shallowest over deeper idle state cpu,
				 * of same capacity cpus.
				 */
				if (idle_exit_latency > min_exit_latency)
					continue;

				new_util_cuml = cpu_util_cum(i);
				if (min_exit_latency == idle_exit_latency &&
					(best_idle_cpu_cluster == prev_cpu ||
					(i != prev_cpu &&
					new_util_cuml > best_idle_cuml_util)))
					continue;

				min_exit_latency = idle_exit_latency;
				best_idle_cuml_util = new_util_cuml;
				best_idle_cpu_cluster = i;
				best_complex_idle = this_complex_idle;
				continue;
			}

			/* skip visiting any more busy if idle was found */
			if (best_idle_cpu_cluster != -1)
				continue;

			/*
			 * Compute the maximum possible capacity we expect
			 * to have available on this CPU once the task is
			 * enqueued here.
			 */
			spare_cap = capacity_orig - new_cpu_util;

			/*
			 * Try to spread the rtg high prio tasks so that they
			 * don't preempt each other. This is a optimisitc
			 * check assuming rtg high prio can actually preempt
			 * the current running task with the given vruntime
			 * boost.
			 */
			if (rtg_high_prio_task) {
				if (walt_nr_rtg_high_prio(i) > target_nr_rtg_high_prio)
					continue;

				/* Favor CPUs with maximum spare capacity */
				if (walt_nr_rtg_high_prio(i) == target_nr_rtg_high_prio &&
						spare_cap < target_max_spare_cap)
					continue;
			} else {
				/* Favor CPUs with maximum spare capacity */
				if (spare_cap < target_max_spare_cap)
					continue;
			}

			target_max_spare_cap = spare_cap;
			target_nr_rtg_high_prio = walt_nr_rtg_high_prio(i);
			target_cpu_cluster = i;
		}

		if (best_idle_cpu_cluster != -1)
			cpumask_set_cpu(best_idle_cpu_cluster, candidates);
		else if (target_cpu_cluster != -1)
			cpumask_set_cpu(target_cpu_cluster, candidates);

		if ((cluster >= end_index) && (!cpumask_empty(candidates)) &&
			walt_target_ok(target_cpu_cluster, order_index))
			break;

		if (most_spare_cap_cpu != -1 && cluster >= stop_index)
			break;
	}

	/*
	 * We have set idle or target as long as they are valid CPUs.
	 * If we don't find either, then we fallback to most_spare_cap,
	 * If we don't find most spare cap, we fallback to prev_cpu,
	 * provided that the prev_cpu is active and has less than
	 * DIRE_STRAITS_PREV_NR_LIMIT runnables otherwise, we fallback to cpu
	 * with least number of runnables.
	 */

	if (unlikely(cpumask_empty(candidates))) {
		if (most_spare_cap_cpu != -1)
			cpumask_set_cpu(most_spare_cap_cpu, candidates);
		else if (cpu_active(prev_cpu)
			 && (cpu_rq(prev_cpu)->nr_running < DIRE_STRAITS_PREV_NR_LIMIT))
			cpumask_set_cpu(prev_cpu, candidates);
		else if (least_nr_cpu != -1)
			cpumask_set_cpu(least_nr_cpu, candidates);
	}

out:
	search_sibling_cluster = 0;
	for_each_cpu(cpu, candidates) {
		struct walt_sched_cluster *cluster = cpu_cluster(cpu);
		int sibling = cluster->sibling_cluster;

		if ((sibling >= 0) && !visited_clusters[sibling]) {
			search_sibling_cluster |= BIT(sibling);
		}
	}
	if (search_sibling_cluster) {
		*force_energy_eval = true;
		goto retry;
	}

	trace_sched_find_best_target(p, min_task_util, start_cpu, cpumask_bits(candidates)[0],
			     most_spare_cap_cpu, order_index, end_index,
			     fbt_env->skip_cpu, task_on_rq_queued(p), least_nr_cpu,
			     cpu_rq_runnable_cnt);
}

static inline unsigned long
cpu_util_next_walt(int cpu, struct task_struct *p, int dst_cpu)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu);
	unsigned long util = wrq->walt_stats.cumulative_runnable_avg_scaled;
	bool queued = task_on_rq_queued(p);

	/*
	 * When task is queued,
	 * (a) The evaluating CPU (cpu) is task's current CPU. If the
	 * task is migrating, discount the task contribution from the
	 * evaluation cpu.
	 * (b) The evaluating CPU (cpu) is task's current CPU. If the
	 * task is NOT migrating, nothing to do. The contribution is
	 * already present on the evaluation CPU.
	 * (c) The evaluating CPU (cpu) is not task's current CPU. But
	 * the task is migrating to the evaluating CPU. So add the
	 * task contribution to it.
	 * (d) The evaluating CPU (cpu) is neither the current CPU nor
	 * the destination CPU. don't care.
	 *
	 * When task is NOT queued i.e waking. Task contribution is not
	 * present on any CPU.
	 *
	 * (a) If the evaluating CPU is the destination CPU, add the task
	 * contribution.
	 * (b) The evaluation CPU is not the destination CPU, don't care.
	 */
	if (unlikely(queued)) {
		if (task_cpu(p) == cpu) {
			if (dst_cpu != cpu)
				util = max_t(long, util - task_util(p), 0);
		} else if (dst_cpu == cpu) {
			util += task_util(p);
		}
	} else if (dst_cpu == cpu) {
		util += task_util(p);
	}

	return min_t(unsigned long, util, capacity_orig_of(cpu));
}

static inline u64
cpu_util_next_walt_prs(int cpu, struct task_struct *p, int dst_cpu, bool prev_dst_same_cluster,
											u64 *prs)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;
	long util = prs[cpu];

	if (wts->prev_window) {
		if (!prev_dst_same_cluster) {
			/* intercluster migration of non rtg task - mimic fixups */
			util -= wts->prev_window_cpu[cpu];
			if (util < 0)
				util = 0;
			if (cpu == dst_cpu)
				util += wts->prev_window;
		}
	} else {
		if (cpu == dst_cpu)
			util += wts->demand;
	}

	return util;
}

static inline unsigned long get_util_to_cost(int cpu, unsigned long util)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu);

	if (cpu == 0 && util > sysctl_em_inflate_thres)
		return mult_frac(wrq->cluster->util_to_cost[util], sysctl_em_inflate_pct, 100);
	else
		return wrq->cluster->util_to_cost[util];
}

/**
 * walt_em_cpu_energy() - Estimates the energy consumed by the CPUs of a
		performance domain
 * @pd		: performance domain for which energy has to be estimated
 * @max_util	: highest utilization among CPUs of the domain
 * @sum_util	: sum of the utilization of all CPUs in the domain
 *
 * This function must be used only for CPU devices. There is no validation,
 * i.e. if the EM is a CPU type and has cpumask allocated. It is called from
 * the scheduler code quite frequently and that is why there is not checks.
 *
 * Return: the sum of the energy consumed by the CPUs of the domain assuming
 * a capacity state satisfying the max utilization of the domain.
 */
static inline unsigned long walt_em_cpu_energy(struct em_perf_domain *pd,
				unsigned long max_util, unsigned long sum_util,
				struct compute_energy_output *output, unsigned int x)
{
	unsigned long scale_cpu, cost;
	int cpu;

	if (!sum_util)
		return 0;

	/*
	 * In order to predict the capacity state, map the utilization of the
	 * most utilized CPU of the performance domain to a requested frequency,
	 * like schedutil.
	 */
	cpu = cpumask_first(to_cpumask(pd->cpus));
	scale_cpu = arch_scale_cpu_capacity(cpu);

	max_util = max_util + (max_util >> 2); /* account  for TARGET_LOAD usually 80 */
	max_util = max(max_util,
			(arch_scale_freq_capacity(cpu) * scale_cpu) >>
			SCHED_CAPACITY_SHIFT);

	/*
	 * The capacity of a CPU in the domain at the performance state (ps)
	 * can be computed as:
	 *
	 *             ps->freq * scale_cpu
	 *   ps->cap = --------------------                          (1)
	 *                 cpu_max_freq
	 *
	 * So, ignoring the costs of idle states (which are not available in
	 * the EM), the energy consumed by this CPU at that performance state
	 * is estimated as:
	 *
	 *             ps->power * cpu_util
	 *   cpu_nrg = --------------------                          (2)
	 *                   ps->cap
	 *
	 * since 'cpu_util / ps->cap' represents its percentage of busy time.
	 *
	 *   NOTE: Although the result of this computation actually is in
	 *         units of power, it can be manipulated as an energy value
	 *         over a scheduling period, since it is assumed to be
	 *         constant during that interval.
	 *
	 * By injecting (1) in (2), 'cpu_nrg' can be re-expressed as a product
	 * of two terms:
	 *
	 *             ps->power * cpu_max_freq   cpu_util
	 *   cpu_nrg = ------------------------ * ---------          (3)
	 *                    ps->freq            scale_cpu
	 *
	 * The first term is static, and is stored in the em_perf_state struct
	 * as 'ps->cost'.
	 *
	 * Since all CPUs of the domain have the same micro-architecture, they
	 * share the same 'ps->cost', and the same CPU capacity. Hence, the
	 * total energy of the domain (which is the simple sum of the energy of
	 * all of its CPUs) can be factorized as:
	 *
	 *            ps->cost * \Sum cpu_util
	 *   pd_nrg = ------------------------                       (4)
	 *                  scale_cpu
	 */
	if (max_util >= 1024)
		max_util = 1023;

	cost = get_util_to_cost(cpu, max_util);

	if (output) {
		output->cost[x] = cost;
		output->max_util[x] = max_util;
		output->sum_util[x] = sum_util;
	}
	return cost * sum_util / scale_cpu;
}

/*
 * walt_pd_compute_energy(): Estimates the energy that @pd would consume if @p was
 * migrated to @dst_cpu. compute_energy() predicts what will be the utilization
 * landscape of @pd's CPUs after the task migration, and uses the Energy Model
 * to compute what would be the energy if we decided to actually migrate that
 * task.
 */
static long
walt_pd_compute_energy(struct task_struct *p, int dst_cpu, struct perf_domain *pd, u64 *prs,
		struct compute_energy_output *output, unsigned int x)
{
	struct cpumask *pd_mask = perf_domain_span(pd);
	unsigned long max_util = 0, sum_util = 0;
	int cpu;
	unsigned long cpu_util;
	bool prev_dst_same_cluster = false;

	if (same_cluster(task_cpu(p), dst_cpu))
		prev_dst_same_cluster = true;

	/*
	 * The capacity state of CPUs of the current rd can be driven by CPUs
	 * of another rd if they belong to the same pd. So, account for the
	 * utilization of these CPUs too by masking pd with cpu_online_mask
	 * instead of the rd span.
	 *
	 * If an entire pd is outside of the current rd, it will not appear in
	 * its pd list and will not be accounted by compute_energy().
	 */
	for_each_cpu_and(cpu, pd_mask, cpu_online_mask) {
		sum_util += cpu_util_next_walt(cpu, p, dst_cpu);
		cpu_util = cpu_util_next_walt_prs(cpu, p, dst_cpu, prev_dst_same_cluster, prs);
		max_util = max(max_util, cpu_util);
	}

	max_util = scale_time_to_util(max_util);

	if (output)
		output->cluster_first_cpu[x] = cpumask_first(pd_mask);

	return walt_em_cpu_energy(pd->em_pd, max_util, sum_util, output, x);
}

static inline long
walt_compute_energy(struct task_struct *p, int dst_cpu, struct perf_domain *pd,
			cpumask_t *candidates, u64 *prs, struct compute_energy_output *output)
{
	long energy = 0;
	unsigned int x = 0;

	for (; pd; pd = pd->next) {
		struct cpumask *pd_mask = perf_domain_span(pd);

		if (cpumask_intersects(candidates, pd_mask)
				|| cpumask_test_cpu(task_cpu(p), pd_mask)) {
			energy += walt_pd_compute_energy(p, dst_cpu, pd, prs, output, x);
			x++;
		}
	}

	return energy;
}

static inline int wake_to_idle(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;
	struct walt_task_struct *cur_wts =
		(struct walt_task_struct *) current->android_vendor_data1;

	return (cur_wts->wake_up_idle || wts->wake_up_idle);
}

/* return true if cpu should be chosen over best_energy_cpu */
static inline bool select_cpu_same_energy(int cpu, int best_cpu, int prev_cpu)
{
	bool new_cpu_is_idle = available_idle_cpu(cpu);
	bool best_cpu_is_idle = available_idle_cpu(best_cpu);

	if (check_for_higher_capacity(cpu, best_cpu))
		return false;
	if (check_for_higher_capacity(best_cpu, cpu))
		return true;

	if (best_cpu_is_idle && walt_get_idle_exit_latency(cpu_rq(best_cpu)) <= 1)
		return false;
	if (new_cpu_is_idle && walt_get_idle_exit_latency(cpu_rq(cpu)) <= 1)
		return true;

	if (best_cpu_is_idle && !new_cpu_is_idle)
		return false;
	if (new_cpu_is_idle && !best_cpu_is_idle)
		return true;

	if (best_cpu == prev_cpu)
		return false;
	if (cpu == prev_cpu)
		return true;

	if (best_cpu_is_idle && new_cpu_is_idle)
		return false;

	if (cpu_util(best_cpu) <= cpu_util(cpu))
		return false;

	return true;
}

static inline unsigned int capacity_spare_of(int cpu)
{
	return capacity_orig_of(cpu) - cpu_util(cpu);
}

static DEFINE_PER_CPU(cpumask_t, energy_cpus);
int walt_find_energy_efficient_cpu(struct task_struct *p, int prev_cpu,
				     int sync, int sibling_count_hint)
{
	unsigned long prev_energy = ULONG_MAX, best_energy = ULONG_MAX;
	struct root_domain *rd = cpu_rq(cpumask_first(cpu_active_mask))->rd;
	int weight, cpu = smp_processor_id(), best_energy_cpu = prev_cpu;
	struct perf_domain *pd;
	unsigned long cur_energy;
	cpumask_t *candidates;
	bool is_rtg, curr_is_rtg;
	struct find_best_target_env fbt_env;
	bool need_idle = wake_to_idle(p);
	u64 start_t = 0;
	int delta = 0;
	int task_boost = per_task_boost(p);
	bool uclamp_boost = walt_uclamp_boosted(p);
	int start_cpu = 0, order_index, end_index;
	int first_cpu;
	bool energy_eval_needed = true;
	struct compute_energy_output output;
	struct walt_task_struct *wts;
	int pipeline_cpu;
	bool force_energy_eval = false;

	if (walt_is_many_wakeup(sibling_count_hint) && prev_cpu != cpu &&
			cpumask_test_cpu(prev_cpu, p->cpus_ptr))
		return prev_cpu;

	if (unlikely(!cpu_array))
		return prev_cpu;

	/* Pre-select a set of candidate CPUs. */
	candidates = this_cpu_ptr(&energy_cpus);
	cpumask_clear(candidates);

	wts = (struct walt_task_struct *) p->android_vendor_data1;
	pipeline_cpu = wts->pipeline_cpu;
	if ((wts->low_latency & WALT_LOW_LATENCY_MASK) &&
			(pipeline_cpu != -1) &&
			walt_task_skip_min_cpu(p) &&
			cpumask_test_cpu(pipeline_cpu, p->cpus_ptr) &&
			cpu_active(pipeline_cpu) &&
			!cpu_halted(pipeline_cpu)) {
		if ((p == cpu_rq(pipeline_cpu)->curr) ||
			!walt_pipeline_low_latency_task(cpu_rq(pipeline_cpu)->curr)) {
			best_energy_cpu = pipeline_cpu;
			fbt_env.fastpath = PIPELINE_FASTPATH;
			goto out;
		}
	}

	walt_get_indicies(p, &order_index, &end_index, task_boost, uclamp_boost,
								&energy_eval_needed);
	start_cpu = cpumask_first(&cpu_array[order_index][0]);

	is_rtg = task_in_related_thread_group(p);
	curr_is_rtg = task_in_related_thread_group(cpu_rq(cpu)->curr);

	if (trace_sched_task_util_enabled())
		start_t = sched_clock();


	rcu_read_lock();

	fbt_env.fastpath = 0;
	fbt_env.need_idle = need_idle;

	if (sync && (need_idle || (is_rtg && curr_is_rtg)))
		sync = 0;

	if (sysctl_sched_sync_hint_enable && sync
	    && bias_to_this_cpu(p, cpu, start_cpu) && !cpu_halted(cpu)) {
		if (cpu_cluster(cpu)->sibling_cluster == -1) {
			best_energy_cpu = cpu;
			fbt_env.fastpath = SYNC_WAKEUP;
			goto unlock;
		}
	}

	/* if symmetrical system, default to upstream behavior */
	pd = rcu_dereference(rd->pd);
	if (!pd)
		goto fail;

	fbt_env.is_rtg = is_rtg;
	fbt_env.start_cpu = start_cpu;
	fbt_env.order_index = order_index;
	fbt_env.end_index = end_index;
	fbt_env.strict_max = is_rtg &&
		(task_boost == TASK_BOOST_STRICT_MAX);
	fbt_env.skip_cpu = walt_is_many_wakeup(sibling_count_hint) ?
			   cpu : -1;

	walt_find_best_target(NULL, candidates, p, &fbt_env, &force_energy_eval);


	/* Bail out if no candidate was found. */
	weight = cpumask_weight(candidates);
	if (!weight)
		goto unlock;

	first_cpu = cpumask_first(candidates);

	if ((fbt_env.fastpath == CLUSTER_PACKING_FASTPATH) && !force_energy_eval) {
		best_energy_cpu = first_cpu;
		goto unlock;
	}

	if (weight == 1) {
		if (available_idle_cpu(first_cpu) || first_cpu == prev_cpu) {
			best_energy_cpu = first_cpu;
			goto unlock;
		}
	}

	if (need_idle && available_idle_cpu(first_cpu)) {
		best_energy_cpu = first_cpu;
		goto unlock;
	}

	if (!energy_eval_needed && !force_energy_eval) {
		int max_spare_cpu = first_cpu;

		for_each_cpu(cpu, candidates) {
			if (capacity_spare_of(max_spare_cpu) < capacity_spare_of(cpu))
				max_spare_cpu = cpu;
		}
		best_energy_cpu = max_spare_cpu;
		goto unlock;
	}

	if (READ_ONCE(p->__state) == TASK_WAKING)
		delta = task_util(p);

	if (cpumask_test_cpu(prev_cpu, p->cpus_ptr) && !__cpu_overutilized(prev_cpu, delta)) {
		if (trace_sched_compute_energy_enabled()) {
			memset(&output, 0, sizeof(output));
			prev_energy = walt_compute_energy(p, prev_cpu, pd, candidates, fbt_env.prs,
					&output);
		} else {
			prev_energy = walt_compute_energy(p, prev_cpu, pd, candidates, fbt_env.prs,
					NULL);
		}

		best_energy = prev_energy;
		trace_sched_compute_energy(p, prev_cpu, prev_energy, 0, 0, 0, &output);
	} else {
		prev_energy = best_energy = ULONG_MAX;
		if (weight == 1) {
			best_energy_cpu = first_cpu;
			goto unlock;
		}
	}

	/* Select the best candidate energy-wise. */
	for_each_cpu(cpu, candidates) {
		if (cpu == prev_cpu)
			continue;

		if (trace_sched_compute_energy_enabled()) {
			memset(&output, 0, sizeof(output));
			cur_energy = walt_compute_energy(p, cpu, pd, candidates, fbt_env.prs,
					&output);
		} else {
			cur_energy = walt_compute_energy(p, cpu, pd, candidates, fbt_env.prs,
					NULL);
		}

		if (cur_energy < best_energy) {
			best_energy = cur_energy;
			best_energy_cpu = cpu;
		} else if (cur_energy == best_energy) {
			if (select_cpu_same_energy(cpu, best_energy_cpu,
							prev_cpu)) {
				best_energy = cur_energy;
				best_energy_cpu = cpu;
			}
		}

		trace_sched_compute_energy(p, cpu, cur_energy,
			prev_energy, best_energy, best_energy_cpu, &output);
	}

	/*
	 * Pick the prev CPU, if best energy CPU can't saves at least 6% of
	 * the energy used by prev_cpu.
	 */
	if (!(available_idle_cpu(best_energy_cpu) &&
	    walt_get_idle_exit_latency(cpu_rq(best_energy_cpu)) <= 1) &&
	    (prev_energy != ULONG_MAX) && (best_energy_cpu != prev_cpu) &&
	    ((prev_energy - best_energy) <= prev_energy >> 5) &&
	    !check_for_higher_capacity(prev_cpu, start_cpu))
		best_energy_cpu = prev_cpu;

unlock:
	rcu_read_unlock();
out:
	if (best_energy_cpu < 0 || best_energy_cpu >= WALT_NR_CPUS)
		best_energy_cpu = prev_cpu;

	trace_sched_task_util(p, cpumask_bits(candidates)[0], best_energy_cpu,
			sync, fbt_env.need_idle, fbt_env.fastpath,
			start_t, uclamp_boost, start_cpu);

	return best_energy_cpu;

fail:
	rcu_read_unlock();
	return -1;
}

static void
walt_select_task_rq_fair(void *unused, struct task_struct *p, int prev_cpu,
				int sd_flag, int wake_flags, int *target_cpu)
{
	int sync;
	int sibling_count_hint;

	if (unlikely(walt_disabled))
		return;

	sync = (wake_flags & WF_SYNC) && !(current->flags & PF_EXITING);
	sibling_count_hint = p->wake_q_count;
	p->wake_q_count = 0;

	*target_cpu = walt_find_energy_efficient_cpu(p, prev_cpu, sync, sibling_count_hint);
}

static void walt_binder_low_latency_set(void *unused, struct task_struct *task,
					bool sync, struct binder_proc *proc)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) task->android_vendor_data1;

	if (unlikely(walt_disabled))
		return;
	if (task && ((task_in_related_thread_group(current) &&
			task->group_leader->prio < MAX_RT_PRIO) ||
			(current->group_leader->prio < MAX_RT_PRIO &&
			task_in_related_thread_group(task))))
		wts->low_latency |= WALT_LOW_LATENCY_BINDER;
	else
		/*
		 * Clear low_latency flag if criterion above is not met, this
		 * will handle usecase where for a binder thread WALT_LOW_LATENCY_BINDER
		 * is set by one task and before WALT clears this flag after timer expiry
		 * some other task tries to use same binder thread.
		 *
		 * The only gets cleared when binder transaction is initiated
		 * and the above condition to set flasg is nto satisfied.
		 */
		wts->low_latency &= ~WALT_LOW_LATENCY_BINDER;

}

static void binder_set_priority_hook(void *data,
				struct binder_transaction *bndrtrans, struct task_struct *task)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) task->android_vendor_data1;
	struct walt_task_struct *current_wts =
			(struct walt_task_struct *) current->android_vendor_data1;

	if (unlikely(walt_disabled))
		return;

	if (bndrtrans && bndrtrans->need_reply && current_wts->boost == TASK_BOOST_STRICT_MAX) {
		bndrtrans->android_vendor_data1  = wts->boost;
		wts->boost = TASK_BOOST_STRICT_MAX;
	}
}

static void binder_restore_priority_hook(void *data,
				struct binder_transaction *bndrtrans, struct task_struct *task)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) task->android_vendor_data1;

	if (unlikely(walt_disabled))
		return;

	if (bndrtrans && wts->boost == TASK_BOOST_STRICT_MAX)
		wts->boost = bndrtrans->android_vendor_data1;
}

/*
 * Higher prio mvp can preempt lower prio mvp.
 *
 * However, the lower prio MVP slice will be more since we expect them to
 * be the work horses. For example, binders will have higher prio MVP and
 * they can preempt long running rtg prio tasks but binders loose their
 * powers with in 3 msec where as rtg prio tasks can run more than that.
 */
int walt_get_mvp_task_prio(struct task_struct *p)
{
	if (walt_procfs_low_latency_task(p) ||
			walt_pipeline_low_latency_task(p))
		return WALT_LL_PIPE_MVP;

	if (per_task_boost(p) == TASK_BOOST_STRICT_MAX)
		return WALT_TASK_BOOST_MVP;

	if (walt_binder_low_latency_task(p))
		return WALT_BINDER_MVP;

	if (task_rtg_high_prio(p))
		return WALT_RTG_MVP;

	return WALT_NOT_MVP;
}

static inline unsigned int walt_cfs_mvp_task_limit(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	/* Binder MVP tasks are high prio but have only single slice */
	if (wts->mvp_prio == WALT_BINDER_MVP)
		return WALT_MVP_SLICE;

	return WALT_MVP_LIMIT;
}

static void walt_cfs_insert_mvp_task(struct walt_rq *wrq, struct walt_task_struct *wts,
				     bool at_front)
{
	struct list_head *pos;

	list_for_each(pos, &wrq->mvp_tasks) {
		struct walt_task_struct *tmp_wts = container_of(pos, struct walt_task_struct,
								mvp_list);

		if (at_front) {
			if (wts->mvp_prio >= tmp_wts->mvp_prio)
				break;
		} else {
			if (wts->mvp_prio > tmp_wts->mvp_prio)
				break;
		}
	}

	list_add(&wts->mvp_list, pos->prev);
	wrq->num_mvp_tasks++;
}

void walt_cfs_deactivate_mvp_task(struct rq *rq, struct task_struct *p)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu_of(rq));
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	list_del_init(&wts->mvp_list);
	wts->mvp_prio = WALT_NOT_MVP;
	wrq->num_mvp_tasks--;
}

/*
 * MVP task runtime update happens here. Three possibilities:
 *
 * de-activated: The MVP consumed its runtime. Non MVP can preempt.
 * slice expired: MVP slice is expired and other MVP can preempt.
 * slice not expired: This MVP task can continue to run.
 */
#define MAX_MVP_TIME_NS			500000000ULL
#define MVP_THROTTLE_TIME_NS		100000000ULL
static void walt_cfs_account_mvp_runtime(struct rq *rq, struct task_struct *curr)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu_of(rq));
	struct walt_task_struct *wts = (struct walt_task_struct *) curr->android_vendor_data1;
	u64 slice;
	unsigned int limit;

	walt_lockdep_assert_rq(rq, NULL);

	/*
	 * RQ clock update happens in tick path in the scheduler.
	 * Since we drop the lock in the scheduler before calling
	 * into vendor hook, it is possible that update flags are
	 * reset by another rq lock and unlock. Do the update here
	 * if required.
	 */
	if (!(rq->clock_update_flags & RQCF_UPDATED))
		update_rq_clock(rq);

	if (wrq->mvp_throttle_time) {
		if ((rq->clock - wrq->mvp_throttle_time) > MVP_THROTTLE_TIME_NS) {
			wrq->skip_mvp = false;
			wrq->mvp_throttle_time = 0;
		}
	} else if (wrq->mvp_arrival_time) {
		if ((rq->clock - wrq->mvp_arrival_time) > MAX_MVP_TIME_NS) {
			wrq->skip_mvp = true;
			wrq->mvp_arrival_time = 0;
			wrq->mvp_throttle_time = rq->clock;
		}
	}

	/*
	 * continue accounting even in skip_mvp state if a MVP task is selected
	 * by scheduler core to run on CPU.
	 */
	if (curr->se.sum_exec_runtime > wts->sum_exec_snapshot_for_total)
		wts->total_exec = curr->se.sum_exec_runtime - wts->sum_exec_snapshot_for_total;
	else
		wts->total_exec = 0;

	if (curr->se.sum_exec_runtime > wts->sum_exec_snapshot_for_slice)
		slice = curr->se.sum_exec_runtime - wts->sum_exec_snapshot_for_slice;
	else
		slice = 0;

	/* slice is not expired */
	if (slice < WALT_MVP_SLICE)
		return;

	wts->sum_exec_snapshot_for_slice = curr->se.sum_exec_runtime;
	/*
	 * slice is expired, check if we have to deactivate the
	 * MVP task, otherwise requeue the task in the list so
	 * that other MVP tasks gets a chance.
	 */

	limit = walt_cfs_mvp_task_limit(curr);
	if (wts->total_exec > limit) {
		walt_cfs_deactivate_mvp_task(rq, curr);
		trace_walt_cfs_deactivate_mvp_task(curr, wts, limit);
		return;
	}

	if (wrq->num_mvp_tasks == 1)
		return;

	/* slice expired. re-queue the task */
	list_del(&wts->mvp_list);
	wrq->num_mvp_tasks--;
	walt_cfs_insert_mvp_task(wrq, wts, false);
}

void walt_cfs_enqueue_task(struct rq *rq, struct task_struct *p)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu_of(rq));
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;
	int mvp_prio = walt_get_mvp_task_prio(p);

	if (mvp_prio == WALT_NOT_MVP)
		return;

	/*
	 * This can happen during migration or enq/deq for prio/class change.
	 * it was once MVP but got demoted, it will not be MVP until
	 * it goes to sleep again.
	 */
	if (wts->total_exec > walt_cfs_mvp_task_limit(p))
		return;

	wts->mvp_prio = mvp_prio;
	walt_cfs_insert_mvp_task(wrq, wts, task_on_cpu(rq, p));

	/*
	 * We inserted the task at the appropriate position. Take the
	 * task runtime snapshot. From now onwards we use this point as a
	 * baseline to enforce the slice and demotion.
	 */
	if (!wts->total_exec) /* queue after sleep */ {
		wts->sum_exec_snapshot_for_total = p->se.sum_exec_runtime;
		wts->sum_exec_snapshot_for_slice = p->se.sum_exec_runtime;
	}
}

void walt_cfs_dequeue_task(struct rq *rq, struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	if (!list_empty(&wts->mvp_list) && wts->mvp_list.next)
		walt_cfs_deactivate_mvp_task(rq, p);

	/*
	 * Reset the exec time during sleep so that it starts
	 * from scratch upon next wakeup. total_exec should
	 * be preserved when task is enq/deq while it is on
	 * runqueue.
	 */
	if (READ_ONCE(p->__state) != TASK_RUNNING)
		wts->total_exec = 0;
}

void walt_cfs_tick(struct rq *rq)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu_of(rq));
	struct walt_task_struct *wts = (struct walt_task_struct *) rq->curr->android_vendor_data1;
	bool skip_mvp;

	if (unlikely(walt_disabled))
		return;

	raw_spin_lock(&rq->__lock);

	if (list_empty(&wts->mvp_list) || (wts->mvp_list.next == NULL))
		goto out;

	/* Reschedule if RQ's skip_mvp state changes */
	skip_mvp = wrq->skip_mvp;
	walt_cfs_account_mvp_runtime(rq, rq->curr);
	/*
	 * If the current is not MVP means, we have to re-schedule to
	 * see if we can run any other task including MVP tasks.
	 */
	if (((skip_mvp != wrq->skip_mvp) ||
		(wrq->mvp_tasks.next != &wts->mvp_list)) && rq->cfs.h_nr_running > 1)
		resched_curr(rq);

out:
	raw_spin_unlock(&rq->__lock);
}

/*
 * When preempt = false and nopreempt = false, we leave the preemption
 * decision to CFS.
 */
static void walt_cfs_check_preempt_wakeup(void *unused, struct rq *rq, struct task_struct *p,
					  bool *preempt, bool *nopreempt, int wake_flags,
					  struct sched_entity *se, struct sched_entity *pse,
					  int next_buddy_marked, unsigned int granularity)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu_of(rq));
	struct walt_task_struct *wts_p = (struct walt_task_struct *) p->android_vendor_data1;
	struct task_struct *c = rq->curr;
	struct walt_task_struct *wts_c = (struct walt_task_struct *) rq->curr->android_vendor_data1;
	bool resched = false, skip_mvp;
	bool p_is_mvp, curr_is_mvp;

	if (unlikely(walt_disabled))
		return;

	p_is_mvp = !list_empty(&wts_p->mvp_list) && wts_p->mvp_list.next;
	curr_is_mvp = !list_empty(&wts_c->mvp_list) && wts_c->mvp_list.next;

	/*
	 * current is not MVP, so preemption decision
	 * is simple.
	 */
	if (!curr_is_mvp) {
		if (p_is_mvp && !wrq->skip_mvp)
			goto preempt;
		return; /* CFS decides preemption */
	}

	/*
	 * current is MVP. update its runtime before deciding the
	 * preemption.
	 */
	skip_mvp = wrq->skip_mvp;
	walt_cfs_account_mvp_runtime(rq, c);
	resched = (skip_mvp != wrq->skip_mvp) || (wrq->mvp_tasks.next != &wts_c->mvp_list);

	/*
	 * current is no longer eligible to run. It must have been
	 * picked (because of MVP) ahead of other tasks in the CFS
	 * tree, so drive preemption to pick up the next task from
	 * the tree, which also includes picking up the first in
	 * the MVP queue.
	 */
	if (resched)
		goto preempt;

	/* current is the first in the queue, so no preemption */
	*nopreempt = true;
	trace_walt_cfs_mvp_wakeup_nopreempt(c, wts_c, walt_cfs_mvp_task_limit(c));
	return;
preempt:
	*preempt = true;
	trace_walt_cfs_mvp_wakeup_preempt(p, wts_p, walt_cfs_mvp_task_limit(p));
}

#ifdef CONFIG_FAIR_GROUP_SCHED
/* Walk up scheduling entities hierarchy */
#define for_each_sched_entity(se) \
		for (; se; se = se->parent)
#else	/* !CONFIG_FAIR_GROUP_SCHED */
#define for_each_sched_entity(se) \
		for (; se; se = NULL)
#endif

extern void set_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *se);
static void walt_cfs_replace_next_task_fair(void *unused, struct rq *rq, struct task_struct **p,
					    struct sched_entity **se, bool *repick, bool simple,
					    struct task_struct *prev)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu_of(rq));
	struct walt_task_struct *wts;
	struct task_struct *mvp;
	struct cfs_rq *cfs_rq;

	if (unlikely(walt_disabled))
		return;

	if ((*p) && (*p) != prev && ((*p)->on_cpu == 1 || (*p)->on_rq == 0 ||
				     (*p)->on_rq == TASK_ON_RQ_MIGRATING ||
				     task_thread_info(*p)->cpu != cpu_of(rq)))
		WALT_BUG(WALT_BUG_UPSTREAM, *p,
			 "picked %s(%d) on_cpu=%d on_rq=%d p->cpu=%d cpu_of(rq)=%d kthread=%d\n",
			 (*p)->comm, (*p)->pid, (*p)->on_cpu,
			 (*p)->on_rq, task_thread_info(*p)->cpu,
			 cpu_of(rq), ((*p)->flags & PF_KTHREAD));

	/* RQ is in MVP throttled state*/
	if (wrq->skip_mvp)
		return;

	if (list_empty(&wrq->mvp_tasks)) {
		wrq->mvp_arrival_time = 0;
		return;
	}

	/* Return the first task from MVP queue */
	wts = list_first_entry(&wrq->mvp_tasks, struct walt_task_struct, mvp_list);
	mvp = wts_to_ts(wts);

	*p = mvp;
	*se = &mvp->se;
	*repick = true;

	/* TODO: check with team if it is fine in case clock is not updated */
	/* Mark arrival of MVP task */
	if (!wrq->mvp_arrival_time)
		wrq->mvp_arrival_time = rq->clock;

	if (simple) {
		for_each_sched_entity((*se)) {
			/*
			 * TODO If CFS_BANDWIDTH is enabled, we might pick
			 * from a throttled cfs_rq
			 */
			cfs_rq = cfs_rq_of(*se);
			set_next_entity(cfs_rq, *se);
		}
	}

	if ((*p) && (*p) != prev && ((*p)->on_cpu == 1 || (*p)->on_rq == 0 ||
				     (*p)->on_rq == TASK_ON_RQ_MIGRATING ||
				     task_thread_info(*p)->cpu != cpu_of(rq)))
		WALT_BUG(WALT_BUG_UPSTREAM, *p,
			 "picked %s(%d) on_cpu=%d on_rq=%d p->cpu=%d cpu_of(rq)=%d kthread=%d\n",
			 (*p)->comm, (*p)->pid, (*p)->on_cpu,
			 (*p)->on_rq, task_thread_info(*p)->cpu,
			 cpu_of(rq), ((*p)->flags & PF_KTHREAD));

	trace_walt_cfs_mvp_pick_next(mvp, wts, walt_cfs_mvp_task_limit(mvp));
}

void walt_cfs_init(void)
{
	register_trace_android_rvh_select_task_rq_fair(walt_select_task_rq_fair, NULL);

	register_trace_android_vh_binder_wakeup_ilocked(walt_binder_low_latency_set, NULL);

	register_trace_android_vh_binder_set_priority(binder_set_priority_hook, NULL);
	register_trace_android_vh_binder_restore_priority(binder_restore_priority_hook, NULL);

	register_trace_android_rvh_check_preempt_wakeup(walt_cfs_check_preempt_wakeup, NULL);
	register_trace_android_rvh_replace_next_task_fair(walt_cfs_replace_next_task_fair, NULL);
}
