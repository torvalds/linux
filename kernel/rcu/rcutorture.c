// SPDX-License-Identifier: GPL-2.0+
/*
 * Read-Copy Update module-based torture test facility
 *
 * Copyright (C) IBM Corporation, 2005, 2006
 *
 * Authors: Paul E. McKenney <paulmck@linux.ibm.com>
 *	  Josh Triplett <josh@joshtriplett.org>
 *
 * See also:  Documentation/RCU/torture.txt
 */

#define pr_fmt(fmt) fmt

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/rcupdate.h>
#include <linux/interrupt.h>
#include <linux/sched/signal.h>
#include <uapi/linux/sched/types.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/moduleparam.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/freezer.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/stat.h>
#include <linux/srcu.h>
#include <linux/slab.h>
#include <linux/trace_clock.h>
#include <asm/byteorder.h>
#include <linux/torture.h>
#include <linux/vmalloc.h>
#include <linux/sched/debug.h>
#include <linux/sched/sysctl.h>
#include <linux/oom.h>
#include <linux/tick.h>

#include "rcu.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Paul E. McKenney <paulmck@linux.ibm.com> and Josh Triplett <josh@joshtriplett.org>");


/* Bits for ->extendables field, extendables param, and related definitions. */
#define RCUTORTURE_RDR_SHIFT	 8	/* Put SRCU index in upper bits. */
#define RCUTORTURE_RDR_MASK	 ((1 << RCUTORTURE_RDR_SHIFT) - 1)
#define RCUTORTURE_RDR_BH	 0x01	/* Extend readers by disabling bh. */
#define RCUTORTURE_RDR_IRQ	 0x02	/*  ... disabling interrupts. */
#define RCUTORTURE_RDR_PREEMPT	 0x04	/*  ... disabling preemption. */
#define RCUTORTURE_RDR_RBH	 0x08	/*  ... rcu_read_lock_bh(). */
#define RCUTORTURE_RDR_SCHED	 0x10	/*  ... rcu_read_lock_sched(). */
#define RCUTORTURE_RDR_RCU	 0x20	/*  ... entering another RCU reader. */
#define RCUTORTURE_RDR_NBITS	 6	/* Number of bits defined above. */
#define RCUTORTURE_MAX_EXTEND	 \
	(RCUTORTURE_RDR_BH | RCUTORTURE_RDR_IRQ | RCUTORTURE_RDR_PREEMPT | \
	 RCUTORTURE_RDR_RBH | RCUTORTURE_RDR_SCHED)
#define RCUTORTURE_RDR_MAX_LOOPS 0x7	/* Maximum reader extensions. */
					/* Must be power of two minus one. */
#define RCUTORTURE_RDR_MAX_SEGS (RCUTORTURE_RDR_MAX_LOOPS + 3)

torture_param(int, extendables, RCUTORTURE_MAX_EXTEND,
	      "Extend readers by disabling bh (1), irqs (2), or preempt (4)");
torture_param(int, fqs_duration, 0,
	      "Duration of fqs bursts (us), 0 to disable");
torture_param(int, fqs_holdoff, 0, "Holdoff time within fqs bursts (us)");
torture_param(int, fqs_stutter, 3, "Wait time between fqs bursts (s)");
torture_param(bool, fwd_progress, 1, "Test grace-period forward progress");
torture_param(int, fwd_progress_div, 4, "Fraction of CPU stall to wait");
torture_param(int, fwd_progress_holdoff, 60,
	      "Time between forward-progress tests (s)");
torture_param(bool, fwd_progress_need_resched, 1,
	      "Hide cond_resched() behind need_resched()");
torture_param(bool, gp_cond, false, "Use conditional/async GP wait primitives");
torture_param(bool, gp_exp, false, "Use expedited GP wait primitives");
torture_param(bool, gp_normal, false,
	     "Use normal (non-expedited) GP wait primitives");
torture_param(bool, gp_sync, false, "Use synchronous GP wait primitives");
torture_param(int, irqreader, 1, "Allow RCU readers from irq handlers");
torture_param(int, n_barrier_cbs, 0,
	     "# of callbacks/kthreads for barrier testing");
torture_param(int, nfakewriters, 4, "Number of RCU fake writer threads");
torture_param(int, nreaders, -1, "Number of RCU reader threads");
torture_param(int, object_debug, 0,
	     "Enable debug-object double call_rcu() testing");
torture_param(int, onoff_holdoff, 0, "Time after boot before CPU hotplugs (s)");
torture_param(int, onoff_interval, 0,
	     "Time between CPU hotplugs (jiffies), 0=disable");
torture_param(int, shuffle_interval, 3, "Number of seconds between shuffles");
torture_param(int, shutdown_secs, 0, "Shutdown time (s), <= zero to disable.");
torture_param(int, stall_cpu, 0, "Stall duration (s), zero to disable.");
torture_param(int, stall_cpu_holdoff, 10,
	     "Time to wait before starting stall (s).");
torture_param(int, stall_cpu_irqsoff, 0, "Disable interrupts while stalling.");
torture_param(int, stat_interval, 60,
	     "Number of seconds between stats printk()s");
torture_param(int, stutter, 5, "Number of seconds to run/halt test");
torture_param(int, test_boost, 1, "Test RCU prio boost: 0=no, 1=maybe, 2=yes.");
torture_param(int, test_boost_duration, 4,
	     "Duration of each boost test, seconds.");
torture_param(int, test_boost_interval, 7,
	     "Interval between boost tests, seconds.");
torture_param(bool, test_no_idle_hz, true,
	     "Test support for tickless idle CPUs");
torture_param(int, verbose, 1,
	     "Enable verbose debugging printk()s");

static char *torture_type = "rcu";
module_param(torture_type, charp, 0444);
MODULE_PARM_DESC(torture_type, "Type of RCU to torture (rcu, srcu, ...)");

static int nrealreaders;
static struct task_struct *writer_task;
static struct task_struct **fakewriter_tasks;
static struct task_struct **reader_tasks;
static struct task_struct *stats_task;
static struct task_struct *fqs_task;
static struct task_struct *boost_tasks[NR_CPUS];
static struct task_struct *stall_task;
static struct task_struct *fwd_prog_task;
static struct task_struct **barrier_cbs_tasks;
static struct task_struct *barrier_task;

#define RCU_TORTURE_PIPE_LEN 10

struct rcu_torture {
	struct rcu_head rtort_rcu;
	int rtort_pipe_count;
	struct list_head rtort_free;
	int rtort_mbtest;
};

static LIST_HEAD(rcu_torture_freelist);
static struct rcu_torture __rcu *rcu_torture_current;
static unsigned long rcu_torture_current_version;
static struct rcu_torture rcu_tortures[10 * RCU_TORTURE_PIPE_LEN];
static DEFINE_SPINLOCK(rcu_torture_lock);
static DEFINE_PER_CPU(long [RCU_TORTURE_PIPE_LEN + 1], rcu_torture_count);
static DEFINE_PER_CPU(long [RCU_TORTURE_PIPE_LEN + 1], rcu_torture_batch);
static atomic_t rcu_torture_wcount[RCU_TORTURE_PIPE_LEN + 1];
static atomic_t n_rcu_torture_alloc;
static atomic_t n_rcu_torture_alloc_fail;
static atomic_t n_rcu_torture_free;
static atomic_t n_rcu_torture_mberror;
static atomic_t n_rcu_torture_error;
static long n_rcu_torture_barrier_error;
static long n_rcu_torture_boost_ktrerror;
static long n_rcu_torture_boost_rterror;
static long n_rcu_torture_boost_failure;
static long n_rcu_torture_boosts;
static atomic_long_t n_rcu_torture_timers;
static long n_barrier_attempts;
static long n_barrier_successes; /* did rcu_barrier test succeed? */
static struct list_head rcu_torture_removed;
static unsigned long shutdown_jiffies;

static int rcu_torture_writer_state;
#define RTWS_FIXED_DELAY	0
#define RTWS_DELAY		1
#define RTWS_REPLACE		2
#define RTWS_DEF_FREE		3
#define RTWS_EXP_SYNC		4
#define RTWS_COND_GET		5
#define RTWS_COND_SYNC		6
#define RTWS_SYNC		7
#define RTWS_STUTTER		8
#define RTWS_STOPPING		9
static const char * const rcu_torture_writer_state_names[] = {
	"RTWS_FIXED_DELAY",
	"RTWS_DELAY",
	"RTWS_REPLACE",
	"RTWS_DEF_FREE",
	"RTWS_EXP_SYNC",
	"RTWS_COND_GET",
	"RTWS_COND_SYNC",
	"RTWS_SYNC",
	"RTWS_STUTTER",
	"RTWS_STOPPING",
};

/* Record reader segment types and duration for first failing read. */
struct rt_read_seg {
	int rt_readstate;
	unsigned long rt_delay_jiffies;
	unsigned long rt_delay_ms;
	unsigned long rt_delay_us;
	bool rt_preempted;
};
static int err_segs_recorded;
static struct rt_read_seg err_segs[RCUTORTURE_RDR_MAX_SEGS];
static int rt_read_nsegs;

static const char *rcu_torture_writer_state_getname(void)
{
	unsigned int i = READ_ONCE(rcu_torture_writer_state);

	if (i >= ARRAY_SIZE(rcu_torture_writer_state_names))
		return "???";
	return rcu_torture_writer_state_names[i];
}

#if defined(CONFIG_RCU_BOOST) && !defined(CONFIG_HOTPLUG_CPU)
#define rcu_can_boost() 1
#else /* #if defined(CONFIG_RCU_BOOST) && !defined(CONFIG_HOTPLUG_CPU) */
#define rcu_can_boost() 0
#endif /* #else #if defined(CONFIG_RCU_BOOST) && !defined(CONFIG_HOTPLUG_CPU) */

#ifdef CONFIG_RCU_TRACE
static u64 notrace rcu_trace_clock_local(void)
{
	u64 ts = trace_clock_local();

	(void)do_div(ts, NSEC_PER_USEC);
	return ts;
}
#else /* #ifdef CONFIG_RCU_TRACE */
static u64 notrace rcu_trace_clock_local(void)
{
	return 0ULL;
}
#endif /* #else #ifdef CONFIG_RCU_TRACE */

/*
 * Stop aggressive CPU-hog tests a bit before the end of the test in order
 * to avoid interfering with test shutdown.
 */
static bool shutdown_time_arrived(void)
{
	return shutdown_secs && time_after(jiffies, shutdown_jiffies - 30 * HZ);
}

static unsigned long boost_starttime;	/* jiffies of next boost test start. */
static DEFINE_MUTEX(boost_mutex);	/* protect setting boost_starttime */
					/*  and boost task create/destroy. */
static atomic_t barrier_cbs_count;	/* Barrier callbacks registered. */
static bool barrier_phase;		/* Test phase. */
static atomic_t barrier_cbs_invoked;	/* Barrier callbacks invoked. */
static wait_queue_head_t *barrier_cbs_wq; /* Coordinate barrier testing. */
static DECLARE_WAIT_QUEUE_HEAD(barrier_wq);

static bool rcu_fwd_cb_nodelay;		/* Short rcu_torture_delay() delays. */

/*
 * Allocate an element from the rcu_tortures pool.
 */
static struct rcu_torture *
rcu_torture_alloc(void)
{
	struct list_head *p;

	spin_lock_bh(&rcu_torture_lock);
	if (list_empty(&rcu_torture_freelist)) {
		atomic_inc(&n_rcu_torture_alloc_fail);
		spin_unlock_bh(&rcu_torture_lock);
		return NULL;
	}
	atomic_inc(&n_rcu_torture_alloc);
	p = rcu_torture_freelist.next;
	list_del_init(p);
	spin_unlock_bh(&rcu_torture_lock);
	return container_of(p, struct rcu_torture, rtort_free);
}

/*
 * Free an element to the rcu_tortures pool.
 */
static void
rcu_torture_free(struct rcu_torture *p)
{
	atomic_inc(&n_rcu_torture_free);
	spin_lock_bh(&rcu_torture_lock);
	list_add_tail(&p->rtort_free, &rcu_torture_freelist);
	spin_unlock_bh(&rcu_torture_lock);
}

/*
 * Operations vector for selecting different types of tests.
 */

struct rcu_torture_ops {
	int ttype;
	void (*init)(void);
	void (*cleanup)(void);
	int (*readlock)(void);
	void (*read_delay)(struct torture_random_state *rrsp,
			   struct rt_read_seg *rtrsp);
	void (*readunlock)(int idx);
	unsigned long (*get_gp_seq)(void);
	unsigned long (*gp_diff)(unsigned long new, unsigned long old);
	void (*deferred_free)(struct rcu_torture *p);
	void (*sync)(void);
	void (*exp_sync)(void);
	unsigned long (*get_state)(void);
	void (*cond_sync)(unsigned long oldstate);
	call_rcu_func_t call;
	void (*cb_barrier)(void);
	void (*fqs)(void);
	void (*stats)(void);
	int (*stall_dur)(void);
	int irq_capable;
	int can_boost;
	int extendables;
	int slow_gps;
	const char *name;
};

static struct rcu_torture_ops *cur_ops;

/*
 * Definitions for rcu torture testing.
 */

static int rcu_torture_read_lock(void) __acquires(RCU)
{
	rcu_read_lock();
	return 0;
}

static void
rcu_read_delay(struct torture_random_state *rrsp, struct rt_read_seg *rtrsp)
{
	unsigned long started;
	unsigned long completed;
	const unsigned long shortdelay_us = 200;
	unsigned long longdelay_ms = 300;
	unsigned long long ts;

	/* We want a short delay sometimes to make a reader delay the grace
	 * period, and we want a long delay occasionally to trigger
	 * force_quiescent_state. */

	if (!rcu_fwd_cb_nodelay &&
	    !(torture_random(rrsp) % (nrealreaders * 2000 * longdelay_ms))) {
		started = cur_ops->get_gp_seq();
		ts = rcu_trace_clock_local();
		if (preempt_count() & (SOFTIRQ_MASK | HARDIRQ_MASK))
			longdelay_ms = 5; /* Avoid triggering BH limits. */
		mdelay(longdelay_ms);
		rtrsp->rt_delay_ms = longdelay_ms;
		completed = cur_ops->get_gp_seq();
		do_trace_rcu_torture_read(cur_ops->name, NULL, ts,
					  started, completed);
	}
	if (!(torture_random(rrsp) % (nrealreaders * 2 * shortdelay_us))) {
		udelay(shortdelay_us);
		rtrsp->rt_delay_us = shortdelay_us;
	}
	if (!preempt_count() &&
	    !(torture_random(rrsp) % (nrealreaders * 500))) {
		torture_preempt_schedule();  /* QS only if preemptible. */
		rtrsp->rt_preempted = true;
	}
}

static void rcu_torture_read_unlock(int idx) __releases(RCU)
{
	rcu_read_unlock();
}

/*
 * Update callback in the pipe.  This should be invoked after a grace period.
 */
static bool
rcu_torture_pipe_update_one(struct rcu_torture *rp)
{
	int i;

	i = rp->rtort_pipe_count;
	if (i > RCU_TORTURE_PIPE_LEN)
		i = RCU_TORTURE_PIPE_LEN;
	atomic_inc(&rcu_torture_wcount[i]);
	if (++rp->rtort_pipe_count >= RCU_TORTURE_PIPE_LEN) {
		rp->rtort_mbtest = 0;
		return true;
	}
	return false;
}

/*
 * Update all callbacks in the pipe.  Suitable for synchronous grace-period
 * primitives.
 */
static void
rcu_torture_pipe_update(struct rcu_torture *old_rp)
{
	struct rcu_torture *rp;
	struct rcu_torture *rp1;

	if (old_rp)
		list_add(&old_rp->rtort_free, &rcu_torture_removed);
	list_for_each_entry_safe(rp, rp1, &rcu_torture_removed, rtort_free) {
		if (rcu_torture_pipe_update_one(rp)) {
			list_del(&rp->rtort_free);
			rcu_torture_free(rp);
		}
	}
}

static void
rcu_torture_cb(struct rcu_head *p)
{
	struct rcu_torture *rp = container_of(p, struct rcu_torture, rtort_rcu);

	if (torture_must_stop_irq()) {
		/* Test is ending, just drop callbacks on the floor. */
		/* The next initialization will pick up the pieces. */
		return;
	}
	if (rcu_torture_pipe_update_one(rp))
		rcu_torture_free(rp);
	else
		cur_ops->deferred_free(rp);
}

static unsigned long rcu_no_completed(void)
{
	return 0;
}

static void rcu_torture_deferred_free(struct rcu_torture *p)
{
	call_rcu(&p->rtort_rcu, rcu_torture_cb);
}

static void rcu_sync_torture_init(void)
{
	INIT_LIST_HEAD(&rcu_torture_removed);
}

static struct rcu_torture_ops rcu_ops = {
	.ttype		= RCU_FLAVOR,
	.init		= rcu_sync_torture_init,
	.readlock	= rcu_torture_read_lock,
	.read_delay	= rcu_read_delay,
	.readunlock	= rcu_torture_read_unlock,
	.get_gp_seq	= rcu_get_gp_seq,
	.gp_diff	= rcu_seq_diff,
	.deferred_free	= rcu_torture_deferred_free,
	.sync		= synchronize_rcu,
	.exp_sync	= synchronize_rcu_expedited,
	.get_state	= get_state_synchronize_rcu,
	.cond_sync	= cond_synchronize_rcu,
	.call		= call_rcu,
	.cb_barrier	= rcu_barrier,
	.fqs		= rcu_force_quiescent_state,
	.stats		= NULL,
	.stall_dur	= rcu_jiffies_till_stall_check,
	.irq_capable	= 1,
	.can_boost	= rcu_can_boost(),
	.extendables	= RCUTORTURE_MAX_EXTEND,
	.name		= "rcu"
};

/*
 * Don't even think about trying any of these in real life!!!
 * The names includes "busted", and they really means it!
 * The only purpose of these functions is to provide a buggy RCU
 * implementation to make sure that rcutorture correctly emits
 * buggy-RCU error messages.
 */
static void rcu_busted_torture_deferred_free(struct rcu_torture *p)
{
	/* This is a deliberate bug for testing purposes only! */
	rcu_torture_cb(&p->rtort_rcu);
}

static void synchronize_rcu_busted(void)
{
	/* This is a deliberate bug for testing purposes only! */
}

static void
call_rcu_busted(struct rcu_head *head, rcu_callback_t func)
{
	/* This is a deliberate bug for testing purposes only! */
	func(head);
}

static struct rcu_torture_ops rcu_busted_ops = {
	.ttype		= INVALID_RCU_FLAVOR,
	.init		= rcu_sync_torture_init,
	.readlock	= rcu_torture_read_lock,
	.read_delay	= rcu_read_delay,  /* just reuse rcu's version. */
	.readunlock	= rcu_torture_read_unlock,
	.get_gp_seq	= rcu_no_completed,
	.deferred_free	= rcu_busted_torture_deferred_free,
	.sync		= synchronize_rcu_busted,
	.exp_sync	= synchronize_rcu_busted,
	.call		= call_rcu_busted,
	.cb_barrier	= NULL,
	.fqs		= NULL,
	.stats		= NULL,
	.irq_capable	= 1,
	.name		= "busted"
};

/*
 * Definitions for srcu torture testing.
 */

DEFINE_STATIC_SRCU(srcu_ctl);
static struct srcu_struct srcu_ctld;
static struct srcu_struct *srcu_ctlp = &srcu_ctl;

static int srcu_torture_read_lock(void) __acquires(srcu_ctlp)
{
	return srcu_read_lock(srcu_ctlp);
}

static void
srcu_read_delay(struct torture_random_state *rrsp, struct rt_read_seg *rtrsp)
{
	long delay;
	const long uspertick = 1000000 / HZ;
	const long longdelay = 10;

	/* We want there to be long-running readers, but not all the time. */

	delay = torture_random(rrsp) %
		(nrealreaders * 2 * longdelay * uspertick);
	if (!delay && in_task()) {
		schedule_timeout_interruptible(longdelay);
		rtrsp->rt_delay_jiffies = longdelay;
	} else {
		rcu_read_delay(rrsp, rtrsp);
	}
}

static void srcu_torture_read_unlock(int idx) __releases(srcu_ctlp)
{
	srcu_read_unlock(srcu_ctlp, idx);
}

static unsigned long srcu_torture_completed(void)
{
	return srcu_batches_completed(srcu_ctlp);
}

static void srcu_torture_deferred_free(struct rcu_torture *rp)
{
	call_srcu(srcu_ctlp, &rp->rtort_rcu, rcu_torture_cb);
}

static void srcu_torture_synchronize(void)
{
	synchronize_srcu(srcu_ctlp);
}

static void srcu_torture_call(struct rcu_head *head,
			      rcu_callback_t func)
{
	call_srcu(srcu_ctlp, head, func);
}

static void srcu_torture_barrier(void)
{
	srcu_barrier(srcu_ctlp);
}

static void srcu_torture_stats(void)
{
	srcu_torture_stats_print(srcu_ctlp, torture_type, TORTURE_FLAG);
}

static void srcu_torture_synchronize_expedited(void)
{
	synchronize_srcu_expedited(srcu_ctlp);
}

static struct rcu_torture_ops srcu_ops = {
	.ttype		= SRCU_FLAVOR,
	.init		= rcu_sync_torture_init,
	.readlock	= srcu_torture_read_lock,
	.read_delay	= srcu_read_delay,
	.readunlock	= srcu_torture_read_unlock,
	.get_gp_seq	= srcu_torture_completed,
	.deferred_free	= srcu_torture_deferred_free,
	.sync		= srcu_torture_synchronize,
	.exp_sync	= srcu_torture_synchronize_expedited,
	.call		= srcu_torture_call,
	.cb_barrier	= srcu_torture_barrier,
	.stats		= srcu_torture_stats,
	.irq_capable	= 1,
	.name		= "srcu"
};

static void srcu_torture_init(void)
{
	rcu_sync_torture_init();
	WARN_ON(init_srcu_struct(&srcu_ctld));
	srcu_ctlp = &srcu_ctld;
}

static void srcu_torture_cleanup(void)
{
	cleanup_srcu_struct(&srcu_ctld);
	srcu_ctlp = &srcu_ctl; /* In case of a later rcutorture run. */
}

/* As above, but dynamically allocated. */
static struct rcu_torture_ops srcud_ops = {
	.ttype		= SRCU_FLAVOR,
	.init		= srcu_torture_init,
	.cleanup	= srcu_torture_cleanup,
	.readlock	= srcu_torture_read_lock,
	.read_delay	= srcu_read_delay,
	.readunlock	= srcu_torture_read_unlock,
	.get_gp_seq	= srcu_torture_completed,
	.deferred_free	= srcu_torture_deferred_free,
	.sync		= srcu_torture_synchronize,
	.exp_sync	= srcu_torture_synchronize_expedited,
	.call		= srcu_torture_call,
	.cb_barrier	= srcu_torture_barrier,
	.stats		= srcu_torture_stats,
	.irq_capable	= 1,
	.name		= "srcud"
};

/* As above, but broken due to inappropriate reader extension. */
static struct rcu_torture_ops busted_srcud_ops = {
	.ttype		= SRCU_FLAVOR,
	.init		= srcu_torture_init,
	.cleanup	= srcu_torture_cleanup,
	.readlock	= srcu_torture_read_lock,
	.read_delay	= rcu_read_delay,
	.readunlock	= srcu_torture_read_unlock,
	.get_gp_seq	= srcu_torture_completed,
	.deferred_free	= srcu_torture_deferred_free,
	.sync		= srcu_torture_synchronize,
	.exp_sync	= srcu_torture_synchronize_expedited,
	.call		= srcu_torture_call,
	.cb_barrier	= srcu_torture_barrier,
	.stats		= srcu_torture_stats,
	.irq_capable	= 1,
	.extendables	= RCUTORTURE_MAX_EXTEND,
	.name		= "busted_srcud"
};

/*
 * Definitions for RCU-tasks torture testing.
 */

static int tasks_torture_read_lock(void)
{
	return 0;
}

static void tasks_torture_read_unlock(int idx)
{
}

static void rcu_tasks_torture_deferred_free(struct rcu_torture *p)
{
	call_rcu_tasks(&p->rtort_rcu, rcu_torture_cb);
}

static struct rcu_torture_ops tasks_ops = {
	.ttype		= RCU_TASKS_FLAVOR,
	.init		= rcu_sync_torture_init,
	.readlock	= tasks_torture_read_lock,
	.read_delay	= rcu_read_delay,  /* just reuse rcu's version. */
	.readunlock	= tasks_torture_read_unlock,
	.get_gp_seq	= rcu_no_completed,
	.deferred_free	= rcu_tasks_torture_deferred_free,
	.sync		= synchronize_rcu_tasks,
	.exp_sync	= synchronize_rcu_tasks,
	.call		= call_rcu_tasks,
	.cb_barrier	= rcu_barrier_tasks,
	.fqs		= NULL,
	.stats		= NULL,
	.irq_capable	= 1,
	.slow_gps	= 1,
	.name		= "tasks"
};

/*
 * Definitions for trivial CONFIG_PREEMPT=n-only torture testing.
 * This implementation does not necessarily work well with CPU hotplug.
 */

static void synchronize_rcu_trivial(void)
{
	int cpu;

	for_each_online_cpu(cpu) {
		rcutorture_sched_setaffinity(current->pid, cpumask_of(cpu));
		WARN_ON_ONCE(raw_smp_processor_id() != cpu);
	}
}

static int rcu_torture_read_lock_trivial(void) __acquires(RCU)
{
	preempt_disable();
	return 0;
}

static void rcu_torture_read_unlock_trivial(int idx) __releases(RCU)
{
	preempt_enable();
}

static struct rcu_torture_ops trivial_ops = {
	.ttype		= RCU_TRIVIAL_FLAVOR,
	.init		= rcu_sync_torture_init,
	.readlock	= rcu_torture_read_lock_trivial,
	.read_delay	= rcu_read_delay,  /* just reuse rcu's version. */
	.readunlock	= rcu_torture_read_unlock_trivial,
	.get_gp_seq	= rcu_no_completed,
	.sync		= synchronize_rcu_trivial,
	.exp_sync	= synchronize_rcu_trivial,
	.fqs		= NULL,
	.stats		= NULL,
	.irq_capable	= 1,
	.name		= "trivial"
};

static unsigned long rcutorture_seq_diff(unsigned long new, unsigned long old)
{
	if (!cur_ops->gp_diff)
		return new - old;
	return cur_ops->gp_diff(new, old);
}

static bool __maybe_unused torturing_tasks(void)
{
	return cur_ops == &tasks_ops;
}

/*
 * RCU torture priority-boost testing.  Runs one real-time thread per
 * CPU for moderate bursts, repeatedly registering RCU callbacks and
 * spinning waiting for them to be invoked.  If a given callback takes
 * too long to be invoked, we assume that priority inversion has occurred.
 */

struct rcu_boost_inflight {
	struct rcu_head rcu;
	int inflight;
};

static void rcu_torture_boost_cb(struct rcu_head *head)
{
	struct rcu_boost_inflight *rbip =
		container_of(head, struct rcu_boost_inflight, rcu);

	/* Ensure RCU-core accesses precede clearing ->inflight */
	smp_store_release(&rbip->inflight, 0);
}

static int old_rt_runtime = -1;

static void rcu_torture_disable_rt_throttle(void)
{
	/*
	 * Disable RT throttling so that rcutorture's boost threads don't get
	 * throttled. Only possible if rcutorture is built-in otherwise the
	 * user should manually do this by setting the sched_rt_period_us and
	 * sched_rt_runtime sysctls.
	 */
	if (!IS_BUILTIN(CONFIG_RCU_TORTURE_TEST) || old_rt_runtime != -1)
		return;

	old_rt_runtime = sysctl_sched_rt_runtime;
	sysctl_sched_rt_runtime = -1;
}

static void rcu_torture_enable_rt_throttle(void)
{
	if (!IS_BUILTIN(CONFIG_RCU_TORTURE_TEST) || old_rt_runtime == -1)
		return;

	sysctl_sched_rt_runtime = old_rt_runtime;
	old_rt_runtime = -1;
}

static bool rcu_torture_boost_failed(unsigned long start, unsigned long end)
{
	if (end - start > test_boost_duration * HZ - HZ / 2) {
		VERBOSE_TOROUT_STRING("rcu_torture_boost boosting failed");
		n_rcu_torture_boost_failure++;

		return true; /* failed */
	}

	return false; /* passed */
}

static int rcu_torture_boost(void *arg)
{
	unsigned long call_rcu_time;
	unsigned long endtime;
	unsigned long oldstarttime;
	struct rcu_boost_inflight rbi = { .inflight = 0 };
	struct sched_param sp;

	VERBOSE_TOROUT_STRING("rcu_torture_boost started");

	/* Set real-time priority. */
	sp.sched_priority = 1;
	if (sched_setscheduler(current, SCHED_FIFO, &sp) < 0) {
		VERBOSE_TOROUT_STRING("rcu_torture_boost RT prio failed!");
		n_rcu_torture_boost_rterror++;
	}

	init_rcu_head_on_stack(&rbi.rcu);
	/* Each pass through the following loop does one boost-test cycle. */
	do {
		/* Track if the test failed already in this test interval? */
		bool failed = false;

		/* Increment n_rcu_torture_boosts once per boost-test */
		while (!kthread_should_stop()) {
			if (mutex_trylock(&boost_mutex)) {
				n_rcu_torture_boosts++;
				mutex_unlock(&boost_mutex);
				break;
			}
			schedule_timeout_uninterruptible(1);
		}
		if (kthread_should_stop())
			goto checkwait;

		/* Wait for the next test interval. */
		oldstarttime = boost_starttime;
		while (ULONG_CMP_LT(jiffies, oldstarttime)) {
			schedule_timeout_interruptible(oldstarttime - jiffies);
			stutter_wait("rcu_torture_boost");
			if (torture_must_stop())
				goto checkwait;
		}

		/* Do one boost-test interval. */
		endtime = oldstarttime + test_boost_duration * HZ;
		call_rcu_time = jiffies;
		while (ULONG_CMP_LT(jiffies, endtime)) {
			/* If we don't have a callback in flight, post one. */
			if (!smp_load_acquire(&rbi.inflight)) {
				/* RCU core before ->inflight = 1. */
				smp_store_release(&rbi.inflight, 1);
				call_rcu(&rbi.rcu, rcu_torture_boost_cb);
				/* Check if the boost test failed */
				failed = failed ||
					 rcu_torture_boost_failed(call_rcu_time,
								 jiffies);
				call_rcu_time = jiffies;
			}
			stutter_wait("rcu_torture_boost");
			if (torture_must_stop())
				goto checkwait;
		}

		/*
		 * If boost never happened, then inflight will always be 1, in
		 * this case the boost check would never happen in the above
		 * loop so do another one here.
		 */
		if (!failed && smp_load_acquire(&rbi.inflight))
			rcu_torture_boost_failed(call_rcu_time, jiffies);

		/*
		 * Set the start time of the next test interval.
		 * Yes, this is vulnerable to long delays, but such
		 * delays simply cause a false negative for the next
		 * interval.  Besides, we are running at RT priority,
		 * so delays should be relatively rare.
		 */
		while (oldstarttime == boost_starttime &&
		       !kthread_should_stop()) {
			if (mutex_trylock(&boost_mutex)) {
				boost_starttime = jiffies +
						  test_boost_interval * HZ;
				mutex_unlock(&boost_mutex);
				break;
			}
			schedule_timeout_uninterruptible(1);
		}

		/* Go do the stutter. */
checkwait:	stutter_wait("rcu_torture_boost");
	} while (!torture_must_stop());

	/* Clean up and exit. */
	while (!kthread_should_stop() || smp_load_acquire(&rbi.inflight)) {
		torture_shutdown_absorb("rcu_torture_boost");
		schedule_timeout_uninterruptible(1);
	}
	destroy_rcu_head_on_stack(&rbi.rcu);
	torture_kthread_stopping("rcu_torture_boost");
	return 0;
}

/*
 * RCU torture force-quiescent-state kthread.  Repeatedly induces
 * bursts of calls to force_quiescent_state(), increasing the probability
 * of occurrence of some important types of race conditions.
 */
static int
rcu_torture_fqs(void *arg)
{
	unsigned long fqs_resume_time;
	int fqs_burst_remaining;

	VERBOSE_TOROUT_STRING("rcu_torture_fqs task started");
	do {
		fqs_resume_time = jiffies + fqs_stutter * HZ;
		while (ULONG_CMP_LT(jiffies, fqs_resume_time) &&
		       !kthread_should_stop()) {
			schedule_timeout_interruptible(1);
		}
		fqs_burst_remaining = fqs_duration;
		while (fqs_burst_remaining > 0 &&
		       !kthread_should_stop()) {
			cur_ops->fqs();
			udelay(fqs_holdoff);
			fqs_burst_remaining -= fqs_holdoff;
		}
		stutter_wait("rcu_torture_fqs");
	} while (!torture_must_stop());
	torture_kthread_stopping("rcu_torture_fqs");
	return 0;
}

/*
 * RCU torture writer kthread.  Repeatedly substitutes a new structure
 * for that pointed to by rcu_torture_current, freeing the old structure
 * after a series of grace periods (the "pipeline").
 */
static int
rcu_torture_writer(void *arg)
{
	bool can_expedite = !rcu_gp_is_expedited() && !rcu_gp_is_normal();
	int expediting = 0;
	unsigned long gp_snap;
	bool gp_cond1 = gp_cond, gp_exp1 = gp_exp, gp_normal1 = gp_normal;
	bool gp_sync1 = gp_sync;
	int i;
	struct rcu_torture *rp;
	struct rcu_torture *old_rp;
	static DEFINE_TORTURE_RANDOM(rand);
	int synctype[] = { RTWS_DEF_FREE, RTWS_EXP_SYNC,
			   RTWS_COND_GET, RTWS_SYNC };
	int nsynctypes = 0;

	VERBOSE_TOROUT_STRING("rcu_torture_writer task started");
	if (!can_expedite)
		pr_alert("%s" TORTURE_FLAG
			 " GP expediting controlled from boot/sysfs for %s.\n",
			 torture_type, cur_ops->name);

	/* Initialize synctype[] array.  If none set, take default. */
	if (!gp_cond1 && !gp_exp1 && !gp_normal1 && !gp_sync1)
		gp_cond1 = gp_exp1 = gp_normal1 = gp_sync1 = true;
	if (gp_cond1 && cur_ops->get_state && cur_ops->cond_sync) {
		synctype[nsynctypes++] = RTWS_COND_GET;
		pr_info("%s: Testing conditional GPs.\n", __func__);
	} else if (gp_cond && (!cur_ops->get_state || !cur_ops->cond_sync)) {
		pr_alert("%s: gp_cond without primitives.\n", __func__);
	}
	if (gp_exp1 && cur_ops->exp_sync) {
		synctype[nsynctypes++] = RTWS_EXP_SYNC;
		pr_info("%s: Testing expedited GPs.\n", __func__);
	} else if (gp_exp && !cur_ops->exp_sync) {
		pr_alert("%s: gp_exp without primitives.\n", __func__);
	}
	if (gp_normal1 && cur_ops->deferred_free) {
		synctype[nsynctypes++] = RTWS_DEF_FREE;
		pr_info("%s: Testing asynchronous GPs.\n", __func__);
	} else if (gp_normal && !cur_ops->deferred_free) {
		pr_alert("%s: gp_normal without primitives.\n", __func__);
	}
	if (gp_sync1 && cur_ops->sync) {
		synctype[nsynctypes++] = RTWS_SYNC;
		pr_info("%s: Testing normal GPs.\n", __func__);
	} else if (gp_sync && !cur_ops->sync) {
		pr_alert("%s: gp_sync without primitives.\n", __func__);
	}
	if (WARN_ONCE(nsynctypes == 0,
		      "rcu_torture_writer: No update-side primitives.\n")) {
		/*
		 * No updates primitives, so don't try updating.
		 * The resulting test won't be testing much, hence the
		 * above WARN_ONCE().
		 */
		rcu_torture_writer_state = RTWS_STOPPING;
		torture_kthread_stopping("rcu_torture_writer");
	}

	do {
		rcu_torture_writer_state = RTWS_FIXED_DELAY;
		schedule_timeout_uninterruptible(1);
		rp = rcu_torture_alloc();
		if (rp == NULL)
			continue;
		rp->rtort_pipe_count = 0;
		rcu_torture_writer_state = RTWS_DELAY;
		udelay(torture_random(&rand) & 0x3ff);
		rcu_torture_writer_state = RTWS_REPLACE;
		old_rp = rcu_dereference_check(rcu_torture_current,
					       current == writer_task);
		rp->rtort_mbtest = 1;
		rcu_assign_pointer(rcu_torture_current, rp);
		smp_wmb(); /* Mods to old_rp must follow rcu_assign_pointer() */
		if (old_rp) {
			i = old_rp->rtort_pipe_count;
			if (i > RCU_TORTURE_PIPE_LEN)
				i = RCU_TORTURE_PIPE_LEN;
			atomic_inc(&rcu_torture_wcount[i]);
			old_rp->rtort_pipe_count++;
			switch (synctype[torture_random(&rand) % nsynctypes]) {
			case RTWS_DEF_FREE:
				rcu_torture_writer_state = RTWS_DEF_FREE;
				cur_ops->deferred_free(old_rp);
				break;
			case RTWS_EXP_SYNC:
				rcu_torture_writer_state = RTWS_EXP_SYNC;
				cur_ops->exp_sync();
				rcu_torture_pipe_update(old_rp);
				break;
			case RTWS_COND_GET:
				rcu_torture_writer_state = RTWS_COND_GET;
				gp_snap = cur_ops->get_state();
				i = torture_random(&rand) % 16;
				if (i != 0)
					schedule_timeout_interruptible(i);
				udelay(torture_random(&rand) % 1000);
				rcu_torture_writer_state = RTWS_COND_SYNC;
				cur_ops->cond_sync(gp_snap);
				rcu_torture_pipe_update(old_rp);
				break;
			case RTWS_SYNC:
				rcu_torture_writer_state = RTWS_SYNC;
				cur_ops->sync();
				rcu_torture_pipe_update(old_rp);
				break;
			default:
				WARN_ON_ONCE(1);
				break;
			}
		}
		WRITE_ONCE(rcu_torture_current_version,
			   rcu_torture_current_version + 1);
		/* Cycle through nesting levels of rcu_expedite_gp() calls. */
		if (can_expedite &&
		    !(torture_random(&rand) & 0xff & (!!expediting - 1))) {
			WARN_ON_ONCE(expediting == 0 && rcu_gp_is_expedited());
			if (expediting >= 0)
				rcu_expedite_gp();
			else
				rcu_unexpedite_gp();
			if (++expediting > 3)
				expediting = -expediting;
		} else if (!can_expedite) { /* Disabled during boot, recheck. */
			can_expedite = !rcu_gp_is_expedited() &&
				       !rcu_gp_is_normal();
		}
		rcu_torture_writer_state = RTWS_STUTTER;
		if (stutter_wait("rcu_torture_writer") &&
		    !READ_ONCE(rcu_fwd_cb_nodelay) &&
		    !cur_ops->slow_gps &&
		    !torture_must_stop())
			for (i = 0; i < ARRAY_SIZE(rcu_tortures); i++)
				if (list_empty(&rcu_tortures[i].rtort_free) &&
				    rcu_access_pointer(rcu_torture_current) !=
				    &rcu_tortures[i]) {
					rcu_ftrace_dump(DUMP_ALL);
					WARN(1, "%s: rtort_pipe_count: %d\n", __func__, rcu_tortures[i].rtort_pipe_count);
				}
	} while (!torture_must_stop());
	/* Reset expediting back to unexpedited. */
	if (expediting > 0)
		expediting = -expediting;
	while (can_expedite && expediting++ < 0)
		rcu_unexpedite_gp();
	WARN_ON_ONCE(can_expedite && rcu_gp_is_expedited());
	if (!can_expedite)
		pr_alert("%s" TORTURE_FLAG
			 " Dynamic grace-period expediting was disabled.\n",
			 torture_type);
	rcu_torture_writer_state = RTWS_STOPPING;
	torture_kthread_stopping("rcu_torture_writer");
	return 0;
}

/*
 * RCU torture fake writer kthread.  Repeatedly calls sync, with a random
 * delay between calls.
 */
static int
rcu_torture_fakewriter(void *arg)
{
	DEFINE_TORTURE_RANDOM(rand);

	VERBOSE_TOROUT_STRING("rcu_torture_fakewriter task started");
	set_user_nice(current, MAX_NICE);

	do {
		schedule_timeout_uninterruptible(1 + torture_random(&rand)%10);
		udelay(torture_random(&rand) & 0x3ff);
		if (cur_ops->cb_barrier != NULL &&
		    torture_random(&rand) % (nfakewriters * 8) == 0) {
			cur_ops->cb_barrier();
		} else if (gp_normal == gp_exp) {
			if (cur_ops->sync && torture_random(&rand) & 0x80)
				cur_ops->sync();
			else if (cur_ops->exp_sync)
				cur_ops->exp_sync();
		} else if (gp_normal && cur_ops->sync) {
			cur_ops->sync();
		} else if (cur_ops->exp_sync) {
			cur_ops->exp_sync();
		}
		stutter_wait("rcu_torture_fakewriter");
	} while (!torture_must_stop());

	torture_kthread_stopping("rcu_torture_fakewriter");
	return 0;
}

static void rcu_torture_timer_cb(struct rcu_head *rhp)
{
	kfree(rhp);
}

/*
 * Do one extension of an RCU read-side critical section using the
 * current reader state in readstate (set to zero for initial entry
 * to extended critical section), set the new state as specified by
 * newstate (set to zero for final exit from extended critical section),
 * and random-number-generator state in trsp.  If this is neither the
 * beginning or end of the critical section and if there was actually a
 * change, do a ->read_delay().
 */
static void rcutorture_one_extend(int *readstate, int newstate,
				  struct torture_random_state *trsp,
				  struct rt_read_seg *rtrsp)
{
	int idxnew = -1;
	int idxold = *readstate;
	int statesnew = ~*readstate & newstate;
	int statesold = *readstate & ~newstate;

	WARN_ON_ONCE(idxold < 0);
	WARN_ON_ONCE((idxold >> RCUTORTURE_RDR_SHIFT) > 1);
	rtrsp->rt_readstate = newstate;

	/* First, put new protection in place to avoid critical-section gap. */
	if (statesnew & RCUTORTURE_RDR_BH)
		local_bh_disable();
	if (statesnew & RCUTORTURE_RDR_IRQ)
		local_irq_disable();
	if (statesnew & RCUTORTURE_RDR_PREEMPT)
		preempt_disable();
	if (statesnew & RCUTORTURE_RDR_RBH)
		rcu_read_lock_bh();
	if (statesnew & RCUTORTURE_RDR_SCHED)
		rcu_read_lock_sched();
	if (statesnew & RCUTORTURE_RDR_RCU)
		idxnew = cur_ops->readlock() << RCUTORTURE_RDR_SHIFT;

	/* Next, remove old protection, irq first due to bh conflict. */
	if (statesold & RCUTORTURE_RDR_IRQ)
		local_irq_enable();
	if (statesold & RCUTORTURE_RDR_BH)
		local_bh_enable();
	if (statesold & RCUTORTURE_RDR_PREEMPT)
		preempt_enable();
	if (statesold & RCUTORTURE_RDR_RBH)
		rcu_read_unlock_bh();
	if (statesold & RCUTORTURE_RDR_SCHED)
		rcu_read_unlock_sched();
	if (statesold & RCUTORTURE_RDR_RCU)
		cur_ops->readunlock(idxold >> RCUTORTURE_RDR_SHIFT);

	/* Delay if neither beginning nor end and there was a change. */
	if ((statesnew || statesold) && *readstate && newstate)
		cur_ops->read_delay(trsp, rtrsp);

	/* Update the reader state. */
	if (idxnew == -1)
		idxnew = idxold & ~RCUTORTURE_RDR_MASK;
	WARN_ON_ONCE(idxnew < 0);
	WARN_ON_ONCE((idxnew >> RCUTORTURE_RDR_SHIFT) > 1);
	*readstate = idxnew | newstate;
	WARN_ON_ONCE((*readstate >> RCUTORTURE_RDR_SHIFT) < 0);
	WARN_ON_ONCE((*readstate >> RCUTORTURE_RDR_SHIFT) > 1);
}

/* Return the biggest extendables mask given current RCU and boot parameters. */
static int rcutorture_extend_mask_max(void)
{
	int mask;

	WARN_ON_ONCE(extendables & ~RCUTORTURE_MAX_EXTEND);
	mask = extendables & RCUTORTURE_MAX_EXTEND & cur_ops->extendables;
	mask = mask | RCUTORTURE_RDR_RCU;
	return mask;
}

/* Return a random protection state mask, but with at least one bit set. */
static int
rcutorture_extend_mask(int oldmask, struct torture_random_state *trsp)
{
	int mask = rcutorture_extend_mask_max();
	unsigned long randmask1 = torture_random(trsp) >> 8;
	unsigned long randmask2 = randmask1 >> 3;

	WARN_ON_ONCE(mask >> RCUTORTURE_RDR_SHIFT);
	/* Mostly only one bit (need preemption!), sometimes lots of bits. */
	if (!(randmask1 & 0x7))
		mask = mask & randmask2;
	else
		mask = mask & (1 << (randmask2 % RCUTORTURE_RDR_NBITS));
	/* Can't enable bh w/irq disabled. */
	if ((mask & RCUTORTURE_RDR_IRQ) &&
	    ((!(mask & RCUTORTURE_RDR_BH) && (oldmask & RCUTORTURE_RDR_BH)) ||
	     (!(mask & RCUTORTURE_RDR_RBH) && (oldmask & RCUTORTURE_RDR_RBH))))
		mask |= RCUTORTURE_RDR_BH | RCUTORTURE_RDR_RBH;
	return mask ?: RCUTORTURE_RDR_RCU;
}

/*
 * Do a randomly selected number of extensions of an existing RCU read-side
 * critical section.
 */
static struct rt_read_seg *
rcutorture_loop_extend(int *readstate, struct torture_random_state *trsp,
		       struct rt_read_seg *rtrsp)
{
	int i;
	int j;
	int mask = rcutorture_extend_mask_max();

	WARN_ON_ONCE(!*readstate); /* -Existing- RCU read-side critsect! */
	if (!((mask - 1) & mask))
		return rtrsp;  /* Current RCU reader not extendable. */
	/* Bias towards larger numbers of loops. */
	i = (torture_random(trsp) >> 3);
	i = ((i | (i >> 3)) & RCUTORTURE_RDR_MAX_LOOPS) + 1;
	for (j = 0; j < i; j++) {
		mask = rcutorture_extend_mask(*readstate, trsp);
		rcutorture_one_extend(readstate, mask, trsp, &rtrsp[j]);
	}
	return &rtrsp[j];
}

/*
 * Do one read-side critical section, returning false if there was
 * no data to read.  Can be invoked both from process context and
 * from a timer handler.
 */
static bool rcu_torture_one_read(struct torture_random_state *trsp)
{
	int i;
	unsigned long started;
	unsigned long completed;
	int newstate;
	struct rcu_torture *p;
	int pipe_count;
	int readstate = 0;
	struct rt_read_seg rtseg[RCUTORTURE_RDR_MAX_SEGS] = { { 0 } };
	struct rt_read_seg *rtrsp = &rtseg[0];
	struct rt_read_seg *rtrsp1;
	unsigned long long ts;

	newstate = rcutorture_extend_mask(readstate, trsp);
	rcutorture_one_extend(&readstate, newstate, trsp, rtrsp++);
	started = cur_ops->get_gp_seq();
	ts = rcu_trace_clock_local();
	p = rcu_dereference_check(rcu_torture_current,
				  rcu_read_lock_bh_held() ||
				  rcu_read_lock_sched_held() ||
				  srcu_read_lock_held(srcu_ctlp) ||
				  torturing_tasks());
	if (p == NULL) {
		/* Wait for rcu_torture_writer to get underway */
		rcutorture_one_extend(&readstate, 0, trsp, rtrsp);
		return false;
	}
	if (p->rtort_mbtest == 0)
		atomic_inc(&n_rcu_torture_mberror);
	rtrsp = rcutorture_loop_extend(&readstate, trsp, rtrsp);
	preempt_disable();
	pipe_count = p->rtort_pipe_count;
	if (pipe_count > RCU_TORTURE_PIPE_LEN) {
		/* Should not happen, but... */
		pipe_count = RCU_TORTURE_PIPE_LEN;
	}
	completed = cur_ops->get_gp_seq();
	if (pipe_count > 1) {
		do_trace_rcu_torture_read(cur_ops->name, &p->rtort_rcu,
					  ts, started, completed);
		rcu_ftrace_dump(DUMP_ALL);
	}
	__this_cpu_inc(rcu_torture_count[pipe_count]);
	completed = rcutorture_seq_diff(completed, started);
	if (completed > RCU_TORTURE_PIPE_LEN) {
		/* Should not happen, but... */
		completed = RCU_TORTURE_PIPE_LEN;
	}
	__this_cpu_inc(rcu_torture_batch[completed]);
	preempt_enable();
	rcutorture_one_extend(&readstate, 0, trsp, rtrsp);
	WARN_ON_ONCE(readstate & RCUTORTURE_RDR_MASK);

	/* If error or close call, record the sequence of reader protections. */
	if ((pipe_count > 1 || completed > 1) && !xchg(&err_segs_recorded, 1)) {
		i = 0;
		for (rtrsp1 = &rtseg[0]; rtrsp1 < rtrsp; rtrsp1++)
			err_segs[i++] = *rtrsp1;
		rt_read_nsegs = i;
	}

	return true;
}

static DEFINE_TORTURE_RANDOM_PERCPU(rcu_torture_timer_rand);

/*
 * RCU torture reader from timer handler.  Dereferences rcu_torture_current,
 * incrementing the corresponding element of the pipeline array.  The
 * counter in the element should never be greater than 1, otherwise, the
 * RCU implementation is broken.
 */
static void rcu_torture_timer(struct timer_list *unused)
{
	atomic_long_inc(&n_rcu_torture_timers);
	(void)rcu_torture_one_read(this_cpu_ptr(&rcu_torture_timer_rand));

	/* Test call_rcu() invocation from interrupt handler. */
	if (cur_ops->call) {
		struct rcu_head *rhp = kmalloc(sizeof(*rhp), GFP_NOWAIT);

		if (rhp)
			cur_ops->call(rhp, rcu_torture_timer_cb);
	}
}

/*
 * RCU torture reader kthread.  Repeatedly dereferences rcu_torture_current,
 * incrementing the corresponding element of the pipeline array.  The
 * counter in the element should never be greater than 1, otherwise, the
 * RCU implementation is broken.
 */
static int
rcu_torture_reader(void *arg)
{
	unsigned long lastsleep = jiffies;
	long myid = (long)arg;
	int mynumonline = myid;
	DEFINE_TORTURE_RANDOM(rand);
	struct timer_list t;

	VERBOSE_TOROUT_STRING("rcu_torture_reader task started");
	set_user_nice(current, MAX_NICE);
	if (irqreader && cur_ops->irq_capable)
		timer_setup_on_stack(&t, rcu_torture_timer, 0);
	tick_dep_set_task(current, TICK_DEP_BIT_RCU);
	do {
		if (irqreader && cur_ops->irq_capable) {
			if (!timer_pending(&t))
				mod_timer(&t, jiffies + 1);
		}
		if (!rcu_torture_one_read(&rand) && !torture_must_stop())
			schedule_timeout_interruptible(HZ);
		if (time_after(jiffies, lastsleep) && !torture_must_stop()) {
			schedule_timeout_interruptible(1);
			lastsleep = jiffies + 10;
		}
		while (num_online_cpus() < mynumonline && !torture_must_stop())
			schedule_timeout_interruptible(HZ / 5);
		stutter_wait("rcu_torture_reader");
	} while (!torture_must_stop());
	if (irqreader && cur_ops->irq_capable) {
		del_timer_sync(&t);
		destroy_timer_on_stack(&t);
	}
	tick_dep_clear_task(current, TICK_DEP_BIT_RCU);
	torture_kthread_stopping("rcu_torture_reader");
	return 0;
}

/*
 * Print torture statistics.  Caller must ensure that there is only
 * one call to this function at a given time!!!  This is normally
 * accomplished by relying on the module system to only have one copy
 * of the module loaded, and then by giving the rcu_torture_stats
 * kthread full control (or the init/cleanup functions when rcu_torture_stats
 * thread is not running).
 */
static void
rcu_torture_stats_print(void)
{
	int cpu;
	int i;
	long pipesummary[RCU_TORTURE_PIPE_LEN + 1] = { 0 };
	long batchsummary[RCU_TORTURE_PIPE_LEN + 1] = { 0 };
	static unsigned long rtcv_snap = ULONG_MAX;
	static bool splatted;
	struct task_struct *wtp;

	for_each_possible_cpu(cpu) {
		for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++) {
			pipesummary[i] += per_cpu(rcu_torture_count, cpu)[i];
			batchsummary[i] += per_cpu(rcu_torture_batch, cpu)[i];
		}
	}
	for (i = RCU_TORTURE_PIPE_LEN - 1; i >= 0; i--) {
		if (pipesummary[i] != 0)
			break;
	}

	pr_alert("%s%s ", torture_type, TORTURE_FLAG);
	pr_cont("rtc: %p %s: %lu tfle: %d rta: %d rtaf: %d rtf: %d ",
		rcu_torture_current,
		rcu_torture_current ? "ver" : "VER",
		rcu_torture_current_version,
		list_empty(&rcu_torture_freelist),
		atomic_read(&n_rcu_torture_alloc),
		atomic_read(&n_rcu_torture_alloc_fail),
		atomic_read(&n_rcu_torture_free));
	pr_cont("rtmbe: %d rtbe: %ld rtbke: %ld rtbre: %ld ",
		atomic_read(&n_rcu_torture_mberror),
		n_rcu_torture_barrier_error,
		n_rcu_torture_boost_ktrerror,
		n_rcu_torture_boost_rterror);
	pr_cont("rtbf: %ld rtb: %ld nt: %ld ",
		n_rcu_torture_boost_failure,
		n_rcu_torture_boosts,
		atomic_long_read(&n_rcu_torture_timers));
	torture_onoff_stats();
	pr_cont("barrier: %ld/%ld:%ld\n",
		n_barrier_successes,
		n_barrier_attempts,
		n_rcu_torture_barrier_error);

	pr_alert("%s%s ", torture_type, TORTURE_FLAG);
	if (atomic_read(&n_rcu_torture_mberror) ||
	    n_rcu_torture_barrier_error || n_rcu_torture_boost_ktrerror ||
	    n_rcu_torture_boost_rterror || n_rcu_torture_boost_failure ||
	    i > 1) {
		pr_cont("%s", "!!! ");
		atomic_inc(&n_rcu_torture_error);
		WARN_ON_ONCE(atomic_read(&n_rcu_torture_mberror));
		WARN_ON_ONCE(n_rcu_torture_barrier_error);  // rcu_barrier()
		WARN_ON_ONCE(n_rcu_torture_boost_ktrerror); // no boost kthread
		WARN_ON_ONCE(n_rcu_torture_boost_rterror); // can't set RT prio
		WARN_ON_ONCE(n_rcu_torture_boost_failure); // RCU boost failed
		WARN_ON_ONCE(i > 1); // Too-short grace period
	}
	pr_cont("Reader Pipe: ");
	for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++)
		pr_cont(" %ld", pipesummary[i]);
	pr_cont("\n");

	pr_alert("%s%s ", torture_type, TORTURE_FLAG);
	pr_cont("Reader Batch: ");
	for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++)
		pr_cont(" %ld", batchsummary[i]);
	pr_cont("\n");

	pr_alert("%s%s ", torture_type, TORTURE_FLAG);
	pr_cont("Free-Block Circulation: ");
	for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++) {
		pr_cont(" %d", atomic_read(&rcu_torture_wcount[i]));
	}
	pr_cont("\n");

	if (cur_ops->stats)
		cur_ops->stats();
	if (rtcv_snap == rcu_torture_current_version &&
	    rcu_torture_current != NULL) {
		int __maybe_unused flags = 0;
		unsigned long __maybe_unused gp_seq = 0;

		rcutorture_get_gp_data(cur_ops->ttype,
				       &flags, &gp_seq);
		srcutorture_get_gp_data(cur_ops->ttype, srcu_ctlp,
					&flags, &gp_seq);
		wtp = READ_ONCE(writer_task);
		pr_alert("??? Writer stall state %s(%d) g%lu f%#x ->state %#lx cpu %d\n",
			 rcu_torture_writer_state_getname(),
			 rcu_torture_writer_state, gp_seq, flags,
			 wtp == NULL ? ~0UL : wtp->state,
			 wtp == NULL ? -1 : (int)task_cpu(wtp));
		if (!splatted && wtp) {
			sched_show_task(wtp);
			splatted = true;
		}
		show_rcu_gp_kthreads();
		rcu_ftrace_dump(DUMP_ALL);
	}
	rtcv_snap = rcu_torture_current_version;
}

/*
 * Periodically prints torture statistics, if periodic statistics printing
 * was specified via the stat_interval module parameter.
 */
static int
rcu_torture_stats(void *arg)
{
	VERBOSE_TOROUT_STRING("rcu_torture_stats task started");
	do {
		schedule_timeout_interruptible(stat_interval * HZ);
		rcu_torture_stats_print();
		torture_shutdown_absorb("rcu_torture_stats");
	} while (!torture_must_stop());
	torture_kthread_stopping("rcu_torture_stats");
	return 0;
}

static void
rcu_torture_print_module_parms(struct rcu_torture_ops *cur_ops, const char *tag)
{
	pr_alert("%s" TORTURE_FLAG
		 "--- %s: nreaders=%d nfakewriters=%d "
		 "stat_interval=%d verbose=%d test_no_idle_hz=%d "
		 "shuffle_interval=%d stutter=%d irqreader=%d "
		 "fqs_duration=%d fqs_holdoff=%d fqs_stutter=%d "
		 "test_boost=%d/%d test_boost_interval=%d "
		 "test_boost_duration=%d shutdown_secs=%d "
		 "stall_cpu=%d stall_cpu_holdoff=%d stall_cpu_irqsoff=%d "
		 "n_barrier_cbs=%d "
		 "onoff_interval=%d onoff_holdoff=%d\n",
		 torture_type, tag, nrealreaders, nfakewriters,
		 stat_interval, verbose, test_no_idle_hz, shuffle_interval,
		 stutter, irqreader, fqs_duration, fqs_holdoff, fqs_stutter,
		 test_boost, cur_ops->can_boost,
		 test_boost_interval, test_boost_duration, shutdown_secs,
		 stall_cpu, stall_cpu_holdoff, stall_cpu_irqsoff,
		 n_barrier_cbs,
		 onoff_interval, onoff_holdoff);
}

static int rcutorture_booster_cleanup(unsigned int cpu)
{
	struct task_struct *t;

	if (boost_tasks[cpu] == NULL)
		return 0;
	mutex_lock(&boost_mutex);
	t = boost_tasks[cpu];
	boost_tasks[cpu] = NULL;
	rcu_torture_enable_rt_throttle();
	mutex_unlock(&boost_mutex);

	/* This must be outside of the mutex, otherwise deadlock! */
	torture_stop_kthread(rcu_torture_boost, t);
	return 0;
}

static int rcutorture_booster_init(unsigned int cpu)
{
	int retval;

	if (boost_tasks[cpu] != NULL)
		return 0;  /* Already created, nothing more to do. */

	/* Don't allow time recalculation while creating a new task. */
	mutex_lock(&boost_mutex);
	rcu_torture_disable_rt_throttle();
	VERBOSE_TOROUT_STRING("Creating rcu_torture_boost task");
	boost_tasks[cpu] = kthread_create_on_node(rcu_torture_boost, NULL,
						  cpu_to_node(cpu),
						  "rcu_torture_boost");
	if (IS_ERR(boost_tasks[cpu])) {
		retval = PTR_ERR(boost_tasks[cpu]);
		VERBOSE_TOROUT_STRING("rcu_torture_boost task create failed");
		n_rcu_torture_boost_ktrerror++;
		boost_tasks[cpu] = NULL;
		mutex_unlock(&boost_mutex);
		return retval;
	}
	kthread_bind(boost_tasks[cpu], cpu);
	wake_up_process(boost_tasks[cpu]);
	mutex_unlock(&boost_mutex);
	return 0;
}

/*
 * CPU-stall kthread.  It waits as specified by stall_cpu_holdoff, then
 * induces a CPU stall for the time specified by stall_cpu.
 */
static int rcu_torture_stall(void *args)
{
	unsigned long stop_at;

	VERBOSE_TOROUT_STRING("rcu_torture_stall task started");
	if (stall_cpu_holdoff > 0) {
		VERBOSE_TOROUT_STRING("rcu_torture_stall begin holdoff");
		schedule_timeout_interruptible(stall_cpu_holdoff * HZ);
		VERBOSE_TOROUT_STRING("rcu_torture_stall end holdoff");
	}
	if (!kthread_should_stop()) {
		stop_at = ktime_get_seconds() + stall_cpu;
		/* RCU CPU stall is expected behavior in following code. */
		rcu_read_lock();
		if (stall_cpu_irqsoff)
			local_irq_disable();
		else
			preempt_disable();
		pr_alert("rcu_torture_stall start on CPU %d.\n",
			 smp_processor_id());
		while (ULONG_CMP_LT((unsigned long)ktime_get_seconds(),
				    stop_at))
			continue;  /* Induce RCU CPU stall warning. */
		if (stall_cpu_irqsoff)
			local_irq_enable();
		else
			preempt_enable();
		rcu_read_unlock();
		pr_alert("rcu_torture_stall end.\n");
	}
	torture_shutdown_absorb("rcu_torture_stall");
	while (!kthread_should_stop())
		schedule_timeout_interruptible(10 * HZ);
	return 0;
}

/* Spawn CPU-stall kthread, if stall_cpu specified. */
static int __init rcu_torture_stall_init(void)
{
	if (stall_cpu <= 0)
		return 0;
	return torture_create_kthread(rcu_torture_stall, NULL, stall_task);
}

/* State structure for forward-progress self-propagating RCU callback. */
struct fwd_cb_state {
	struct rcu_head rh;
	int stop;
};

/*
 * Forward-progress self-propagating RCU callback function.  Because
 * callbacks run from softirq, this function is an implicit RCU read-side
 * critical section.
 */
static void rcu_torture_fwd_prog_cb(struct rcu_head *rhp)
{
	struct fwd_cb_state *fcsp = container_of(rhp, struct fwd_cb_state, rh);

	if (READ_ONCE(fcsp->stop)) {
		WRITE_ONCE(fcsp->stop, 2);
		return;
	}
	cur_ops->call(&fcsp->rh, rcu_torture_fwd_prog_cb);
}

/* State for continuous-flood RCU callbacks. */
struct rcu_fwd_cb {
	struct rcu_head rh;
	struct rcu_fwd_cb *rfc_next;
	struct rcu_fwd *rfc_rfp;
	int rfc_gps;
};

#define MAX_FWD_CB_JIFFIES	(8 * HZ) /* Maximum CB test duration. */
#define MIN_FWD_CB_LAUNDERS	3	/* This many CB invocations to count. */
#define MIN_FWD_CBS_LAUNDERED	100	/* Number of counted CBs. */
#define FWD_CBS_HIST_DIV	10	/* Histogram buckets/second. */
#define N_LAUNDERS_HIST (2 * MAX_FWD_CB_JIFFIES / (HZ / FWD_CBS_HIST_DIV))

struct rcu_launder_hist {
	long n_launders;
	unsigned long launder_gp_seq;
};

struct rcu_fwd {
	spinlock_t rcu_fwd_lock;
	struct rcu_fwd_cb *rcu_fwd_cb_head;
	struct rcu_fwd_cb **rcu_fwd_cb_tail;
	long n_launders_cb;
	unsigned long rcu_fwd_startat;
	struct rcu_launder_hist n_launders_hist[N_LAUNDERS_HIST];
	unsigned long rcu_launder_gp_seq_start;
};

struct rcu_fwd rcu_fwds;
bool rcu_fwd_emergency_stop;

static void rcu_torture_fwd_cb_hist(struct rcu_fwd *rfp)
{
	unsigned long gps;
	unsigned long gps_old;
	int i;
	int j;

	for (i = ARRAY_SIZE(rfp->n_launders_hist) - 1; i > 0; i--)
		if (rfp->n_launders_hist[i].n_launders > 0)
			break;
	pr_alert("%s: Callback-invocation histogram (duration %lu jiffies):",
		 __func__, jiffies - rfp->rcu_fwd_startat);
	gps_old = rfp->rcu_launder_gp_seq_start;
	for (j = 0; j <= i; j++) {
		gps = rfp->n_launders_hist[j].launder_gp_seq;
		pr_cont(" %ds/%d: %ld:%ld",
			j + 1, FWD_CBS_HIST_DIV,
			rfp->n_launders_hist[j].n_launders,
			rcutorture_seq_diff(gps, gps_old));
		gps_old = gps;
	}
	pr_cont("\n");
}

/* Callback function for continuous-flood RCU callbacks. */
static void rcu_torture_fwd_cb_cr(struct rcu_head *rhp)
{
	unsigned long flags;
	int i;
	struct rcu_fwd_cb *rfcp = container_of(rhp, struct rcu_fwd_cb, rh);
	struct rcu_fwd_cb **rfcpp;
	struct rcu_fwd *rfp = rfcp->rfc_rfp;

	rfcp->rfc_next = NULL;
	rfcp->rfc_gps++;
	spin_lock_irqsave(&rfp->rcu_fwd_lock, flags);
	rfcpp = rfp->rcu_fwd_cb_tail;
	rfp->rcu_fwd_cb_tail = &rfcp->rfc_next;
	WRITE_ONCE(*rfcpp, rfcp);
	WRITE_ONCE(rfp->n_launders_cb, rfp->n_launders_cb + 1);
	i = ((jiffies - rfp->rcu_fwd_startat) / (HZ / FWD_CBS_HIST_DIV));
	if (i >= ARRAY_SIZE(rfp->n_launders_hist))
		i = ARRAY_SIZE(rfp->n_launders_hist) - 1;
	rfp->n_launders_hist[i].n_launders++;
	rfp->n_launders_hist[i].launder_gp_seq = cur_ops->get_gp_seq();
	spin_unlock_irqrestore(&rfp->rcu_fwd_lock, flags);
}

// Give the scheduler a chance, even on nohz_full CPUs.
static void rcu_torture_fwd_prog_cond_resched(unsigned long iter)
{
	if (IS_ENABLED(CONFIG_PREEMPT) && IS_ENABLED(CONFIG_NO_HZ_FULL)) {
		// Real call_rcu() floods hit userspace, so emulate that.
		if (need_resched() || (iter & 0xfff))
			schedule();
		return;
	}
	// No userspace emulation: CB invocation throttles call_rcu()
	cond_resched();
}

/*
 * Free all callbacks on the rcu_fwd_cb_head list, either because the
 * test is over or because we hit an OOM event.
 */
static unsigned long rcu_torture_fwd_prog_cbfree(void)
{
	unsigned long flags;
	unsigned long freed = 0;
	struct rcu_fwd_cb *rfcp;

	for (;;) {
		spin_lock_irqsave(&rcu_fwds.rcu_fwd_lock, flags);
		rfcp = rcu_fwds.rcu_fwd_cb_head;
		if (!rfcp) {
			spin_unlock_irqrestore(&rcu_fwds.rcu_fwd_lock, flags);
			break;
		}
		rcu_fwds.rcu_fwd_cb_head = rfcp->rfc_next;
		if (!rcu_fwds.rcu_fwd_cb_head)
			rcu_fwds.rcu_fwd_cb_tail = &rcu_fwds.rcu_fwd_cb_head;
		spin_unlock_irqrestore(&rcu_fwds.rcu_fwd_lock, flags);
		kfree(rfcp);
		freed++;
		rcu_torture_fwd_prog_cond_resched(freed);
		if (tick_nohz_full_enabled()) {
			local_irq_save(flags);
			rcu_momentary_dyntick_idle();
			local_irq_restore(flags);
		}
	}
	return freed;
}

/* Carry out need_resched()/cond_resched() forward-progress testing. */
static void rcu_torture_fwd_prog_nr(struct rcu_fwd *rfp,
				    int *tested, int *tested_tries)
{
	unsigned long cver;
	unsigned long dur;
	struct fwd_cb_state fcs;
	unsigned long gps;
	int idx;
	int sd;
	int sd4;
	bool selfpropcb = false;
	unsigned long stopat;
	static DEFINE_TORTURE_RANDOM(trs);

	if  (cur_ops->call && cur_ops->sync && cur_ops->cb_barrier) {
		init_rcu_head_on_stack(&fcs.rh);
		selfpropcb = true;
	}

	/* Tight loop containing cond_resched(). */
	WRITE_ONCE(rcu_fwd_cb_nodelay, true);
	cur_ops->sync(); /* Later readers see above write. */
	if  (selfpropcb) {
		WRITE_ONCE(fcs.stop, 0);
		cur_ops->call(&fcs.rh, rcu_torture_fwd_prog_cb);
	}
	cver = READ_ONCE(rcu_torture_current_version);
	gps = cur_ops->get_gp_seq();
	sd = cur_ops->stall_dur() + 1;
	sd4 = (sd + fwd_progress_div - 1) / fwd_progress_div;
	dur = sd4 + torture_random(&trs) % (sd - sd4);
	WRITE_ONCE(rfp->rcu_fwd_startat, jiffies);
	stopat = rfp->rcu_fwd_startat + dur;
	while (time_before(jiffies, stopat) &&
	       !shutdown_time_arrived() &&
	       !READ_ONCE(rcu_fwd_emergency_stop) && !torture_must_stop()) {
		idx = cur_ops->readlock();
		udelay(10);
		cur_ops->readunlock(idx);
		if (!fwd_progress_need_resched || need_resched())
			cond_resched();
	}
	(*tested_tries)++;
	if (!time_before(jiffies, stopat) &&
	    !shutdown_time_arrived() &&
	    !READ_ONCE(rcu_fwd_emergency_stop) && !torture_must_stop()) {
		(*tested)++;
		cver = READ_ONCE(rcu_torture_current_version) - cver;
		gps = rcutorture_seq_diff(cur_ops->get_gp_seq(), gps);
		WARN_ON(!cver && gps < 2);
		pr_alert("%s: Duration %ld cver %ld gps %ld\n", __func__, dur, cver, gps);
	}
	if (selfpropcb) {
		WRITE_ONCE(fcs.stop, 1);
		cur_ops->sync(); /* Wait for running CB to complete. */
		cur_ops->cb_barrier(); /* Wait for queued callbacks. */
	}

	if (selfpropcb) {
		WARN_ON(READ_ONCE(fcs.stop) != 2);
		destroy_rcu_head_on_stack(&fcs.rh);
	}
	schedule_timeout_uninterruptible(HZ / 10); /* Let kthreads recover. */
	WRITE_ONCE(rcu_fwd_cb_nodelay, false);
}

/* Carry out call_rcu() forward-progress testing. */
static void rcu_torture_fwd_prog_cr(struct rcu_fwd *rfp)
{
	unsigned long cver;
	unsigned long flags;
	unsigned long gps;
	int i;
	long n_launders;
	long n_launders_cb_snap;
	long n_launders_sa;
	long n_max_cbs;
	long n_max_gps;
	struct rcu_fwd_cb *rfcp;
	struct rcu_fwd_cb *rfcpn;
	unsigned long stopat;
	unsigned long stoppedat;

	if (READ_ONCE(rcu_fwd_emergency_stop))
		return; /* Get out of the way quickly, no GP wait! */
	if (!cur_ops->call)
		return; /* Can't do call_rcu() fwd prog without ->call. */

	/* Loop continuously posting RCU callbacks. */
	WRITE_ONCE(rcu_fwd_cb_nodelay, true);
	cur_ops->sync(); /* Later readers see above write. */
	WRITE_ONCE(rfp->rcu_fwd_startat, jiffies);
	stopat = rfp->rcu_fwd_startat + MAX_FWD_CB_JIFFIES;
	n_launders = 0;
	rfp->n_launders_cb = 0; // Hoist initialization for multi-kthread
	n_launders_sa = 0;
	n_max_cbs = 0;
	n_max_gps = 0;
	for (i = 0; i < ARRAY_SIZE(rfp->n_launders_hist); i++)
		rfp->n_launders_hist[i].n_launders = 0;
	cver = READ_ONCE(rcu_torture_current_version);
	gps = cur_ops->get_gp_seq();
	rfp->rcu_launder_gp_seq_start = gps;
	tick_dep_set_task(current, TICK_DEP_BIT_RCU);
	while (time_before(jiffies, stopat) &&
	       !shutdown_time_arrived() &&
	       !READ_ONCE(rcu_fwd_emergency_stop) && !torture_must_stop()) {
		rfcp = READ_ONCE(rfp->rcu_fwd_cb_head);
		rfcpn = NULL;
		if (rfcp)
			rfcpn = READ_ONCE(rfcp->rfc_next);
		if (rfcpn) {
			if (rfcp->rfc_gps >= MIN_FWD_CB_LAUNDERS &&
			    ++n_max_gps >= MIN_FWD_CBS_LAUNDERED)
				break;
			rfp->rcu_fwd_cb_head = rfcpn;
			n_launders++;
			n_launders_sa++;
		} else {
			rfcp = kmalloc(sizeof(*rfcp), GFP_KERNEL);
			if (WARN_ON_ONCE(!rfcp)) {
				schedule_timeout_interruptible(1);
				continue;
			}
			n_max_cbs++;
			n_launders_sa = 0;
			rfcp->rfc_gps = 0;
			rfcp->rfc_rfp = rfp;
		}
		cur_ops->call(&rfcp->rh, rcu_torture_fwd_cb_cr);
		rcu_torture_fwd_prog_cond_resched(n_launders + n_max_cbs);
		if (tick_nohz_full_enabled()) {
			local_irq_save(flags);
			rcu_momentary_dyntick_idle();
			local_irq_restore(flags);
		}
	}
	stoppedat = jiffies;
	n_launders_cb_snap = READ_ONCE(rfp->n_launders_cb);
	cver = READ_ONCE(rcu_torture_current_version) - cver;
	gps = rcutorture_seq_diff(cur_ops->get_gp_seq(), gps);
	cur_ops->cb_barrier(); /* Wait for callbacks to be invoked. */
	(void)rcu_torture_fwd_prog_cbfree();

	if (!torture_must_stop() && !READ_ONCE(rcu_fwd_emergency_stop) &&
	    !shutdown_time_arrived()) {
		WARN_ON(n_max_gps < MIN_FWD_CBS_LAUNDERED);
		pr_alert("%s Duration %lu barrier: %lu pending %ld n_launders: %ld n_launders_sa: %ld n_max_gps: %ld n_max_cbs: %ld cver %ld gps %ld\n",
			 __func__,
			 stoppedat - rfp->rcu_fwd_startat, jiffies - stoppedat,
			 n_launders + n_max_cbs - n_launders_cb_snap,
			 n_launders, n_launders_sa,
			 n_max_gps, n_max_cbs, cver, gps);
		rcu_torture_fwd_cb_hist(rfp);
	}
	schedule_timeout_uninterruptible(HZ); /* Let CBs drain. */
	tick_dep_clear_task(current, TICK_DEP_BIT_RCU);
	WRITE_ONCE(rcu_fwd_cb_nodelay, false);
}


/*
 * OOM notifier, but this only prints diagnostic information for the
 * current forward-progress test.
 */
static int rcutorture_oom_notify(struct notifier_block *self,
				 unsigned long notused, void *nfreed)
{
	WARN(1, "%s invoked upon OOM during forward-progress testing.\n",
	     __func__);
	rcu_torture_fwd_cb_hist(&rcu_fwds);
	rcu_fwd_progress_check(1 + (jiffies - READ_ONCE(rcu_fwds.rcu_fwd_startat)) / 2);
	WRITE_ONCE(rcu_fwd_emergency_stop, true);
	smp_mb(); /* Emergency stop before free and wait to avoid hangs. */
	pr_info("%s: Freed %lu RCU callbacks.\n",
		__func__, rcu_torture_fwd_prog_cbfree());
	rcu_barrier();
	pr_info("%s: Freed %lu RCU callbacks.\n",
		__func__, rcu_torture_fwd_prog_cbfree());
	rcu_barrier();
	pr_info("%s: Freed %lu RCU callbacks.\n",
		__func__, rcu_torture_fwd_prog_cbfree());
	smp_mb(); /* Frees before return to avoid redoing OOM. */
	(*(unsigned long *)nfreed)++; /* Forward progress CBs freed! */
	pr_info("%s returning after OOM processing.\n", __func__);
	return NOTIFY_OK;
}

static struct notifier_block rcutorture_oom_nb = {
	.notifier_call = rcutorture_oom_notify
};

/* Carry out grace-period forward-progress testing. */
static int rcu_torture_fwd_prog(void *args)
{
	struct rcu_fwd *rfp = args;
	int tested = 0;
	int tested_tries = 0;

	VERBOSE_TOROUT_STRING("rcu_torture_fwd_progress task started");
	rcu_bind_current_to_nocb();
	if (!IS_ENABLED(CONFIG_SMP) || !IS_ENABLED(CONFIG_RCU_BOOST))
		set_user_nice(current, MAX_NICE);
	do {
		schedule_timeout_interruptible(fwd_progress_holdoff * HZ);
		WRITE_ONCE(rcu_fwd_emergency_stop, false);
		register_oom_notifier(&rcutorture_oom_nb);
		rcu_torture_fwd_prog_nr(rfp, &tested, &tested_tries);
		rcu_torture_fwd_prog_cr(rfp);
		unregister_oom_notifier(&rcutorture_oom_nb);

		/* Avoid slow periods, better to test when busy. */
		stutter_wait("rcu_torture_fwd_prog");
	} while (!torture_must_stop());
	/* Short runs might not contain a valid forward-progress attempt. */
	WARN_ON(!tested && tested_tries >= 5);
	pr_alert("%s: tested %d tested_tries %d\n", __func__, tested, tested_tries);
	torture_kthread_stopping("rcu_torture_fwd_prog");
	return 0;
}

/* If forward-progress checking is requested and feasible, spawn the thread. */
static int __init rcu_torture_fwd_prog_init(void)
{
	if (!fwd_progress)
		return 0; /* Not requested, so don't do it. */
	if (!cur_ops->stall_dur || cur_ops->stall_dur() <= 0 ||
	    cur_ops == &rcu_busted_ops) {
		VERBOSE_TOROUT_STRING("rcu_torture_fwd_prog_init: Disabled, unsupported by RCU flavor under test");
		return 0;
	}
	if (stall_cpu > 0) {
		VERBOSE_TOROUT_STRING("rcu_torture_fwd_prog_init: Disabled, conflicts with CPU-stall testing");
		if (IS_MODULE(CONFIG_RCU_TORTURE_TESTS))
			return -EINVAL; /* In module, can fail back to user. */
		WARN_ON(1); /* Make sure rcutorture notices conflict. */
		return 0;
	}
	spin_lock_init(&rcu_fwds.rcu_fwd_lock);
	rcu_fwds.rcu_fwd_cb_tail = &rcu_fwds.rcu_fwd_cb_head;
	if (fwd_progress_holdoff <= 0)
		fwd_progress_holdoff = 1;
	if (fwd_progress_div <= 0)
		fwd_progress_div = 4;
	return torture_create_kthread(rcu_torture_fwd_prog,
				      &rcu_fwds, fwd_prog_task);
}

/* Callback function for RCU barrier testing. */
static void rcu_torture_barrier_cbf(struct rcu_head *rcu)
{
	atomic_inc(&barrier_cbs_invoked);
}

/* kthread function to register callbacks used to test RCU barriers. */
static int rcu_torture_barrier_cbs(void *arg)
{
	long myid = (long)arg;
	bool lastphase = 0;
	bool newphase;
	struct rcu_head rcu;

	init_rcu_head_on_stack(&rcu);
	VERBOSE_TOROUT_STRING("rcu_torture_barrier_cbs task started");
	set_user_nice(current, MAX_NICE);
	do {
		wait_event(barrier_cbs_wq[myid],
			   (newphase =
			    smp_load_acquire(&barrier_phase)) != lastphase ||
			   torture_must_stop());
		lastphase = newphase;
		if (torture_must_stop())
			break;
		/*
		 * The above smp_load_acquire() ensures barrier_phase load
		 * is ordered before the following ->call().
		 */
		local_irq_disable(); /* Just to test no-irq call_rcu(). */
		cur_ops->call(&rcu, rcu_torture_barrier_cbf);
		local_irq_enable();
		if (atomic_dec_and_test(&barrier_cbs_count))
			wake_up(&barrier_wq);
	} while (!torture_must_stop());
	if (cur_ops->cb_barrier != NULL)
		cur_ops->cb_barrier();
	destroy_rcu_head_on_stack(&rcu);
	torture_kthread_stopping("rcu_torture_barrier_cbs");
	return 0;
}

/* kthread function to drive and coordinate RCU barrier testing. */
static int rcu_torture_barrier(void *arg)
{
	int i;

	VERBOSE_TOROUT_STRING("rcu_torture_barrier task starting");
	do {
		atomic_set(&barrier_cbs_invoked, 0);
		atomic_set(&barrier_cbs_count, n_barrier_cbs);
		/* Ensure barrier_phase ordered after prior assignments. */
		smp_store_release(&barrier_phase, !barrier_phase);
		for (i = 0; i < n_barrier_cbs; i++)
			wake_up(&barrier_cbs_wq[i]);
		wait_event(barrier_wq,
			   atomic_read(&barrier_cbs_count) == 0 ||
			   torture_must_stop());
		if (torture_must_stop())
			break;
		n_barrier_attempts++;
		cur_ops->cb_barrier(); /* Implies smp_mb() for wait_event(). */
		if (atomic_read(&barrier_cbs_invoked) != n_barrier_cbs) {
			n_rcu_torture_barrier_error++;
			pr_err("barrier_cbs_invoked = %d, n_barrier_cbs = %d\n",
			       atomic_read(&barrier_cbs_invoked),
			       n_barrier_cbs);
			WARN_ON_ONCE(1);
		} else {
			n_barrier_successes++;
		}
		schedule_timeout_interruptible(HZ / 10);
	} while (!torture_must_stop());
	torture_kthread_stopping("rcu_torture_barrier");
	return 0;
}

/* Initialize RCU barrier testing. */
static int rcu_torture_barrier_init(void)
{
	int i;
	int ret;

	if (n_barrier_cbs <= 0)
		return 0;
	if (cur_ops->call == NULL || cur_ops->cb_barrier == NULL) {
		pr_alert("%s" TORTURE_FLAG
			 " Call or barrier ops missing for %s,\n",
			 torture_type, cur_ops->name);
		pr_alert("%s" TORTURE_FLAG
			 " RCU barrier testing omitted from run.\n",
			 torture_type);
		return 0;
	}
	atomic_set(&barrier_cbs_count, 0);
	atomic_set(&barrier_cbs_invoked, 0);
	barrier_cbs_tasks =
		kcalloc(n_barrier_cbs, sizeof(barrier_cbs_tasks[0]),
			GFP_KERNEL);
	barrier_cbs_wq =
		kcalloc(n_barrier_cbs, sizeof(barrier_cbs_wq[0]), GFP_KERNEL);
	if (barrier_cbs_tasks == NULL || !barrier_cbs_wq)
		return -ENOMEM;
	for (i = 0; i < n_barrier_cbs; i++) {
		init_waitqueue_head(&barrier_cbs_wq[i]);
		ret = torture_create_kthread(rcu_torture_barrier_cbs,
					     (void *)(long)i,
					     barrier_cbs_tasks[i]);
		if (ret)
			return ret;
	}
	return torture_create_kthread(rcu_torture_barrier, NULL, barrier_task);
}

/* Clean up after RCU barrier testing. */
static void rcu_torture_barrier_cleanup(void)
{
	int i;

	torture_stop_kthread(rcu_torture_barrier, barrier_task);
	if (barrier_cbs_tasks != NULL) {
		for (i = 0; i < n_barrier_cbs; i++)
			torture_stop_kthread(rcu_torture_barrier_cbs,
					     barrier_cbs_tasks[i]);
		kfree(barrier_cbs_tasks);
		barrier_cbs_tasks = NULL;
	}
	if (barrier_cbs_wq != NULL) {
		kfree(barrier_cbs_wq);
		barrier_cbs_wq = NULL;
	}
}

static bool rcu_torture_can_boost(void)
{
	static int boost_warn_once;
	int prio;

	if (!(test_boost == 1 && cur_ops->can_boost) && test_boost != 2)
		return false;

	prio = rcu_get_gp_kthreads_prio();
	if (!prio)
		return false;

	if (prio < 2) {
		if (boost_warn_once  == 1)
			return false;

		pr_alert("%s: WARN: RCU kthread priority too low to test boosting.  Skipping RCU boost test. Try passing rcutree.kthread_prio > 1 on the kernel command line.\n", KBUILD_MODNAME);
		boost_warn_once = 1;
		return false;
	}

	return true;
}

static enum cpuhp_state rcutor_hp;

static void
rcu_torture_cleanup(void)
{
	int firsttime;
	int flags = 0;
	unsigned long gp_seq = 0;
	int i;

	if (torture_cleanup_begin()) {
		if (cur_ops->cb_barrier != NULL)
			cur_ops->cb_barrier();
		return;
	}
	if (!cur_ops) {
		torture_cleanup_end();
		return;
	}

	show_rcu_gp_kthreads();
	rcu_torture_barrier_cleanup();
	torture_stop_kthread(rcu_torture_fwd_prog, fwd_prog_task);
	torture_stop_kthread(rcu_torture_stall, stall_task);
	torture_stop_kthread(rcu_torture_writer, writer_task);

	if (reader_tasks) {
		for (i = 0; i < nrealreaders; i++)
			torture_stop_kthread(rcu_torture_reader,
					     reader_tasks[i]);
		kfree(reader_tasks);
	}
	rcu_torture_current = NULL;

	if (fakewriter_tasks) {
		for (i = 0; i < nfakewriters; i++) {
			torture_stop_kthread(rcu_torture_fakewriter,
					     fakewriter_tasks[i]);
		}
		kfree(fakewriter_tasks);
		fakewriter_tasks = NULL;
	}

	rcutorture_get_gp_data(cur_ops->ttype, &flags, &gp_seq);
	srcutorture_get_gp_data(cur_ops->ttype, srcu_ctlp, &flags, &gp_seq);
	pr_alert("%s:  End-test grace-period state: g%lu f%#x\n",
		 cur_ops->name, gp_seq, flags);
	torture_stop_kthread(rcu_torture_stats, stats_task);
	torture_stop_kthread(rcu_torture_fqs, fqs_task);
	if (rcu_torture_can_boost())
		cpuhp_remove_state(rcutor_hp);

	/*
	 * Wait for all RCU callbacks to fire, then do torture-type-specific
	 * cleanup operations.
	 */
	if (cur_ops->cb_barrier != NULL)
		cur_ops->cb_barrier();
	if (cur_ops->cleanup != NULL)
		cur_ops->cleanup();

	rcu_torture_stats_print();  /* -After- the stats thread is stopped! */

	if (err_segs_recorded) {
		pr_alert("Failure/close-call rcutorture reader segments:\n");
		if (rt_read_nsegs == 0)
			pr_alert("\t: No segments recorded!!!\n");
		firsttime = 1;
		for (i = 0; i < rt_read_nsegs; i++) {
			pr_alert("\t%d: %#x ", i, err_segs[i].rt_readstate);
			if (err_segs[i].rt_delay_jiffies != 0) {
				pr_cont("%s%ldjiffies", firsttime ? "" : "+",
					err_segs[i].rt_delay_jiffies);
				firsttime = 0;
			}
			if (err_segs[i].rt_delay_ms != 0) {
				pr_cont("%s%ldms", firsttime ? "" : "+",
					err_segs[i].rt_delay_ms);
				firsttime = 0;
			}
			if (err_segs[i].rt_delay_us != 0) {
				pr_cont("%s%ldus", firsttime ? "" : "+",
					err_segs[i].rt_delay_us);
				firsttime = 0;
			}
			pr_cont("%s\n",
				err_segs[i].rt_preempted ? "preempted" : "");

		}
	}
	if (atomic_read(&n_rcu_torture_error) || n_rcu_torture_barrier_error)
		rcu_torture_print_module_parms(cur_ops, "End of test: FAILURE");
	else if (torture_onoff_failures())
		rcu_torture_print_module_parms(cur_ops,
					       "End of test: RCU_HOTPLUG");
	else
		rcu_torture_print_module_parms(cur_ops, "End of test: SUCCESS");
	torture_cleanup_end();
}

#ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD
static void rcu_torture_leak_cb(struct rcu_head *rhp)
{
}

static void rcu_torture_err_cb(struct rcu_head *rhp)
{
	/*
	 * This -might- happen due to race conditions, but is unlikely.
	 * The scenario that leads to this happening is that the
	 * first of the pair of duplicate callbacks is queued,
	 * someone else starts a grace period that includes that
	 * callback, then the second of the pair must wait for the
	 * next grace period.  Unlikely, but can happen.  If it
	 * does happen, the debug-objects subsystem won't have splatted.
	 */
	pr_alert("%s: duplicated callback was invoked.\n", KBUILD_MODNAME);
}
#endif /* #ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD */

/*
 * Verify that double-free causes debug-objects to complain, but only
 * if CONFIG_DEBUG_OBJECTS_RCU_HEAD=y.  Otherwise, say that the test
 * cannot be carried out.
 */
static void rcu_test_debug_objects(void)
{
#ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD
	struct rcu_head rh1;
	struct rcu_head rh2;

	init_rcu_head_on_stack(&rh1);
	init_rcu_head_on_stack(&rh2);
	pr_alert("%s: WARN: Duplicate call_rcu() test starting.\n", KBUILD_MODNAME);

	/* Try to queue the rh2 pair of callbacks for the same grace period. */
	preempt_disable(); /* Prevent preemption from interrupting test. */
	rcu_read_lock(); /* Make it impossible to finish a grace period. */
	call_rcu(&rh1, rcu_torture_leak_cb); /* Start grace period. */
	local_irq_disable(); /* Make it harder to start a new grace period. */
	call_rcu(&rh2, rcu_torture_leak_cb);
	call_rcu(&rh2, rcu_torture_err_cb); /* Duplicate callback. */
	local_irq_enable();
	rcu_read_unlock();
	preempt_enable();

	/* Wait for them all to get done so we can safely return. */
	rcu_barrier();
	pr_alert("%s: WARN: Duplicate call_rcu() test complete.\n", KBUILD_MODNAME);
	destroy_rcu_head_on_stack(&rh1);
	destroy_rcu_head_on_stack(&rh2);
#else /* #ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD */
	pr_alert("%s: !CONFIG_DEBUG_OBJECTS_RCU_HEAD, not testing duplicate call_rcu()\n", KBUILD_MODNAME);
#endif /* #else #ifdef CONFIG_DEBUG_OBJECTS_RCU_HEAD */
}

static void rcutorture_sync(void)
{
	static unsigned long n;

	if (cur_ops->sync && !(++n & 0xfff))
		cur_ops->sync();
}

static int __init
rcu_torture_init(void)
{
	long i;
	int cpu;
	int firsterr = 0;
	static struct rcu_torture_ops *torture_ops[] = {
		&rcu_ops, &rcu_busted_ops, &srcu_ops, &srcud_ops,
		&busted_srcud_ops, &tasks_ops, &trivial_ops,
	};

	if (!torture_init_begin(torture_type, verbose))
		return -EBUSY;

	/* Process args and tell the world that the torturer is on the job. */
	for (i = 0; i < ARRAY_SIZE(torture_ops); i++) {
		cur_ops = torture_ops[i];
		if (strcmp(torture_type, cur_ops->name) == 0)
			break;
	}
	if (i == ARRAY_SIZE(torture_ops)) {
		pr_alert("rcu-torture: invalid torture type: \"%s\"\n",
			 torture_type);
		pr_alert("rcu-torture types:");
		for (i = 0; i < ARRAY_SIZE(torture_ops); i++)
			pr_cont(" %s", torture_ops[i]->name);
		pr_cont("\n");
		WARN_ON(!IS_MODULE(CONFIG_RCU_TORTURE_TEST));
		firsterr = -EINVAL;
		cur_ops = NULL;
		goto unwind;
	}
	if (cur_ops->fqs == NULL && fqs_duration != 0) {
		pr_alert("rcu-torture: ->fqs NULL and non-zero fqs_duration, fqs disabled.\n");
		fqs_duration = 0;
	}
	if (cur_ops->init)
		cur_ops->init();

	if (nreaders >= 0) {
		nrealreaders = nreaders;
	} else {
		nrealreaders = num_online_cpus() - 2 - nreaders;
		if (nrealreaders <= 0)
			nrealreaders = 1;
	}
	rcu_torture_print_module_parms(cur_ops, "Start of test");

	/* Set up the freelist. */

	INIT_LIST_HEAD(&rcu_torture_freelist);
	for (i = 0; i < ARRAY_SIZE(rcu_tortures); i++) {
		rcu_tortures[i].rtort_mbtest = 0;
		list_add_tail(&rcu_tortures[i].rtort_free,
			      &rcu_torture_freelist);
	}

	/* Initialize the statistics so that each run gets its own numbers. */

	rcu_torture_current = NULL;
	rcu_torture_current_version = 0;
	atomic_set(&n_rcu_torture_alloc, 0);
	atomic_set(&n_rcu_torture_alloc_fail, 0);
	atomic_set(&n_rcu_torture_free, 0);
	atomic_set(&n_rcu_torture_mberror, 0);
	atomic_set(&n_rcu_torture_error, 0);
	n_rcu_torture_barrier_error = 0;
	n_rcu_torture_boost_ktrerror = 0;
	n_rcu_torture_boost_rterror = 0;
	n_rcu_torture_boost_failure = 0;
	n_rcu_torture_boosts = 0;
	for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++)
		atomic_set(&rcu_torture_wcount[i], 0);
	for_each_possible_cpu(cpu) {
		for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++) {
			per_cpu(rcu_torture_count, cpu)[i] = 0;
			per_cpu(rcu_torture_batch, cpu)[i] = 0;
		}
	}
	err_segs_recorded = 0;
	rt_read_nsegs = 0;

	/* Start up the kthreads. */

	firsterr = torture_create_kthread(rcu_torture_writer, NULL,
					  writer_task);
	if (firsterr)
		goto unwind;
	if (nfakewriters > 0) {
		fakewriter_tasks = kcalloc(nfakewriters,
					   sizeof(fakewriter_tasks[0]),
					   GFP_KERNEL);
		if (fakewriter_tasks == NULL) {
			VERBOSE_TOROUT_ERRSTRING("out of memory");
			firsterr = -ENOMEM;
			goto unwind;
		}
	}
	for (i = 0; i < nfakewriters; i++) {
		firsterr = torture_create_kthread(rcu_torture_fakewriter,
						  NULL, fakewriter_tasks[i]);
		if (firsterr)
			goto unwind;
	}
	reader_tasks = kcalloc(nrealreaders, sizeof(reader_tasks[0]),
			       GFP_KERNEL);
	if (reader_tasks == NULL) {
		VERBOSE_TOROUT_ERRSTRING("out of memory");
		firsterr = -ENOMEM;
		goto unwind;
	}
	for (i = 0; i < nrealreaders; i++) {
		firsterr = torture_create_kthread(rcu_torture_reader, (void *)i,
						  reader_tasks[i]);
		if (firsterr)
			goto unwind;
	}
	if (stat_interval > 0) {
		firsterr = torture_create_kthread(rcu_torture_stats, NULL,
						  stats_task);
		if (firsterr)
			goto unwind;
	}
	if (test_no_idle_hz && shuffle_interval > 0) {
		firsterr = torture_shuffle_init(shuffle_interval * HZ);
		if (firsterr)
			goto unwind;
	}
	if (stutter < 0)
		stutter = 0;
	if (stutter) {
		int t;

		t = cur_ops->stall_dur ? cur_ops->stall_dur() : stutter * HZ;
		firsterr = torture_stutter_init(stutter * HZ, t);
		if (firsterr)
			goto unwind;
	}
	if (fqs_duration < 0)
		fqs_duration = 0;
	if (fqs_duration) {
		/* Create the fqs thread */
		firsterr = torture_create_kthread(rcu_torture_fqs, NULL,
						  fqs_task);
		if (firsterr)
			goto unwind;
	}
	if (test_boost_interval < 1)
		test_boost_interval = 1;
	if (test_boost_duration < 2)
		test_boost_duration = 2;
	if (rcu_torture_can_boost()) {

		boost_starttime = jiffies + test_boost_interval * HZ;

		firsterr = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "RCU_TORTURE",
					     rcutorture_booster_init,
					     rcutorture_booster_cleanup);
		if (firsterr < 0)
			goto unwind;
		rcutor_hp = firsterr;
	}
	shutdown_jiffies = jiffies + shutdown_secs * HZ;
	firsterr = torture_shutdown_init(shutdown_secs, rcu_torture_cleanup);
	if (firsterr)
		goto unwind;
	firsterr = torture_onoff_init(onoff_holdoff * HZ, onoff_interval,
				      rcutorture_sync);
	if (firsterr)
		goto unwind;
	firsterr = rcu_torture_stall_init();
	if (firsterr)
		goto unwind;
	firsterr = rcu_torture_fwd_prog_init();
	if (firsterr)
		goto unwind;
	firsterr = rcu_torture_barrier_init();
	if (firsterr)
		goto unwind;
	if (object_debug)
		rcu_test_debug_objects();
	torture_init_end();
	return 0;

unwind:
	torture_init_end();
	rcu_torture_cleanup();
	return firsterr;
}

module_init(rcu_torture_init);
module_exit(rcu_torture_cleanup);
