/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Task-based RCU implementations.
 *
 * Copyright (C) 2020 Paul E. McKenney
 */

#ifdef CONFIG_TASKS_RCU_GENERIC

////////////////////////////////////////////////////////////////////////
//
// Generic data structures.

struct rcu_tasks;
typedef void (*rcu_tasks_gp_func_t)(struct rcu_tasks *rtp);
typedef void (*pregp_func_t)(void);
typedef void (*pertask_func_t)(struct task_struct *t, struct list_head *hop);
typedef void (*postscan_func_t)(void);
typedef void (*holdouts_func_t)(struct list_head *hop, bool ndrpt, bool *frptp);
typedef void (*postgp_func_t)(struct rcu_tasks *rtp);

/**
 * Definition for a Tasks-RCU-like mechanism.
 * @cbs_head: Head of callback list.
 * @cbs_tail: Tail pointer for callback list.
 * @cbs_wq: Wait queue allowning new callback to get kthread's attention.
 * @cbs_lock: Lock protecting callback list.
 * @kthread_ptr: This flavor's grace-period/callback-invocation kthread.
 * @gp_func: This flavor's grace-period-wait function.
 * @gp_state: Grace period's most recent state transition (debugging).
 * @gp_jiffies: Time of last @gp_state transition.
 * @gp_start: Most recent grace-period start in jiffies.
 * @n_gps: Number of grace periods completed since boot.
 * @n_ipis: Number of IPIs sent to encourage grace periods to end.
 * @pregp_func: This flavor's pre-grace-period function (optional).
 * @pertask_func: This flavor's per-task scan function (optional).
 * @postscan_func: This flavor's post-task scan function (optional).
 * @holdout_func: This flavor's holdout-list scan function (optional).
 * @postgp_func: This flavor's post-grace-period function (optional).
 * @call_func: This flavor's call_rcu()-equivalent function.
 * @name: This flavor's textual name.
 * @kname: This flavor's kthread name.
 */
struct rcu_tasks {
	struct rcu_head *cbs_head;
	struct rcu_head **cbs_tail;
	struct wait_queue_head cbs_wq;
	raw_spinlock_t cbs_lock;
	int gp_state;
	unsigned long gp_jiffies;
	unsigned long gp_start;
	unsigned long n_gps;
	unsigned long n_ipis;
	struct task_struct *kthread_ptr;
	rcu_tasks_gp_func_t gp_func;
	pregp_func_t pregp_func;
	pertask_func_t pertask_func;
	postscan_func_t postscan_func;
	holdouts_func_t holdouts_func;
	postgp_func_t postgp_func;
	call_rcu_func_t call_func;
	char *name;
	char *kname;
};

#define DEFINE_RCU_TASKS(rt_name, gp, call, n)				\
static struct rcu_tasks rt_name =					\
{									\
	.cbs_tail = &rt_name.cbs_head,					\
	.cbs_wq = __WAIT_QUEUE_HEAD_INITIALIZER(rt_name.cbs_wq),	\
	.cbs_lock = __RAW_SPIN_LOCK_UNLOCKED(rt_name.cbs_lock),		\
	.gp_func = gp,							\
	.call_func = call,						\
	.name = n,							\
	.kname = #rt_name,						\
}

/* Track exiting tasks in order to allow them to be waited for. */
DEFINE_STATIC_SRCU(tasks_rcu_exit_srcu);

/* Avoid IPIing CPUs early in the grace period. */
#define RCU_TASK_IPI_DELAY (HZ / 2)
static int rcu_task_ipi_delay __read_mostly = RCU_TASK_IPI_DELAY;
module_param(rcu_task_ipi_delay, int, 0644);

/* Control stall timeouts.  Disable with <= 0, otherwise jiffies till stall. */
#define RCU_TASK_STALL_TIMEOUT (HZ * 60 * 10)
static int rcu_task_stall_timeout __read_mostly = RCU_TASK_STALL_TIMEOUT;
module_param(rcu_task_stall_timeout, int, 0644);

/* RCU tasks grace-period state for debugging. */
#define RTGS_INIT		 0
#define RTGS_WAIT_WAIT_CBS	 1
#define RTGS_WAIT_GP		 2
#define RTGS_PRE_WAIT_GP	 3
#define RTGS_SCAN_TASKLIST	 4
#define RTGS_POST_SCAN_TASKLIST	 5
#define RTGS_WAIT_SCAN_HOLDOUTS	 6
#define RTGS_SCAN_HOLDOUTS	 7
#define RTGS_POST_GP		 8
#define RTGS_WAIT_READERS	 9
#define RTGS_INVOKE_CBS		10
#define RTGS_WAIT_CBS		11
static const char * const rcu_tasks_gp_state_names[] = {
	"RTGS_INIT",
	"RTGS_WAIT_WAIT_CBS",
	"RTGS_WAIT_GP",
	"RTGS_PRE_WAIT_GP",
	"RTGS_SCAN_TASKLIST",
	"RTGS_POST_SCAN_TASKLIST",
	"RTGS_WAIT_SCAN_HOLDOUTS",
	"RTGS_SCAN_HOLDOUTS",
	"RTGS_POST_GP",
	"RTGS_WAIT_READERS",
	"RTGS_INVOKE_CBS",
	"RTGS_WAIT_CBS",
};

////////////////////////////////////////////////////////////////////////
//
// Generic code.

/* Record grace-period phase and time. */
static void set_tasks_gp_state(struct rcu_tasks *rtp, int newstate)
{
	rtp->gp_state = newstate;
	rtp->gp_jiffies = jiffies;
}

/* Return state name. */
static const char *tasks_gp_state_getname(struct rcu_tasks *rtp)
{
	int i = data_race(rtp->gp_state); // Let KCSAN detect update races
	int j = READ_ONCE(i); // Prevent the compiler from reading twice

	if (j >= ARRAY_SIZE(rcu_tasks_gp_state_names))
		return "???";
	return rcu_tasks_gp_state_names[j];
}

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
		smp_mb__after_spinlock(); // Order updates vs. GP.
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
				set_tasks_gp_state(rtp, RTGS_WAIT_WAIT_CBS);
				schedule_timeout_interruptible(HZ/10);
			}
			continue;
		}

		// Wait for one grace period.
		set_tasks_gp_state(rtp, RTGS_WAIT_GP);
		rtp->gp_start = jiffies;
		rtp->gp_func(rtp);
		rtp->n_gps++;

		/* Invoke the callbacks. */
		set_tasks_gp_state(rtp, RTGS_INVOKE_CBS);
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

		set_tasks_gp_state(rtp, RTGS_WAIT_CBS);
	}
}

/* Spawn RCU-tasks grace-period kthread, e.g., at core_initcall() time. */
static void __init rcu_spawn_tasks_kthread_generic(struct rcu_tasks *rtp)
{
	struct task_struct *t;

	t = kthread_run(rcu_tasks_kthread, rtp, "%s_kthread", rtp->kname);
	if (WARN_ONCE(IS_ERR(t), "%s: Could not start %s grace-period kthread, OOM is now expected behavior\n", __func__, rtp->name))
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

static void exit_tasks_rcu_finish_trace(struct task_struct *t);

/* Do the srcu_read_unlock() for the above synchronize_srcu().  */
void exit_tasks_rcu_finish(void) __releases(&tasks_rcu_exit_srcu)
{
	struct task_struct *t = current;

	preempt_disable();
	__srcu_read_unlock(&tasks_rcu_exit_srcu, t->rcu_tasks_idx);
	preempt_enable();
	exit_tasks_rcu_finish_trace(t);
}

#ifndef CONFIG_TINY_RCU

/*
 * Print any non-default Tasks RCU settings.
 */
static void __init rcu_tasks_bootup_oddness(void)
{
#if defined(CONFIG_TASKS_RCU) || defined(CONFIG_TASKS_TRACE_RCU)
	if (rcu_task_stall_timeout != RCU_TASK_STALL_TIMEOUT)
		pr_info("\tTasks-RCU CPU stall warnings timeout set to %d (rcu_task_stall_timeout).\n", rcu_task_stall_timeout);
#endif /* #ifdef CONFIG_TASKS_RCU */
#ifdef CONFIG_TASKS_RCU
	pr_info("\tTrampoline variant of Tasks RCU enabled.\n");
#endif /* #ifdef CONFIG_TASKS_RCU */
#ifdef CONFIG_TASKS_RUDE_RCU
	pr_info("\tRude variant of Tasks RCU enabled.\n");
#endif /* #ifdef CONFIG_TASKS_RUDE_RCU */
#ifdef CONFIG_TASKS_TRACE_RCU
	pr_info("\tTracing variant of Tasks RCU enabled.\n");
#endif /* #ifdef CONFIG_TASKS_TRACE_RCU */
}

#endif /* #ifndef CONFIG_TINY_RCU */

/* Dump out rcutorture-relevant state common to all RCU-tasks flavors. */
static void show_rcu_tasks_generic_gp_kthread(struct rcu_tasks *rtp, char *s)
{
	pr_info("%s: %s(%d) since %lu g:%lu i:%lu %c%c %s\n",
		rtp->kname,
		tasks_gp_state_getname(rtp),
		data_race(rtp->gp_state),
		jiffies - data_race(rtp->gp_jiffies),
		data_race(rtp->n_gps), data_race(rtp->n_ipis),
		".k"[!!data_race(rtp->kthread_ptr)],
		".C"[!!data_race(rtp->cbs_head)],
		s);
}

#ifdef CONFIG_TASKS_RCU

////////////////////////////////////////////////////////////////////////
//
// Shared code between task-list-scanning variants of Tasks RCU.

/* Wait for one RCU-tasks grace period. */
static void rcu_tasks_wait_gp(struct rcu_tasks *rtp)
{
	struct task_struct *g, *t;
	unsigned long lastreport;
	LIST_HEAD(holdouts);
	int fract;

	set_tasks_gp_state(rtp, RTGS_PRE_WAIT_GP);
	rtp->pregp_func();

	/*
	 * There were callbacks, so we need to wait for an RCU-tasks
	 * grace period.  Start off by scanning the task list for tasks
	 * that are not already voluntarily blocked.  Mark these tasks
	 * and make a list of them in holdouts.
	 */
	set_tasks_gp_state(rtp, RTGS_SCAN_TASKLIST);
	rcu_read_lock();
	for_each_process_thread(g, t)
		rtp->pertask_func(t, &holdouts);
	rcu_read_unlock();

	set_tasks_gp_state(rtp, RTGS_POST_SCAN_TASKLIST);
	rtp->postscan_func();

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

		if (list_empty(&holdouts))
			break;

		/* Slowly back off waiting for holdouts */
		set_tasks_gp_state(rtp, RTGS_WAIT_SCAN_HOLDOUTS);
		schedule_timeout_interruptible(HZ/fract);

		if (fract > 1)
			fract--;

		rtst = READ_ONCE(rcu_task_stall_timeout);
		needreport = rtst > 0 && time_after(jiffies, lastreport + rtst);
		if (needreport)
			lastreport = jiffies;
		firstreport = true;
		WARN_ON(signal_pending(current));
		set_tasks_gp_state(rtp, RTGS_SCAN_HOLDOUTS);
		rtp->holdouts_func(&holdouts, needreport, &firstreport);
	}

	set_tasks_gp_state(rtp, RTGS_POST_GP);
	rtp->postgp_func(rtp);
}

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

/* Pre-grace-period preparation. */
static void rcu_tasks_pregp_step(void)
{
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
}

/* Per-task initial processing. */
static void rcu_tasks_pertask(struct task_struct *t, struct list_head *hop)
{
	if (t != current && READ_ONCE(t->on_rq) && !is_idle_task(t)) {
		get_task_struct(t);
		t->rcu_tasks_nvcsw = READ_ONCE(t->nvcsw);
		WRITE_ONCE(t->rcu_tasks_holdout, true);
		list_add(&t->rcu_tasks_holdout_list, hop);
	}
}

/* Processing between scanning taskslist and draining the holdout list. */
void rcu_tasks_postscan(void)
{
	/*
	 * Wait for tasks that are in the process of exiting.  This
	 * does only part of the job, ensuring that all tasks that were
	 * previously exiting reach the point where they have disabled
	 * preemption, allowing the later synchronize_rcu() to finish
	 * the job.
	 */
	synchronize_srcu(&tasks_rcu_exit_srcu);
}

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

/* Scan the holdout lists for tasks no longer holding out. */
static void check_all_holdout_tasks(struct list_head *hop,
				    bool needreport, bool *firstreport)
{
	struct task_struct *t, *t1;

	list_for_each_entry_safe(t, t1, hop, rcu_tasks_holdout_list) {
		check_holdout_task(t, needreport, firstreport);
		cond_resched();
	}
}

/* Finish off the Tasks-RCU grace period. */
static void rcu_tasks_postgp(struct rcu_tasks *rtp)
{
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
DEFINE_RCU_TASKS(rcu_tasks, rcu_tasks_wait_gp, call_rcu_tasks, "RCU Tasks");

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
	rcu_tasks.pregp_func = rcu_tasks_pregp_step;
	rcu_tasks.pertask_func = rcu_tasks_pertask;
	rcu_tasks.postscan_func = rcu_tasks_postscan;
	rcu_tasks.holdouts_func = check_all_holdout_tasks;
	rcu_tasks.postgp_func = rcu_tasks_postgp;
	rcu_spawn_tasks_kthread_generic(&rcu_tasks);
	return 0;
}
core_initcall(rcu_spawn_tasks_kthread);

static void show_rcu_tasks_classic_gp_kthread(void)
{
	show_rcu_tasks_generic_gp_kthread(&rcu_tasks, "");
}

#else /* #ifdef CONFIG_TASKS_RCU */
static void show_rcu_tasks_classic_gp_kthread(void) { }
#endif /* #else #ifdef CONFIG_TASKS_RCU */

#ifdef CONFIG_TASKS_RUDE_RCU

////////////////////////////////////////////////////////////////////////
//
// "Rude" variant of Tasks RCU, inspired by Steve Rostedt's trick of
// passing an empty function to schedule_on_each_cpu().  This approach
// provides an asynchronous call_rcu_tasks_rude() API and batching
// of concurrent calls to the synchronous synchronize_rcu_rude() API.
// This sends IPIs far and wide and induces otherwise unnecessary context
// switches on all online CPUs, whether idle or not.

// Empty function to allow workqueues to force a context switch.
static void rcu_tasks_be_rude(struct work_struct *work)
{
}

// Wait for one rude RCU-tasks grace period.
static void rcu_tasks_rude_wait_gp(struct rcu_tasks *rtp)
{
	rtp->n_ipis += cpumask_weight(cpu_online_mask);
	schedule_on_each_cpu(rcu_tasks_be_rude);
}

void call_rcu_tasks_rude(struct rcu_head *rhp, rcu_callback_t func);
DEFINE_RCU_TASKS(rcu_tasks_rude, rcu_tasks_rude_wait_gp, call_rcu_tasks_rude,
		 "RCU Tasks Rude");

/**
 * call_rcu_tasks_rude() - Queue a callback rude task-based grace period
 * @rhp: structure to be used for queueing the RCU updates.
 * @func: actual callback function to be invoked after the grace period
 *
 * The callback function will be invoked some time after a full grace
 * period elapses, in other words after all currently executing RCU
 * read-side critical sections have completed. call_rcu_tasks_rude()
 * assumes that the read-side critical sections end at context switch,
 * cond_resched_rcu_qs(), or transition to usermode execution.  As such,
 * there are no read-side primitives analogous to rcu_read_lock() and
 * rcu_read_unlock() because this primitive is intended to determine
 * that all tasks have passed through a safe state, not so much for
 * data-strcuture synchronization.
 *
 * See the description of call_rcu() for more detailed information on
 * memory ordering guarantees.
 */
void call_rcu_tasks_rude(struct rcu_head *rhp, rcu_callback_t func)
{
	call_rcu_tasks_generic(rhp, func, &rcu_tasks_rude);
}
EXPORT_SYMBOL_GPL(call_rcu_tasks_rude);

/**
 * synchronize_rcu_tasks_rude - wait for a rude rcu-tasks grace period
 *
 * Control will return to the caller some time after a rude rcu-tasks
 * grace period has elapsed, in other words after all currently
 * executing rcu-tasks read-side critical sections have elapsed.  These
 * read-side critical sections are delimited by calls to schedule(),
 * cond_resched_tasks_rcu_qs(), userspace execution, and (in theory,
 * anyway) cond_resched().
 *
 * This is a very specialized primitive, intended only for a few uses in
 * tracing and other situations requiring manipulation of function preambles
 * and profiling hooks.  The synchronize_rcu_tasks_rude() function is not
 * (yet) intended for heavy use from multiple CPUs.
 *
 * See the description of synchronize_rcu() for more detailed information
 * on memory ordering guarantees.
 */
void synchronize_rcu_tasks_rude(void)
{
	synchronize_rcu_tasks_generic(&rcu_tasks_rude);
}
EXPORT_SYMBOL_GPL(synchronize_rcu_tasks_rude);

/**
 * rcu_barrier_tasks_rude - Wait for in-flight call_rcu_tasks_rude() callbacks.
 *
 * Although the current implementation is guaranteed to wait, it is not
 * obligated to, for example, if there are no pending callbacks.
 */
void rcu_barrier_tasks_rude(void)
{
	/* There is only one callback queue, so this is easy.  ;-) */
	synchronize_rcu_tasks_rude();
}
EXPORT_SYMBOL_GPL(rcu_barrier_tasks_rude);

static int __init rcu_spawn_tasks_rude_kthread(void)
{
	rcu_spawn_tasks_kthread_generic(&rcu_tasks_rude);
	return 0;
}
core_initcall(rcu_spawn_tasks_rude_kthread);

static void show_rcu_tasks_rude_gp_kthread(void)
{
	show_rcu_tasks_generic_gp_kthread(&rcu_tasks_rude, "");
}

#else /* #ifdef CONFIG_TASKS_RUDE_RCU */
static void show_rcu_tasks_rude_gp_kthread(void) {}
#endif /* #else #ifdef CONFIG_TASKS_RUDE_RCU */

////////////////////////////////////////////////////////////////////////
//
// Tracing variant of Tasks RCU.  This variant is designed to be used
// to protect tracing hooks, including those of BPF.  This variant
// therefore:
//
// 1.	Has explicit read-side markers to allow finite grace periods
//	in the face of in-kernel loops for PREEMPT=n builds.
//
// 2.	Protects code in the idle loop, exception entry/exit, and
//	CPU-hotplug code paths, similar to the capabilities of SRCU.
//
// 3.	Avoids expensive read-side instruction, having overhead similar
//	to that of Preemptible RCU.
//
// There are of course downsides.  The grace-period code can send IPIs to
// CPUs, even when those CPUs are in the idle loop or in nohz_full userspace.
// It is necessary to scan the full tasklist, much as for Tasks RCU.  There
// is a single callback queue guarded by a single lock, again, much as for
// Tasks RCU.  If needed, these downsides can be at least partially remedied.
//
// Perhaps most important, this variant of RCU does not affect the vanilla
// flavors, rcu_preempt and rcu_sched.  The fact that RCU Tasks Trace
// readers can operate from idle, offline, and exception entry/exit in no
// way allows rcu_preempt and rcu_sched readers to also do so.

// The lockdep state must be outside of #ifdef to be useful.
#ifdef CONFIG_DEBUG_LOCK_ALLOC
static struct lock_class_key rcu_lock_trace_key;
struct lockdep_map rcu_trace_lock_map =
	STATIC_LOCKDEP_MAP_INIT("rcu_read_lock_trace", &rcu_lock_trace_key);
EXPORT_SYMBOL_GPL(rcu_trace_lock_map);
#endif /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

#ifdef CONFIG_TASKS_TRACE_RCU

atomic_t trc_n_readers_need_end;	// Number of waited-for readers.
DECLARE_WAIT_QUEUE_HEAD(trc_wait);	// List of holdout tasks.

// Record outstanding IPIs to each CPU.  No point in sending two...
static DEFINE_PER_CPU(bool, trc_ipi_to_cpu);

void call_rcu_tasks_trace(struct rcu_head *rhp, rcu_callback_t func);
DEFINE_RCU_TASKS(rcu_tasks_trace, rcu_tasks_wait_gp, call_rcu_tasks_trace,
		 "RCU Tasks Trace");

/*
 * This irq_work handler allows rcu_read_unlock_trace() to be invoked
 * while the scheduler locks are held.
 */
static void rcu_read_unlock_iw(struct irq_work *iwp)
{
	wake_up(&trc_wait);
}
static DEFINE_IRQ_WORK(rcu_tasks_trace_iw, rcu_read_unlock_iw);

/* If we are the last reader, wake up the grace-period kthread. */
void rcu_read_unlock_trace_special(struct task_struct *t, int nesting)
{
	int nq = t->trc_reader_special.b.need_qs;

	if (IS_ENABLED(CONFIG_TASKS_TRACE_RCU_READ_MB) &&
	    t->trc_reader_special.b.need_mb)
		smp_mb(); // Pairs with update-side barriers.
	// Update .need_qs before ->trc_reader_nesting for irq/NMI handlers.
	if (nq)
		WRITE_ONCE(t->trc_reader_special.b.need_qs, false);
	WRITE_ONCE(t->trc_reader_nesting, nesting);
	if (nq && atomic_dec_and_test(&trc_n_readers_need_end))
		irq_work_queue(&rcu_tasks_trace_iw);
}
EXPORT_SYMBOL_GPL(rcu_read_unlock_trace_special);

/* Add a task to the holdout list, if it is not already on the list. */
static void trc_add_holdout(struct task_struct *t, struct list_head *bhp)
{
	if (list_empty(&t->trc_holdout_list)) {
		get_task_struct(t);
		list_add(&t->trc_holdout_list, bhp);
	}
}

/* Remove a task from the holdout list, if it is in fact present. */
static void trc_del_holdout(struct task_struct *t)
{
	if (!list_empty(&t->trc_holdout_list)) {
		list_del_init(&t->trc_holdout_list);
		put_task_struct(t);
	}
}

/* IPI handler to check task state. */
static void trc_read_check_handler(void *t_in)
{
	struct task_struct *t = current;
	struct task_struct *texp = t_in;

	// If the task is no longer running on this CPU, leave.
	if (unlikely(texp != t)) {
		if (WARN_ON_ONCE(atomic_dec_and_test(&trc_n_readers_need_end)))
			wake_up(&trc_wait);
		goto reset_ipi; // Already on holdout list, so will check later.
	}

	// If the task is not in a read-side critical section, and
	// if this is the last reader, awaken the grace-period kthread.
	if (likely(!t->trc_reader_nesting)) {
		if (WARN_ON_ONCE(atomic_dec_and_test(&trc_n_readers_need_end)))
			wake_up(&trc_wait);
		// Mark as checked after decrement to avoid false
		// positives on the above WARN_ON_ONCE().
		WRITE_ONCE(t->trc_reader_checked, true);
		goto reset_ipi;
	}
	WRITE_ONCE(t->trc_reader_checked, true);

	// Get here if the task is in a read-side critical section.  Set
	// its state so that it will awaken the grace-period kthread upon
	// exit from that critical section.
	WARN_ON_ONCE(t->trc_reader_special.b.need_qs);
	WRITE_ONCE(t->trc_reader_special.b.need_qs, true);

reset_ipi:
	// Allow future IPIs to be sent on CPU and for task.
	// Also order this IPI handler against any later manipulations of
	// the intended task.
	smp_store_release(&per_cpu(trc_ipi_to_cpu, smp_processor_id()), false); // ^^^
	smp_store_release(&texp->trc_ipi_to_cpu, -1); // ^^^
}

/* Callback function for scheduler to check locked-down task.  */
static bool trc_inspect_reader(struct task_struct *t, void *arg)
{
	int cpu = task_cpu(t);
	bool in_qs = false;

	if (task_curr(t)) {
		// If no chance of heavyweight readers, do it the hard way.
		if (!IS_ENABLED(CONFIG_TASKS_TRACE_RCU_READ_MB))
			return false;

		// If heavyweight readers are enabled on the remote task,
		// we can inspect its state despite its currently running.
		// However, we cannot safely change its state.
		if (!rcu_dynticks_zero_in_eqs(cpu, &t->trc_reader_nesting))
			return false; // No quiescent state, do it the hard way.
		in_qs = true;
	} else {
		in_qs = likely(!t->trc_reader_nesting);
	}

	// Mark as checked.  Because this is called from the grace-period
	// kthread, also remove the task from the holdout list.
	t->trc_reader_checked = true;
	trc_del_holdout(t);

	if (in_qs)
		return true;  // Already in quiescent state, done!!!

	// The task is in a read-side critical section, so set up its
	// state so that it will awaken the grace-period kthread upon exit
	// from that critical section.
	atomic_inc(&trc_n_readers_need_end); // One more to wait on.
	WARN_ON_ONCE(t->trc_reader_special.b.need_qs);
	WRITE_ONCE(t->trc_reader_special.b.need_qs, true);
	return true;
}

/* Attempt to extract the state for the specified task. */
static void trc_wait_for_one_reader(struct task_struct *t,
				    struct list_head *bhp)
{
	int cpu;

	// If a previous IPI is still in flight, let it complete.
	if (smp_load_acquire(&t->trc_ipi_to_cpu) != -1) // Order IPI
		return;

	// The current task had better be in a quiescent state.
	if (t == current) {
		t->trc_reader_checked = true;
		trc_del_holdout(t);
		WARN_ON_ONCE(t->trc_reader_nesting);
		return;
	}

	// Attempt to nail down the task for inspection.
	get_task_struct(t);
	if (try_invoke_on_locked_down_task(t, trc_inspect_reader, NULL)) {
		put_task_struct(t);
		return;
	}
	put_task_struct(t);

	// If currently running, send an IPI, either way, add to list.
	trc_add_holdout(t, bhp);
	if (task_curr(t) && time_after(jiffies, rcu_tasks_trace.gp_start + rcu_task_ipi_delay)) {
		// The task is currently running, so try IPIing it.
		cpu = task_cpu(t);

		// If there is already an IPI outstanding, let it happen.
		if (per_cpu(trc_ipi_to_cpu, cpu) || t->trc_ipi_to_cpu >= 0)
			return;

		atomic_inc(&trc_n_readers_need_end);
		per_cpu(trc_ipi_to_cpu, cpu) = true;
		t->trc_ipi_to_cpu = cpu;
		rcu_tasks_trace.n_ipis++;
		if (smp_call_function_single(cpu,
					     trc_read_check_handler, t, 0)) {
			// Just in case there is some other reason for
			// failure than the target CPU being offline.
			per_cpu(trc_ipi_to_cpu, cpu) = false;
			t->trc_ipi_to_cpu = cpu;
			if (atomic_dec_and_test(&trc_n_readers_need_end)) {
				WARN_ON_ONCE(1);
				wake_up(&trc_wait);
			}
		}
	}
}

/* Initialize for a new RCU-tasks-trace grace period. */
static void rcu_tasks_trace_pregp_step(void)
{
	int cpu;

	// Allow for fast-acting IPIs.
	atomic_set(&trc_n_readers_need_end, 1);

	// There shouldn't be any old IPIs, but...
	for_each_possible_cpu(cpu)
		WARN_ON_ONCE(per_cpu(trc_ipi_to_cpu, cpu));

	// Disable CPU hotplug across the tasklist scan.
	// This also waits for all readers in CPU-hotplug code paths.
	cpus_read_lock();
}

/* Do first-round processing for the specified task. */
static void rcu_tasks_trace_pertask(struct task_struct *t,
				    struct list_head *hop)
{
	WRITE_ONCE(t->trc_reader_special.b.need_qs, false);
	WRITE_ONCE(t->trc_reader_checked, false);
	t->trc_ipi_to_cpu = -1;
	trc_wait_for_one_reader(t, hop);
}

/* Do intermediate processing between task and holdout scans. */
static void rcu_tasks_trace_postscan(void)
{
	// Re-enable CPU hotplug now that the tasklist scan has completed.
	cpus_read_unlock();

	// Wait for late-stage exiting tasks to finish exiting.
	// These might have passed the call to exit_tasks_rcu_finish().
	synchronize_rcu();
	// Any tasks that exit after this point will set ->trc_reader_checked.
}

/* Show the state of a task stalling the current RCU tasks trace GP. */
static void show_stalled_task_trace(struct task_struct *t, bool *firstreport)
{
	int cpu;

	if (*firstreport) {
		pr_err("INFO: rcu_tasks_trace detected stalls on tasks:\n");
		*firstreport = false;
	}
	// FIXME: This should attempt to use try_invoke_on_nonrunning_task().
	cpu = task_cpu(t);
	pr_alert("P%d: %c%c%c nesting: %d%c cpu: %d\n",
		 t->pid,
		 ".I"[READ_ONCE(t->trc_ipi_to_cpu) > 0],
		 ".i"[is_idle_task(t)],
		 ".N"[cpu > 0 && tick_nohz_full_cpu(cpu)],
		 t->trc_reader_nesting,
		 " N"[!!t->trc_reader_special.b.need_qs],
		 cpu);
	sched_show_task(t);
}

/* List stalled IPIs for RCU tasks trace. */
static void show_stalled_ipi_trace(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		if (per_cpu(trc_ipi_to_cpu, cpu))
			pr_alert("\tIPI outstanding to CPU %d\n", cpu);
}

/* Do one scan of the holdout list. */
static void check_all_holdout_tasks_trace(struct list_head *hop,
					  bool needreport, bool *firstreport)
{
	struct task_struct *g, *t;

	// Disable CPU hotplug across the holdout list scan.
	cpus_read_lock();

	list_for_each_entry_safe(t, g, hop, trc_holdout_list) {
		// If safe and needed, try to check the current task.
		if (READ_ONCE(t->trc_ipi_to_cpu) == -1 &&
		    !READ_ONCE(t->trc_reader_checked))
			trc_wait_for_one_reader(t, hop);

		// If check succeeded, remove this task from the list.
		if (READ_ONCE(t->trc_reader_checked))
			trc_del_holdout(t);
		else if (needreport)
			show_stalled_task_trace(t, firstreport);
	}

	// Re-enable CPU hotplug now that the holdout list scan has completed.
	cpus_read_unlock();

	if (needreport) {
		if (firstreport)
			pr_err("INFO: rcu_tasks_trace detected stalls? (Late IPI?)\n");
		show_stalled_ipi_trace();
	}
}

/* Wait for grace period to complete and provide ordering. */
static void rcu_tasks_trace_postgp(struct rcu_tasks *rtp)
{
	bool firstreport;
	struct task_struct *g, *t;
	LIST_HEAD(holdouts);
	long ret;

	// Remove the safety count.
	smp_mb__before_atomic();  // Order vs. earlier atomics
	atomic_dec(&trc_n_readers_need_end);
	smp_mb__after_atomic();  // Order vs. later atomics

	// Wait for readers.
	set_tasks_gp_state(rtp, RTGS_WAIT_READERS);
	for (;;) {
		ret = wait_event_idle_exclusive_timeout(
				trc_wait,
				atomic_read(&trc_n_readers_need_end) == 0,
				READ_ONCE(rcu_task_stall_timeout));
		if (ret)
			break;  // Count reached zero.
		// Stall warning time, so make a list of the offenders.
		for_each_process_thread(g, t)
			if (READ_ONCE(t->trc_reader_special.b.need_qs))
				trc_add_holdout(t, &holdouts);
		firstreport = true;
		list_for_each_entry_safe(t, g, &holdouts, trc_holdout_list)
			if (READ_ONCE(t->trc_reader_special.b.need_qs)) {
				show_stalled_task_trace(t, &firstreport);
				trc_del_holdout(t);
			}
		if (firstreport)
			pr_err("INFO: rcu_tasks_trace detected stalls? (Counter/taskslist mismatch?)\n");
		show_stalled_ipi_trace();
		pr_err("\t%d holdouts\n", atomic_read(&trc_n_readers_need_end));
	}
	smp_mb(); // Caller's code must be ordered after wakeup.
		  // Pairs with pretty much every ordering primitive.
}

/* Report any needed quiescent state for this exiting task. */
void exit_tasks_rcu_finish_trace(struct task_struct *t)
{
	WRITE_ONCE(t->trc_reader_checked, true);
	WARN_ON_ONCE(t->trc_reader_nesting);
	WRITE_ONCE(t->trc_reader_nesting, 0);
	if (WARN_ON_ONCE(READ_ONCE(t->trc_reader_special.b.need_qs)))
		rcu_read_unlock_trace_special(t, 0);
}

/**
 * call_rcu_tasks_trace() - Queue a callback trace task-based grace period
 * @rhp: structure to be used for queueing the RCU updates.
 * @func: actual callback function to be invoked after the grace period
 *
 * The callback function will be invoked some time after a full grace
 * period elapses, in other words after all currently executing RCU
 * read-side critical sections have completed. call_rcu_tasks_trace()
 * assumes that the read-side critical sections end at context switch,
 * cond_resched_rcu_qs(), or transition to usermode execution.  As such,
 * there are no read-side primitives analogous to rcu_read_lock() and
 * rcu_read_unlock() because this primitive is intended to determine
 * that all tasks have passed through a safe state, not so much for
 * data-strcuture synchronization.
 *
 * See the description of call_rcu() for more detailed information on
 * memory ordering guarantees.
 */
void call_rcu_tasks_trace(struct rcu_head *rhp, rcu_callback_t func)
{
	call_rcu_tasks_generic(rhp, func, &rcu_tasks_trace);
}
EXPORT_SYMBOL_GPL(call_rcu_tasks_trace);

/**
 * synchronize_rcu_tasks_trace - wait for a trace rcu-tasks grace period
 *
 * Control will return to the caller some time after a trace rcu-tasks
 * grace period has elapsed, in other words after all currently
 * executing rcu-tasks read-side critical sections have elapsed.  These
 * read-side critical sections are delimited by calls to schedule(),
 * cond_resched_tasks_rcu_qs(), userspace execution, and (in theory,
 * anyway) cond_resched().
 *
 * This is a very specialized primitive, intended only for a few uses in
 * tracing and other situations requiring manipulation of function preambles
 * and profiling hooks.  The synchronize_rcu_tasks_trace() function is not
 * (yet) intended for heavy use from multiple CPUs.
 *
 * See the description of synchronize_rcu() for more detailed information
 * on memory ordering guarantees.
 */
void synchronize_rcu_tasks_trace(void)
{
	RCU_LOCKDEP_WARN(lock_is_held(&rcu_trace_lock_map), "Illegal synchronize_rcu_tasks_trace() in RCU Tasks Trace read-side critical section");
	synchronize_rcu_tasks_generic(&rcu_tasks_trace);
}
EXPORT_SYMBOL_GPL(synchronize_rcu_tasks_trace);

/**
 * rcu_barrier_tasks_trace - Wait for in-flight call_rcu_tasks_trace() callbacks.
 *
 * Although the current implementation is guaranteed to wait, it is not
 * obligated to, for example, if there are no pending callbacks.
 */
void rcu_barrier_tasks_trace(void)
{
	/* There is only one callback queue, so this is easy.  ;-) */
	synchronize_rcu_tasks_trace();
}
EXPORT_SYMBOL_GPL(rcu_barrier_tasks_trace);

static int __init rcu_spawn_tasks_trace_kthread(void)
{
	rcu_tasks_trace.pregp_func = rcu_tasks_trace_pregp_step;
	rcu_tasks_trace.pertask_func = rcu_tasks_trace_pertask;
	rcu_tasks_trace.postscan_func = rcu_tasks_trace_postscan;
	rcu_tasks_trace.holdouts_func = check_all_holdout_tasks_trace;
	rcu_tasks_trace.postgp_func = rcu_tasks_trace_postgp;
	rcu_spawn_tasks_kthread_generic(&rcu_tasks_trace);
	return 0;
}
core_initcall(rcu_spawn_tasks_trace_kthread);

static void show_rcu_tasks_trace_gp_kthread(void)
{
	char buf[32];

	sprintf(buf, "N%d", atomic_read(&trc_n_readers_need_end));
	show_rcu_tasks_generic_gp_kthread(&rcu_tasks_trace, buf);
}

#else /* #ifdef CONFIG_TASKS_TRACE_RCU */
void exit_tasks_rcu_finish_trace(struct task_struct *t) { }
static inline void show_rcu_tasks_trace_gp_kthread(void) {}
#endif /* #else #ifdef CONFIG_TASKS_TRACE_RCU */

void show_rcu_tasks_gp_kthreads(void)
{
	show_rcu_tasks_classic_gp_kthread();
	show_rcu_tasks_rude_gp_kthread();
	show_rcu_tasks_trace_gp_kthread();
}

#else /* #ifdef CONFIG_TASKS_RCU_GENERIC */
static inline void rcu_tasks_bootup_oddness(void) {}
void show_rcu_tasks_gp_kthreads(void) {}
#endif /* #else #ifdef CONFIG_TASKS_RCU_GENERIC */
