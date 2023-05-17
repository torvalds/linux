/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Task-based RCU implementations.
 *
 * Copyright (C) 2020 Paul E. McKenney
 */

#ifdef CONFIG_TASKS_RCU_GENERIC
#include "rcu_segcblist.h"

////////////////////////////////////////////////////////////////////////
//
// Generic data structures.

struct rcu_tasks;
typedef void (*rcu_tasks_gp_func_t)(struct rcu_tasks *rtp);
typedef void (*pregp_func_t)(struct list_head *hop);
typedef void (*pertask_func_t)(struct task_struct *t, struct list_head *hop);
typedef void (*postscan_func_t)(struct list_head *hop);
typedef void (*holdouts_func_t)(struct list_head *hop, bool ndrpt, bool *frptp);
typedef void (*postgp_func_t)(struct rcu_tasks *rtp);

/**
 * struct rcu_tasks_percpu - Per-CPU component of definition for a Tasks-RCU-like mechanism.
 * @cblist: Callback list.
 * @lock: Lock protecting per-CPU callback list.
 * @rtp_jiffies: Jiffies counter value for statistics.
 * @rtp_n_lock_retries: Rough lock-contention statistic.
 * @rtp_work: Work queue for invoking callbacks.
 * @rtp_irq_work: IRQ work queue for deferred wakeups.
 * @barrier_q_head: RCU callback for barrier operation.
 * @rtp_blkd_tasks: List of tasks blocked as readers.
 * @cpu: CPU number corresponding to this entry.
 * @rtpp: Pointer to the rcu_tasks structure.
 */
struct rcu_tasks_percpu {
	struct rcu_segcblist cblist;
	raw_spinlock_t __private lock;
	unsigned long rtp_jiffies;
	unsigned long rtp_n_lock_retries;
	struct work_struct rtp_work;
	struct irq_work rtp_irq_work;
	struct rcu_head barrier_q_head;
	struct list_head rtp_blkd_tasks;
	int cpu;
	struct rcu_tasks *rtpp;
};

/**
 * struct rcu_tasks - Definition for a Tasks-RCU-like mechanism.
 * @cbs_wait: RCU wait allowing a new callback to get kthread's attention.
 * @cbs_gbl_lock: Lock protecting callback list.
 * @tasks_gp_mutex: Mutex protecting grace period, needed during mid-boot dead zone.
 * @kthread_ptr: This flavor's grace-period/callback-invocation kthread.
 * @gp_func: This flavor's grace-period-wait function.
 * @gp_state: Grace period's most recent state transition (debugging).
 * @gp_sleep: Per-grace-period sleep to prevent CPU-bound looping.
 * @init_fract: Initial backoff sleep interval.
 * @gp_jiffies: Time of last @gp_state transition.
 * @gp_start: Most recent grace-period start in jiffies.
 * @tasks_gp_seq: Number of grace periods completed since boot.
 * @n_ipis: Number of IPIs sent to encourage grace periods to end.
 * @n_ipis_fails: Number of IPI-send failures.
 * @pregp_func: This flavor's pre-grace-period function (optional).
 * @pertask_func: This flavor's per-task scan function (optional).
 * @postscan_func: This flavor's post-task scan function (optional).
 * @holdouts_func: This flavor's holdout-list scan function (optional).
 * @postgp_func: This flavor's post-grace-period function (optional).
 * @call_func: This flavor's call_rcu()-equivalent function.
 * @rtpcpu: This flavor's rcu_tasks_percpu structure.
 * @percpu_enqueue_shift: Shift down CPU ID this much when enqueuing callbacks.
 * @percpu_enqueue_lim: Number of per-CPU callback queues in use for enqueuing.
 * @percpu_dequeue_lim: Number of per-CPU callback queues in use for dequeuing.
 * @percpu_dequeue_gpseq: RCU grace-period number to propagate enqueue limit to dequeuers.
 * @barrier_q_mutex: Serialize barrier operations.
 * @barrier_q_count: Number of queues being waited on.
 * @barrier_q_completion: Barrier wait/wakeup mechanism.
 * @barrier_q_seq: Sequence number for barrier operations.
 * @name: This flavor's textual name.
 * @kname: This flavor's kthread name.
 */
struct rcu_tasks {
	struct rcuwait cbs_wait;
	raw_spinlock_t cbs_gbl_lock;
	struct mutex tasks_gp_mutex;
	int gp_state;
	int gp_sleep;
	int init_fract;
	unsigned long gp_jiffies;
	unsigned long gp_start;
	unsigned long tasks_gp_seq;
	unsigned long n_ipis;
	unsigned long n_ipis_fails;
	struct task_struct *kthread_ptr;
	rcu_tasks_gp_func_t gp_func;
	pregp_func_t pregp_func;
	pertask_func_t pertask_func;
	postscan_func_t postscan_func;
	holdouts_func_t holdouts_func;
	postgp_func_t postgp_func;
	call_rcu_func_t call_func;
	struct rcu_tasks_percpu __percpu *rtpcpu;
	int percpu_enqueue_shift;
	int percpu_enqueue_lim;
	int percpu_dequeue_lim;
	unsigned long percpu_dequeue_gpseq;
	struct mutex barrier_q_mutex;
	atomic_t barrier_q_count;
	struct completion barrier_q_completion;
	unsigned long barrier_q_seq;
	char *name;
	char *kname;
};

static void call_rcu_tasks_iw_wakeup(struct irq_work *iwp);

#define DEFINE_RCU_TASKS(rt_name, gp, call, n)						\
static DEFINE_PER_CPU(struct rcu_tasks_percpu, rt_name ## __percpu) = {			\
	.lock = __RAW_SPIN_LOCK_UNLOCKED(rt_name ## __percpu.cbs_pcpu_lock),		\
	.rtp_irq_work = IRQ_WORK_INIT_HARD(call_rcu_tasks_iw_wakeup),			\
};											\
static struct rcu_tasks rt_name =							\
{											\
	.cbs_wait = __RCUWAIT_INITIALIZER(rt_name.wait),				\
	.cbs_gbl_lock = __RAW_SPIN_LOCK_UNLOCKED(rt_name.cbs_gbl_lock),			\
	.tasks_gp_mutex = __MUTEX_INITIALIZER(rt_name.tasks_gp_mutex),			\
	.gp_func = gp,									\
	.call_func = call,								\
	.rtpcpu = &rt_name ## __percpu,							\
	.name = n,									\
	.percpu_enqueue_shift = order_base_2(CONFIG_NR_CPUS),				\
	.percpu_enqueue_lim = 1,							\
	.percpu_dequeue_lim = 1,							\
	.barrier_q_mutex = __MUTEX_INITIALIZER(rt_name.barrier_q_mutex),		\
	.barrier_q_seq = (0UL - 50UL) << RCU_SEQ_CTR_SHIFT,				\
	.kname = #rt_name,								\
}

#ifdef CONFIG_TASKS_RCU
/* Track exiting tasks in order to allow them to be waited for. */
DEFINE_STATIC_SRCU(tasks_rcu_exit_srcu);
#endif

#ifdef CONFIG_TASKS_RCU
/* Report delay in synchronize_srcu() completion in rcu_tasks_postscan(). */
static void tasks_rcu_exit_srcu_stall(struct timer_list *unused);
static DEFINE_TIMER(tasks_rcu_exit_srcu_stall_timer, tasks_rcu_exit_srcu_stall);
#endif

/* Avoid IPIing CPUs early in the grace period. */
#define RCU_TASK_IPI_DELAY (IS_ENABLED(CONFIG_TASKS_TRACE_RCU_READ_MB) ? HZ / 2 : 0)
static int rcu_task_ipi_delay __read_mostly = RCU_TASK_IPI_DELAY;
module_param(rcu_task_ipi_delay, int, 0644);

/* Control stall timeouts.  Disable with <= 0, otherwise jiffies till stall. */
#define RCU_TASK_BOOT_STALL_TIMEOUT (HZ * 30)
#define RCU_TASK_STALL_TIMEOUT (HZ * 60 * 10)
static int rcu_task_stall_timeout __read_mostly = RCU_TASK_STALL_TIMEOUT;
module_param(rcu_task_stall_timeout, int, 0644);
#define RCU_TASK_STALL_INFO (HZ * 10)
static int rcu_task_stall_info __read_mostly = RCU_TASK_STALL_INFO;
module_param(rcu_task_stall_info, int, 0644);
static int rcu_task_stall_info_mult __read_mostly = 3;
module_param(rcu_task_stall_info_mult, int, 0444);

static int rcu_task_enqueue_lim __read_mostly = -1;
module_param(rcu_task_enqueue_lim, int, 0444);

static bool rcu_task_cb_adjust;
static int rcu_task_contend_lim __read_mostly = 100;
module_param(rcu_task_contend_lim, int, 0444);
static int rcu_task_collapse_lim __read_mostly = 10;
module_param(rcu_task_collapse_lim, int, 0444);

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
#ifndef CONFIG_TINY_RCU
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
#endif /* #ifndef CONFIG_TINY_RCU */

////////////////////////////////////////////////////////////////////////
//
// Generic code.

static void rcu_tasks_invoke_cbs_wq(struct work_struct *wp);

/* Record grace-period phase and time. */
static void set_tasks_gp_state(struct rcu_tasks *rtp, int newstate)
{
	rtp->gp_state = newstate;
	rtp->gp_jiffies = jiffies;
}

#ifndef CONFIG_TINY_RCU
/* Return state name. */
static const char *tasks_gp_state_getname(struct rcu_tasks *rtp)
{
	int i = data_race(rtp->gp_state); // Let KCSAN detect update races
	int j = READ_ONCE(i); // Prevent the compiler from reading twice

	if (j >= ARRAY_SIZE(rcu_tasks_gp_state_names))
		return "???";
	return rcu_tasks_gp_state_names[j];
}
#endif /* #ifndef CONFIG_TINY_RCU */

// Initialize per-CPU callback lists for the specified flavor of
// Tasks RCU.
static void cblist_init_generic(struct rcu_tasks *rtp)
{
	int cpu;
	unsigned long flags;
	int lim;
	int shift;

	raw_spin_lock_irqsave(&rtp->cbs_gbl_lock, flags);
	if (rcu_task_enqueue_lim < 0) {
		rcu_task_enqueue_lim = 1;
		rcu_task_cb_adjust = true;
		pr_info("%s: Setting adjustable number of callback queues.\n", __func__);
	} else if (rcu_task_enqueue_lim == 0) {
		rcu_task_enqueue_lim = 1;
	}
	lim = rcu_task_enqueue_lim;

	if (lim > nr_cpu_ids)
		lim = nr_cpu_ids;
	shift = ilog2(nr_cpu_ids / lim);
	if (((nr_cpu_ids - 1) >> shift) >= lim)
		shift++;
	WRITE_ONCE(rtp->percpu_enqueue_shift, shift);
	WRITE_ONCE(rtp->percpu_dequeue_lim, lim);
	smp_store_release(&rtp->percpu_enqueue_lim, lim);
	for_each_possible_cpu(cpu) {
		struct rcu_tasks_percpu *rtpcp = per_cpu_ptr(rtp->rtpcpu, cpu);

		WARN_ON_ONCE(!rtpcp);
		if (cpu)
			raw_spin_lock_init(&ACCESS_PRIVATE(rtpcp, lock));
		raw_spin_lock_rcu_node(rtpcp); // irqs already disabled.
		if (rcu_segcblist_empty(&rtpcp->cblist))
			rcu_segcblist_init(&rtpcp->cblist);
		INIT_WORK(&rtpcp->rtp_work, rcu_tasks_invoke_cbs_wq);
		rtpcp->cpu = cpu;
		rtpcp->rtpp = rtp;
		if (!rtpcp->rtp_blkd_tasks.next)
			INIT_LIST_HEAD(&rtpcp->rtp_blkd_tasks);
		raw_spin_unlock_rcu_node(rtpcp); // irqs remain disabled.
	}
	raw_spin_unlock_irqrestore(&rtp->cbs_gbl_lock, flags);
	pr_info("%s: Setting shift to %d and lim to %d.\n", __func__, data_race(rtp->percpu_enqueue_shift), data_race(rtp->percpu_enqueue_lim));
}

// IRQ-work handler that does deferred wakeup for call_rcu_tasks_generic().
static void call_rcu_tasks_iw_wakeup(struct irq_work *iwp)
{
	struct rcu_tasks *rtp;
	struct rcu_tasks_percpu *rtpcp = container_of(iwp, struct rcu_tasks_percpu, rtp_irq_work);

	rtp = rtpcp->rtpp;
	rcuwait_wake_up(&rtp->cbs_wait);
}

// Enqueue a callback for the specified flavor of Tasks RCU.
static void call_rcu_tasks_generic(struct rcu_head *rhp, rcu_callback_t func,
				   struct rcu_tasks *rtp)
{
	int chosen_cpu;
	unsigned long flags;
	int ideal_cpu;
	unsigned long j;
	bool needadjust = false;
	bool needwake;
	struct rcu_tasks_percpu *rtpcp;

	rhp->next = NULL;
	rhp->func = func;
	local_irq_save(flags);
	rcu_read_lock();
	ideal_cpu = smp_processor_id() >> READ_ONCE(rtp->percpu_enqueue_shift);
	chosen_cpu = cpumask_next(ideal_cpu - 1, cpu_possible_mask);
	rtpcp = per_cpu_ptr(rtp->rtpcpu, chosen_cpu);
	if (!raw_spin_trylock_rcu_node(rtpcp)) { // irqs already disabled.
		raw_spin_lock_rcu_node(rtpcp); // irqs already disabled.
		j = jiffies;
		if (rtpcp->rtp_jiffies != j) {
			rtpcp->rtp_jiffies = j;
			rtpcp->rtp_n_lock_retries = 0;
		}
		if (rcu_task_cb_adjust && ++rtpcp->rtp_n_lock_retries > rcu_task_contend_lim &&
		    READ_ONCE(rtp->percpu_enqueue_lim) != nr_cpu_ids)
			needadjust = true;  // Defer adjustment to avoid deadlock.
	}
	if (!rcu_segcblist_is_enabled(&rtpcp->cblist)) {
		raw_spin_unlock_rcu_node(rtpcp); // irqs remain disabled.
		cblist_init_generic(rtp);
		raw_spin_lock_rcu_node(rtpcp); // irqs already disabled.
	}
	needwake = rcu_segcblist_empty(&rtpcp->cblist);
	rcu_segcblist_enqueue(&rtpcp->cblist, rhp);
	raw_spin_unlock_irqrestore_rcu_node(rtpcp, flags);
	if (unlikely(needadjust)) {
		raw_spin_lock_irqsave(&rtp->cbs_gbl_lock, flags);
		if (rtp->percpu_enqueue_lim != nr_cpu_ids) {
			WRITE_ONCE(rtp->percpu_enqueue_shift, 0);
			WRITE_ONCE(rtp->percpu_dequeue_lim, nr_cpu_ids);
			smp_store_release(&rtp->percpu_enqueue_lim, nr_cpu_ids);
			pr_info("Switching %s to per-CPU callback queuing.\n", rtp->name);
		}
		raw_spin_unlock_irqrestore(&rtp->cbs_gbl_lock, flags);
	}
	rcu_read_unlock();
	/* We can't create the thread unless interrupts are enabled. */
	if (needwake && READ_ONCE(rtp->kthread_ptr))
		irq_work_queue(&rtpcp->rtp_irq_work);
}

// RCU callback function for rcu_barrier_tasks_generic().
static void rcu_barrier_tasks_generic_cb(struct rcu_head *rhp)
{
	struct rcu_tasks *rtp;
	struct rcu_tasks_percpu *rtpcp;

	rtpcp = container_of(rhp, struct rcu_tasks_percpu, barrier_q_head);
	rtp = rtpcp->rtpp;
	if (atomic_dec_and_test(&rtp->barrier_q_count))
		complete(&rtp->barrier_q_completion);
}

// Wait for all in-flight callbacks for the specified RCU Tasks flavor.
// Operates in a manner similar to rcu_barrier().
static void rcu_barrier_tasks_generic(struct rcu_tasks *rtp)
{
	int cpu;
	unsigned long flags;
	struct rcu_tasks_percpu *rtpcp;
	unsigned long s = rcu_seq_snap(&rtp->barrier_q_seq);

	mutex_lock(&rtp->barrier_q_mutex);
	if (rcu_seq_done(&rtp->barrier_q_seq, s)) {
		smp_mb();
		mutex_unlock(&rtp->barrier_q_mutex);
		return;
	}
	rcu_seq_start(&rtp->barrier_q_seq);
	init_completion(&rtp->barrier_q_completion);
	atomic_set(&rtp->barrier_q_count, 2);
	for_each_possible_cpu(cpu) {
		if (cpu >= smp_load_acquire(&rtp->percpu_dequeue_lim))
			break;
		rtpcp = per_cpu_ptr(rtp->rtpcpu, cpu);
		rtpcp->barrier_q_head.func = rcu_barrier_tasks_generic_cb;
		raw_spin_lock_irqsave_rcu_node(rtpcp, flags);
		if (rcu_segcblist_entrain(&rtpcp->cblist, &rtpcp->barrier_q_head))
			atomic_inc(&rtp->barrier_q_count);
		raw_spin_unlock_irqrestore_rcu_node(rtpcp, flags);
	}
	if (atomic_sub_and_test(2, &rtp->barrier_q_count))
		complete(&rtp->barrier_q_completion);
	wait_for_completion(&rtp->barrier_q_completion);
	rcu_seq_end(&rtp->barrier_q_seq);
	mutex_unlock(&rtp->barrier_q_mutex);
}

// Advance callbacks and indicate whether either a grace period or
// callback invocation is needed.
static int rcu_tasks_need_gpcb(struct rcu_tasks *rtp)
{
	int cpu;
	unsigned long flags;
	bool gpdone = poll_state_synchronize_rcu(rtp->percpu_dequeue_gpseq);
	long n;
	long ncbs = 0;
	long ncbsnz = 0;
	int needgpcb = 0;

	for (cpu = 0; cpu < smp_load_acquire(&rtp->percpu_dequeue_lim); cpu++) {
		struct rcu_tasks_percpu *rtpcp = per_cpu_ptr(rtp->rtpcpu, cpu);

		/* Advance and accelerate any new callbacks. */
		if (!rcu_segcblist_n_cbs(&rtpcp->cblist))
			continue;
		raw_spin_lock_irqsave_rcu_node(rtpcp, flags);
		// Should we shrink down to a single callback queue?
		n = rcu_segcblist_n_cbs(&rtpcp->cblist);
		if (n) {
			ncbs += n;
			if (cpu > 0)
				ncbsnz += n;
		}
		rcu_segcblist_advance(&rtpcp->cblist, rcu_seq_current(&rtp->tasks_gp_seq));
		(void)rcu_segcblist_accelerate(&rtpcp->cblist, rcu_seq_snap(&rtp->tasks_gp_seq));
		if (rcu_segcblist_pend_cbs(&rtpcp->cblist))
			needgpcb |= 0x3;
		if (!rcu_segcblist_empty(&rtpcp->cblist))
			needgpcb |= 0x1;
		raw_spin_unlock_irqrestore_rcu_node(rtpcp, flags);
	}

	// Shrink down to a single callback queue if appropriate.
	// This is done in two stages: (1) If there are no more than
	// rcu_task_collapse_lim callbacks on CPU 0 and none on any other
	// CPU, limit enqueueing to CPU 0.  (2) After an RCU grace period,
	// if there has not been an increase in callbacks, limit dequeuing
	// to CPU 0.  Note the matching RCU read-side critical section in
	// call_rcu_tasks_generic().
	if (rcu_task_cb_adjust && ncbs <= rcu_task_collapse_lim) {
		raw_spin_lock_irqsave(&rtp->cbs_gbl_lock, flags);
		if (rtp->percpu_enqueue_lim > 1) {
			WRITE_ONCE(rtp->percpu_enqueue_shift, order_base_2(nr_cpu_ids));
			smp_store_release(&rtp->percpu_enqueue_lim, 1);
			rtp->percpu_dequeue_gpseq = get_state_synchronize_rcu();
			gpdone = false;
			pr_info("Starting switch %s to CPU-0 callback queuing.\n", rtp->name);
		}
		raw_spin_unlock_irqrestore(&rtp->cbs_gbl_lock, flags);
	}
	if (rcu_task_cb_adjust && !ncbsnz && gpdone) {
		raw_spin_lock_irqsave(&rtp->cbs_gbl_lock, flags);
		if (rtp->percpu_enqueue_lim < rtp->percpu_dequeue_lim) {
			WRITE_ONCE(rtp->percpu_dequeue_lim, 1);
			pr_info("Completing switch %s to CPU-0 callback queuing.\n", rtp->name);
		}
		if (rtp->percpu_dequeue_lim == 1) {
			for (cpu = rtp->percpu_dequeue_lim; cpu < nr_cpu_ids; cpu++) {
				struct rcu_tasks_percpu *rtpcp = per_cpu_ptr(rtp->rtpcpu, cpu);

				WARN_ON_ONCE(rcu_segcblist_n_cbs(&rtpcp->cblist));
			}
		}
		raw_spin_unlock_irqrestore(&rtp->cbs_gbl_lock, flags);
	}

	return needgpcb;
}

// Advance callbacks and invoke any that are ready.
static void rcu_tasks_invoke_cbs(struct rcu_tasks *rtp, struct rcu_tasks_percpu *rtpcp)
{
	int cpu;
	int cpunext;
	unsigned long flags;
	int len;
	struct rcu_head *rhp;
	struct rcu_cblist rcl = RCU_CBLIST_INITIALIZER(rcl);
	struct rcu_tasks_percpu *rtpcp_next;

	cpu = rtpcp->cpu;
	cpunext = cpu * 2 + 1;
	if (cpunext < smp_load_acquire(&rtp->percpu_dequeue_lim)) {
		rtpcp_next = per_cpu_ptr(rtp->rtpcpu, cpunext);
		queue_work_on(cpunext, system_wq, &rtpcp_next->rtp_work);
		cpunext++;
		if (cpunext < smp_load_acquire(&rtp->percpu_dequeue_lim)) {
			rtpcp_next = per_cpu_ptr(rtp->rtpcpu, cpunext);
			queue_work_on(cpunext, system_wq, &rtpcp_next->rtp_work);
		}
	}

	if (rcu_segcblist_empty(&rtpcp->cblist) || !cpu_possible(cpu))
		return;
	raw_spin_lock_irqsave_rcu_node(rtpcp, flags);
	rcu_segcblist_advance(&rtpcp->cblist, rcu_seq_current(&rtp->tasks_gp_seq));
	rcu_segcblist_extract_done_cbs(&rtpcp->cblist, &rcl);
	raw_spin_unlock_irqrestore_rcu_node(rtpcp, flags);
	len = rcl.len;
	for (rhp = rcu_cblist_dequeue(&rcl); rhp; rhp = rcu_cblist_dequeue(&rcl)) {
		local_bh_disable();
		rhp->func(rhp);
		local_bh_enable();
		cond_resched();
	}
	raw_spin_lock_irqsave_rcu_node(rtpcp, flags);
	rcu_segcblist_add_len(&rtpcp->cblist, -len);
	(void)rcu_segcblist_accelerate(&rtpcp->cblist, rcu_seq_snap(&rtp->tasks_gp_seq));
	raw_spin_unlock_irqrestore_rcu_node(rtpcp, flags);
}

// Workqueue flood to advance callbacks and invoke any that are ready.
static void rcu_tasks_invoke_cbs_wq(struct work_struct *wp)
{
	struct rcu_tasks *rtp;
	struct rcu_tasks_percpu *rtpcp = container_of(wp, struct rcu_tasks_percpu, rtp_work);

	rtp = rtpcp->rtpp;
	rcu_tasks_invoke_cbs(rtp, rtpcp);
}

// Wait for one grace period.
static void rcu_tasks_one_gp(struct rcu_tasks *rtp, bool midboot)
{
	int needgpcb;

	mutex_lock(&rtp->tasks_gp_mutex);

	// If there were none, wait a bit and start over.
	if (unlikely(midboot)) {
		needgpcb = 0x2;
	} else {
		set_tasks_gp_state(rtp, RTGS_WAIT_CBS);
		rcuwait_wait_event(&rtp->cbs_wait,
				   (needgpcb = rcu_tasks_need_gpcb(rtp)),
				   TASK_IDLE);
	}

	if (needgpcb & 0x2) {
		// Wait for one grace period.
		set_tasks_gp_state(rtp, RTGS_WAIT_GP);
		rtp->gp_start = jiffies;
		rcu_seq_start(&rtp->tasks_gp_seq);
		rtp->gp_func(rtp);
		rcu_seq_end(&rtp->tasks_gp_seq);
	}

	// Invoke callbacks.
	set_tasks_gp_state(rtp, RTGS_INVOKE_CBS);
	rcu_tasks_invoke_cbs(rtp, per_cpu_ptr(rtp->rtpcpu, 0));
	mutex_unlock(&rtp->tasks_gp_mutex);
}

// RCU-tasks kthread that detects grace periods and invokes callbacks.
static int __noreturn rcu_tasks_kthread(void *arg)
{
	struct rcu_tasks *rtp = arg;

	/* Run on housekeeping CPUs by default.  Sysadm can move if desired. */
	housekeeping_affine(current, HK_TYPE_RCU);
	WRITE_ONCE(rtp->kthread_ptr, current); // Let GPs start!

	/*
	 * Each pass through the following loop makes one check for
	 * newly arrived callbacks, and, if there are some, waits for
	 * one RCU-tasks grace period and then invokes the callbacks.
	 * This loop is terminated by the system going down.  ;-)
	 */
	for (;;) {
		// Wait for one grace period and invoke any callbacks
		// that are ready.
		rcu_tasks_one_gp(rtp, false);

		// Paranoid sleep to keep this from entering a tight loop.
		schedule_timeout_idle(rtp->gp_sleep);
	}
}

// Wait for a grace period for the specified flavor of Tasks RCU.
static void synchronize_rcu_tasks_generic(struct rcu_tasks *rtp)
{
	/* Complain if the scheduler has not started.  */
	if (WARN_ONCE(rcu_scheduler_active == RCU_SCHEDULER_INACTIVE,
			 "synchronize_%s() called too soon", rtp->name))
		return;

	// If the grace-period kthread is running, use it.
	if (READ_ONCE(rtp->kthread_ptr)) {
		wait_rcu_gp(rtp->call_func);
		return;
	}
	rcu_tasks_one_gp(rtp, true);
}

/* Spawn RCU-tasks grace-period kthread. */
static void __init rcu_spawn_tasks_kthread_generic(struct rcu_tasks *rtp)
{
	struct task_struct *t;

	t = kthread_run(rcu_tasks_kthread, rtp, "%s_kthread", rtp->kname);
	if (WARN_ONCE(IS_ERR(t), "%s: Could not start %s grace-period kthread, OOM is now expected behavior\n", __func__, rtp->name))
		return;
	smp_mb(); /* Ensure others see full kthread. */
}

#ifndef CONFIG_TINY_RCU

/*
 * Print any non-default Tasks RCU settings.
 */
static void __init rcu_tasks_bootup_oddness(void)
{
#if defined(CONFIG_TASKS_RCU) || defined(CONFIG_TASKS_TRACE_RCU)
	int rtsimc;

	if (rcu_task_stall_timeout != RCU_TASK_STALL_TIMEOUT)
		pr_info("\tTasks-RCU CPU stall warnings timeout set to %d (rcu_task_stall_timeout).\n", rcu_task_stall_timeout);
	rtsimc = clamp(rcu_task_stall_info_mult, 1, 10);
	if (rtsimc != rcu_task_stall_info_mult) {
		pr_info("\tTasks-RCU CPU stall info multiplier clamped to %d (rcu_task_stall_info_mult).\n", rtsimc);
		rcu_task_stall_info_mult = rtsimc;
	}
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

#ifndef CONFIG_TINY_RCU
/* Dump out rcutorture-relevant state common to all RCU-tasks flavors. */
static void show_rcu_tasks_generic_gp_kthread(struct rcu_tasks *rtp, char *s)
{
	int cpu;
	bool havecbs = false;

	for_each_possible_cpu(cpu) {
		struct rcu_tasks_percpu *rtpcp = per_cpu_ptr(rtp->rtpcpu, cpu);

		if (!data_race(rcu_segcblist_empty(&rtpcp->cblist))) {
			havecbs = true;
			break;
		}
	}
	pr_info("%s: %s(%d) since %lu g:%lu i:%lu/%lu %c%c %s\n",
		rtp->kname,
		tasks_gp_state_getname(rtp), data_race(rtp->gp_state),
		jiffies - data_race(rtp->gp_jiffies),
		data_race(rcu_seq_current(&rtp->tasks_gp_seq)),
		data_race(rtp->n_ipis_fails), data_race(rtp->n_ipis),
		".k"[!!data_race(rtp->kthread_ptr)],
		".C"[havecbs],
		s);
}
#endif // #ifndef CONFIG_TINY_RCU

static void exit_tasks_rcu_finish_trace(struct task_struct *t);

#if defined(CONFIG_TASKS_RCU) || defined(CONFIG_TASKS_TRACE_RCU)

////////////////////////////////////////////////////////////////////////
//
// Shared code between task-list-scanning variants of Tasks RCU.

/* Wait for one RCU-tasks grace period. */
static void rcu_tasks_wait_gp(struct rcu_tasks *rtp)
{
	struct task_struct *g;
	int fract;
	LIST_HEAD(holdouts);
	unsigned long j;
	unsigned long lastinfo;
	unsigned long lastreport;
	bool reported = false;
	int rtsi;
	struct task_struct *t;

	set_tasks_gp_state(rtp, RTGS_PRE_WAIT_GP);
	rtp->pregp_func(&holdouts);

	/*
	 * There were callbacks, so we need to wait for an RCU-tasks
	 * grace period.  Start off by scanning the task list for tasks
	 * that are not already voluntarily blocked.  Mark these tasks
	 * and make a list of them in holdouts.
	 */
	set_tasks_gp_state(rtp, RTGS_SCAN_TASKLIST);
	if (rtp->pertask_func) {
		rcu_read_lock();
		for_each_process_thread(g, t)
			rtp->pertask_func(t, &holdouts);
		rcu_read_unlock();
	}

	set_tasks_gp_state(rtp, RTGS_POST_SCAN_TASKLIST);
	rtp->postscan_func(&holdouts);

	/*
	 * Each pass through the following loop scans the list of holdout
	 * tasks, removing any that are no longer holdouts.  When the list
	 * is empty, we are done.
	 */
	lastreport = jiffies;
	lastinfo = lastreport;
	rtsi = READ_ONCE(rcu_task_stall_info);

	// Start off with initial wait and slowly back off to 1 HZ wait.
	fract = rtp->init_fract;

	while (!list_empty(&holdouts)) {
		ktime_t exp;
		bool firstreport;
		bool needreport;
		int rtst;

		// Slowly back off waiting for holdouts
		set_tasks_gp_state(rtp, RTGS_WAIT_SCAN_HOLDOUTS);
		if (!IS_ENABLED(CONFIG_PREEMPT_RT)) {
			schedule_timeout_idle(fract);
		} else {
			exp = jiffies_to_nsecs(fract);
			__set_current_state(TASK_IDLE);
			schedule_hrtimeout_range(&exp, jiffies_to_nsecs(HZ / 2), HRTIMER_MODE_REL_HARD);
		}

		if (fract < HZ)
			fract++;

		rtst = READ_ONCE(rcu_task_stall_timeout);
		needreport = rtst > 0 && time_after(jiffies, lastreport + rtst);
		if (needreport) {
			lastreport = jiffies;
			reported = true;
		}
		firstreport = true;
		WARN_ON(signal_pending(current));
		set_tasks_gp_state(rtp, RTGS_SCAN_HOLDOUTS);
		rtp->holdouts_func(&holdouts, needreport, &firstreport);

		// Print pre-stall informational messages if needed.
		j = jiffies;
		if (rtsi > 0 && !reported && time_after(j, lastinfo + rtsi)) {
			lastinfo = j;
			rtsi = rtsi * rcu_task_stall_info_mult;
			pr_info("%s: %s grace period number %lu (since boot) is %lu jiffies old.\n",
				__func__, rtp->kname, rtp->tasks_gp_seq, j - rtp->gp_start);
		}
	}

	set_tasks_gp_state(rtp, RTGS_POST_GP);
	rtp->postgp_func(rtp);
}

#endif /* #if defined(CONFIG_TASKS_RCU) || defined(CONFIG_TASKS_TRACE_RCU) */

#ifdef CONFIG_TASKS_RCU

////////////////////////////////////////////////////////////////////////
//
// Simple variant of RCU whose quiescent states are voluntary context
// switch, cond_resched_tasks_rcu_qs(), user-space execution, and idle.
// As such, grace periods can take one good long time.  There are no
// read-side primitives similar to rcu_read_lock() and rcu_read_unlock()
// because this implementation is intended to get the system into a safe
// state for some of the manipulations involved in tracing and the like.
// Finally, this implementation does not support high call_rcu_tasks()
// rates from multiple CPUs.  If this is required, per-CPU callback lists
// will be needed.
//
// The implementation uses rcu_tasks_wait_gp(), which relies on function
// pointers in the rcu_tasks structure.  The rcu_spawn_tasks_kthread()
// function sets these function pointers up so that rcu_tasks_wait_gp()
// invokes these functions in this order:
//
// rcu_tasks_pregp_step():
//	Invokes synchronize_rcu() in order to wait for all in-flight
//	t->on_rq and t->nvcsw transitions to complete.	This works because
//	all such transitions are carried out with interrupts disabled.
// rcu_tasks_pertask(), invoked on every non-idle task:
//	For every runnable non-idle task other than the current one, use
//	get_task_struct() to pin down that task, snapshot that task's
//	number of voluntary context switches, and add that task to the
//	holdout list.
// rcu_tasks_postscan():
//	Invoke synchronize_srcu() to ensure that all tasks that were
//	in the process of exiting (and which thus might not know to
//	synchronize with this RCU Tasks grace period) have completed
//	exiting.
// check_all_holdout_tasks(), repeatedly until holdout list is empty:
//	Scans the holdout list, attempting to identify a quiescent state
//	for each task on the list.  If there is a quiescent state, the
//	corresponding task is removed from the holdout list.
// rcu_tasks_postgp():
//	Invokes synchronize_rcu() in order to ensure that all prior
//	t->on_rq and t->nvcsw transitions are seen by all CPUs and tasks
//	to have happened before the end of this RCU Tasks grace period.
//	Again, this works because all such transitions are carried out
//	with interrupts disabled.
//
// For each exiting task, the exit_tasks_rcu_start() and
// exit_tasks_rcu_finish() functions begin and end, respectively, the SRCU
// read-side critical sections waited for by rcu_tasks_postscan().
//
// Pre-grace-period update-side code is ordered before the grace
// via the raw_spin_lock.*rcu_node().  Pre-grace-period read-side code
// is ordered before the grace period via synchronize_rcu() call in
// rcu_tasks_pregp_step() and by the scheduler's locks and interrupt
// disabling.

/* Pre-grace-period preparation. */
static void rcu_tasks_pregp_step(struct list_head *hop)
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
static void rcu_tasks_postscan(struct list_head *hop)
{
	int rtsi = READ_ONCE(rcu_task_stall_info);

	if (!IS_ENABLED(CONFIG_TINY_RCU)) {
		tasks_rcu_exit_srcu_stall_timer.expires = jiffies + rtsi;
		add_timer(&tasks_rcu_exit_srcu_stall_timer);
	}

	/*
	 * Exiting tasks may escape the tasklist scan. Those are vulnerable
	 * until their final schedule() with TASK_DEAD state. To cope with
	 * this, divide the fragile exit path part in two intersecting
	 * read side critical sections:
	 *
	 * 1) An _SRCU_ read side starting before calling exit_notify(),
	 *    which may remove the task from the tasklist, and ending after
	 *    the final preempt_disable() call in do_exit().
	 *
	 * 2) An _RCU_ read side starting with the final preempt_disable()
	 *    call in do_exit() and ending with the final call to schedule()
	 *    with TASK_DEAD state.
	 *
	 * This handles the part 1). And postgp will handle part 2) with a
	 * call to synchronize_rcu().
	 */
	synchronize_srcu(&tasks_rcu_exit_srcu);

	if (!IS_ENABLED(CONFIG_TINY_RCU))
		del_timer_sync(&tasks_rcu_exit_srcu_stall_timer);
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
	 * cleaning up after synchronize_srcu(&tasks_rcu_exit_srcu),
	 * enforcing the whole region before tasklist removal until
	 * the final schedule() with TASK_DEAD state to be an RCU TASKS
	 * read side critical section.
	 */
	synchronize_rcu();
}

void call_rcu_tasks(struct rcu_head *rhp, rcu_callback_t func);
DEFINE_RCU_TASKS(rcu_tasks, rcu_tasks_wait_gp, call_rcu_tasks, "RCU Tasks");

static void tasks_rcu_exit_srcu_stall(struct timer_list *unused)
{
#ifndef CONFIG_TINY_RCU
	int rtsi;

	rtsi = READ_ONCE(rcu_task_stall_info);
	pr_info("%s: %s grace period number %lu (since boot) gp_state: %s is %lu jiffies old.\n",
		__func__, rcu_tasks.kname, rcu_tasks.tasks_gp_seq,
		tasks_gp_state_getname(&rcu_tasks), jiffies - rcu_tasks.gp_jiffies);
	pr_info("Please check any exiting tasks stuck between calls to exit_tasks_rcu_start() and exit_tasks_rcu_finish()\n");
	tasks_rcu_exit_srcu_stall_timer.expires = jiffies + rtsi;
	add_timer(&tasks_rcu_exit_srcu_stall_timer);
#endif // #ifndef CONFIG_TINY_RCU
}

/**
 * call_rcu_tasks() - Queue an RCU for invocation task-based grace period
 * @rhp: structure to be used for queueing the RCU updates.
 * @func: actual callback function to be invoked after the grace period
 *
 * The callback function will be invoked some time after a full grace
 * period elapses, in other words after all currently executing RCU
 * read-side critical sections have completed. call_rcu_tasks() assumes
 * that the read-side critical sections end at a voluntary context
 * switch (not a preemption!), cond_resched_tasks_rcu_qs(), entry into idle,
 * or transition to usermode execution.  As such, there are no read-side
 * primitives analogous to rcu_read_lock() and rcu_read_unlock() because
 * this primitive is intended to determine that all tasks have passed
 * through a safe state, not so much for data-structure synchronization.
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
	rcu_barrier_tasks_generic(&rcu_tasks);
}
EXPORT_SYMBOL_GPL(rcu_barrier_tasks);

static int __init rcu_spawn_tasks_kthread(void)
{
	cblist_init_generic(&rcu_tasks);
	rcu_tasks.gp_sleep = HZ / 10;
	rcu_tasks.init_fract = HZ / 10;
	rcu_tasks.pregp_func = rcu_tasks_pregp_step;
	rcu_tasks.pertask_func = rcu_tasks_pertask;
	rcu_tasks.postscan_func = rcu_tasks_postscan;
	rcu_tasks.holdouts_func = check_all_holdout_tasks;
	rcu_tasks.postgp_func = rcu_tasks_postgp;
	rcu_spawn_tasks_kthread_generic(&rcu_tasks);
	return 0;
}

#if !defined(CONFIG_TINY_RCU)
void show_rcu_tasks_classic_gp_kthread(void)
{
	show_rcu_tasks_generic_gp_kthread(&rcu_tasks, "");
}
EXPORT_SYMBOL_GPL(show_rcu_tasks_classic_gp_kthread);
#endif // !defined(CONFIG_TINY_RCU)

/*
 * Contribute to protect against tasklist scan blind spot while the
 * task is exiting and may be removed from the tasklist. See
 * corresponding synchronize_srcu() for further details.
 */
void exit_tasks_rcu_start(void) __acquires(&tasks_rcu_exit_srcu)
{
	current->rcu_tasks_idx = __srcu_read_lock(&tasks_rcu_exit_srcu);
}

/*
 * Contribute to protect against tasklist scan blind spot while the
 * task is exiting and may be removed from the tasklist. See
 * corresponding synchronize_srcu() for further details.
 */
void exit_tasks_rcu_stop(void) __releases(&tasks_rcu_exit_srcu)
{
	struct task_struct *t = current;

	__srcu_read_unlock(&tasks_rcu_exit_srcu, t->rcu_tasks_idx);
}

/*
 * Contribute to protect against tasklist scan blind spot while the
 * task is exiting and may be removed from the tasklist. See
 * corresponding synchronize_srcu() for further details.
 */
void exit_tasks_rcu_finish(void)
{
	exit_tasks_rcu_stop();
	exit_tasks_rcu_finish_trace(current);
}

#else /* #ifdef CONFIG_TASKS_RCU */
void exit_tasks_rcu_start(void) { }
void exit_tasks_rcu_stop(void) { }
void exit_tasks_rcu_finish(void) { exit_tasks_rcu_finish_trace(current); }
#endif /* #else #ifdef CONFIG_TASKS_RCU */

#ifdef CONFIG_TASKS_RUDE_RCU

////////////////////////////////////////////////////////////////////////
//
// "Rude" variant of Tasks RCU, inspired by Steve Rostedt's trick of
// passing an empty function to schedule_on_each_cpu().  This approach
// provides an asynchronous call_rcu_tasks_rude() API and batching of
// concurrent calls to the synchronous synchronize_rcu_tasks_rude() API.
// This invokes schedule_on_each_cpu() in order to send IPIs far and wide
// and induces otherwise unnecessary context switches on all online CPUs,
// whether idle or not.
//
// Callback handling is provided by the rcu_tasks_kthread() function.
//
// Ordering is provided by the scheduler's context-switch code.

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
 * cond_resched_tasks_rcu_qs(), or transition to usermode execution (as
 * usermode execution is schedulable). As such, there are no read-side
 * primitives analogous to rcu_read_lock() and rcu_read_unlock() because
 * this primitive is intended to determine that all tasks have passed
 * through a safe state, not so much for data-structure synchronization.
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
 * cond_resched_tasks_rcu_qs(), userspace execution (which is a schedulable
 * context), and (in theory, anyway) cond_resched().
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
	rcu_barrier_tasks_generic(&rcu_tasks_rude);
}
EXPORT_SYMBOL_GPL(rcu_barrier_tasks_rude);

static int __init rcu_spawn_tasks_rude_kthread(void)
{
	cblist_init_generic(&rcu_tasks_rude);
	rcu_tasks_rude.gp_sleep = HZ / 10;
	rcu_spawn_tasks_kthread_generic(&rcu_tasks_rude);
	return 0;
}

#if !defined(CONFIG_TINY_RCU)
void show_rcu_tasks_rude_gp_kthread(void)
{
	show_rcu_tasks_generic_gp_kthread(&rcu_tasks_rude, "");
}
EXPORT_SYMBOL_GPL(show_rcu_tasks_rude_gp_kthread);
#endif // !defined(CONFIG_TINY_RCU)
#endif /* #ifdef CONFIG_TASKS_RUDE_RCU */

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
// 3.	Avoids expensive read-side instructions, having overhead similar
//	to that of Preemptible RCU.
//
// There are of course downsides.  For example, the grace-period code
// can send IPIs to CPUs, even when those CPUs are in the idle loop or
// in nohz_full userspace.  If needed, these downsides can be at least
// partially remedied.
//
// Perhaps most important, this variant of RCU does not affect the vanilla
// flavors, rcu_preempt and rcu_sched.  The fact that RCU Tasks Trace
// readers can operate from idle, offline, and exception entry/exit in no
// way allows rcu_preempt and rcu_sched readers to also do so.
//
// The implementation uses rcu_tasks_wait_gp(), which relies on function
// pointers in the rcu_tasks structure.  The rcu_spawn_tasks_trace_kthread()
// function sets these function pointers up so that rcu_tasks_wait_gp()
// invokes these functions in this order:
//
// rcu_tasks_trace_pregp_step():
//	Disables CPU hotplug, adds all currently executing tasks to the
//	holdout list, then checks the state of all tasks that blocked
//	or were preempted within their current RCU Tasks Trace read-side
//	critical section, adding them to the holdout list if appropriate.
//	Finally, this function re-enables CPU hotplug.
// The ->pertask_func() pointer is NULL, so there is no per-task processing.
// rcu_tasks_trace_postscan():
//	Invokes synchronize_rcu() to wait for late-stage exiting tasks
//	to finish exiting.
// check_all_holdout_tasks_trace(), repeatedly until holdout list is empty:
//	Scans the holdout list, attempting to identify a quiescent state
//	for each task on the list.  If there is a quiescent state, the
//	corresponding task is removed from the holdout list.  Once this
//	list is empty, the grace period has completed.
// rcu_tasks_trace_postgp():
//	Provides the needed full memory barrier and does debug checks.
//
// The exit_tasks_rcu_finish_trace() synchronizes with exiting tasks.
//
// Pre-grace-period update-side code is ordered before the grace period
// via the ->cbs_lock and barriers in rcu_tasks_kthread().  Pre-grace-period
// read-side code is ordered before the grace period by atomic operations
// on .b.need_qs flag of each task involved in this process, or by scheduler
// context-switch ordering (for locked-down non-running readers).

// The lockdep state must be outside of #ifdef to be useful.
#ifdef CONFIG_DEBUG_LOCK_ALLOC
static struct lock_class_key rcu_lock_trace_key;
struct lockdep_map rcu_trace_lock_map =
	STATIC_LOCKDEP_MAP_INIT("rcu_read_lock_trace", &rcu_lock_trace_key);
EXPORT_SYMBOL_GPL(rcu_trace_lock_map);
#endif /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */

#ifdef CONFIG_TASKS_TRACE_RCU

// Record outstanding IPIs to each CPU.  No point in sending two...
static DEFINE_PER_CPU(bool, trc_ipi_to_cpu);

// The number of detections of task quiescent state relying on
// heavyweight readers executing explicit memory barriers.
static unsigned long n_heavy_reader_attempts;
static unsigned long n_heavy_reader_updates;
static unsigned long n_heavy_reader_ofl_updates;
static unsigned long n_trc_holdouts;

void call_rcu_tasks_trace(struct rcu_head *rhp, rcu_callback_t func);
DEFINE_RCU_TASKS(rcu_tasks_trace, rcu_tasks_wait_gp, call_rcu_tasks_trace,
		 "RCU Tasks Trace");

/* Load from ->trc_reader_special.b.need_qs with proper ordering. */
static u8 rcu_ld_need_qs(struct task_struct *t)
{
	smp_mb(); // Enforce full grace-period ordering.
	return smp_load_acquire(&t->trc_reader_special.b.need_qs);
}

/* Store to ->trc_reader_special.b.need_qs with proper ordering. */
static void rcu_st_need_qs(struct task_struct *t, u8 v)
{
	smp_store_release(&t->trc_reader_special.b.need_qs, v);
	smp_mb(); // Enforce full grace-period ordering.
}

/*
 * Do a cmpxchg() on ->trc_reader_special.b.need_qs, allowing for
 * the four-byte operand-size restriction of some platforms.
 * Returns the old value, which is often ignored.
 */
u8 rcu_trc_cmpxchg_need_qs(struct task_struct *t, u8 old, u8 new)
{
	union rcu_special ret;
	union rcu_special trs_old = READ_ONCE(t->trc_reader_special);
	union rcu_special trs_new = trs_old;

	if (trs_old.b.need_qs != old)
		return trs_old.b.need_qs;
	trs_new.b.need_qs = new;
	ret.s = cmpxchg(&t->trc_reader_special.s, trs_old.s, trs_new.s);
	return ret.b.need_qs;
}
EXPORT_SYMBOL_GPL(rcu_trc_cmpxchg_need_qs);

/*
 * If we are the last reader, signal the grace-period kthread.
 * Also remove from the per-CPU list of blocked tasks.
 */
void rcu_read_unlock_trace_special(struct task_struct *t)
{
	unsigned long flags;
	struct rcu_tasks_percpu *rtpcp;
	union rcu_special trs;

	// Open-coded full-word version of rcu_ld_need_qs().
	smp_mb(); // Enforce full grace-period ordering.
	trs = smp_load_acquire(&t->trc_reader_special);

	if (IS_ENABLED(CONFIG_TASKS_TRACE_RCU_READ_MB) && t->trc_reader_special.b.need_mb)
		smp_mb(); // Pairs with update-side barriers.
	// Update .need_qs before ->trc_reader_nesting for irq/NMI handlers.
	if (trs.b.need_qs == (TRC_NEED_QS_CHECKED | TRC_NEED_QS)) {
		u8 result = rcu_trc_cmpxchg_need_qs(t, TRC_NEED_QS_CHECKED | TRC_NEED_QS,
						       TRC_NEED_QS_CHECKED);

		WARN_ONCE(result != trs.b.need_qs, "%s: result = %d", __func__, result);
	}
	if (trs.b.blocked) {
		rtpcp = per_cpu_ptr(rcu_tasks_trace.rtpcpu, t->trc_blkd_cpu);
		raw_spin_lock_irqsave_rcu_node(rtpcp, flags);
		list_del_init(&t->trc_blkd_node);
		WRITE_ONCE(t->trc_reader_special.b.blocked, false);
		raw_spin_unlock_irqrestore_rcu_node(rtpcp, flags);
	}
	WRITE_ONCE(t->trc_reader_nesting, 0);
}
EXPORT_SYMBOL_GPL(rcu_read_unlock_trace_special);

/* Add a newly blocked reader task to its CPU's list. */
void rcu_tasks_trace_qs_blkd(struct task_struct *t)
{
	unsigned long flags;
	struct rcu_tasks_percpu *rtpcp;

	local_irq_save(flags);
	rtpcp = this_cpu_ptr(rcu_tasks_trace.rtpcpu);
	raw_spin_lock_rcu_node(rtpcp); // irqs already disabled
	t->trc_blkd_cpu = smp_processor_id();
	if (!rtpcp->rtp_blkd_tasks.next)
		INIT_LIST_HEAD(&rtpcp->rtp_blkd_tasks);
	list_add(&t->trc_blkd_node, &rtpcp->rtp_blkd_tasks);
	WRITE_ONCE(t->trc_reader_special.b.blocked, true);
	raw_spin_unlock_irqrestore_rcu_node(rtpcp, flags);
}
EXPORT_SYMBOL_GPL(rcu_tasks_trace_qs_blkd);

/* Add a task to the holdout list, if it is not already on the list. */
static void trc_add_holdout(struct task_struct *t, struct list_head *bhp)
{
	if (list_empty(&t->trc_holdout_list)) {
		get_task_struct(t);
		list_add(&t->trc_holdout_list, bhp);
		n_trc_holdouts++;
	}
}

/* Remove a task from the holdout list, if it is in fact present. */
static void trc_del_holdout(struct task_struct *t)
{
	if (!list_empty(&t->trc_holdout_list)) {
		list_del_init(&t->trc_holdout_list);
		put_task_struct(t);
		n_trc_holdouts--;
	}
}

/* IPI handler to check task state. */
static void trc_read_check_handler(void *t_in)
{
	int nesting;
	struct task_struct *t = current;
	struct task_struct *texp = t_in;

	// If the task is no longer running on this CPU, leave.
	if (unlikely(texp != t))
		goto reset_ipi; // Already on holdout list, so will check later.

	// If the task is not in a read-side critical section, and
	// if this is the last reader, awaken the grace-period kthread.
	nesting = READ_ONCE(t->trc_reader_nesting);
	if (likely(!nesting)) {
		rcu_trc_cmpxchg_need_qs(t, 0, TRC_NEED_QS_CHECKED);
		goto reset_ipi;
	}
	// If we are racing with an rcu_read_unlock_trace(), try again later.
	if (unlikely(nesting < 0))
		goto reset_ipi;

	// Get here if the task is in a read-side critical section.
	// Set its state so that it will update state for the grace-period
	// kthread upon exit from that critical section.
	rcu_trc_cmpxchg_need_qs(t, 0, TRC_NEED_QS | TRC_NEED_QS_CHECKED);

reset_ipi:
	// Allow future IPIs to be sent on CPU and for task.
	// Also order this IPI handler against any later manipulations of
	// the intended task.
	smp_store_release(per_cpu_ptr(&trc_ipi_to_cpu, smp_processor_id()), false); // ^^^
	smp_store_release(&texp->trc_ipi_to_cpu, -1); // ^^^
}

/* Callback function for scheduler to check locked-down task.  */
static int trc_inspect_reader(struct task_struct *t, void *bhp_in)
{
	struct list_head *bhp = bhp_in;
	int cpu = task_cpu(t);
	int nesting;
	bool ofl = cpu_is_offline(cpu);

	if (task_curr(t) && !ofl) {
		// If no chance of heavyweight readers, do it the hard way.
		if (!IS_ENABLED(CONFIG_TASKS_TRACE_RCU_READ_MB))
			return -EINVAL;

		// If heavyweight readers are enabled on the remote task,
		// we can inspect its state despite its currently running.
		// However, we cannot safely change its state.
		n_heavy_reader_attempts++;
		// Check for "running" idle tasks on offline CPUs.
		if (!rcu_dynticks_zero_in_eqs(cpu, &t->trc_reader_nesting))
			return -EINVAL; // No quiescent state, do it the hard way.
		n_heavy_reader_updates++;
		nesting = 0;
	} else {
		// The task is not running, so C-language access is safe.
		nesting = t->trc_reader_nesting;
		WARN_ON_ONCE(ofl && task_curr(t) && !is_idle_task(t));
		if (IS_ENABLED(CONFIG_TASKS_TRACE_RCU_READ_MB) && ofl)
			n_heavy_reader_ofl_updates++;
	}

	// If not exiting a read-side critical section, mark as checked
	// so that the grace-period kthread will remove it from the
	// holdout list.
	if (!nesting) {
		rcu_trc_cmpxchg_need_qs(t, 0, TRC_NEED_QS_CHECKED);
		return 0;  // In QS, so done.
	}
	if (nesting < 0)
		return -EINVAL; // Reader transitioning, try again later.

	// The task is in a read-side critical section, so set up its
	// state so that it will update state upon exit from that critical
	// section.
	if (!rcu_trc_cmpxchg_need_qs(t, 0, TRC_NEED_QS | TRC_NEED_QS_CHECKED))
		trc_add_holdout(t, bhp);
	return 0;
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
		rcu_trc_cmpxchg_need_qs(t, 0, TRC_NEED_QS_CHECKED);
		WARN_ON_ONCE(READ_ONCE(t->trc_reader_nesting));
		return;
	}

	// Attempt to nail down the task for inspection.
	get_task_struct(t);
	if (!task_call_func(t, trc_inspect_reader, bhp)) {
		put_task_struct(t);
		return;
	}
	put_task_struct(t);

	// If this task is not yet on the holdout list, then we are in
	// an RCU read-side critical section.  Otherwise, the invocation of
	// trc_add_holdout() that added it to the list did the necessary
	// get_task_struct().  Either way, the task cannot be freed out
	// from under this code.

	// If currently running, send an IPI, either way, add to list.
	trc_add_holdout(t, bhp);
	if (task_curr(t) &&
	    time_after(jiffies + 1, rcu_tasks_trace.gp_start + rcu_task_ipi_delay)) {
		// The task is currently running, so try IPIing it.
		cpu = task_cpu(t);

		// If there is already an IPI outstanding, let it happen.
		if (per_cpu(trc_ipi_to_cpu, cpu) || t->trc_ipi_to_cpu >= 0)
			return;

		per_cpu(trc_ipi_to_cpu, cpu) = true;
		t->trc_ipi_to_cpu = cpu;
		rcu_tasks_trace.n_ipis++;
		if (smp_call_function_single(cpu, trc_read_check_handler, t, 0)) {
			// Just in case there is some other reason for
			// failure than the target CPU being offline.
			WARN_ONCE(1, "%s():  smp_call_function_single() failed for CPU: %d\n",
				  __func__, cpu);
			rcu_tasks_trace.n_ipis_fails++;
			per_cpu(trc_ipi_to_cpu, cpu) = false;
			t->trc_ipi_to_cpu = -1;
		}
	}
}

/*
 * Initialize for first-round processing for the specified task.
 * Return false if task is NULL or already taken care of, true otherwise.
 */
static bool rcu_tasks_trace_pertask_prep(struct task_struct *t, bool notself)
{
	// During early boot when there is only the one boot CPU, there
	// is no idle task for the other CPUs.	Also, the grace-period
	// kthread is always in a quiescent state.  In addition, just return
	// if this task is already on the list.
	if (unlikely(t == NULL) || (t == current && notself) || !list_empty(&t->trc_holdout_list))
		return false;

	rcu_st_need_qs(t, 0);
	t->trc_ipi_to_cpu = -1;
	return true;
}

/* Do first-round processing for the specified task. */
static void rcu_tasks_trace_pertask(struct task_struct *t, struct list_head *hop)
{
	if (rcu_tasks_trace_pertask_prep(t, true))
		trc_wait_for_one_reader(t, hop);
}

/* Initialize for a new RCU-tasks-trace grace period. */
static void rcu_tasks_trace_pregp_step(struct list_head *hop)
{
	LIST_HEAD(blkd_tasks);
	int cpu;
	unsigned long flags;
	struct rcu_tasks_percpu *rtpcp;
	struct task_struct *t;

	// There shouldn't be any old IPIs, but...
	for_each_possible_cpu(cpu)
		WARN_ON_ONCE(per_cpu(trc_ipi_to_cpu, cpu));

	// Disable CPU hotplug across the CPU scan for the benefit of
	// any IPIs that might be needed.  This also waits for all readers
	// in CPU-hotplug code paths.
	cpus_read_lock();

	// These rcu_tasks_trace_pertask_prep() calls are serialized to
	// allow safe access to the hop list.
	for_each_online_cpu(cpu) {
		rcu_read_lock();
		t = cpu_curr_snapshot(cpu);
		if (rcu_tasks_trace_pertask_prep(t, true))
			trc_add_holdout(t, hop);
		rcu_read_unlock();
		cond_resched_tasks_rcu_qs();
	}

	// Only after all running tasks have been accounted for is it
	// safe to take care of the tasks that have blocked within their
	// current RCU tasks trace read-side critical section.
	for_each_possible_cpu(cpu) {
		rtpcp = per_cpu_ptr(rcu_tasks_trace.rtpcpu, cpu);
		raw_spin_lock_irqsave_rcu_node(rtpcp, flags);
		list_splice_init(&rtpcp->rtp_blkd_tasks, &blkd_tasks);
		while (!list_empty(&blkd_tasks)) {
			rcu_read_lock();
			t = list_first_entry(&blkd_tasks, struct task_struct, trc_blkd_node);
			list_del_init(&t->trc_blkd_node);
			list_add(&t->trc_blkd_node, &rtpcp->rtp_blkd_tasks);
			raw_spin_unlock_irqrestore_rcu_node(rtpcp, flags);
			rcu_tasks_trace_pertask(t, hop);
			rcu_read_unlock();
			raw_spin_lock_irqsave_rcu_node(rtpcp, flags);
		}
		raw_spin_unlock_irqrestore_rcu_node(rtpcp, flags);
		cond_resched_tasks_rcu_qs();
	}

	// Re-enable CPU hotplug now that the holdout list is populated.
	cpus_read_unlock();
}

/*
 * Do intermediate processing between task and holdout scans.
 */
static void rcu_tasks_trace_postscan(struct list_head *hop)
{
	// Wait for late-stage exiting tasks to finish exiting.
	// These might have passed the call to exit_tasks_rcu_finish().

	// If you remove the following line, update rcu_trace_implies_rcu_gp()!!!
	synchronize_rcu();
	// Any tasks that exit after this point will set
	// TRC_NEED_QS_CHECKED in ->trc_reader_special.b.need_qs.
}

/* Communicate task state back to the RCU tasks trace stall warning request. */
struct trc_stall_chk_rdr {
	int nesting;
	int ipi_to_cpu;
	u8 needqs;
};

static int trc_check_slow_task(struct task_struct *t, void *arg)
{
	struct trc_stall_chk_rdr *trc_rdrp = arg;

	if (task_curr(t) && cpu_online(task_cpu(t)))
		return false; // It is running, so decline to inspect it.
	trc_rdrp->nesting = READ_ONCE(t->trc_reader_nesting);
	trc_rdrp->ipi_to_cpu = READ_ONCE(t->trc_ipi_to_cpu);
	trc_rdrp->needqs = rcu_ld_need_qs(t);
	return true;
}

/* Show the state of a task stalling the current RCU tasks trace GP. */
static void show_stalled_task_trace(struct task_struct *t, bool *firstreport)
{
	int cpu;
	struct trc_stall_chk_rdr trc_rdr;
	bool is_idle_tsk = is_idle_task(t);

	if (*firstreport) {
		pr_err("INFO: rcu_tasks_trace detected stalls on tasks:\n");
		*firstreport = false;
	}
	cpu = task_cpu(t);
	if (!task_call_func(t, trc_check_slow_task, &trc_rdr))
		pr_alert("P%d: %c%c\n",
			 t->pid,
			 ".I"[t->trc_ipi_to_cpu >= 0],
			 ".i"[is_idle_tsk]);
	else
		pr_alert("P%d: %c%c%c%c nesting: %d%c%c cpu: %d%s\n",
			 t->pid,
			 ".I"[trc_rdr.ipi_to_cpu >= 0],
			 ".i"[is_idle_tsk],
			 ".N"[cpu >= 0 && tick_nohz_full_cpu(cpu)],
			 ".B"[!!data_race(t->trc_reader_special.b.blocked)],
			 trc_rdr.nesting,
			 " !CN"[trc_rdr.needqs & 0x3],
			 " ?"[trc_rdr.needqs > 0x3],
			 cpu, cpu_online(cpu) ? "" : "(offline)");
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

	// Disable CPU hotplug across the holdout list scan for IPIs.
	cpus_read_lock();

	list_for_each_entry_safe(t, g, hop, trc_holdout_list) {
		// If safe and needed, try to check the current task.
		if (READ_ONCE(t->trc_ipi_to_cpu) == -1 &&
		    !(rcu_ld_need_qs(t) & TRC_NEED_QS_CHECKED))
			trc_wait_for_one_reader(t, hop);

		// If check succeeded, remove this task from the list.
		if (smp_load_acquire(&t->trc_ipi_to_cpu) == -1 &&
		    rcu_ld_need_qs(t) == TRC_NEED_QS_CHECKED)
			trc_del_holdout(t);
		else if (needreport)
			show_stalled_task_trace(t, firstreport);
		cond_resched_tasks_rcu_qs();
	}

	// Re-enable CPU hotplug now that the holdout list scan has completed.
	cpus_read_unlock();

	if (needreport) {
		if (*firstreport)
			pr_err("INFO: rcu_tasks_trace detected stalls? (Late IPI?)\n");
		show_stalled_ipi_trace();
	}
}

static void rcu_tasks_trace_empty_fn(void *unused)
{
}

/* Wait for grace period to complete and provide ordering. */
static void rcu_tasks_trace_postgp(struct rcu_tasks *rtp)
{
	int cpu;

	// Wait for any lingering IPI handlers to complete.  Note that
	// if a CPU has gone offline or transitioned to userspace in the
	// meantime, all IPI handlers should have been drained beforehand.
	// Yes, this assumes that CPUs process IPIs in order.  If that ever
	// changes, there will need to be a recheck and/or timed wait.
	for_each_online_cpu(cpu)
		if (WARN_ON_ONCE(smp_load_acquire(per_cpu_ptr(&trc_ipi_to_cpu, cpu))))
			smp_call_function_single(cpu, rcu_tasks_trace_empty_fn, NULL, 1);

	smp_mb(); // Caller's code must be ordered after wakeup.
		  // Pairs with pretty much every ordering primitive.
}

/* Report any needed quiescent state for this exiting task. */
static void exit_tasks_rcu_finish_trace(struct task_struct *t)
{
	union rcu_special trs = READ_ONCE(t->trc_reader_special);

	rcu_trc_cmpxchg_need_qs(t, 0, TRC_NEED_QS_CHECKED);
	WARN_ON_ONCE(READ_ONCE(t->trc_reader_nesting));
	if (WARN_ON_ONCE(rcu_ld_need_qs(t) & TRC_NEED_QS || trs.b.blocked))
		rcu_read_unlock_trace_special(t);
	else
		WRITE_ONCE(t->trc_reader_nesting, 0);
}

/**
 * call_rcu_tasks_trace() - Queue a callback trace task-based grace period
 * @rhp: structure to be used for queueing the RCU updates.
 * @func: actual callback function to be invoked after the grace period
 *
 * The callback function will be invoked some time after a trace rcu-tasks
 * grace period elapses, in other words after all currently executing
 * trace rcu-tasks read-side critical sections have completed. These
 * read-side critical sections are delimited by calls to rcu_read_lock_trace()
 * and rcu_read_unlock_trace().
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
 * grace period has elapsed, in other words after all currently executing
 * trace rcu-tasks read-side critical sections have elapsed. These read-side
 * critical sections are delimited by calls to rcu_read_lock_trace()
 * and rcu_read_unlock_trace().
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
	rcu_barrier_tasks_generic(&rcu_tasks_trace);
}
EXPORT_SYMBOL_GPL(rcu_barrier_tasks_trace);

static int __init rcu_spawn_tasks_trace_kthread(void)
{
	cblist_init_generic(&rcu_tasks_trace);
	if (IS_ENABLED(CONFIG_TASKS_TRACE_RCU_READ_MB)) {
		rcu_tasks_trace.gp_sleep = HZ / 10;
		rcu_tasks_trace.init_fract = HZ / 10;
	} else {
		rcu_tasks_trace.gp_sleep = HZ / 200;
		if (rcu_tasks_trace.gp_sleep <= 0)
			rcu_tasks_trace.gp_sleep = 1;
		rcu_tasks_trace.init_fract = HZ / 200;
		if (rcu_tasks_trace.init_fract <= 0)
			rcu_tasks_trace.init_fract = 1;
	}
	rcu_tasks_trace.pregp_func = rcu_tasks_trace_pregp_step;
	rcu_tasks_trace.postscan_func = rcu_tasks_trace_postscan;
	rcu_tasks_trace.holdouts_func = check_all_holdout_tasks_trace;
	rcu_tasks_trace.postgp_func = rcu_tasks_trace_postgp;
	rcu_spawn_tasks_kthread_generic(&rcu_tasks_trace);
	return 0;
}

#if !defined(CONFIG_TINY_RCU)
void show_rcu_tasks_trace_gp_kthread(void)
{
	char buf[64];

	sprintf(buf, "N%lu h:%lu/%lu/%lu",
		data_race(n_trc_holdouts),
		data_race(n_heavy_reader_ofl_updates),
		data_race(n_heavy_reader_updates),
		data_race(n_heavy_reader_attempts));
	show_rcu_tasks_generic_gp_kthread(&rcu_tasks_trace, buf);
}
EXPORT_SYMBOL_GPL(show_rcu_tasks_trace_gp_kthread);
#endif // !defined(CONFIG_TINY_RCU)

#else /* #ifdef CONFIG_TASKS_TRACE_RCU */
static void exit_tasks_rcu_finish_trace(struct task_struct *t) { }
#endif /* #else #ifdef CONFIG_TASKS_TRACE_RCU */

#ifndef CONFIG_TINY_RCU
void show_rcu_tasks_gp_kthreads(void)
{
	show_rcu_tasks_classic_gp_kthread();
	show_rcu_tasks_rude_gp_kthread();
	show_rcu_tasks_trace_gp_kthread();
}
#endif /* #ifndef CONFIG_TINY_RCU */

#ifdef CONFIG_PROVE_RCU
struct rcu_tasks_test_desc {
	struct rcu_head rh;
	const char *name;
	bool notrun;
	unsigned long runstart;
};

static struct rcu_tasks_test_desc tests[] = {
	{
		.name = "call_rcu_tasks()",
		/* If not defined, the test is skipped. */
		.notrun = IS_ENABLED(CONFIG_TASKS_RCU),
	},
	{
		.name = "call_rcu_tasks_rude()",
		/* If not defined, the test is skipped. */
		.notrun = IS_ENABLED(CONFIG_TASKS_RUDE_RCU),
	},
	{
		.name = "call_rcu_tasks_trace()",
		/* If not defined, the test is skipped. */
		.notrun = IS_ENABLED(CONFIG_TASKS_TRACE_RCU)
	}
};

static void test_rcu_tasks_callback(struct rcu_head *rhp)
{
	struct rcu_tasks_test_desc *rttd =
		container_of(rhp, struct rcu_tasks_test_desc, rh);

	pr_info("Callback from %s invoked.\n", rttd->name);

	rttd->notrun = false;
}

static void rcu_tasks_initiate_self_tests(void)
{
	pr_info("Running RCU-tasks wait API self tests\n");
#ifdef CONFIG_TASKS_RCU
	tests[0].runstart = jiffies;
	synchronize_rcu_tasks();
	call_rcu_tasks(&tests[0].rh, test_rcu_tasks_callback);
#endif

#ifdef CONFIG_TASKS_RUDE_RCU
	tests[1].runstart = jiffies;
	synchronize_rcu_tasks_rude();
	call_rcu_tasks_rude(&tests[1].rh, test_rcu_tasks_callback);
#endif

#ifdef CONFIG_TASKS_TRACE_RCU
	tests[2].runstart = jiffies;
	synchronize_rcu_tasks_trace();
	call_rcu_tasks_trace(&tests[2].rh, test_rcu_tasks_callback);
#endif
}

/*
 * Return:  0 - test passed
 *	    1 - test failed, but have not timed out yet
 *	   -1 - test failed and timed out
 */
static int rcu_tasks_verify_self_tests(void)
{
	int ret = 0;
	int i;
	unsigned long bst = rcu_task_stall_timeout;

	if (bst <= 0 || bst > RCU_TASK_BOOT_STALL_TIMEOUT)
		bst = RCU_TASK_BOOT_STALL_TIMEOUT;
	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		while (tests[i].notrun) {		// still hanging.
			if (time_after(jiffies, tests[i].runstart + bst)) {
				pr_err("%s has failed boot-time tests.\n", tests[i].name);
				ret = -1;
				break;
			}
			ret = 1;
			break;
		}
	}
	WARN_ON(ret < 0);

	return ret;
}

/*
 * Repeat the rcu_tasks_verify_self_tests() call once every second until the
 * test passes or has timed out.
 */
static struct delayed_work rcu_tasks_verify_work;
static void rcu_tasks_verify_work_fn(struct work_struct *work __maybe_unused)
{
	int ret = rcu_tasks_verify_self_tests();

	if (ret <= 0)
		return;

	/* Test fails but not timed out yet, reschedule another check */
	schedule_delayed_work(&rcu_tasks_verify_work, HZ);
}

static int rcu_tasks_verify_schedule_work(void)
{
	INIT_DELAYED_WORK(&rcu_tasks_verify_work, rcu_tasks_verify_work_fn);
	rcu_tasks_verify_work_fn(NULL);
	return 0;
}
late_initcall(rcu_tasks_verify_schedule_work);
#else /* #ifdef CONFIG_PROVE_RCU */
static void rcu_tasks_initiate_self_tests(void) { }
#endif /* #else #ifdef CONFIG_PROVE_RCU */

void __init rcu_init_tasks_generic(void)
{
#ifdef CONFIG_TASKS_RCU
	rcu_spawn_tasks_kthread();
#endif

#ifdef CONFIG_TASKS_RUDE_RCU
	rcu_spawn_tasks_rude_kthread();
#endif

#ifdef CONFIG_TASKS_TRACE_RCU
	rcu_spawn_tasks_trace_kthread();
#endif

	// Run the self-tests.
	rcu_tasks_initiate_self_tests();
}

#else /* #ifdef CONFIG_TASKS_RCU_GENERIC */
static inline void rcu_tasks_bootup_oddness(void) {}
#endif /* #else #ifdef CONFIG_TASKS_RCU_GENERIC */
