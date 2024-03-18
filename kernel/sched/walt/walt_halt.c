// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/sched/isolation.h>
#include <trace/hooks/sched.h>
#include <walt.h>
#include "trace.h"

#ifdef CONFIG_HOTPLUG_CPU

enum pause_type {
	HALT,
	PARTIAL_HALT,

	MAX_PAUSE_TYPE
};

/* if a cpu is halting */
struct cpumask __cpu_halt_mask;
struct cpumask __cpu_partial_halt_mask;

/* spin lock to allow calling from non-preemptible context */
static DEFINE_RAW_SPINLOCK(halt_lock);

struct halt_cpu_state {
	u8		client_vote_mask[MAX_PAUSE_TYPE];
};

static DEFINE_PER_CPU(struct halt_cpu_state, halt_state);
static DEFINE_RAW_SPINLOCK(walt_drain_pending_lock);

/* the amount of time allowed for enqueue operations that happen
 * just after a halt operation.
 */
#define WALT_HALT_CHECK_THRESHOLD_NS 400000

/*
 * Remove a task from the runqueue and pretend that it's migrating. This
 * should prevent migrations for the detached task and disallow further
 * changes to tsk_cpus_allowed.
 */
void
detach_one_task_core(struct task_struct *p, struct rq *rq,
		     struct list_head *tasks)
{
	walt_lockdep_assert_rq(rq, p);

	p->on_rq = TASK_ON_RQ_MIGRATING;
	deactivate_task(rq, p, 0);
	list_add(&p->se.group_node, tasks);
}

void attach_tasks_core(struct list_head *tasks, struct rq *rq)
{
	struct task_struct *p;

	walt_lockdep_assert_rq(rq, NULL);

	while (!list_empty(tasks)) {
		p = list_first_entry(tasks, struct task_struct, se.group_node);
		list_del_init(&p->se.group_node);

		BUG_ON(task_rq(p) != rq);
		activate_task(rq, p, 0);
		p->on_rq = TASK_ON_RQ_QUEUED;
	}
}

/*
 * Migrate all tasks from the rq, sleeping tasks will be migrated by
 * try_to_wake_up()->select_task_rq().
 *
 * Called with rq->__lock held even though we'er in stop_machine() and
 * there's no concurrency possible, we hold the required locks anyway
 * because of lock validation efforts.
 *
 * The function will skip CPU pinned kthreads.
 */
static void migrate_tasks(struct rq *dead_rq, struct rq_flags *rf)
{
	struct rq *rq = dead_rq;
	struct task_struct *next, *stop = rq->stop;
	LIST_HEAD(percpu_kthreads);
	unsigned int num_pinned_kthreads = 1;
	struct rq_flags orf = *rf;
	int dest_cpu;

	/*
	 * Fudge the rq selection such that the below task selection loop
	 * doesn't get stuck on the currently eligible stop task.
	 *
	 * We're currently inside stop_machine() and the rq is either stuck
	 * in the stop_machine_cpu_stop() loop, or we're executing this code,
	 * either way we should never end up calling schedule() until we're
	 * done here.
	 */
	rq->stop = NULL;

	/*
	 * put_prev_task() and pick_next_task() sched
	 * class method both need to have an up-to-date
	 * value of rq->clock[_task]
	 */
	update_rq_clock(rq);

#ifdef CONFIG_SCHED_DEBUG
	/* note the clock update in orf */
	orf.clock_update_flags |= RQCF_UPDATED;
#endif

	for (;;) {
		/*
		 * There's this thread running, bail when that's the only
		 * remaining thread:
		 */
		if (rq->nr_running == 1)
			break;

		next = pick_migrate_task(rq);

		/*
		 * Argh ... no iterator for tasks, we need to remove the
		 * kthread from the run-queue to continue.
		 */

		if (is_per_cpu_kthread(next)) {
			detach_one_task_core(next, rq, &percpu_kthreads);
			num_pinned_kthreads += 1;
			continue;
		}

		/*
		 * Rules for changing task_struct::cpus_mask are holding
		 * both pi_lock and rq->__lock, such that holding either
		 * stabilizes the mask.
		 *
		 * Drop rq->__lock is not quite as disastrous as it usually is
		 * because !cpu_active at this point, which means load-balance
		 * will not interfere. Also, stop-machine.
		 */
		rq_unlock(rq, rf);
		raw_spin_lock(&next->pi_lock);
		raw_spin_rq_lock(rq);
		rq_repin_lock(rq, rf);

		/*
		 * Since we're inside stop-machine, _nothing_ should have
		 * changed the task, WARN if weird stuff happened, because in
		 * that case the above rq->__lock drop is a fail too.
		 */
		if (task_rq(next) != rq || !task_on_rq_queued(next)) {
			raw_spin_unlock(&next->pi_lock);
			continue;
		}

		/* Find suitable destination for @next */
		dest_cpu = select_fallback_rq(dead_rq->cpu, next);

		if (cpu_of(rq) != dest_cpu && !is_migration_disabled(next)) {
			/* only perform a required migration */
			rq = __migrate_task(rq, rf, next, dest_cpu);

			if (rq != dead_rq) {
				rq_unlock(rq, rf);
				rq = dead_rq;
				*rf = orf;
				raw_spin_rq_lock(rq);
				rq_repin_lock(rq, rf);
			}
		} else {
			detach_one_task_core(next, rq, &percpu_kthreads);
			num_pinned_kthreads += 1;
		}

		raw_spin_unlock(&next->pi_lock);
	}

	if (num_pinned_kthreads > 1)
		attach_tasks_core(&percpu_kthreads, rq);

	rq->stop = stop;
}

void __balance_callbacks(struct rq *rq);

static int drain_rq_cpu_stop(void *data)
{
	struct rq *rq = this_rq();
	struct rq_flags rf;
	struct walt_rq *wrq = &per_cpu(walt_rq, cpu_of(rq));

	rq_lock_irqsave(rq, &rf);
	/* rq lock is pinned */

	/* migrate tasks assumes that the lock is pinned, and will unlock/repin */
	migrate_tasks(rq, &rf);

	/* __balance_callbacks can unlock and relock the rq lock. unpin */
	rq_unpin_lock(rq, &rf);

	/*
	 * service any callbacks that were accumulated, prior to unlocking. such that
	 * any subsequent calls to rq_lock... will see an rq->balance_callback set to
	 * the default (0 or balance_push_callback);
	 */
	wrq->enqueue_counter = 0;
	__balance_callbacks(rq);
	if (wrq->enqueue_counter)
		WALT_BUG(WALT_BUG_WALT, NULL, "cpu: %d task was re-enqueued", cpu_of(rq));

	/* lock is no longer pinned, raw unlock using same flags as locking */
	raw_spin_rq_unlock_irqrestore(rq, rf.flags);

	return 0;
}

static int cpu_drain_rq(unsigned int cpu)
{
	if (!cpu_online(cpu))
		return 0;

	if (available_idle_cpu(cpu))
		return 0;

	/* this will schedule, must not be in atomic context */
	return stop_one_cpu(cpu, drain_rq_cpu_stop, NULL);
}

struct drain_thread_data {
	cpumask_t cpus_to_drain;
};

static struct drain_thread_data drain_data = {
	.cpus_to_drain = { CPU_BITS_NONE }
};

static int __ref try_drain_rqs(void *data)
{
	cpumask_t *cpus_ptr = &((struct drain_thread_data *)data)->cpus_to_drain;
	int cpu;
	unsigned long flags;

	while (!kthread_should_stop()) {
		raw_spin_lock_irqsave(&walt_drain_pending_lock, flags);
		if (cpumask_weight(cpus_ptr)) {
			cpumask_t local_cpus;

			cpumask_copy(&local_cpus, cpus_ptr);
			raw_spin_unlock_irqrestore(&walt_drain_pending_lock, flags);

			for_each_cpu(cpu, &local_cpus)
				cpu_drain_rq(cpu);

			raw_spin_lock_irqsave(&walt_drain_pending_lock, flags);
			cpumask_andnot(cpus_ptr, cpus_ptr, &local_cpus);

		}
		raw_spin_unlock_irqrestore(&walt_drain_pending_lock, flags);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		set_current_state(TASK_RUNNING);
	}

	return 0;
}

void restrict_cpus_and_freq(struct cpumask *cpus)
{
	struct cpumask restrict_cpus;
	int cpu = 0;

	cpumask_copy(&restrict_cpus, cpus);

	if (cpumask_intersects(cpus, cpu_partial_halt_mask) &&
			!cpumask_intersects(cpus, cpu_halt_mask) &&
			is_state1()) {
		for_each_cpu(cpu, cpus)
			fmax_cap[PARTIAL_HALT_CAP][cpu_cluster(cpu)->id] =
				sysctl_max_freq_partial_halt;
	} else {
		for_each_cpu(cpu, cpus) {
			cpumask_or(&restrict_cpus, &restrict_cpus, &(cpu_cluster(cpu)->cpus));
			fmax_cap[PARTIAL_HALT_CAP][cpu_cluster(cpu)->id] =
				FREQ_QOS_MAX_DEFAULT_VALUE;
		}
	}

	update_fmax_cap_capacities();
}

struct task_struct *walt_drain_thread;

static int halt_cpus(struct cpumask *cpus, enum pause_type type)
{
	int cpu;
	int ret = 0;
	u64 start_time = 0;
	struct halt_cpu_state *halt_cpu_state;
	unsigned long flags;

	if (trace_halt_cpus_enabled())
		start_time = sched_clock();

	trace_halt_cpus_start(cpus, 1);

	/* add the cpus to the halt mask */
	for_each_cpu(cpu, cpus) {
		if (cpu == cpumask_first(system_32bit_el0_cpumask())) {
			ret = -EINVAL;
			goto out;
		}

		halt_cpu_state = per_cpu_ptr(&halt_state, cpu);

		if (type == HALT)
			cpumask_set_cpu(cpu, cpu_halt_mask);
		else
			cpumask_set_cpu(cpu, cpu_partial_halt_mask);

		/* guarantee mask written at this time */
		wmb();
	}

	restrict_cpus_and_freq(cpus);

	/* migrate tasks off the cpu */
	if (type == HALT) {
		/* signal and wakeup the drain kthread */
		raw_spin_lock_irqsave(&walt_drain_pending_lock, flags);
		cpumask_or(&drain_data.cpus_to_drain, &drain_data.cpus_to_drain, cpus);
		raw_spin_unlock_irqrestore(&walt_drain_pending_lock, flags);

		wake_up_process(walt_drain_thread);
	}
out:
	trace_halt_cpus(cpus, start_time, 1, ret);

	return ret;
}

/* start the cpus again, and kick them to balance */
static int start_cpus(struct cpumask *cpus, enum pause_type type)
{
	u64 start_time = sched_clock();
	struct halt_cpu_state *halt_cpu_state;
	int cpu;

	trace_halt_cpus_start(cpus, 0);

	for_each_cpu(cpu, cpus) {
		halt_cpu_state = per_cpu_ptr(&halt_state, cpu);

		/* guarantee the halt state is updated */
		wmb();

		if (type == HALT)
			cpumask_clear_cpu(cpu, cpu_halt_mask);
		else
			cpumask_clear_cpu(cpu, cpu_partial_halt_mask);

		/* kick the cpu so it can pull tasks
		 * after the mask has been cleared.
		 */
		walt_smp_call_newidle_balance(cpu);
	}

	restrict_cpus_and_freq(cpus);

	trace_halt_cpus(cpus, start_time, 0, 0);

	return 0;
}

/* update client for cpus in yield/halt mask */
static void update_clients(struct cpumask *cpus, bool halt, enum pause_client client,
			   enum pause_type type)
{
	int cpu;
	struct halt_cpu_state *halt_cpu_state;

	for_each_cpu(cpu, cpus) {
		halt_cpu_state = per_cpu_ptr(&halt_state, cpu);
		if (halt)
			halt_cpu_state->client_vote_mask[type] |=  client;
		else
			halt_cpu_state->client_vote_mask[type] &= ~client;
	}
}

/* remove cpus that are already halted */
static void update_halt_cpus(struct cpumask *cpus, enum pause_type type)
{
	int cpu;
	struct halt_cpu_state *halt_cpu_state;

	for_each_cpu(cpu, cpus) {
		halt_cpu_state = per_cpu_ptr(&halt_state, cpu);
		if (halt_cpu_state->client_vote_mask[type])
			cpumask_clear_cpu(cpu, cpus);
	}
}

/* cpus will be modified */
static int walt_halt_cpus(struct cpumask *cpus, enum pause_client client, enum pause_type type)
{
	int ret = 0;
	cpumask_t requested_cpus;
	unsigned long flags;

	raw_spin_lock_irqsave(&halt_lock, flags);

	cpumask_copy(&requested_cpus, cpus);

	/* remove cpus that are already halted */
	update_halt_cpus(cpus, type);

	if (cpumask_empty(cpus)) {
		update_clients(&requested_cpus, true, client, type);
		goto unlock;
	}

	ret = halt_cpus(cpus, type);

	if (ret < 0)
		pr_debug("halt_cpus failure ret=%d cpus=%*pbl\n", ret,
			 cpumask_pr_args(&requested_cpus));
	else
		update_clients(&requested_cpus, true, client, type);
unlock:
	raw_spin_unlock_irqrestore(&halt_lock, flags);

	return ret;
}

int walt_pause_cpus(struct cpumask *cpus, enum pause_client client)
{
	if (walt_disabled)
		return -EAGAIN;
	return walt_halt_cpus(cpus, client, HALT);
}
EXPORT_SYMBOL_GPL(walt_pause_cpus);

int walt_partial_pause_cpus(struct cpumask *cpus, enum pause_client client)
{
	if (walt_disabled)
		return -EAGAIN;
	return walt_halt_cpus(cpus, client, PARTIAL_HALT);
}
EXPORT_SYMBOL_GPL(walt_partial_pause_cpus);

/* cpus will be modified */
static int walt_start_cpus(struct cpumask *cpus, enum pause_client client, enum pause_type type)
{
	int ret = 0;
	cpumask_t requested_cpus;
	unsigned long flags;

	raw_spin_lock_irqsave(&halt_lock, flags);
	cpumask_copy(&requested_cpus, cpus);
	update_clients(&requested_cpus, false, client, type);

	/* remove cpus that should still be halted */
	update_halt_cpus(cpus, type);

	ret = start_cpus(cpus, type);

	if (ret < 0) {
		pr_debug("halt_cpus failure ret=%d cpus=%*pbl\n", ret,
			 cpumask_pr_args(&requested_cpus));
		/* restore/increment ref counts in case of error */
		update_clients(&requested_cpus, true, client, type);
	}

	raw_spin_unlock_irqrestore(&halt_lock, flags);

	return ret;
}

int walt_resume_cpus(struct cpumask *cpus, enum pause_client client)
{
	if (walt_disabled)
		return -EAGAIN;
	return walt_start_cpus(cpus, client, HALT);
}
EXPORT_SYMBOL_GPL(walt_resume_cpus);

int walt_partial_resume_cpus(struct cpumask *cpus, enum pause_client client)
{
	if (walt_disabled)
		return -EAGAIN;
	return walt_start_cpus(cpus, client, PARTIAL_HALT);
}
EXPORT_SYMBOL_GPL(walt_partial_resume_cpus);

/**
 * cpus_halted_by_client: determine if client has halted a cpu
 *   where all cpus in the mask are halted.
 *
 * If all cpus in the cluster are halted, and one of them is
 * halted for this client, then and only then indicate pass.
 *
 * Otherwise, if not all cpus are halted, or none of the cpus
 * are halted by this particular client, then reject.
 *
 * return true if conditions are met, false otherwise.
 */
bool cpus_halted_by_client(struct cpumask *cpus, enum pause_client client)
{
	struct halt_cpu_state *halt_cpu_state;
	bool cpu_halted_for_client = false;
	int cpu;

	for_each_cpu(cpu, cpus) {
		halt_cpu_state = per_cpu_ptr(&halt_state, cpu);

		if (!halt_cpu_state->client_vote_mask[HALT])
			return false;

		if (halt_cpu_state->client_vote_mask[HALT] & client)
			cpu_halted_for_client = true;
	}

	if (cpu_halted_for_client)
		return true;

	return false;
}

static void android_rvh_get_nohz_timer_target(void *unused, int *cpu, bool *done)
{
	int i, default_cpu = -1;
	struct sched_domain *sd;
	cpumask_t active_unhalted;

	*done = true;
	cpumask_andnot(&active_unhalted, cpu_active_mask, cpu_halt_mask);

	if (housekeeping_cpu(*cpu, HK_TYPE_TIMER) && !cpu_halted(*cpu)) {
		if (!available_idle_cpu(*cpu))
			return;
		default_cpu = *cpu;
	}

	/*
	 * find first cpu halted by core control and try to avoid
	 * affecting externally halted cpus.
	 */
	if (!cpumask_weight(&active_unhalted)) {
		cpumask_t tmp_pause, tmp_part_pause, tmp_halt, *tmp;

		cpumask_and(&tmp_part_pause, cpu_active_mask, &cpus_part_paused_by_us);
		cpumask_and(&tmp_pause, cpu_active_mask, &cpus_paused_by_us);
		cpumask_and(&tmp_halt, cpu_active_mask, cpu_halt_mask);
		tmp = cpumask_weight(&tmp_part_pause) ? &tmp_part_pause :
			cpumask_weight(&tmp_pause) ? &tmp_pause : &tmp_halt;

		for_each_cpu(i, tmp) {
			if ((*cpu == i) && cpumask_weight(tmp) > 1)
				continue;

			*cpu = i;
			return;
		}
	}

	rcu_read_lock();
	for_each_domain(*cpu, sd) {
		for_each_cpu_and(i, sched_domain_span(sd),
			housekeeping_cpumask(HK_TYPE_TIMER)) {
			if (*cpu == i)
				continue;

			if (!available_idle_cpu(i) && !cpu_halted(i)) {
				*cpu = i;
				goto unlock;
			}
		}
	}

	if (default_cpu == -1) {
		for_each_cpu_and(i, &active_unhalted,
				 housekeeping_cpumask(HK_TYPE_TIMER)) {
			if (*cpu == i)
				continue;

			if (!available_idle_cpu(i)) {
				*cpu = i;
				goto unlock;
			}
		}

		/* choose any active unhalted cpu */
		default_cpu = cpumask_any(&active_unhalted);

		if (unlikely(default_cpu >= nr_cpu_ids))
			goto unlock;
	}

	*cpu = default_cpu;
unlock:
	rcu_read_unlock();
}

/**
 * android_rvh_set_cpus_allowed_by_task: disallow cpus that are halted
 *
 * NOTES: may be called if migration is disabled for the task
 *        if per-cpu-kthread, must not deliberately return an invalid cpu
 *        if !per-cpu-kthread, may return an invalid cpu (reject dest_cpu)
 *        must not change cpu in in_exec 32bit task case
 */
static void android_rvh_set_cpus_allowed_by_task(void *unused,
						 const struct cpumask *cpu_valid_mask,
						 const struct cpumask *new_mask,
						 struct task_struct *p,
						 unsigned int *dest_cpu)
{
	if (unlikely(walt_disabled))
		return;

	/* allow kthreads to change affinity regardless of halt status of dest_cpu */
	if (p->flags & PF_KTHREAD)
		return;

	if (cpu_halted(*dest_cpu) && !p->migration_disabled) {
		cpumask_t allowed_cpus;

		if (unlikely(is_compat_thread(task_thread_info(p)) && p->in_execve))
			return;

		/* remove halted cpus from the valid mask, and store locally */
		cpumask_andnot(&allowed_cpus, cpu_valid_mask, cpu_halt_mask);
		cpumask_and(&allowed_cpus, &allowed_cpus, new_mask);

		/* do not modify dest_cpu if there are no cpus to choose from */
		if (!cpumask_empty(&allowed_cpus))
			*dest_cpu = cpumask_any_and_distribute(&allowed_cpus, new_mask);
	}
}

/**
 * android_rvh_rto_next-cpu: disallow halted cpus for irq workfunctions
 */
static void android_rvh_rto_next_cpu(void *unused, int rto_cpu, struct cpumask *rto_mask, int *cpu)
{
	cpumask_t allowed_cpus;

	if (unlikely(walt_disabled))
		return;

	if (cpu_halted(*cpu)) {
		/* remove halted cpus from the valid mask, and store locally */
		cpumask_andnot(&allowed_cpus, rto_mask, cpu_halt_mask);
		*cpu = cpumask_next(rto_cpu, &allowed_cpus);
	}
}

/**
 * android_rvh_is_cpu_allowed: disallow cpus that are halted
 *
 * NOTE: this function will not be called if migration is disabled for the task.
 */
static void android_rvh_is_cpu_allowed(void *unused, struct task_struct *p, int cpu, bool *allowed)
{
	if (unlikely(walt_disabled))
		return;

	if (cpumask_test_cpu(cpu, cpu_halt_mask)) {
		cpumask_t cpus_allowed;

		/* default reject for any halted cpu */
		*allowed = false;

		if (unlikely(is_compat_thread(task_thread_info(p)) && p->in_execve)) {
			/* 32bit task in execve. allow this cpu. */
			*allowed = true;
			return;
		}

		/*
		 * for cfs threads, active cpus in the affinity are allowed
		 * but halted cpus are not allowed
		 */
		cpumask_and(&cpus_allowed, cpu_active_mask, p->cpus_ptr);
		cpumask_andnot(&cpus_allowed, &cpus_allowed, cpu_halt_mask);

		if (!(p->flags & PF_KTHREAD)) {
			if (cpumask_empty(&cpus_allowed)) {
				/*
				 * All affined cpus are inactive or halted.
				 * Allow this cpu for user threads
				 */
				*allowed = true;
			}
			return;
		}

		/* for kthreads, dying cpus are not allowed */
		cpumask_andnot(&cpus_allowed, &cpus_allowed, cpu_dying_mask);
		if (cpumask_empty(&cpus_allowed)) {
			/*
			 * All affined cpus inactive or halted or dying.
			 * Allow this cpu for kthreads
			 */
			*allowed = true;
		}
	}
}

void walt_halt_init(void)
{
	struct sched_param param = { .sched_priority = MAX_RT_PRIO-1 };

	walt_drain_thread = kthread_run(try_drain_rqs, &drain_data, "halt_drain_rqs");
	if (IS_ERR(walt_drain_thread)) {
		pr_err("Error creating walt drain thread\n");
		return;
	}

	sched_setscheduler_nocheck(walt_drain_thread, SCHED_FIFO, &param);

	register_trace_android_rvh_get_nohz_timer_target(android_rvh_get_nohz_timer_target, NULL);
	register_trace_android_rvh_set_cpus_allowed_by_task(
						android_rvh_set_cpus_allowed_by_task, NULL);
	register_trace_android_rvh_rto_next_cpu(android_rvh_rto_next_cpu, NULL);
	register_trace_android_rvh_is_cpu_allowed(android_rvh_is_cpu_allowed, NULL);

}

#endif /* CONFIG_HOTPLUG_CPU */
