// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#include <trace/hooks/sched.h>
#include <trace/hooks/binder.h>

#include "walt.h"
#include "trace.h"
#include "../../../drivers/android/binder_trace.h"

/* Migration margins */
unsigned int sched_capacity_margin_up[WALT_NR_CPUS] = {
			[0 ... WALT_NR_CPUS-1] = 1078 /* ~5% margin */
};
unsigned int sched_capacity_margin_down[WALT_NR_CPUS] = {
			[0 ... WALT_NR_CPUS-1] = 1205 /* ~15% margin */
};

__read_mostly unsigned int sysctl_sched_prefer_spread;
unsigned int sysctl_walt_rtg_cfs_boost_prio = 99; /* disabled by default */
unsigned int sched_small_task_threshold = 102;
__read_mostly unsigned int sysctl_sched_force_lb_enable = 1;
unsigned int capacity_margin_freq = 1280; /* ~20% margin */

static inline bool prefer_spread_on_idle(int cpu, bool new_ilb)
{
	switch (sysctl_sched_prefer_spread) {
	case 1:
		return is_min_capacity_cpu(cpu);
	case 2:
		return true;
	case 3:
		return (new_ilb && is_min_capacity_cpu(cpu));
	case 4:
		return new_ilb;
	default:
		return false;
	}
}

static inline bool
bias_to_this_cpu(struct task_struct *p, int cpu, int start_cpu)
{
	bool base_test = cpumask_test_cpu(cpu, &p->cpus_mask) &&
						cpu_active(cpu);
	bool start_cap_test = (capacity_orig_of(cpu) >=
					capacity_orig_of(start_cpu));

	return base_test && start_cap_test;
}

static inline bool task_demand_fits(struct task_struct *p, int cpu)
{
	unsigned long capacity = capacity_orig_of(cpu);
	unsigned long max_capacity = max_possible_capacity;

	if (capacity == max_capacity)
		return true;

	return task_fits_capacity(p, capacity, cpu);
}

struct find_best_target_env {
	bool	is_rtg;
	int	need_idle;
	bool	boosted;
	int	fastpath;
	int	start_cpu;
	int	order_index;
	int	end_index;
	bool	strict_max;
	int	skip_cpu;
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
	if (likely(p->state == TASK_WAKING))
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

static inline bool walt_get_rtg_status(struct task_struct *p)
{
	struct walt_related_thread_group *grp;
	bool ret = false;

	rcu_read_lock();

	grp = task_related_thread_group(p);
	if (grp)
		ret = grp->skip_min;

	rcu_read_unlock();

	return ret;
}

static inline bool walt_task_skip_min_cpu(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	return sched_boost() != CONSERVATIVE_BOOST &&
		walt_get_rtg_status(p) && wts->unfilter;
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

static void walt_get_indicies(struct task_struct *p, int *order_index,
		int *end_index, int task_boost, bool boosted)
{
	int i = 0;

	*order_index = 0;
	*end_index = 0;

	if (num_sched_clusters <= 1)
		return;

	if (task_boost > TASK_BOOST_ON_MID) {
		*order_index = num_sched_clusters - 1;
		return;
	}

	if (is_full_throttle_boost()) {
		*order_index = num_sched_clusters - 1;
		if ((*order_index > 1) && task_demand_fits(p,
			cpumask_first(&cpu_array[*order_index][1])))
			*end_index = 1;
		return;
	}

	if (boosted || task_boost_policy(p) == SCHED_BOOST_ON_BIG ||
		walt_task_skip_min_cpu(p))
		*order_index = 1;

	for (i = *order_index ; i < num_sched_clusters - 1; i++) {
		if (task_demand_fits(p, cpumask_first(&cpu_array[i][0])))
			break;
	}

	*order_index = i;
}

enum fastpaths {
	NONE = 0,
	SYNC_WAKEUP,
	PREV_CPU_FASTPATH,
};

static void walt_find_best_target(struct sched_domain *sd,
					cpumask_t *candidates,
					struct task_struct *p,
					struct find_best_target_env *fbt_env)
{
	unsigned long min_util = uclamp_task_util(p);
	long target_max_spare_cap = 0;
	unsigned long best_idle_cuml_util = ULONG_MAX;
	unsigned int min_exit_latency = UINT_MAX;
	int best_idle_cpu = -1;
	int target_cpu = -1;
	int i, start_cpu;
	long spare_wake_cap, most_spare_wake_cap = 0;
	int most_spare_cap_cpu = -1;
	int prev_cpu = task_cpu(p);
	int active_candidate = -1;
	int order_index = fbt_env->order_index, end_index = fbt_env->end_index;
	int stop_index = INT_MAX;
	int cluster;
	unsigned int target_nr_rtg_high_prio = UINT_MAX;
	bool rtg_high_prio_task = task_rtg_high_prio(p);
	cpumask_t visit_cpus;
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;
	struct cfs_rq *cfs_rq;

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

	if (p->state == TASK_RUNNING)
		most_spare_wake_cap = ULONG_MAX;

	/* fast path for prev_cpu */
	if (((capacity_orig_of(prev_cpu) == capacity_orig_of(start_cpu)) ||
				asym_cap_siblings(prev_cpu, start_cpu)) &&
				cpu_active(prev_cpu) && cpu_online(prev_cpu) &&
				available_idle_cpu(prev_cpu)) {
		target_cpu = prev_cpu;
		fbt_env->fastpath = PREV_CPU_FASTPATH;
		cpumask_set_cpu(target_cpu, candidates);
		goto out;
	}

	for (cluster = 0; cluster < num_sched_clusters; cluster++) {
		cpumask_and(&visit_cpus, &p->cpus_mask,
				&cpu_array[order_index][cluster]);
		for_each_cpu(i, &visit_cpus) {
			unsigned long capacity_orig = capacity_orig_of(i);
			unsigned long wake_util, new_util, new_util_cuml;
			long spare_cap;
			unsigned int idle_exit_latency = UINT_MAX;

			trace_sched_cpu_util(i);

			if (!cpu_active(i))
				continue;

			if (active_candidate == -1)
				active_candidate = i;

			/*
			 * This CPU is the target of an active migration that's
			 * yet to complete. Avoid placing another task on it.
			 */
			if (is_reserved(i))
				continue;

			if (sched_cpu_high_irqload(i))
				continue;

			if (fbt_env->skip_cpu == i)
				continue;

			/*
			 * p's blocked utilization is still accounted for on prev_cpu
			 * so prev_cpu will receive a negative bias due to the double
			 * accounting. However, the blocked utilization may be zero.
			 */
			wake_util = cpu_util_without(i, p);
			new_util = wake_util + uclamp_task_util(p);
			spare_wake_cap = capacity_orig - wake_util;

			if (spare_wake_cap > most_spare_wake_cap) {
				most_spare_wake_cap = spare_wake_cap;
				most_spare_cap_cpu = i;
			}

			if (per_task_boost(cpu_rq(i)->curr) ==
					TASK_BOOST_STRICT_MAX)
				continue;

			/* get rq's utilization with this task included */
			cfs_rq = &cpu_rq(i)->cfs;
			new_util_cuml = READ_ONCE(cfs_rq->avg.util_avg) + min_util;

			/*
			 * Ensure minimum capacity to grant the required boost.
			 * The target CPU can be already at a capacity level higher
			 * than the one required to boost the task.
			 */
			new_util = max(min_util, new_util);
			if (new_util > capacity_orig)
				continue;

			/*
			 * Pre-compute the maximum possible capacity we expect
			 * to have available on this CPU once the task is
			 * enqueued here.
			 */
			spare_cap = capacity_orig - new_util;

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

				/*
				 * Prefer shallowest over deeper idle state cpu,
				 * of same capacity cpus.
				 */
				if (idle_exit_latency > min_exit_latency)
					continue;
				if (min_exit_latency == idle_exit_latency &&
					(best_idle_cpu == prev_cpu ||
					(i != prev_cpu &&
					new_util_cuml > best_idle_cuml_util)))
					continue;

				min_exit_latency = idle_exit_latency;
				best_idle_cuml_util = new_util_cuml;
				best_idle_cpu = i;
				continue;
			}

			/*
			 * Consider only idle CPUs for active migration.
			 */
			if (p->state == TASK_RUNNING)
				continue;

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
			target_cpu = i;
		}

		if (best_idle_cpu != -1)
			break;

		if ((cluster >= end_index) && (target_cpu != -1) &&
			walt_target_ok(target_cpu, order_index))
			break;

		if (most_spare_cap_cpu != -1 && cluster >= stop_index)
			break;
	}

	if (best_idle_cpu != -1)
		target_cpu = -1;
	/*
	 * We set both idle and target as long as they are valid CPUs.
	 * If we don't find either, then we fallback to most_spare_cap,
	 * If we don't find most spare cap, we fallback to prev_cpu,
	 * provided that the prev_cpu is active.
	 * If the prev_cpu is not active, we fallback to active_candidate.
	 */

	if (unlikely(target_cpu == -1)) {
		if (best_idle_cpu != -1)
			target_cpu = best_idle_cpu;
		else if (most_spare_cap_cpu != -1)
			target_cpu = most_spare_cap_cpu;
		else if (!cpu_active(prev_cpu))
			target_cpu = active_candidate;
	}

	if (target_cpu != -1)
		cpumask_set_cpu(target_cpu, candidates);
	if (best_idle_cpu != -1 && target_cpu != best_idle_cpu)
		cpumask_set_cpu(best_idle_cpu, candidates);
out:
	trace_sched_find_best_target(p, min_util, start_cpu,
			     best_idle_cpu, most_spare_cap_cpu,
			     target_cpu, order_index, end_index,
			     fbt_env->skip_cpu, p->state == TASK_RUNNING);
}

static inline unsigned long
cpu_util_next_walt(int cpu, struct task_struct *p, int dst_cpu)
{
	struct walt_rq *wrq = (struct walt_rq *) cpu_rq(cpu)->android_vendor_data1;
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

/*
 * compute_energy(): Estimates the energy that @pd would consume if @p was
 * migrated to @dst_cpu. compute_energy() predicts what will be the utilization
 * landscape of @pd's CPUs after the task migration, and uses the Energy Model
 * to compute what would be the energy if we decided to actually migrate that
 * task.
 */
static long
compute_energy(struct task_struct *p, int dst_cpu, struct perf_domain *pd)
{
	struct cpumask *pd_mask = perf_domain_span(pd);
	unsigned long max_util = 0, sum_util = 0;
	int cpu;
	unsigned long cpu_util;

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
		cpu_util = cpu_util_next_walt(cpu, p, dst_cpu);
		sum_util += cpu_util;
		max_util = max(max_util, cpu_util);
	}

	return em_cpu_energy(pd->em_pd, max_util, sum_util);
}

static inline long
walt_compute_energy(struct task_struct *p, int dst_cpu, struct perf_domain *pd)
{
	long energy = 0;

	for (; pd; pd = pd->next)
		energy += compute_energy(p, dst_cpu, pd);

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
	if (capacity_orig_of(cpu) < capacity_orig_of(best_cpu))
		return true;

	if (best_cpu == prev_cpu)
		return false;

	if (available_idle_cpu(best_cpu) && walt_get_idle_exit_latency(cpu_rq(best_cpu)) <= 1)
		return false; /* best_cpu is idle wfi or shallower */

	if (available_idle_cpu(cpu) && walt_get_idle_exit_latency(cpu_rq(cpu)) <= 1)
		return true; /* new cpu is idle wfi or shallower */

	/*
	 * If we are this far this must be a tie between a busy and deep idle,
	 * pick the busy.
	 */
	return available_idle_cpu(best_cpu);
}

static DEFINE_PER_CPU(cpumask_t, energy_cpus);
int walt_find_energy_efficient_cpu(struct task_struct *p, int prev_cpu,
				     int sync, int sibling_count_hint)
{
	unsigned long prev_delta = ULONG_MAX, best_delta = ULONG_MAX;
	struct root_domain *rd = cpu_rq(cpumask_first(cpu_active_mask))->rd;
	int weight, cpu = smp_processor_id(), best_energy_cpu = prev_cpu;
	struct perf_domain *pd;
	unsigned long cur_energy;
	cpumask_t *candidates;
	bool is_rtg, curr_is_rtg;
	struct find_best_target_env fbt_env;
	bool need_idle = wake_to_idle(p) || uclamp_latency_sensitive(p);
	u64 start_t = 0;
	int delta = 0;
	int task_boost = per_task_boost(p);
	bool is_uclamp_boosted = uclamp_boosted(p);
	bool boosted = is_uclamp_boosted || (task_boost > 0);
	int start_cpu, order_index, end_index;

	if (walt_is_many_wakeup(sibling_count_hint) && prev_cpu != cpu &&
			cpumask_test_cpu(prev_cpu, &p->cpus_mask))
		return prev_cpu;

	if (unlikely(!cpu_array))
		return -EPERM;

	walt_get_indicies(p, &order_index, &end_index, task_boost, boosted);
	start_cpu = cpumask_first(&cpu_array[order_index][0]);

	is_rtg = task_in_related_thread_group(p);
	curr_is_rtg = task_in_related_thread_group(cpu_rq(cpu)->curr);

	fbt_env.fastpath = 0;
	fbt_env.need_idle = need_idle;

	if (trace_sched_task_util_enabled())
		start_t = sched_clock();

	/* Pre-select a set of candidate CPUs. */
	candidates = this_cpu_ptr(&energy_cpus);
	cpumask_clear(candidates);

	if (sync && (need_idle || (is_rtg && curr_is_rtg)))
		sync = 0;

	if (sync && bias_to_this_cpu(p, cpu, start_cpu)) {
		best_energy_cpu = cpu;
		fbt_env.fastpath = SYNC_WAKEUP;
		goto done;
	}

	rcu_read_lock();
	pd = rcu_dereference(rd->pd);
	if (!pd)
		goto fail;

	fbt_env.is_rtg = is_rtg;
	fbt_env.start_cpu = start_cpu;
	fbt_env.order_index = order_index;
	fbt_env.end_index = end_index;
	fbt_env.boosted = boosted;
	fbt_env.strict_max = is_rtg &&
		(task_boost == TASK_BOOST_STRICT_MAX);
	fbt_env.skip_cpu = walt_is_many_wakeup(sibling_count_hint) ?
			   cpu : -1;

	walt_find_best_target(NULL, candidates, p, &fbt_env);

	/* Bail out if no candidate was found. */
	weight = cpumask_weight(candidates);
	if (!weight)
		goto unlock;

	/* If there is only one sensible candidate, select it now. */
	cpu = cpumask_first(candidates);
	if (weight == 1 && (available_idle_cpu(cpu) || cpu == prev_cpu)) {
		best_energy_cpu = cpu;
		goto unlock;
	}

	if (p->state == TASK_WAKING)
		delta = task_util(p);

	if (task_placement_boost_enabled(p) || fbt_env.need_idle ||
	    boosted || is_rtg || __cpu_overutilized(prev_cpu, delta) ||
	    !task_fits_max(p, prev_cpu) || !cpu_active(prev_cpu)) {
		best_energy_cpu = cpu;
		goto unlock;
	}

	if (cpumask_test_cpu(prev_cpu, &p->cpus_mask))
		prev_delta = best_delta =
			walt_compute_energy(p, prev_cpu, pd);
	else
		prev_delta = best_delta = ULONG_MAX;

	/* Select the best candidate energy-wise. */
	for_each_cpu(cpu, candidates) {
		if (cpu == prev_cpu)
			continue;

		cur_energy = walt_compute_energy(p, cpu, pd);
		trace_sched_compute_energy(p, cpu, cur_energy,
			prev_delta, best_delta, best_energy_cpu);

		if (cur_energy < best_delta) {
			best_delta = cur_energy;
			best_energy_cpu = cpu;
		} else if (cur_energy == best_delta) {
			if (select_cpu_same_energy(cpu, best_energy_cpu,
							prev_cpu)) {
				best_delta = cur_energy;
				best_energy_cpu = cpu;
			}
		}
	}

unlock:
	rcu_read_unlock();

	/*
	 * Pick the prev CPU, if best energy CPU can't saves at least 6% of
	 * the energy used by prev_cpu.
	 */
	if (!(available_idle_cpu(best_energy_cpu) &&
	    walt_get_idle_exit_latency(cpu_rq(best_energy_cpu)) <= 1) &&
	    (prev_delta != ULONG_MAX) && (best_energy_cpu != prev_cpu) &&
	    ((prev_delta - best_delta) <= prev_delta >> 4) &&
	    (capacity_orig_of(prev_cpu) <= capacity_orig_of(start_cpu)))
		best_energy_cpu = prev_cpu;

done:
	trace_sched_task_util(p, cpumask_bits(candidates)[0], best_energy_cpu,
			sync, fbt_env.need_idle, fbt_env.fastpath,
			task_boost_policy(p), start_t, boosted, is_rtg,
			walt_get_rtg_status(p), start_cpu);

	return best_energy_cpu;

fail:
	rcu_read_unlock();
	return -EPERM;
}

static void
walt_select_task_rq_fair(void *unused, struct task_struct *p, int prev_cpu,
				int sd_flag, int wake_flags, int *target_cpu)
{
	int sync = (wake_flags & WF_SYNC) && !(current->flags & PF_EXITING);
	int sibling_count_hint = p->wake_q_head ? p->wake_q_head->count : 1;

	if (static_branch_unlikely(&walt_disabled))
		return;
	*target_cpu = walt_find_energy_efficient_cpu(p, prev_cpu, sync, sibling_count_hint);
	if (unlikely(*target_cpu < 0))
		*target_cpu = prev_cpu;
}

#ifdef CONFIG_FAIR_GROUP_SCHED
static unsigned long task_h_load(struct task_struct *p)
{
	struct cfs_rq *cfs_rq = task_cfs_rq(p);

	update_cfs_rq_h_load(cfs_rq);
	return div64_ul(p->se.avg.load_avg * cfs_rq->h_load,
			cfs_rq_load_avg(cfs_rq) + 1);
}
#else
static unsigned long task_h_load(struct task_struct *p)
{
	return p->se.avg.load_avg;
}
#endif

static void walt_update_misfit_status(void *unused, struct task_struct *p,
					struct rq *rq, bool *need_update)
{
	if (static_branch_unlikely(&walt_disabled))
		return;
	*need_update = false;

	if (!p) {
		rq->misfit_task_load = 0;
		return;
	}

	if (task_fits_max(p, cpu_of(rq))) {
		rq->misfit_task_load = 0;
		return;
	}

	/*
	 * Make sure that misfit_task_load will not be null even if
	 * task_h_load() returns 0.
	 */
	rq->misfit_task_load = max_t(unsigned long, task_h_load(p), 1);
}

static inline struct task_struct *task_of(struct sched_entity *se)
{
	return container_of(se, struct task_struct, se);
}

static void walt_place_entity(void *unused, struct sched_entity *se, u64 *vruntime)
{
	if (static_branch_unlikely(&walt_disabled))
		return;
	if (entity_is_task(se)) {
		unsigned long thresh = sysctl_sched_latency;

		/*
		 * Halve their sleep time's effect, to allow
		 * for a gentler effect of sleepers:
		 */
		if (sched_feat(GENTLE_FAIR_SLEEPERS))
			thresh >>= 1;

		if ((per_task_boost(task_of(se)) == TASK_BOOST_STRICT_MAX) ||
				walt_low_latency_task(task_of(se)) ||
				task_rtg_high_prio(task_of(se))) {
			*vruntime -= sysctl_sched_latency;
			*vruntime -= thresh;
			se->vruntime = *vruntime;
		}
	}
}

static void walt_binder_low_latency_set(void *unused, struct task_struct *task)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) task->android_vendor_data1;

	if (static_branch_unlikely(&walt_disabled))
		return;
	if (task && current->signal &&
			(current->signal->oom_score_adj == 0) &&
			(current->prio < DEFAULT_PRIO))
		wts->low_latency |= WALT_LOW_LATENCY_BINDER;
}

static void walt_binder_low_latency_clear(void *unused, struct binder_transaction *t)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) current->android_vendor_data1;

	if (static_branch_unlikely(&walt_disabled))
		return;
	if (wts->low_latency & WALT_LOW_LATENCY_BINDER)
		wts->low_latency &= ~WALT_LOW_LATENCY_BINDER;
}

void walt_cfs_init(void)
{
	register_trace_android_rvh_select_task_rq_fair(walt_select_task_rq_fair, NULL);
	register_trace_android_rvh_update_misfit_status(walt_update_misfit_status, NULL);
	register_trace_android_rvh_place_entity(walt_place_entity, NULL);

	register_trace_android_vh_binder_wakeup_ilocked(walt_binder_low_latency_set, NULL);
	register_trace_binder_transaction_received(walt_binder_low_latency_clear, NULL);
}
