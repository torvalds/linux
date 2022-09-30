// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2021 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/sched/isolation.h>
#include <trace/hooks/sched.h>
#include <walt.h>
#include "trace.h"

#ifdef CONFIG_HOTPLUG_CPU

/* if a cpu is halting */
struct cpumask __cpu_halt_mask;

/* spin lock to allow calling from non-preemptible context */
static DEFINE_RAW_SPINLOCK(halt_lock);

struct halt_cpu_state {
	u64		last_halt;
	u8		reason;
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
	lockdep_assert_held(&rq->__lock);

	p->on_rq = TASK_ON_RQ_MIGRATING;
	deactivate_task(rq, p, 0);
	list_add(&p->se.group_node, tasks);
}

void attach_tasks_core(struct list_head *tasks, struct rq *rq)
{
	struct task_struct *p;

	lockdep_assert_held(&rq->__lock);

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
		rq_relock(rq, rf);

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
				rq_relock(rq, rf);
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
	struct walt_rq *wrq = (struct walt_rq *) rq->android_vendor_data1;

	rq_lock_irqsave(rq, &rf);
	migrate_tasks(rq, &rf);

	/*
	 * service any callbacks that were accumulated, prior to unlocking. such that
	 * any subsequent calls to rq_lock... will see an rq->balance_callback set to
	 * the default (0 or balance_push_callback);
	 */
	wrq->enqueue_counter = 0;
	__balance_callbacks(rq);
	if (wrq->enqueue_counter)
		WALT_BUG(WALT_BUG_WALT, NULL, "cpu: %d task was re-enqueued", cpu_of(rq));

	rq_unlock_irqrestore(rq, &rf);

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

/*
 * returns true if last halt is within threshold
 * note: do not take halt_lock, called from atomic context
 */
bool walt_halt_check_last(int cpu)
{
	u64 last_halt = per_cpu_ptr(&halt_state, cpu)->last_halt;

	/* last_halt is valid, check it against sched_clock */
	if (last_halt != 0 && sched_clock() - last_halt >  WALT_HALT_CHECK_THRESHOLD_NS)
		return false;

	return true;
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

struct task_struct *walt_drain_thread;

/*
 * 1) add the cpus to the halt mask
 * 2) migrate tasks off the cpu
 */
static int halt_cpus(struct cpumask *cpus)
{
	int cpu;
	int ret = 0;
	u64 start_time = sched_clock();
	struct halt_cpu_state *halt_cpu_state;
	unsigned long flags;

	trace_halt_cpus_start(cpus, 1);

	for_each_cpu(cpu, cpus) {

		if (cpu == cpumask_first(system_32bit_el0_cpumask())) {
			ret = -EINVAL;
			goto out;
		}

		halt_cpu_state = per_cpu_ptr(&halt_state, cpu);

		/* set the cpu as halted */
		cpumask_set_cpu(cpu, cpu_halt_mask);

		/* guarantee mask written before updating last_halt */
		wmb();

		halt_cpu_state->last_halt = start_time;
	}

	/* signal and wakeup the drain kthread */
	raw_spin_lock_irqsave(&walt_drain_pending_lock, flags);
	cpumask_or(&drain_data.cpus_to_drain, &drain_data.cpus_to_drain, cpus);
	raw_spin_unlock_irqrestore(&walt_drain_pending_lock, flags);

	wake_up_process(walt_drain_thread);

out:
	trace_halt_cpus(cpus, start_time, 1, ret);

	return ret;
}

/*
 * 1) remove the cpus from the halt mask
 *
 */
static int start_cpus(struct cpumask *cpus)
{
	u64 start_time = sched_clock();
	struct halt_cpu_state *halt_cpu_state;
	int cpu;

	trace_halt_cpus_start(cpus, 0);

	for_each_cpu(cpu, cpus) {
		halt_cpu_state = per_cpu_ptr(&halt_state, cpu);
		halt_cpu_state->last_halt = 0;
		wmb();

		/* wmb to guarantee zero'd last_halt before clearing from the mask */

		cpumask_clear_cpu(cpu, cpu_halt_mask);

		/* kick the cpu so it can pull tasks
		 * after the mask has been cleared.
		 */
		walt_smp_call_newidle_balance(cpu);
	}

	trace_halt_cpus(cpus, start_time, 0, 0);

	return 0;
}

/* update reason for cpus in yield/halt mask */
static void update_reasons(struct cpumask *cpus, bool halt, enum pause_reason reason)
{
	int cpu;
	struct halt_cpu_state *halt_cpu_state;

	for_each_cpu(cpu, cpus) {
		halt_cpu_state = per_cpu_ptr(&halt_state, cpu);
		if (halt)
			halt_cpu_state->reason |=  reason;
		else
			halt_cpu_state->reason &= ~reason;
	}
}

/* remove cpus that are already halted */
static void update_halt_cpus(struct cpumask *cpus)
{
	int cpu;
	struct halt_cpu_state *halt_cpu_state;

	for_each_cpu(cpu, cpus) {
		halt_cpu_state = per_cpu_ptr(&halt_state, cpu);
		if (halt_cpu_state->reason)
			cpumask_clear_cpu(cpu, cpus);
	}
}

/* cpus will be modified */
int walt_halt_cpus(struct cpumask *cpus, enum pause_reason reason)
{
	int ret = 0;
	cpumask_t requested_cpus;
	unsigned long flags;

	raw_spin_lock_irqsave(&halt_lock, flags);

	cpumask_copy(&requested_cpus, cpus);

	/* remove cpus that are already halted */
	update_halt_cpus(cpus);

	if (cpumask_empty(cpus)) {
		update_reasons(&requested_cpus, true, reason);
		goto unlock;
	}

	ret = halt_cpus(cpus);

	if (ret < 0)
		pr_debug("halt_cpus failure ret=%d cpus=%*pbl\n", ret,
			 cpumask_pr_args(&requested_cpus));
	else
		update_reasons(&requested_cpus, true, reason);
unlock:
	raw_spin_unlock_irqrestore(&halt_lock, flags);

	return ret;
}

int walt_pause_cpus(struct cpumask *cpus, enum pause_reason reason)
{
	if (walt_disabled)
		return -EAGAIN;
	return walt_halt_cpus(cpus, reason);
}
EXPORT_SYMBOL(walt_pause_cpus);

/* cpus will be modified */
int walt_start_cpus(struct cpumask *cpus, enum pause_reason reason)
{
	int ret = 0;
	cpumask_t requested_cpus;
	unsigned long flags;

	raw_spin_lock_irqsave(&halt_lock, flags);
	cpumask_copy(&requested_cpus, cpus);
	update_reasons(&requested_cpus, false, reason);

	/* remove cpus that should still be halted */
	update_halt_cpus(cpus);

	ret = start_cpus(cpus);

	if (ret < 0) {
		pr_debug("halt_cpus failure ret=%d cpus=%*pbl\n", ret,
			 cpumask_pr_args(&requested_cpus));
		/* restore/increment ref counts in case of error */
		update_reasons(&requested_cpus, true, reason);
	}

	raw_spin_unlock_irqrestore(&halt_lock, flags);

	return ret;
}

int walt_resume_cpus(struct cpumask *cpus, enum pause_reason reason)
{
	if (walt_disabled)
		return -EAGAIN;
	return walt_start_cpus(cpus, reason);
}
EXPORT_SYMBOL(walt_resume_cpus);

static void android_rvh_get_nohz_timer_target(void *unused, int *cpu, bool *done)
{
	int i, default_cpu = -1;
	struct sched_domain *sd;
	cpumask_t unhalted;

	*done = true;

	if (housekeeping_cpu(*cpu, HK_FLAG_TIMER) && !cpu_halted(*cpu)) {
		if (!available_idle_cpu(*cpu))
			return;
		default_cpu = *cpu;
	}

	rcu_read_lock();
	for_each_domain(*cpu, sd) {
		for_each_cpu_and(i, sched_domain_span(sd),
			housekeeping_cpumask(HK_FLAG_TIMER)) {
			if (*cpu == i)
				continue;

			if (!available_idle_cpu(i) && !cpu_halted(i)) {
				*cpu = i;
				goto unlock;
			}
		}
	}

	if (default_cpu == -1) {
		cpumask_complement(&unhalted, cpu_halt_mask);
		for_each_cpu_and(i, &unhalted,
				 housekeeping_cpumask(HK_FLAG_TIMER)) {
			if (*cpu == i)
				continue;

			if (!available_idle_cpu(i)) {
				*cpu = i;
				goto unlock;
			}
		}

		/* no active, non-halted, not-idle, choose any */
		default_cpu = cpumask_any(&unhalted);

		if (unlikely(default_cpu >= nr_cpu_ids))
			goto unlock;
	}

	*cpu = default_cpu;
unlock:
	rcu_read_unlock();
}

static void android_rvh_set_cpus_allowed_by_task(void *unused,
						 const struct cpumask *cpu_valid_mask,
						 const struct cpumask *new_mask,
						 struct task_struct *p,
						 unsigned int *dest_cpu)
{
	cpumask_t allowed_cpus;

	if (unlikely(walt_disabled))
		return;

	if (cpu_halted(*dest_cpu) && !p->migration_disabled) {
		/* remove halted cpus from the valid mask, and store locally */
		cpumask_andnot(&allowed_cpus, cpu_valid_mask, cpu_halt_mask);
		*dest_cpu = cpumask_any_and_distribute(&allowed_cpus, new_mask);
	}
}

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
 * Caveat: For 32 bit tasks that are being directed to a halted cpu, allow the halted cpu
 *         in a particular case (32 bit task, in execve, moving to a 32 bit cpu)
 *         This is to handle the call to is_cpu_allowed() from __migrate_task, in the
 *         event that a 32bit task is being execve'd.
 */
static void android_rvh_is_cpu_allowed(void *unused, struct task_struct *p, int cpu, bool *allowed)
{
	if (unlikely(walt_disabled))
		return;

	if (cpumask_test_cpu(cpu, cpu_halt_mask)) {

		/* default reject for any halted cpu */
		*allowed = false;

		if (is_compat_thread(task_thread_info(p)) &&
		    p->in_execve) {
			/*
			 * the task is 32 bit capable and
			 * the context is execve. allow this cpu.
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
