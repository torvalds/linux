// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <trace/hooks/sched.h>

#include "walt.h"
#include "trace.h"

static inline unsigned long walt_lb_cpu_util(int cpu)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu);

	return wrq->walt_stats.cumulative_runnable_avg_scaled;
}

static void walt_detach_task(struct task_struct *p, struct rq *src_rq,
			     struct rq *dst_rq)
{
	//TODO can we just replace with detach_task in fair.c??
	deactivate_task(src_rq, p, 0);
	set_task_cpu(p, dst_rq->cpu);
}

static void walt_attach_task(struct task_struct *p, struct rq *rq)
{
	activate_task(rq, p, 0);
	check_preempt_curr(rq, p, 0);
}

static int stop_walt_lb_active_migration(void *data)
{
	struct rq *busiest_rq = data;
	int busiest_cpu = cpu_of(busiest_rq);
	int target_cpu = busiest_rq->push_cpu;
	struct rq *target_rq = cpu_rq(target_cpu);
	struct walt_rq *wrq = &per_cpu(walt_rq, busiest_cpu);
	struct task_struct *push_task;
	int push_task_detached = 0;

	raw_spin_lock_irq(&busiest_rq->__lock);
	push_task = wrq->push_task;

	/* sanity checks before initiating the pull */
	if (!cpu_active(busiest_cpu) || !cpu_active(target_cpu) || !push_task)
		goto out_unlock;

	if (unlikely(busiest_cpu != raw_smp_processor_id() ||
		     !busiest_rq->active_balance))
		goto out_unlock;

	if (busiest_rq->nr_running <= 1)
		goto out_unlock;

	BUG_ON(busiest_rq == target_rq);

	if (task_on_rq_queued(push_task) &&
			READ_ONCE(push_task->__state) == TASK_RUNNING &&
			task_cpu(push_task) == busiest_cpu &&
			cpu_active(target_cpu) &&
			cpumask_test_cpu(target_cpu, push_task->cpus_ptr)) {
		walt_detach_task(push_task, busiest_rq, target_rq);
		push_task_detached = 1;
	}

out_unlock: /* called with busiest_rq lock */
	busiest_rq->active_balance = 0;
	target_cpu = busiest_rq->push_cpu;
	clear_reserved(target_cpu);
	wrq->push_task = NULL;
	raw_spin_unlock(&busiest_rq->__lock);

	if (push_task_detached) {
		raw_spin_lock(&target_rq->__lock);
		walt_attach_task(push_task, target_rq);
		raw_spin_unlock(&target_rq->__lock);
	}

	if (push_task)
		put_task_struct(push_task);

	local_irq_enable();

	return 0;
}

struct walt_lb_rotate_work {
	struct work_struct	w;
	struct task_struct	*src_task;
	struct task_struct	*dst_task;
	int			src_cpu;
	int			dst_cpu;
};

DEFINE_PER_CPU(struct walt_lb_rotate_work, walt_lb_rotate_works);

static void walt_lb_rotate_work_func(struct work_struct *work)
{
	struct walt_lb_rotate_work *wr = container_of(work,
					struct walt_lb_rotate_work, w);
	struct rq *src_rq = cpu_rq(wr->src_cpu), *dst_rq = cpu_rq(wr->dst_cpu);
	unsigned long flags;

	migrate_swap(wr->src_task, wr->dst_task, wr->dst_cpu, wr->src_cpu);

	put_task_struct(wr->src_task);
	put_task_struct(wr->dst_task);

	local_irq_save(flags);
	double_rq_lock(src_rq, dst_rq);
	dst_rq->active_balance = 0;
	src_rq->active_balance = 0;
	double_rq_unlock(src_rq, dst_rq);
	local_irq_restore(flags);

	clear_reserved(wr->src_cpu);
	clear_reserved(wr->dst_cpu);
}

static void walt_lb_rotate_work_init(void)
{
	int i;

	for_each_possible_cpu(i) {
		struct walt_lb_rotate_work *wr = &per_cpu(walt_lb_rotate_works, i);

		INIT_WORK(&wr->w, walt_lb_rotate_work_func);
	}
}

#define WALT_ROTATION_THRESHOLD_NS	16000000
static void walt_lb_check_for_rotation(struct rq *src_rq)
{
	u64 wc, wait, max_wait = 0, run, max_run = 0;
	int deserved_cpu = nr_cpu_ids, dst_cpu = nr_cpu_ids;
	int i, src_cpu = cpu_of(src_rq);
	struct rq *dst_rq;
	struct walt_lb_rotate_work *wr = NULL;
	struct walt_task_struct *wts;

	if (!is_min_possible_cluster_cpu(src_cpu))
		return;

	/*
	 * Use src_rq->clock directly instead of rq_clock() since
	 * we do not have the rq lock and
	 * src_rq->clock was updated in the tick callpath.
	 */
	wc = src_rq->clock;

	for_each_possible_cpu(i) {
		struct rq *rq = cpu_rq(i);

		if (!is_min_possible_cluster_cpu(i))
			break;

		if (is_reserved(i))
			continue;

		if (!rq->misfit_task_load || !walt_fair_task(rq->curr))
			continue;

		wts = (struct walt_task_struct *) rq->curr->android_vendor_data1;
		wait = wc - wts->last_enqueued_ts;
		if (wait > max_wait) {
			max_wait = wait;
			deserved_cpu = i;
		}
	}

	if (deserved_cpu != src_cpu)
		return;

	for_each_possible_cpu(i) {
		struct rq *rq = cpu_rq(i);

		if (is_min_possible_cluster_cpu(i))
			continue;

		if (is_reserved(i))
			continue;

		if (!walt_fair_task(rq->curr))
			continue;

		if (rq->nr_running > 1)
			continue;

		wts = (struct walt_task_struct *) rq->curr->android_vendor_data1;
		run = wc - wts->last_enqueued_ts;

		if (run < WALT_ROTATION_THRESHOLD_NS)
			continue;

		if (run > max_run) {
			max_run = run;
			dst_cpu = i;
		}
	}

	if (dst_cpu == nr_cpu_ids)
		return;

	dst_rq = cpu_rq(dst_cpu);

	double_rq_lock(src_rq, dst_rq);
	if (walt_fair_task(dst_rq->curr) &&
		!src_rq->active_balance && !dst_rq->active_balance &&
		cpumask_test_cpu(dst_cpu, src_rq->curr->cpus_ptr) &&
		cpumask_test_cpu(src_cpu, dst_rq->curr->cpus_ptr)) {
		get_task_struct(src_rq->curr);
		get_task_struct(dst_rq->curr);

		mark_reserved(src_cpu);
		mark_reserved(dst_cpu);
		wr = &per_cpu(walt_lb_rotate_works, src_cpu);

		wr->src_task = src_rq->curr;
		wr->dst_task = dst_rq->curr;

		wr->src_cpu = src_cpu;
		wr->dst_cpu = dst_cpu;

		dst_rq->active_balance = 1;
		src_rq->active_balance = 1;
	}
	double_rq_unlock(src_rq, dst_rq);

	if (wr)
		queue_work_on(src_cpu, system_highpri_wq, &wr->w);
}

static inline bool _walt_can_migrate_task(struct task_struct *p, int dst_cpu,
					  bool to_lower, bool to_higher, bool force)
{
	struct walt_rq *wrq = &per_cpu(walt_rq, task_cpu(p));
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	/* Don't detach task if it is under active migration */
	if (wrq->push_task == p)
		return false;

	if (to_lower) {
		if (wts->iowaited)
			return false;
		if (per_task_boost(p) == TASK_BOOST_STRICT_MAX &&
				task_in_related_thread_group(p))
			return false;
		if (walt_pipeline_low_latency_task(p))
			return false;
		if (!force && walt_get_rtg_status(p))
			return false;
		if (!force && !task_fits_max(p, dst_cpu))
			return false;
	} else if (!to_higher) {
		if (!task_fits_max(p, dst_cpu) &&
			wrq->walt_stats.nr_big_tasks < 2)
			return false;
	}

	/* Don't detach task if dest cpu is halted */
	if (cpu_halted(dst_cpu))
		return false;

	if (task_reject_partialhalt_cpu(p, dst_cpu))
		return false;

	return true;
}

static inline bool need_active_lb(struct task_struct *p, int dst_cpu,
				  int src_cpu)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	if (cpu_rq(src_cpu)->active_balance)
		return false;

	if (!check_for_higher_capacity(dst_cpu, src_cpu))
		return false;

	if (!wts->misfit)
		return false;

	if (!is_min_possible_cluster_cpu(src_cpu) && !task_fits_max(p, dst_cpu))
		return false;

	if (task_reject_partialhalt_cpu(p, dst_cpu))
		return false;

	return true;
}

static int walt_lb_pull_tasks(int dst_cpu, int src_cpu, struct task_struct *pulled_task)
{
	struct rq *dst_rq = cpu_rq(dst_cpu);
	struct rq *src_rq = cpu_rq(src_cpu);
	unsigned long flags;
	struct task_struct *p;
	bool active_balance = false, to_lower, to_higher;
	struct walt_rq *src_wrq = &per_cpu(walt_rq, src_cpu);
	struct walt_task_struct *wts;
	struct task_struct *pull_me;
	int task_visited;

	BUG_ON(src_cpu == dst_cpu);

	to_lower = check_for_higher_capacity(src_cpu, dst_cpu);
	to_higher = check_for_higher_capacity(dst_cpu, src_cpu);

	raw_spin_lock_irqsave(&src_rq->__lock, flags);

	pull_me = NULL;
	task_visited = 0;
	list_for_each_entry_reverse(p, &src_rq->cfs_tasks, se.group_node) {
		if (!cpumask_test_cpu(dst_cpu, p->cpus_ptr))
			continue;

		if (task_on_cpu(src_rq, p))
			continue;

		if (!_walt_can_migrate_task(p, dst_cpu, to_lower, to_higher,
					false))
			continue;

		if (pull_me == NULL) {
			pull_me = p;
		} else {
			if (to_lower) {
				if (task_util(p) < task_util(pull_me))
					pull_me = p;
			} else if (task_util(p) > task_util(pull_me)) {
				pull_me = p;
			}
		}

		task_visited++;
		if (task_visited > 5)
			break;
	}
	if (pull_me) {
		walt_detach_task(pull_me, src_rq, dst_rq);
		pulled_task = pull_me;
		goto unlock;
	}

	pull_me = NULL;
	task_visited = 0;
	list_for_each_entry_reverse(p, &src_rq->cfs_tasks, se.group_node) {
		if (!cpumask_test_cpu(dst_cpu, p->cpus_ptr))
			continue;

		if (task_on_cpu(src_rq, p))
			continue;

		if (!_walt_can_migrate_task(p, dst_cpu, to_lower, to_higher,
					true))
			continue;

		if (pull_me == NULL) {
			pull_me = p;
		} else {
			if (to_lower) {
				if (task_util(p) < task_util(pull_me))
					pull_me = p;
			} else if (task_util(p) > task_util(pull_me)) {
				pull_me = p;
			}
		}

		task_visited++;
		if (task_visited > 5)
			break;
	}
	if (pull_me) {
		walt_detach_task(pull_me, src_rq, dst_rq);
		pulled_task = pull_me;
		goto unlock;
	}

	list_for_each_entry_reverse(p, &src_rq->cfs_tasks, se.group_node) {

		if (task_on_cpu(src_rq, p)) {
			if (cpumask_test_cpu(dst_cpu, p->cpus_ptr)
				&& need_active_lb(p, dst_cpu, src_cpu)) {
				bool success;
				active_balance = true;
				src_rq->active_balance = 1;
				src_rq->push_cpu = dst_cpu;
				get_task_struct(p);
				src_wrq->push_task = p;
				mark_reserved(dst_cpu);

				/* lock must be dropped before waking the stopper */
				raw_spin_unlock_irqrestore(&src_rq->__lock, flags);

				/*
				 * Using our custom active load balance callback so that
				 * the push_task is really pulled onto this CPU.
				 */
				wts = (struct walt_task_struct *) p->android_vendor_data1;
				trace_walt_active_load_balance(p, src_cpu, dst_cpu, wts);
				success = stop_one_cpu_nowait(src_cpu,
						stop_walt_lb_active_migration,
						src_rq, &src_rq->active_balance_work);
				if (!success)
					clear_reserved(dst_cpu);

				return 0; /* we did not pull any task here */
			}
			goto unlock;
		}
	}
unlock:
	/* lock must be dropped before waking the stopper */
	raw_spin_unlock_irqrestore(&src_rq->__lock, flags);

	if (!pulled_task)
		return 0;

	raw_spin_lock_irqsave(&dst_rq->__lock, flags);
	walt_attach_task(pulled_task, dst_rq);
	raw_spin_unlock_irqrestore(&dst_rq->__lock, flags);

	return 1; /* we pulled 1 task */
}

#define SMALL_TASK_THRESHOLD	102
/*
 * find_first_idle_if_others_are_busy
 *
 * Get an idle cpu in the middle clusters
 *
 * Returns -1 if there are no idle cpus in the mask, or if there
 * is a CPU that is about to be come newly idle. Returns <cpu>
 * if there is an idle cpu in a cluster that's not about to do
 * newly idle load balancing.
 * Also returns -1  if called on a single or dual cluster system
 */
static int find_first_idle_if_others_are_busy(void)
{
	int i, first_idle = -1;
	struct cpumask src_mask;

	cpumask_clear(&src_mask);
	for (i = 0; i < num_sched_clusters; i++) {
		if (i == 0 || i == num_sched_clusters - 1)
			continue;
		else
			cpumask_or(&src_mask, &src_mask, &cpu_array[0][i]);
	}

	for_each_cpu(i, &src_mask) {
		if (!cpu_active(i))
			continue;

		if (cpu_halted(i))
			continue;

		if (available_idle_cpu(i))
			first_idle = i;

		if (cpu_util(i) < SMALL_TASK_THRESHOLD && cpu_rq(i)->nr_running == 1) {
			/*
			 * there was one CPU in the mask that was almost idle, but not
			 * quite. when it becomes idle, it will do newidle load-balance,
			 * and start off with it's own cluster. So no reason to kick
			 * anything in this cluster. i.e. don't perform a wasted kick.
			 */
			first_idle = -1;

			/* return -1, a cpu in this cluster is about to do
			 * newly idle load balancing
			 */
			break;
		}
	}

	return first_idle;
}

static int walt_lb_find_busiest_similar_cap_cpu(int dst_cpu, const cpumask_t *src_mask,
		int *has_misfit, bool is_newidle)
{
	int i;
	int busiest_cpu = -1;
	unsigned long util, busiest_util = 0;
	struct walt_rq *wrq;

	for_each_cpu(i, src_mask) {
		wrq = &per_cpu(walt_rq, i);
		trace_walt_lb_cpu_util(i, wrq);

		if (cpu_rq(i)->nr_running < 2 || !cpu_rq(i)->cfs.h_nr_running)
			continue;

		if (cpu_partial_halted(dst_cpu) && !cpu_partial_halted(i))
			continue;

		util = walt_lb_cpu_util(i);
		if (util < busiest_util)
			continue;

		busiest_util = util;
		busiest_cpu = i;
	}

	return busiest_cpu;
}

static int walt_lb_find_busiest_from_higher_cap_cpu(int dst_cpu, const cpumask_t *src_mask,
		int *has_misfit, bool is_newidle)
{
	int i;
	int busiest_cpu = -1;
	unsigned long util, busiest_util = 0;
	unsigned long total_capacity = 0, total_util = 0, total_nr = 0;
	int total_cpus = 0;
	struct walt_rq *wrq;
	bool asymcap_boost = ASYMCAP_BOOST(dst_cpu);

	if (cpu_partial_halted(dst_cpu))
		return -1;

	for_each_cpu(i, src_mask) {

		if (!cpu_active(i))
			continue;

		wrq = &per_cpu(walt_rq, i);
		trace_walt_lb_cpu_util(i, wrq);

		util = walt_lb_cpu_util(i);
		total_cpus += 1;
		total_util += util;
		total_capacity += capacity_orig_of(i);
		total_nr += cpu_rq(i)->cfs.h_nr_running;

		if (cpu_rq(i)->cfs.h_nr_running < 2)
			continue;

		if (cpu_rq(i)->cfs.h_nr_running == 2 &&
			task_util(cpu_rq(i)->curr) < SMALL_TASK_THRESHOLD)
			continue;

		/*
		 * During rotation, two silver fmax tasks gets
		 * placed on gold/prime and the CPU may not be
		 * overutilized but for rotation, we have to
		 * spread out.
		 */
		if (!walt_rotation_enabled && !cpu_overutilized(i) &&
			!asymcap_boost)
			continue;

		if (util < busiest_util)
			continue;

		busiest_util = util;
		busiest_cpu = i;
	}

	/*
	 * Don't allow migrating to lower cluster unless this high
	 * capacity cluster is sufficiently loaded.
	 */
	if (!walt_rotation_enabled && !asymcap_boost) {
		if (total_nr <= total_cpus || total_util * 1280 < total_capacity * 1024)
			busiest_cpu = -1;
	}

	return busiest_cpu;
}

static int walt_lb_find_busiest_from_lower_cap_cpu(int dst_cpu, const cpumask_t *src_mask,
		int *has_misfit, bool is_newidle)
{
	int i;
	int busiest_cpu = -1;
	unsigned long util, busiest_util = 0;
	unsigned long total_capacity = 0, total_util = 0, total_nr = 0;
	int total_cpus = 0;
	int busy_nr_big_tasks = 0;
	struct walt_rq *wrq;
	bool treat_dst_idle = is_newidle || available_idle_cpu(dst_cpu);

	/*
	 * A higher capacity CPU is looking at a lower capacity
	 * cluster. active balance and big tasks are in play.
	 * other than that, it is very much same as above. we
	 * really don't need this as a separate block. will
	 * refactor this after final testing is done.
	 */
	for_each_cpu(i, src_mask) {
		wrq = &per_cpu(walt_rq, i);

		if (!cpu_active(i))
			continue;

		trace_walt_lb_cpu_util(i, wrq);

		util = walt_lb_cpu_util(i);
		total_cpus += 1;
		total_util += util;
		total_capacity += capacity_orig_of(i);
		total_nr += cpu_rq(i)->cfs.h_nr_running;

		/*
		 * no point in selecting this CPU as busy, as
		 * active balance is in progress.
		 */
		if (cpu_rq(i)->active_balance)
			continue;

		/* active migration is allowed only to idle cpu */
		if (cpu_rq(i)->cfs.h_nr_running < 2 &&
			(!wrq->walt_stats.nr_big_tasks || !treat_dst_idle))
			continue;

		if (!walt_rotation_enabled && !cpu_overutilized(i) &&
			!ASYMCAP_BOOST(i))
			continue;

		if (util < busiest_util)
			continue;

		busiest_util = util;
		busiest_cpu = i;
		busy_nr_big_tasks = wrq->walt_stats.nr_big_tasks;
	}

	if (!walt_rotation_enabled && !busy_nr_big_tasks &&
		!(busiest_cpu != -1 && ASYMCAP_BOOST(busiest_cpu))) {
		if (total_nr <= total_cpus || total_util * 1280 < total_capacity * 1024)
			busiest_cpu = -1;
	}

	if (busy_nr_big_tasks && busiest_cpu != -1)
		*has_misfit = true;

	return busiest_cpu;
}

static int walt_lb_find_busiest_cpu(int dst_cpu, const cpumask_t *src_mask, int *has_misfit,
				    bool is_newidle)
{
	int fsrc_cpu = cpumask_first(src_mask);
	int busiest_cpu;

	if (check_for_higher_capacity(dst_cpu, fsrc_cpu))
		busiest_cpu = walt_lb_find_busiest_from_lower_cap_cpu(dst_cpu,
								src_mask, has_misfit, is_newidle);
	else if (check_for_higher_capacity(fsrc_cpu, dst_cpu))
		busiest_cpu = walt_lb_find_busiest_from_higher_cap_cpu(dst_cpu,
								src_mask, has_misfit, is_newidle);
	else
		busiest_cpu = walt_lb_find_busiest_similar_cap_cpu(dst_cpu,
								src_mask, has_misfit, is_newidle);

	return busiest_cpu;
}

static DEFINE_RAW_SPINLOCK(walt_lb_migration_lock);
void walt_lb_tick(struct rq *rq)
{
	int prev_cpu = rq->cpu, new_cpu, ret;
	struct task_struct *p = rq->curr;
	unsigned long flags;
	struct walt_rq *prev_wrq = &per_cpu(walt_rq, cpu_of(rq));
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	raw_spin_lock(&rq->__lock);
	if (available_idle_cpu(prev_cpu) && is_reserved(prev_cpu) && !rq->active_balance)
		clear_reserved(prev_cpu);
	raw_spin_unlock(&rq->__lock);

	if (!walt_fair_task(p))
		return;

	walt_cfs_tick(rq);

	if (!rq->misfit_task_load)
		return;

	if (READ_ONCE(p->__state) != TASK_RUNNING || p->nr_cpus_allowed == 1)
		return;

	raw_spin_lock_irqsave(&walt_lb_migration_lock, flags);

	if (walt_rotation_enabled) {
		walt_lb_check_for_rotation(rq);
		goto out_unlock;
	}

	rcu_read_lock();
	new_cpu = walt_find_energy_efficient_cpu(p, prev_cpu, 0, 1);
	rcu_read_unlock();

	if (new_cpu < 0)
		goto out_unlock;

	/* prevent active task migration to busy or same/lower capacity CPU */
	if (!available_idle_cpu(new_cpu) || !check_for_higher_capacity(new_cpu, prev_cpu))
		goto out_unlock;

	if (!is_min_possible_cluster_cpu(prev_cpu) && !task_fits_max(p, new_cpu))
		goto out_unlock;

	raw_spin_lock(&rq->__lock);
	if (rq->active_balance) {
		raw_spin_unlock(&rq->__lock);
		goto out_unlock;
	}
	rq->active_balance = 1;
	rq->push_cpu = new_cpu;
	get_task_struct(p);
	prev_wrq->push_task = p;
	raw_spin_unlock(&rq->__lock);

	mark_reserved(new_cpu);
	raw_spin_unlock_irqrestore(&walt_lb_migration_lock, flags);

	trace_walt_active_load_balance(p, prev_cpu, new_cpu, wts);
	ret = stop_one_cpu_nowait(prev_cpu,
			stop_walt_lb_active_migration, rq,
			&rq->active_balance_work);
	if (!ret)
		clear_reserved(new_cpu);
	else
		wake_up_if_idle(new_cpu);

	return;

out_unlock:
	raw_spin_unlock_irqrestore(&walt_lb_migration_lock, flags);
}

static inline int has_pushable_tasks(struct rq *rq)
{
	return !plist_head_empty(&rq->rt.pushable_tasks);
}

#define WALT_RT_PULL_THRESHOLD_NS	250000
static bool walt_balance_rt(struct rq *this_rq)
{
	int i, this_cpu = this_rq->cpu, src_cpu = this_cpu;
	struct rq *src_rq;
	struct task_struct *p;
	struct walt_task_struct *wts;
	bool pulled = false;
	u64 wallclock;

	/* can't help if this has a runnable RT */
	if (sched_rt_runnable(this_rq))
		return false;

	/* check if any CPU has a pushable RT task */
	for_each_possible_cpu(i) {
		struct rq *rq = cpu_rq(i);

		if (!has_pushable_tasks(rq))
			continue;

		src_cpu = i;
		break;
	}

	if (src_cpu == this_cpu)
		return false;

	src_rq = cpu_rq(src_cpu);
	double_lock_balance(this_rq, src_rq);

	/* lock is dropped, so check again */
	if (sched_rt_runnable(this_rq))
		goto unlock;

	p = pick_highest_pushable_task(src_rq, this_cpu);

	if (!p)
		goto unlock;

	if (!cpumask_test_cpu(this_cpu, p->cpus_ptr))
		goto unlock;

	wts = (struct walt_task_struct *) p->android_vendor_data1;

	/*
	 * Use rq->clock directly instead of rq_clock() since
	 * rq->clock was updated recently in the __schedule() -> pick_next_task() callpath.
	 * Time lost in grabbing rq locks will likely be corrected via max.
	 */
	wallclock = max(this_rq->clock, src_rq->clock);

	if (wallclock > wts->last_wake_ts &&
			wallclock - wts->last_wake_ts < WALT_RT_PULL_THRESHOLD_NS)
		goto unlock;

	pulled = true;
	deactivate_task(src_rq, p, 0);
	set_task_cpu(p, this_cpu);
	activate_task(this_rq, p, 0);
unlock:
	double_unlock_balance(this_rq, src_rq);
	return pulled;
}

__read_mostly unsigned int sysctl_sched_force_lb_enable = 1;
static bool should_help_min_cap(int this_cpu)
{
	int cpu;

	if (!sysctl_sched_force_lb_enable || is_min_possible_cluster_cpu(this_cpu))
		return false;

	for_each_cpu(cpu, &cpu_array[0][0]) {
		struct walt_rq *wrq = &per_cpu(walt_rq, cpu);

		if (wrq->walt_stats.nr_big_tasks)
			return true;
	}

	return false;
}

/* similar to sysctl_sched_migration_cost */
#define NEWIDLE_BALANCE_THRESHOLD	500000
static void walt_newidle_balance(struct rq *this_rq,
				 struct rq_flags *rf, int *pulled_task,
				 int *done, int force_overload)
{
	int this_cpu = this_rq->cpu;
	struct walt_rq *wrq = &per_cpu(walt_rq, this_cpu);
	int order_index;
	int busy_cpu = -1;
	bool enough_idle = (this_rq->avg_idle > NEWIDLE_BALANCE_THRESHOLD);
	bool help_min_cap = false;
	int first_idle;
	int has_misfit = 0;
	int i;
	struct task_struct *pulled_task_struct = NULL;

	if (unlikely(walt_disabled))
		return;

	/*
	 * newly idle load balance is completely handled here, so
	 * set done to skip the load balance by the caller.
	 */
	*done = 1;
	*pulled_task = 0;

	/*
	 * This CPU is about to enter idle, so clear the
	 * misfit_task_load and mark the idle stamp.
	 */
	this_rq->misfit_task_load = 0;
	this_rq->idle_stamp = rq_clock(this_rq);

	if (!cpu_active(this_cpu))
		return;

	if (cpu_halted(this_cpu))
		return;

	if (is_reserved(this_cpu))
		return;

	/*Cluster isn't initialized until after WALT is enabled*/
	order_index = wrq->cluster->id;

	rq_unpin_lock(this_rq, rf);

	/*
	 * Since we drop rq lock while doing RT balance,
	 * check if any tasks are queued on this and bail out
	 * early.
	 */
	if (walt_balance_rt(this_rq) || this_rq->nr_running)
		goto rt_pulled;

	if (!force_overload && !READ_ONCE(this_rq->rd->overload))
		goto repin;

	if (atomic_read(&this_rq->nr_iowait) && !enough_idle)
		goto repin;

	help_min_cap = should_help_min_cap(this_cpu);
	raw_spin_unlock(&this_rq->__lock);

	/*
	 * careful, we dropped the lock, and has to be acquired
	 * before returning. Since rq lock is dropped, tasks
	 * can be queued remotely, so keep a check on nr_running
	 * and bail out.
	 */

	order_index = wrq->cluster->id;
	for (i = 0; i < num_sched_clusters; i++) {
		int first_cpu = cpumask_first(&cpu_array[order_index][i]);
		struct walt_rq *src_wrq = &per_cpu(walt_rq, first_cpu);
		int src_cluster_id = src_wrq->cluster->id;

		busy_cpu = walt_lb_find_busiest_cpu(this_cpu, &cpu_array[order_index][i],
										&has_misfit, true);
		if (busy_cpu == -1)
			continue;
		/* when not enough idle
		 *   Small should not help big.
		 *   Big should help small ONLY is mifit is present.
		 *   Same capacity cpus should help each other
		 */
		if (!enough_idle &&
			(capacity_orig_of(this_cpu) < capacity_orig_of(busy_cpu) ||
			(capacity_orig_of(this_cpu) > capacity_orig_of(busy_cpu) && !has_misfit)))
			continue;

		/* if helping farthest cluster,  kick a middle */
		if (num_sched_clusters > 2 &&
		    ((wrq->cluster->id == 0 && src_cluster_id == num_sched_clusters - 1) ||
		    (wrq->cluster->id == num_sched_clusters - 1 && src_cluster_id == 0))) {
			first_idle = find_first_idle_if_others_are_busy();
			if (first_idle != -1) {
				walt_kick_cpu(first_idle);
			} else {
				if (walt_rotation_enabled &&
					capacity_orig_of(this_cpu) >
					capacity_orig_of(busy_cpu)) {
					/*
					 * When BTR active help
					 * smallest immediately
					 */
					goto found_busy_cpu;
				}
			}
		} else {
			goto found_busy_cpu;
		}
	}
	goto unlock;

found_busy_cpu:
	/* sanity checks before attempting the pull */
	if (this_rq->nr_running > 0 || (busy_cpu == this_cpu))
		goto unlock;

	*pulled_task = walt_lb_pull_tasks(this_cpu, busy_cpu, pulled_task_struct);

unlock:
	raw_spin_lock(&this_rq->__lock);
rt_pulled:
	if (this_rq->cfs.h_nr_running && !*pulled_task)
		*pulled_task = 1;

	/* Is there a task of a high priority class? */
	if (this_rq->nr_running != this_rq->cfs.h_nr_running)
		*pulled_task = -1;

	/* reset the idle time stamp if we pulled any task */
	if (*pulled_task)
		this_rq->idle_stamp = 0;

repin:
	rq_repin_lock(this_rq, rf);

	trace_walt_newidle_balance(this_cpu, busy_cpu, *pulled_task,
				   help_min_cap, enough_idle, pulled_task_struct);
}

/* run newidle balance as a result of an unhalt operation */
void walt_smp_newidle_balance(void *ignored)
{
	int cpu = raw_smp_processor_id();
	struct rq *rq = cpu_rq(cpu);
	struct rq_flags rf;
	int pulled_task;
	int done = 0;

	rq_lock(rq, &rf);
	update_rq_clock(rq);
	walt_newidle_balance(rq, &rf, &pulled_task, &done, true);
	resched_curr(rq);
	rq_unlock(rq, &rf);
}

static DEFINE_PER_CPU(call_single_data_t, nib_csd);

void walt_smp_call_newidle_balance(int cpu)
{
	call_single_data_t *csd = &per_cpu(nib_csd, cpu);

	if (unlikely(walt_disabled))
		return;

	smp_call_function_single_async(cpu, csd);
}

static void walt_find_busiest_queue(void *unused, int dst_cpu,
				    struct sched_group *group,
				    struct cpumask *env_cpus,
				    struct rq **busiest, int *done)
{
	int fsrc_cpu = cpumask_first(sched_group_span(group));
	int busiest_cpu = -1;
	struct cpumask src_mask;
	int has_misfit;

	if (unlikely(walt_disabled))
		return;
	*done = 1;
	*busiest = NULL;

	/*
	 * same cluster means, there will only be 1
	 * CPU in the busy group, so just select it.
	 */
	if (same_cluster(dst_cpu, fsrc_cpu)) {
		busiest_cpu = fsrc_cpu;
		goto done;
	}

	/*
	 * We will allow inter cluster migrations
	 * only if the source group is sufficiently
	 * loaded. The upstream load balancer is a
	 * bit more generous.
	 *
	 * re-using the same code that we use it
	 * for newly idle load balance. The policies
	 * remain same.
	 */
	cpumask_and(&src_mask, sched_group_span(group), env_cpus);
	busiest_cpu = walt_lb_find_busiest_cpu(dst_cpu, &src_mask, &has_misfit, false);
done:
	if (busiest_cpu != -1)
		*busiest = cpu_rq(busiest_cpu);

	trace_walt_find_busiest_queue(dst_cpu, busiest_cpu, src_mask.bits[0]);
}

/*
 * we only decide if nohz balance kick is needed or not. the
 * first CPU in the nohz.idle will come out of idle and do
 * load balance on behalf of every CPU. adding another hook
 * to decide which cpu to kick is useless. most of the time,
 * it is impossible to decide which CPU has to come out because
 * we get to kick only once.
 */
static void walt_nohz_balancer_kick(void *unused, struct rq *rq,
				    unsigned int *flags, int *done)
{
	if (unlikely(walt_disabled))
		return;
	*done = 1;

	/*
	 * tick path migration takes care of misfit task.
	 * so we have to check for nr_running >= 2 here.
	 */
	if (rq->nr_running >= 2 && cpu_overutilized(rq->cpu)) {
		*flags = NOHZ_KICK_MASK;
		trace_walt_nohz_balance_kick(rq);
	}
}

static void walt_can_migrate_task(void *unused, struct task_struct *p,
				  int dst_cpu, int *can_migrate)
{
	bool to_lower, to_higher;

	if (unlikely(walt_disabled))
		return;
	to_lower = check_for_higher_capacity(task_cpu(p), dst_cpu);
	to_higher = check_for_higher_capacity(dst_cpu, task_cpu(p));

	if (_walt_can_migrate_task(p, dst_cpu, to_lower,
				to_higher, true))
		return;

	*can_migrate = 0;
}

static void walt_sched_newidle_balance(void *unused, struct rq *this_rq,
				       struct rq_flags *rf, int *pulled_task,
				       int *done)
{
	walt_newidle_balance(this_rq, rf, pulled_task, done, false);
}

void walt_lb_init(void)
{
	int cpu;

	walt_lb_rotate_work_init();

	register_trace_android_rvh_sched_nohz_balancer_kick(walt_nohz_balancer_kick, NULL);
	register_trace_android_rvh_can_migrate_task(walt_can_migrate_task, NULL);
	register_trace_android_rvh_find_busiest_queue(walt_find_busiest_queue, NULL);
	register_trace_android_rvh_sched_newidle_balance(walt_sched_newidle_balance, NULL);

	for_each_cpu(cpu, cpu_possible_mask) {
		call_single_data_t *csd;

		csd = &per_cpu(nib_csd, cpu);
		INIT_CSD(csd, walt_smp_newidle_balance, (void *)(unsigned long)cpu);
	}
}
