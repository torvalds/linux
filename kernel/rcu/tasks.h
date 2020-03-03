/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Task-based RCU implementations.
 *
 * Copyright (C) 2020 Paul E. McKenney
 */


////////////////////////////////////////////////////////////////////////
//
// Generic data structures.

struct rcu_tasks;
typedef void (*rcu_tasks_gp_func_t)(struct rcu_tasks *rtp);

/**
 * Definition for a Tasks-RCU-like mechanism.
 * @cbs_head: Head of callback list.
 * @cbs_tail: Tail pointer for callback list.
 * @cbs_wq: Wait queue allowning new callback to get kthread's attention.
 * @cbs_lock: Lock protecting callback list.
 * @kthread_ptr: This flavor's grace-period/callback-invocation kthread.
 * @gp_func: This flavor's grace-period-wait function.
 * @call_func: This flavor's call_rcu()-equivalent function.
 */
struct rcu_tasks {
	struct rcu_head *cbs_head;
	struct rcu_head **cbs_tail;
	struct wait_queue_head cbs_wq;
	raw_spinlock_t cbs_lock;
	struct task_struct *kthread_ptr;
	rcu_tasks_gp_func_t gp_func;
	call_rcu_func_t call_func;
};

#define DEFINE_RCU_TASKS(name, gp, call)				\
static struct rcu_tasks name =						\
{									\
	.cbs_tail = &name.cbs_head,					\
	.cbs_wq = __WAIT_QUEUE_HEAD_INITIALIZER(name.cbs_wq),		\
	.cbs_lock = __RAW_SPIN_LOCK_UNLOCKED(name.cbs_lock),		\
	.gp_func = gp,							\
	.call_func = call,						\
}

/* Track exiting tasks in order to allow them to be waited for. */
DEFINE_STATIC_SRCU(tasks_rcu_exit_srcu);

/* Control stall timeouts.  Disable with <= 0, otherwise jiffies till stall. */
#define RCU_TASK_STALL_TIMEOUT (HZ * 60 * 10)
static int rcu_task_stall_timeout __read_mostly = RCU_TASK_STALL_TIMEOUT;
module_param(rcu_task_stall_timeout, int, 0644);

////////////////////////////////////////////////////////////////////////
//
// Generic code.

// Enqueue a callback for the specified flavor of Tasks RCU.
static void call_rcu_tasks_generic(struct rcu_head *rhp, rcu_callback_t func,
				   struct rcu_tasks *rtp)
{
	unsigned long flags;
	bool needwake;

	rhp->next = NULL;
	rhp->func = func;
	raw_spin_lock_irqsave(&rtp->cbs_lock, flags);
	needwake = !rtp->cbs_head;
	WRITE_ONCE(*rtp->cbs_tail, rhp);
	rtp->cbs_tail = &rhp->next;
	raw_spin_unlock_irqrestore(&rtp->cbs_lock, flags);
	/* We can't create the thread unless interrupts are enabled. */
	if (needwake && READ_ONCE(rtp->kthread_ptr))
		wake_up(&rtp->cbs_wq);
}

// Wait for a grace period for the specified flavor of Tasks RCU.
static void synchronize_rcu_tasks_generic(struct rcu_tasks *rtp)
{
	/* Complain if the scheduler has not started.  */
	RCU_LOCKDEP_WARN(rcu_scheduler_active == RCU_SCHEDULER_INACTIVE,
			 "synchronize_rcu_tasks called too soon");

	/* Wait for the grace period. */
	wait_rcu_gp(rtp->call_func);
}

/* RCU-tasks kthread that detects grace periods and invokes callbacks. */
static int __noreturn rcu_tasks_kthread(void *arg)
{
	unsigned long flags;
	struct rcu_head *list;
	struct rcu_head *next;
	struct rcu_tasks *rtp = arg;

	/* Run on housekeeping CPUs by default.  Sysadm can move if desired. */
	housekeeping_affine(current, HK_FLAG_RCU);
	WRITE_ONCE(rtp->kthread_ptr, current); // Let GPs start!

	/*
	 * Each pass through the following loop makes one check for
	 * newly arrived callbacks, and, if there are some, waits for
	 * one RCU-tasks grace period and then invokes the callbacks.
	 * This loop is terminated by the system going down.  ;-)
	 */
	for (;;) {

		/* Pick up any new callbacks. */
		raw_spin_lock_irqsave(&rtp->cbs_lock, flags);
		list = rtp->cbs_head;
		rtp->cbs_head = NULL;
		rtp->cbs_tail = &rtp->cbs_head;
		raw_spin_unlock_irqrestore(&rtp->cbs_lock, flags);

		/* If there were none, wait a bit and start over. */
		if (!list) {
			wait_event_interruptible(rtp->cbs_wq,
						 READ_ONCE(rtp->cbs_head));
			if (!rtp->cbs_head) {
				WARN_ON(signal_pending(current));
				schedule_timeout_interruptible(HZ/10);
			}
			continue;
		}

		// Wait for one grace period.
		rtp->gp_func(rtp);

		/* Invoke the callbacks. */
		while (list) {
			next = list->next;
			local_bh_disable();
			list->func(list);
			local_bh_enable();
			list = next;
			cond_resched();
		}
		/* Paranoid sleep to keep this from entering a tight loop */
		schedule_timeout_uninterruptible(HZ/10);
	}
}

/* Spawn RCU-tasks grace-period kthread, e.g., at core_initcall() time. */
static void __init rcu_spawn_tasks_kthread_generic(struct rcu_tasks *rtp)
{
	struct task_struct *t;

	t = kthread_run(rcu_tasks_kthread, rtp, "rcu_tasks_kthread");
	if (WARN_ONCE(IS_ERR(t), "%s: Could not start Tasks-RCU grace-period kthread, OOM is now expected behavior\n", __func__))
		return;
	smp_mb(); /* Ensure others see full kthread. */
}

/* Do the srcu_read_lock() for the above synchronize_srcu().  */
void exit_tasks_rcu_start(void) __acquires(&tasks_rcu_exit_srcu)
{
	preempt_disable();
	current->rcu_tasks_idx = __srcu_read_lock(&tasks_rcu_exit_srcu);
	preempt_enable();
}

/* Do the srcu_read_unlock() for the above synchronize_srcu().  */
void exit_tasks_rcu_finish(void) __releases(&tasks_rcu_exit_srcu)
{
	preempt_disable();
	__srcu_read_unlock(&tasks_rcu_exit_srcu, current->rcu_tasks_idx);
	preempt_enable();
}

#ifndef CONFIG_TINY_RCU

/*
 * Print any non-default Tasks RCU settings.
 */
static void __init rcu_tasks_bootup_oddness(void)
{
#ifdef CONFIG_TASKS_RCU
	if (rcu_task_stall_timeout != RCU_TASK_STALL_TIMEOUT)
		pr_info("\tTasks-RCU CPU stall warnings timeout set to %d (rcu_task_stall_timeout).\n", rcu_task_stall_timeout);
	else
		pr_info("\tTasks RCU enabled.\n");
#endif /* #ifdef CONFIG_TASKS_RCU */
}

#endif /* #ifndef CONFIG_TINY_RCU */

#ifdef CONFIG_TASKS_RCU

////////////////////////////////////////////////////////////////////////
//
// Simple variant of RCU whose quiescent states are voluntary context
// switch, cond_resched_rcu_qs(), user-space execution, and idle.
// As such, grace periods can take one good long time.  There are no
// read-side primitives similar to rcu_read_lock() and rcu_read_unlock()
// because this implementation is intended to get the system into a safe
// state for some of the manipulations involved in tracing and the like.
// Finally, this implementation does not support high call_rcu_tasks()
// rates from multiple CPUs.  If this is required, per-CPU callback lists
// will be needed.

/* See if tasks are still holding out, complain if so. */
static void check_holdout_task(struct task_struct *t,
			       bool needreport, bool *firstreport)
{
	int cpu;

	if (!READ_ONCE(t->rcu_tasks_holdout) ||
	    t->rcu_tasks_nvcsw != READ_ONCE(t->nvcsw) ||
	    !READ_ONCE(t->on_rq) ||
	    (IS_ENABLED(CONFIG_NO_HZ_FULL) &&
	     !is_idle_task(t) && t->rcu_tasks_idle_cpu >= 0)) {
		WRITE_ONCE(t->rcu_tasks_holdout, false);
		list_del_init(&t->rcu_tasks_holdout_list);
		put_task_struct(t);
		return;
	}
	rcu_request_urgent_qs_task(t);
	if (!needreport)
		return;
	if (*firstreport) {
		pr_err("INFO: rcu_tasks detected stalls on tasks:\n");
		*firstreport = false;
	}
	cpu = task_cpu(t);
	pr_alert("%p: %c%c nvcsw: %lu/%lu holdout: %d idle_cpu: %d/%d\n",
		 t, ".I"[is_idle_task(t)],
		 "N."[cpu < 0 || !tick_nohz_full_cpu(cpu)],
		 t->rcu_tasks_nvcsw, t->nvcsw, t->rcu_tasks_holdout,
		 t->rcu_tasks_idle_cpu, cpu);
	sched_show_task(t);
}

/* Wait for one RCU-tasks grace period. */
static void rcu_tasks_wait_gp(struct rcu_tasks *rtp)
{
	struct task_struct *g, *t;
	unsigned long lastreport;
	LIST_HEAD(rcu_tasks_holdouts);
	int fract;

	/*
	 * Wait for all pre-existing t->on_rq and t->nvcsw transitions
	 * to complete.  Invoking synchronize_rcu() suffices because all
	 * these transitions occur with interrupts disabled.  Without this
	 * synchronize_rcu(), a read-side critical section that started
	 * before the grace period might be incorrectly seen as having
	 * started after the grace period.
	 *
	 * This synchronize_rcu() also dispenses with the need for a
	 * memory barrier on the first store to t->rcu_tasks_holdout,
	 * as it forces the store to happen after the beginning of the
	 * grace period.
	 */
	synchronize_rcu();

	/*
	 * There were callbacks, so we need to wait for an RCU-tasks
	 * grace period.  Start off by scanning the task list for tasks
	 * that are not already voluntarily blocked.  Mark these tasks
	 * and make a list of them in rcu_tasks_holdouts.
	 */
	rcu_read_lock();
	for_each_process_thread(g, t) {
		if (t != current && READ_ONCE(t->on_rq) && !is_idle_task(t)) {
			get_task_struct(t);
			t->rcu_tasks_nvcsw = READ_ONCE(t->nvcsw);
			WRITE_ONCE(t->rcu_tasks_holdout, true);
			list_add(&t->rcu_tasks_holdout_list,
				 &rcu_tasks_holdouts);
		}
	}
	rcu_read_unlock();

	/*
	 * Wait for tasks that are in the process of exiting.  This
	 * does only part of the job, ensuring that all tasks that were
	 * previously exiting reach the point where they have disabled
	 * preemption, allowing the later synchronize_rcu() to finish
	 * the job.
	 */
	synchronize_srcu(&tasks_rcu_exit_srcu);

	/*
	 * Each pass through the following loop scans the list of holdout
	 * tasks, removing any that are no longer holdouts.  When the list
	 * is empty, we are done.
	 */
	lastreport = jiffies;

	/* Start off with HZ/10 wait and slowly back off to 1 HZ wait. */
	fract = 10;

	for (;;) {
		bool firstreport;
		bool needreport;
		int rtst;
		struct task_struct *t1;

		if (list_empty(&rcu_tasks_holdouts))
			break;

		/* Slowly back off waiting for holdouts */
		schedule_timeout_interruptible(HZ/fract);

		if (fract > 1)
			fract--;

		rtst = READ_ONCE(rcu_task_stall_timeout);
		needreport = rtst > 0 && time_after(jiffies, lastreport + rtst);
		if (needreport)
			lastreport = jiffies;
		firstreport = true;
		WARN_ON(signal_pending(current));
		list_for_each_entry_safe(t, t1, &rcu_tasks_holdouts,
					 rcu_tasks_holdout_list) {
			check_holdout_task(t, needreport, &firstreport);
			cond_resched();
		}
	}

	/*
	 * Because ->on_rq and ->nvcsw are not guaranteed to have a full
	 * memory barriers prior to them in the schedule() path, memory
	 * reordering on other CPUs could cause their RCU-tasks read-side
	 * critical sections to extend past the end of the grace period.
	 * However, because these ->nvcsw updates are carried out with
	 * interrupts disabled, we can use synchronize_rcu() to force the
	 * needed ordering on all such CPUs.
	 *
	 * This synchronize_rcu() also confines all ->rcu_tasks_holdout
	 * accesses to be within the grace period, avoiding the need for
	 * memory barriers for ->rcu_tasks_holdout accesses.
	 *
	 * In addition, this synchronize_rcu() waits for exiting tasks
	 * to complete their final preempt_disable() region of execution,
	 * cleaning up after the synchronize_srcu() above.
	 */
	synchronize_rcu();
}

void call_rcu_tasks(struct rcu_head *rhp, rcu_callback_t func);
DEFINE_RCU_TASKS(rcu_tasks, rcu_tasks_wait_gp, call_rcu_tasks);

/**
 * call_rcu_tasks() - Queue an RCU for invocation task-based grace period
 * @rhp: structure to be used for queueing the RCU updates.
 * @func: actual callback function to be invoked after the grace period
 *
 * The callback function will be invoked some time after a full grace
 * period elapses, in other words after all currently executing RCU
 * read-side critical sections have completed. call_rcu_tasks() assumes
 * that the read-side critical sections end at a voluntary context
 * switch (not a preemption!), cond_resched_rcu_qs(), entry into idle,
 * or transition to usermode execution.  As such, there are no read-side
 * primitives analogous to rcu_read_lock() and rcu_read_unlock() because
 * this primitive is intended to determine that all tasks have passed
 * through a safe state, not so much for data-strcuture synchronization.
 *
 * See the description of call_rcu() for more detailed information on
 * memory ordering guarantees.
 */
void call_rcu_tasks(struct rcu_head *rhp, rcu_callback_t func)
{
	call_rcu_tasks_generic(rhp, func, &rcu_tasks);
}
EXPORT_SYMBOL_GPL(call_rcu_tasks);

/**
 * synchronize_rcu_tasks - wait until an rcu-tasks grace period has elapsed.
 *
 * Control will return to the caller some time after a full rcu-tasks
 * grace period has elapsed, in other words after all currently
 * executing rcu-tasks read-side critical sections have elapsed.  These
 * read-side critical sections are delimited by calls to schedule(),
 * cond_resched_tasks_rcu_qs(), idle execution, userspace execution, calls
 * to synchronize_rcu_tasks(), and (in theory, anyway) cond_resched().
 *
 * This is a very specialized primitive, intended only for a few uses in
 * tracing and other situations requiring manipulation of function
 * preambles and profiling hooks.  The synchronize_rcu_tasks() function
 * is not (yet) intended for heavy use from multiple CPUs.
 *
 * See the description of synchronize_rcu() for more detailed information
 * on memory ordering guarantees.
 */
void synchronize_rcu_tasks(void)
{
	synchronize_rcu_tasks_generic(&rcu_tasks);
}
EXPORT_SYMBOL_GPL(synchronize_rcu_tasks);

/**
 * rcu_barrier_tasks - Wait for in-flight call_rcu_tasks() callbacks.
 *
 * Although the current implementation is guaranteed to wait, it is not
 * obligated to, for example, if there are no pending callbacks.
 */
void rcu_barrier_tasks(void)
{
	/* There is only one callback queue, so this is easy.  ;-) */
	synchronize_rcu_tasks();
}
EXPORT_SYMBOL_GPL(rcu_barrier_tasks);

static int __init rcu_spawn_tasks_kthread(void)
{
	rcu_spawn_tasks_kthread_generic(&rcu_tasks);
	return 0;
}
core_initcall(rcu_spawn_tasks_kthread);

#endif /* #ifdef CONFIG_TASKS_RCU */
