// SPDX-License-Identifier: GPL-2.0+
/*
 * Read-Copy Update module-based torture test facility
 *
 * Copyright (C) IBM Corporation, 2005, 2006
 *
 * Authors: Paul E. McKenney <paulmck@linux.ibm.com>
 *	  Josh Triplett <josh@joshtriplett.org>
 *
 * See also:  Documentation/RCU/torture.rst
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
#include <linux/rcupdate_wait.h>
#include <linux/rcu_notifier.h>
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
#include <linux/rcupdate_trace.h>
#include <linux/nmi.h>

#include "rcu.h"

MODULE_DESCRIPTION("Read-Copy Update module-based torture test facility");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Paul E. McKenney <paulmck@linux.ibm.com> and Josh Triplett <josh@joshtriplett.org>");

/* Bits for ->extendables field, extendables param, and related definitions. */
#define RCUTORTURE_RDR_SHIFT_1	 8	/* Put SRCU index in upper bits. */
#define RCUTORTURE_RDR_MASK_1	 (0xff << RCUTORTURE_RDR_SHIFT_1)
#define RCUTORTURE_RDR_SHIFT_2	 16	/* Put SRCU index in upper bits. */
#define RCUTORTURE_RDR_MASK_2	 (0xff << RCUTORTURE_RDR_SHIFT_2)
#define RCUTORTURE_RDR_BH	 0x01	/* Extend readers by disabling bh. */
#define RCUTORTURE_RDR_IRQ	 0x02	/*  ... disabling interrupts. */
#define RCUTORTURE_RDR_PREEMPT	 0x04	/*  ... disabling preemption. */
#define RCUTORTURE_RDR_RBH	 0x08	/*  ... rcu_read_lock_bh(). */
#define RCUTORTURE_RDR_SCHED	 0x10	/*  ... rcu_read_lock_sched(). */
#define RCUTORTURE_RDR_RCU_1	 0x20	/*  ... entering another RCU reader. */
#define RCUTORTURE_RDR_RCU_2	 0x40	/*  ... entering another RCU reader. */
#define RCUTORTURE_RDR_NBITS	 7	/* Number of bits defined above. */
#define RCUTORTURE_MAX_EXTEND	 \
	(RCUTORTURE_RDR_BH | RCUTORTURE_RDR_IRQ | RCUTORTURE_RDR_PREEMPT | \
	 RCUTORTURE_RDR_RBH | RCUTORTURE_RDR_SCHED)
#define RCUTORTURE_RDR_ALLBITS	\
	(RCUTORTURE_MAX_EXTEND | RCUTORTURE_RDR_RCU_1 | RCUTORTURE_RDR_RCU_2 | \
	 RCUTORTURE_RDR_MASK_1 | RCUTORTURE_RDR_MASK_2)
#define RCUTORTURE_RDR_MAX_LOOPS 0x7	/* Maximum reader extensions. */
					/* Must be power of two minus one. */
#define RCUTORTURE_RDR_MAX_SEGS (RCUTORTURE_RDR_MAX_LOOPS + 3)

torture_param(int, extendables, RCUTORTURE_MAX_EXTEND,
	      "Extend readers by disabling bh (1), irqs (2), or preempt (4)");
torture_param(int, fqs_duration, 0, "Duration of fqs bursts (us), 0 to disable");
torture_param(int, fqs_holdoff, 0, "Holdoff time within fqs bursts (us)");
torture_param(int, fqs_stutter, 3, "Wait time between fqs bursts (s)");
torture_param(int, fwd_progress, 1, "Number of grace-period forward progress tasks (0 to disable)");
torture_param(int, fwd_progress_div, 4, "Fraction of CPU stall to wait");
torture_param(int, fwd_progress_holdoff, 60, "Time between forward-progress tests (s)");
torture_param(bool, fwd_progress_need_resched, 1, "Hide cond_resched() behind need_resched()");
torture_param(bool, gp_cond, false, "Use conditional/async GP wait primitives");
torture_param(bool, gp_cond_exp, false, "Use conditional/async expedited GP wait primitives");
torture_param(bool, gp_cond_full, false, "Use conditional/async full-state GP wait primitives");
torture_param(bool, gp_cond_exp_full, false,
		    "Use conditional/async full-stateexpedited GP wait primitives");
torture_param(int, gp_cond_wi, 16 * USEC_PER_SEC / HZ,
		   "Wait interval for normal conditional grace periods, us (default 16 jiffies)");
torture_param(int, gp_cond_wi_exp, 128,
		   "Wait interval for expedited conditional grace periods, us (default 128 us)");
torture_param(bool, gp_exp, false, "Use expedited GP wait primitives");
torture_param(bool, gp_normal, false, "Use normal (non-expedited) GP wait primitives");
torture_param(bool, gp_poll, false, "Use polling GP wait primitives");
torture_param(bool, gp_poll_exp, false, "Use polling expedited GP wait primitives");
torture_param(bool, gp_poll_full, false, "Use polling full-state GP wait primitives");
torture_param(bool, gp_poll_exp_full, false, "Use polling full-state expedited GP wait primitives");
torture_param(int, gp_poll_wi, 16 * USEC_PER_SEC / HZ,
		   "Wait interval for normal polled grace periods, us (default 16 jiffies)");
torture_param(int, gp_poll_wi_exp, 128,
		   "Wait interval for expedited polled grace periods, us (default 128 us)");
torture_param(bool, gp_sync, false, "Use synchronous GP wait primitives");
torture_param(int, irqreader, 1, "Allow RCU readers from irq handlers");
torture_param(int, leakpointer, 0, "Leak pointer dereferences from readers");
torture_param(int, n_barrier_cbs, 0, "# of callbacks/kthreads for barrier testing");
torture_param(int, nfakewriters, 4, "Number of RCU fake writer threads");
torture_param(int, nreaders, -1, "Number of RCU reader threads");
torture_param(int, object_debug, 0, "Enable debug-object double call_rcu() testing");
torture_param(int, onoff_holdoff, 0, "Time after boot before CPU hotplugs (s)");
torture_param(int, onoff_interval, 0, "Time between CPU hotplugs (jiffies), 0=disable");
torture_param(int, nocbs_nthreads, 0, "Number of NOCB toggle threads, 0 to disable");
torture_param(int, nocbs_toggle, 1000, "Time between toggling nocb state (ms)");
torture_param(int, preempt_duration, 0, "Preemption duration (ms), zero to disable");
torture_param(int, preempt_interval, MSEC_PER_SEC, "Interval between preemptions (ms)");
torture_param(int, read_exit_delay, 13, "Delay between read-then-exit episodes (s)");
torture_param(int, read_exit_burst, 16, "# of read-then-exit bursts per episode, zero to disable");
torture_param(int, reader_flavor, SRCU_READ_FLAVOR_NORMAL, "Reader flavors to use, one per bit.");
torture_param(int, shuffle_interval, 3, "Number of seconds between shuffles");
torture_param(int, shutdown_secs, 0, "Shutdown time (s), <= zero to disable.");
torture_param(int, stall_cpu, 0, "Stall duration (s), zero to disable.");
torture_param(int, stall_cpu_holdoff, 10, "Time to wait before starting stall (s).");
torture_param(bool, stall_no_softlockup, false, "Avoid softlockup warning during cpu stall.");
torture_param(int, stall_cpu_irqsoff, 0, "Disable interrupts while stalling.");
torture_param(int, stall_cpu_block, 0, "Sleep while stalling.");
torture_param(int, stall_cpu_repeat, 0, "Number of additional stalls after the first one.");
torture_param(int, stall_gp_kthread, 0, "Grace-period kthread stall duration (s).");
torture_param(int, stat_interval, 60, "Number of seconds between stats printk()s");
torture_param(int, stutter, 5, "Number of seconds to run/halt test");
torture_param(int, test_boost, 1, "Test RCU prio boost: 0=no, 1=maybe, 2=yes.");
torture_param(int, test_boost_duration, 4, "Duration of each boost test, seconds.");
torture_param(int, test_boost_holdoff, 0, "Holdoff time from rcutorture start, seconds.");
torture_param(int, test_boost_interval, 7, "Interval between boost tests, seconds.");
torture_param(int, test_nmis, 0, "End-test NMI tests, 0 to disable.");
torture_param(bool, test_no_idle_hz, true, "Test support for tickless idle CPUs");
torture_param(int, test_srcu_lockdep, 0, "Test specified SRCU deadlock scenario.");
torture_param(int, verbose, 1, "Enable verbose debugging printk()s");

static char *torture_type = "rcu";
module_param(torture_type, charp, 0444);
MODULE_PARM_DESC(torture_type, "Type of RCU to torture (rcu, srcu, ...)");

static int nrealnocbers;
static int nrealreaders;
static int nrealfakewriters;
static struct task_struct *writer_task;
static struct task_struct **fakewriter_tasks;
static struct task_struct **reader_tasks;
static struct task_struct **nocb_tasks;
static struct task_struct *stats_task;
static struct task_struct *fqs_task;
static struct task_struct *boost_tasks[NR_CPUS];
static struct task_struct *stall_task;
static struct task_struct **fwd_prog_tasks;
static struct task_struct **barrier_cbs_tasks;
static struct task_struct *barrier_task;
static struct task_struct *read_exit_task;
static struct task_struct *preempt_task;

#define RCU_TORTURE_PIPE_LEN 10

// Mailbox-like structure to check RCU global memory ordering.
struct rcu_torture_reader_check {
	unsigned long rtc_myloops;
	int rtc_chkrdr;
	unsigned long rtc_chkloops;
	int rtc_ready;
	struct rcu_torture_reader_check *rtc_assigner;
} ____cacheline_internodealigned_in_smp;

// Update-side data structure used to check RCU readers.
struct rcu_torture {
	struct rcu_head rtort_rcu;
	int rtort_pipe_count;
	struct list_head rtort_free;
	int rtort_mbtest;
	struct rcu_torture_reader_check *rtort_chkp;
};

static LIST_HEAD(rcu_torture_freelist);
static struct rcu_torture __rcu *rcu_torture_current;
static unsigned long rcu_torture_current_version;
static struct rcu_torture rcu_tortures[10 * RCU_TORTURE_PIPE_LEN];
static DEFINE_SPINLOCK(rcu_torture_lock);
static DEFINE_PER_CPU(long [RCU_TORTURE_PIPE_LEN + 1], rcu_torture_count);
static DEFINE_PER_CPU(long [RCU_TORTURE_PIPE_LEN + 1], rcu_torture_batch);
static atomic_t rcu_torture_wcount[RCU_TORTURE_PIPE_LEN + 1];
static struct rcu_torture_reader_check *rcu_torture_reader_mbchk;
static atomic_t n_rcu_torture_alloc;
static atomic_t n_rcu_torture_alloc_fail;
static atomic_t n_rcu_torture_free;
static atomic_t n_rcu_torture_mberror;
static atomic_t n_rcu_torture_mbchk_fail;
static atomic_t n_rcu_torture_mbchk_tries;
static atomic_t n_rcu_torture_error;
static long n_rcu_torture_barrier_error;
static long n_rcu_torture_boost_ktrerror;
static long n_rcu_torture_boost_failure;
static long n_rcu_torture_boosts;
static atomic_long_t n_rcu_torture_timers;
static long n_barrier_attempts;
static long n_barrier_successes; /* did rcu_barrier test succeed? */
static unsigned long n_read_exits;
static struct list_head rcu_torture_removed;
static unsigned long shutdown_jiffies;
static unsigned long start_gp_seq;
static atomic_long_t n_nocb_offload;
static atomic_long_t n_nocb_deoffload;

static int rcu_torture_writer_state;
#define RTWS_FIXED_DELAY	0
#define RTWS_DELAY		1
#define RTWS_REPLACE		2
#define RTWS_DEF_FREE		3
#define RTWS_EXP_SYNC		4
#define RTWS_COND_GET		5
#define RTWS_COND_GET_FULL	6
#define RTWS_COND_GET_EXP	7
#define RTWS_COND_GET_EXP_FULL	8
#define RTWS_COND_SYNC		9
#define RTWS_COND_SYNC_FULL	10
#define RTWS_COND_SYNC_EXP	11
#define RTWS_COND_SYNC_EXP_FULL	12
#define RTWS_POLL_GET		13
#define RTWS_POLL_GET_FULL	14
#define RTWS_POLL_GET_EXP	15
#define RTWS_POLL_GET_EXP_FULL	16
#define RTWS_POLL_WAIT		17
#define RTWS_POLL_WAIT_FULL	18
#define RTWS_POLL_WAIT_EXP	19
#define RTWS_POLL_WAIT_EXP_FULL	20
#define RTWS_SYNC		21
#define RTWS_STUTTER		22
#define RTWS_STOPPING		23
static const char * const rcu_torture_writer_state_names[] = {
	"RTWS_FIXED_DELAY",
	"RTWS_DELAY",
	"RTWS_REPLACE",
	"RTWS_DEF_FREE",
	"RTWS_EXP_SYNC",
	"RTWS_COND_GET",
	"RTWS_COND_GET_FULL",
	"RTWS_COND_GET_EXP",
	"RTWS_COND_GET_EXP_FULL",
	"RTWS_COND_SYNC",
	"RTWS_COND_SYNC_FULL",
	"RTWS_COND_SYNC_EXP",
	"RTWS_COND_SYNC_EXP_FULL",
	"RTWS_POLL_GET",
	"RTWS_POLL_GET_FULL",
	"RTWS_POLL_GET_EXP",
	"RTWS_POLL_GET_EXP_FULL",
	"RTWS_POLL_WAIT",
	"RTWS_POLL_WAIT_FULL",
	"RTWS_POLL_WAIT_EXP",
	"RTWS_POLL_WAIT_EXP_FULL",
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
	int rt_cpu;
	int rt_end_cpu;
	unsigned long long rt_gp_seq;
	unsigned long long rt_gp_seq_end;
	u64 rt_ts;
};
static int err_segs_recorded;
static struct rt_read_seg err_segs[RCUTORTURE_RDR_MAX_SEGS];
static int rt_read_nsegs;
static int rt_read_preempted;

static const char *rcu_torture_writer_state_getname(void)
{
	unsigned int i = READ_ONCE(rcu_torture_writer_state);

	if (i >= ARRAY_SIZE(rcu_torture_writer_state_names))
		return "???";
	return rcu_torture_writer_state_names[i];
}

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

static atomic_t rcu_fwd_cb_nodelay;	/* Short rcu_torture_delay() delays. */

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
	int (*readlock_held)(void);   // lockdep.
	int (*readlock_nesting)(void); // actual nesting, if available, -1 if not.
	unsigned long (*get_gp_seq)(void);
	unsigned long (*gp_diff)(unsigned long new, unsigned long old);
	void (*deferred_free)(struct rcu_torture *p);
	void (*sync)(void);
	void (*exp_sync)(void);
	unsigned long (*get_gp_state_exp)(void);
	unsigned long (*start_gp_poll_exp)(void);
	void (*start_gp_poll_exp_full)(struct rcu_gp_oldstate *rgosp);
	bool (*poll_gp_state_exp)(unsigned long oldstate);
	void (*cond_sync_exp)(unsigned long oldstate);
	void (*cond_sync_exp_full)(struct rcu_gp_oldstate *rgosp);
	unsigned long (*get_comp_state)(void);
	void (*get_comp_state_full)(struct rcu_gp_oldstate *rgosp);
	bool (*same_gp_state)(unsigned long oldstate1, unsigned long oldstate2);
	bool (*same_gp_state_full)(struct rcu_gp_oldstate *rgosp1, struct rcu_gp_oldstate *rgosp2);
	unsigned long (*get_gp_state)(void);
	void (*get_gp_state_full)(struct rcu_gp_oldstate *rgosp);
	unsigned long (*start_gp_poll)(void);
	void (*start_gp_poll_full)(struct rcu_gp_oldstate *rgosp);
	bool (*poll_gp_state)(unsigned long oldstate);
	bool (*poll_gp_state_full)(struct rcu_gp_oldstate *rgosp);
	bool (*poll_need_2gp)(bool poll, bool poll_full);
	void (*cond_sync)(unsigned long oldstate);
	void (*cond_sync_full)(struct rcu_gp_oldstate *rgosp);
	int poll_active;
	int poll_active_full;
	call_rcu_func_t call;
	void (*cb_barrier)(void);
	void (*fqs)(void);
	void (*stats)(void);
	void (*gp_kthread_dbg)(void);
	bool (*check_boost_failed)(unsigned long gp_state, int *cpup);
	int (*stall_dur)(void);
	void (*get_gp_data)(int *flags, unsigned long *gp_seq);
	void (*gp_slow_register)(atomic_t *rgssp);
	void (*gp_slow_unregister)(atomic_t *rgssp);
	bool (*reader_blocked)(void);
	unsigned long long (*gather_gp_seqs)(void);
	void (*format_gp_seqs)(unsigned long long seqs, char *cp, size_t len);
	long cbflood_max;
	int irq_capable;
	int can_boost;
	int extendables;
	int slow_gps;
	int no_pi_lock;
	int debug_objects;
	int start_poll_irqsoff;
	const char *name;
};

static struct rcu_torture_ops *cur_ops;

/*
 * Definitions for rcu torture testing.
 */

static int torture_readlock_not_held(void)
{
	return rcu_read_lock_bh_held() || rcu_read_lock_sched_held();
}

static int rcu_torture_read_lock(void)
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

	if (!atomic_read(&rcu_fwd_cb_nodelay) &&
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
	    !(torture_random(rrsp) % (nrealreaders * 500)))
		torture_preempt_schedule();  /* QS only if preemptible. */
}

static void rcu_torture_read_unlock(int idx)
{
	rcu_read_unlock();
}

static int rcu_torture_readlock_nesting(void)
{
	if (IS_ENABLED(CONFIG_PREEMPT_RCU))
		return rcu_preempt_depth();
	if (IS_ENABLED(CONFIG_PREEMPT_COUNT))
		return (preempt_count() & PREEMPT_MASK);
	return -1;
}

/*
 * Update callback in the pipe.  This should be invoked after a grace period.
 */
static bool
rcu_torture_pipe_update_one(struct rcu_torture *rp)
{
	int i;
	struct rcu_torture_reader_check *rtrcp = READ_ONCE(rp->rtort_chkp);

	if (rtrcp) {
		WRITE_ONCE(rp->rtort_chkp, NULL);
		smp_store_release(&rtrcp->rtc_ready, 1); // Pair with smp_load_acquire().
	}
	i = rp->rtort_pipe_count;
	if (i > RCU_TORTURE_PIPE_LEN)
		i = RCU_TORTURE_PIPE_LEN;
	atomic_inc(&rcu_torture_wcount[i]);
	WRITE_ONCE(rp->rtort_pipe_count, i + 1);
	ASSERT_EXCLUSIVE_WRITER(rp->rtort_pipe_count);
	if (i + 1 >= RCU_TORTURE_PIPE_LEN) {
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
	call_rcu_hurry(&p->rtort_rcu, rcu_torture_cb);
}

static void rcu_sync_torture_init(void)
{
	INIT_LIST_HEAD(&rcu_torture_removed);
}

static bool rcu_poll_need_2gp(bool poll, bool poll_full)
{
	return poll;
}

static struct rcu_torture_ops rcu_ops = {
	.ttype			= RCU_FLAVOR,
	.init			= rcu_sync_torture_init,
	.readlock		= rcu_torture_read_lock,
	.read_delay		= rcu_read_delay,
	.readunlock		= rcu_torture_read_unlock,
	.readlock_held		= torture_readlock_not_held,
	.readlock_nesting	= rcu_torture_readlock_nesting,
	.get_gp_seq		= rcu_get_gp_seq,
	.gp_diff		= rcu_seq_diff,
	.deferred_free		= rcu_torture_deferred_free,
	.sync			= synchronize_rcu,
	.exp_sync		= synchronize_rcu_expedited,
	.same_gp_state		= same_state_synchronize_rcu,
	.same_gp_state_full	= same_state_synchronize_rcu_full,
	.get_comp_state		= get_completed_synchronize_rcu,
	.get_comp_state_full	= get_completed_synchronize_rcu_full,
	.get_gp_state		= get_state_synchronize_rcu,
	.get_gp_state_full	= get_state_synchronize_rcu_full,
	.start_gp_poll		= start_poll_synchronize_rcu,
	.start_gp_poll_full	= start_poll_synchronize_rcu_full,
	.poll_gp_state		= poll_state_synchronize_rcu,
	.poll_gp_state_full	= poll_state_synchronize_rcu_full,
	.poll_need_2gp		= rcu_poll_need_2gp,
	.cond_sync		= cond_synchronize_rcu,
	.cond_sync_full		= cond_synchronize_rcu_full,
	.poll_active		= NUM_ACTIVE_RCU_POLL_OLDSTATE,
	.poll_active_full	= NUM_ACTIVE_RCU_POLL_FULL_OLDSTATE,
	.get_gp_state_exp	= get_state_synchronize_rcu,
	.start_gp_poll_exp	= start_poll_synchronize_rcu_expedited,
	.start_gp_poll_exp_full	= start_poll_synchronize_rcu_expedited_full,
	.poll_gp_state_exp	= poll_state_synchronize_rcu,
	.cond_sync_exp		= cond_synchronize_rcu_expedited,
	.cond_sync_exp_full	= cond_synchronize_rcu_expedited_full,
	.call			= call_rcu_hurry,
	.cb_barrier		= rcu_barrier,
	.fqs			= rcu_force_quiescent_state,
	.gp_kthread_dbg		= show_rcu_gp_kthreads,
	.check_boost_failed	= rcu_check_boost_fail,
	.stall_dur		= rcu_jiffies_till_stall_check,
	.get_gp_data		= rcutorture_get_gp_data,
	.gp_slow_register	= rcu_gp_slow_register,
	.gp_slow_unregister	= rcu_gp_slow_unregister,
	.reader_blocked		= IS_ENABLED(CONFIG_RCU_TORTURE_TEST_LOG_CPU)
				  ? has_rcu_reader_blocked
				  : NULL,
	.gather_gp_seqs		= rcutorture_gather_gp_seqs,
	.format_gp_seqs		= rcutorture_format_gp_seqs,
	.irq_capable		= 1,
	.can_boost		= IS_ENABLED(CONFIG_RCU_BOOST),
	.extendables		= RCUTORTURE_MAX_EXTEND,
	.debug_objects		= 1,
	.start_poll_irqsoff	= 1,
	.name			= "rcu"
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
	.readlock_held	= torture_readlock_not_held,
	.get_gp_seq	= rcu_no_completed,
	.deferred_free	= rcu_busted_torture_deferred_free,
	.sync		= synchronize_rcu_busted,
	.exp_sync	= synchronize_rcu_busted,
	.call		= call_rcu_busted,
	.gather_gp_seqs	= rcutorture_gather_gp_seqs,
	.format_gp_seqs	= rcutorture_format_gp_seqs,
	.irq_capable	= 1,
	.extendables	= RCUTORTURE_MAX_EXTEND,
	.name		= "busted"
};

/*
 * Definitions for srcu torture testing.
 */

DEFINE_STATIC_SRCU(srcu_ctl);
static struct srcu_struct srcu_ctld;
static struct srcu_struct *srcu_ctlp = &srcu_ctl;
static struct rcu_torture_ops srcud_ops;

static void srcu_get_gp_data(int *flags, unsigned long *gp_seq)
{
	srcutorture_get_gp_data(srcu_ctlp, flags, gp_seq);
}

static int srcu_torture_read_lock(void)
{
	int idx;
	struct srcu_ctr __percpu *scp;
	int ret = 0;

	WARN_ON_ONCE(reader_flavor & ~SRCU_READ_FLAVOR_ALL);

	if ((reader_flavor & SRCU_READ_FLAVOR_NORMAL) || !(reader_flavor & SRCU_READ_FLAVOR_ALL)) {
		idx = srcu_read_lock(srcu_ctlp);
		WARN_ON_ONCE(idx & ~0x1);
		ret += idx;
	}
	if (reader_flavor & SRCU_READ_FLAVOR_NMI) {
		idx = srcu_read_lock_nmisafe(srcu_ctlp);
		WARN_ON_ONCE(idx & ~0x1);
		ret += idx << 1;
	}
	if (reader_flavor & SRCU_READ_FLAVOR_LITE) {
		idx = srcu_read_lock_lite(srcu_ctlp);
		WARN_ON_ONCE(idx & ~0x1);
		ret += idx << 2;
	}
	if (reader_flavor & SRCU_READ_FLAVOR_FAST) {
		scp = srcu_read_lock_fast(srcu_ctlp);
		idx = __srcu_ptr_to_ctr(srcu_ctlp, scp);
		WARN_ON_ONCE(idx & ~0x1);
		ret += idx << 3;
	}
	return ret;
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

static void srcu_torture_read_unlock(int idx)
{
	WARN_ON_ONCE((reader_flavor && (idx & ~reader_flavor)) || (!reader_flavor && (idx & ~0x1)));
	if (reader_flavor & SRCU_READ_FLAVOR_FAST)
		srcu_read_unlock_fast(srcu_ctlp, __srcu_ctr_to_ptr(srcu_ctlp, (idx & 0x8) >> 3));
	if (reader_flavor & SRCU_READ_FLAVOR_LITE)
		srcu_read_unlock_lite(srcu_ctlp, (idx & 0x4) >> 2);
	if (reader_flavor & SRCU_READ_FLAVOR_NMI)
		srcu_read_unlock_nmisafe(srcu_ctlp, (idx & 0x2) >> 1);
	if ((reader_flavor & SRCU_READ_FLAVOR_NORMAL) || !(reader_flavor & SRCU_READ_FLAVOR_ALL))
		srcu_read_unlock(srcu_ctlp, idx & 0x1);
}

static int torture_srcu_read_lock_held(void)
{
	return srcu_read_lock_held(srcu_ctlp);
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

static unsigned long srcu_torture_get_gp_state(void)
{
	return get_state_synchronize_srcu(srcu_ctlp);
}

static unsigned long srcu_torture_start_gp_poll(void)
{
	return start_poll_synchronize_srcu(srcu_ctlp);
}

static bool srcu_torture_poll_gp_state(unsigned long oldstate)
{
	return poll_state_synchronize_srcu(srcu_ctlp, oldstate);
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
	.readlock_held	= torture_srcu_read_lock_held,
	.get_gp_seq	= srcu_torture_completed,
	.gp_diff	= rcu_seq_diff,
	.deferred_free	= srcu_torture_deferred_free,
	.sync		= srcu_torture_synchronize,
	.exp_sync	= srcu_torture_synchronize_expedited,
	.same_gp_state	= same_state_synchronize_srcu,
	.get_comp_state = get_completed_synchronize_srcu,
	.get_gp_state	= srcu_torture_get_gp_state,
	.start_gp_poll	= srcu_torture_start_gp_poll,
	.poll_gp_state	= srcu_torture_poll_gp_state,
	.poll_active	= NUM_ACTIVE_SRCU_POLL_OLDSTATE,
	.call		= srcu_torture_call,
	.cb_barrier	= srcu_torture_barrier,
	.stats		= srcu_torture_stats,
	.get_gp_data	= srcu_get_gp_data,
	.cbflood_max	= 50000,
	.irq_capable	= 1,
	.no_pi_lock	= IS_ENABLED(CONFIG_TINY_SRCU),
	.debug_objects	= 1,
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
	.readlock_held	= torture_srcu_read_lock_held,
	.get_gp_seq	= srcu_torture_completed,
	.gp_diff	= rcu_seq_diff,
	.deferred_free	= srcu_torture_deferred_free,
	.sync		= srcu_torture_synchronize,
	.exp_sync	= srcu_torture_synchronize_expedited,
	.same_gp_state	= same_state_synchronize_srcu,
	.get_comp_state = get_completed_synchronize_srcu,
	.get_gp_state	= srcu_torture_get_gp_state,
	.start_gp_poll	= srcu_torture_start_gp_poll,
	.poll_gp_state	= srcu_torture_poll_gp_state,
	.poll_active	= NUM_ACTIVE_SRCU_POLL_OLDSTATE,
	.call		= srcu_torture_call,
	.cb_barrier	= srcu_torture_barrier,
	.stats		= srcu_torture_stats,
	.get_gp_data	= srcu_get_gp_data,
	.cbflood_max	= 50000,
	.irq_capable	= 1,
	.no_pi_lock	= IS_ENABLED(CONFIG_TINY_SRCU),
	.debug_objects	= 1,
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
	.readlock_held	= torture_srcu_read_lock_held,
	.get_gp_seq	= srcu_torture_completed,
	.deferred_free	= srcu_torture_deferred_free,
	.sync		= srcu_torture_synchronize,
	.exp_sync	= srcu_torture_synchronize_expedited,
	.call		= srcu_torture_call,
	.cb_barrier	= srcu_torture_barrier,
	.stats		= srcu_torture_stats,
	.irq_capable	= 1,
	.no_pi_lock	= IS_ENABLED(CONFIG_TINY_SRCU),
	.extendables	= RCUTORTURE_MAX_EXTEND,
	.name		= "busted_srcud"
};

/*
 * Definitions for trivial CONFIG_PREEMPT=n-only torture testing.
 * This implementation does not necessarily work well with CPU hotplug.
 */

static void synchronize_rcu_trivial(void)
{
	int cpu;

	for_each_online_cpu(cpu) {
		torture_sched_setaffinity(current->pid, cpumask_of(cpu), true);
		WARN_ON_ONCE(raw_smp_processor_id() != cpu);
	}
}

static int rcu_torture_read_lock_trivial(void)
{
	preempt_disable();
	return 0;
}

static void rcu_torture_read_unlock_trivial(int idx)
{
	preempt_enable();
}

static struct rcu_torture_ops trivial_ops = {
	.ttype		= RCU_TRIVIAL_FLAVOR,
	.init		= rcu_sync_torture_init,
	.readlock	= rcu_torture_read_lock_trivial,
	.read_delay	= rcu_read_delay,  /* just reuse rcu's version. */
	.readunlock	= rcu_torture_read_unlock_trivial,
	.readlock_held	= torture_readlock_not_held,
	.get_gp_seq	= rcu_no_completed,
	.sync		= synchronize_rcu_trivial,
	.exp_sync	= synchronize_rcu_trivial,
	.irq_capable	= 1,
	.name		= "trivial"
};

#ifdef CONFIG_TASKS_RCU

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

static void synchronize_rcu_mult_test(void)
{
	synchronize_rcu_mult(call_rcu_tasks, call_rcu_hurry);
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
	.exp_sync	= synchronize_rcu_mult_test,
	.call		= call_rcu_tasks,
	.cb_barrier	= rcu_barrier_tasks,
	.gp_kthread_dbg	= show_rcu_tasks_classic_gp_kthread,
	.get_gp_data	= rcu_tasks_get_gp_data,
	.irq_capable	= 1,
	.slow_gps	= 1,
	.name		= "tasks"
};

#define TASKS_OPS &tasks_ops,

#else // #ifdef CONFIG_TASKS_RCU

#define TASKS_OPS

#endif // #else #ifdef CONFIG_TASKS_RCU


#ifdef CONFIG_TASKS_RUDE_RCU

/*
 * Definitions for rude RCU-tasks torture testing.
 */

static struct rcu_torture_ops tasks_rude_ops = {
	.ttype		= RCU_TASKS_RUDE_FLAVOR,
	.init		= rcu_sync_torture_init,
	.readlock	= rcu_torture_read_lock_trivial,
	.read_delay	= rcu_read_delay,  /* just reuse rcu's version. */
	.readunlock	= rcu_torture_read_unlock_trivial,
	.get_gp_seq	= rcu_no_completed,
	.sync		= synchronize_rcu_tasks_rude,
	.exp_sync	= synchronize_rcu_tasks_rude,
	.gp_kthread_dbg	= show_rcu_tasks_rude_gp_kthread,
	.get_gp_data	= rcu_tasks_rude_get_gp_data,
	.cbflood_max	= 50000,
	.irq_capable	= 1,
	.name		= "tasks-rude"
};

#define TASKS_RUDE_OPS &tasks_rude_ops,

#else // #ifdef CONFIG_TASKS_RUDE_RCU

#define TASKS_RUDE_OPS

#endif // #else #ifdef CONFIG_TASKS_RUDE_RCU


#ifdef CONFIG_TASKS_TRACE_RCU

/*
 * Definitions for tracing RCU-tasks torture testing.
 */

static int tasks_tracing_torture_read_lock(void)
{
	rcu_read_lock_trace();
	return 0;
}

static void tasks_tracing_torture_read_unlock(int idx)
{
	rcu_read_unlock_trace();
}

static void rcu_tasks_tracing_torture_deferred_free(struct rcu_torture *p)
{
	call_rcu_tasks_trace(&p->rtort_rcu, rcu_torture_cb);
}

static struct rcu_torture_ops tasks_tracing_ops = {
	.ttype		= RCU_TASKS_TRACING_FLAVOR,
	.init		= rcu_sync_torture_init,
	.readlock	= tasks_tracing_torture_read_lock,
	.read_delay	= srcu_read_delay,  /* just reuse srcu's version. */
	.readunlock	= tasks_tracing_torture_read_unlock,
	.readlock_held	= rcu_read_lock_trace_held,
	.get_gp_seq	= rcu_no_completed,
	.deferred_free	= rcu_tasks_tracing_torture_deferred_free,
	.sync		= synchronize_rcu_tasks_trace,
	.exp_sync	= synchronize_rcu_tasks_trace,
	.call		= call_rcu_tasks_trace,
	.cb_barrier	= rcu_barrier_tasks_trace,
	.gp_kthread_dbg	= show_rcu_tasks_trace_gp_kthread,
	.get_gp_data    = rcu_tasks_trace_get_gp_data,
	.cbflood_max	= 50000,
	.irq_capable	= 1,
	.slow_gps	= 1,
	.name		= "tasks-tracing"
};

#define TASKS_TRACING_OPS &tasks_tracing_ops,

#else // #ifdef CONFIG_TASKS_TRACE_RCU

#define TASKS_TRACING_OPS

#endif // #else #ifdef CONFIG_TASKS_TRACE_RCU


static unsigned long rcutorture_seq_diff(unsigned long new, unsigned long old)
{
	if (!cur_ops->gp_diff)
		return new - old;
	return cur_ops->gp_diff(new, old);
}

/*
 * RCU torture priority-boost testing.  Runs one real-time thread per
 * CPU for moderate bursts, repeatedly starting grace periods and waiting
 * for them to complete.  If a given grace period takes too long, we assume
 * that priority inversion has occurred.
 */

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

static bool rcu_torture_boost_failed(unsigned long gp_state, unsigned long *start)
{
	int cpu;
	static int dbg_done;
	unsigned long end = jiffies;
	bool gp_done;
	unsigned long j;
	static unsigned long last_persist;
	unsigned long lp;
	unsigned long mininterval = test_boost_duration * HZ - HZ / 2;

	if (end - *start > mininterval) {
		// Recheck after checking time to avoid false positives.
		smp_mb(); // Time check before grace-period check.
		if (cur_ops->poll_gp_state(gp_state))
			return false; // passed, though perhaps just barely
		if (cur_ops->check_boost_failed && !cur_ops->check_boost_failed(gp_state, &cpu)) {
			// At most one persisted message per boost test.
			j = jiffies;
			lp = READ_ONCE(last_persist);
			if (time_after(j, lp + mininterval) &&
			    cmpxchg(&last_persist, lp, j) == lp) {
				if (cpu < 0)
					pr_info("Boost inversion persisted: QS from all CPUs\n");
				else
					pr_info("Boost inversion persisted: No QS from CPU %d\n", cpu);
			}
			return false; // passed on a technicality
		}
		VERBOSE_TOROUT_STRING("rcu_torture_boost boosting failed");
		n_rcu_torture_boost_failure++;
		if (!xchg(&dbg_done, 1) && cur_ops->gp_kthread_dbg) {
			pr_info("Boost inversion thread ->rt_priority %u gp_state %lu jiffies %lu\n",
				current->rt_priority, gp_state, end - *start);
			cur_ops->gp_kthread_dbg();
			// Recheck after print to flag grace period ending during splat.
			gp_done = cur_ops->poll_gp_state(gp_state);
			pr_info("Boost inversion: GP %lu %s.\n", gp_state,
				gp_done ? "ended already" : "still pending");

		}

		return true; // failed
	} else if (cur_ops->check_boost_failed && !cur_ops->check_boost_failed(gp_state, NULL)) {
		*start = jiffies;
	}

	return false; // passed
}

static int rcu_torture_boost(void *arg)
{
	unsigned long endtime;
	unsigned long gp_state;
	unsigned long gp_state_time;
	unsigned long oldstarttime;
	unsigned long booststarttime = get_torture_init_jiffies() + test_boost_holdoff * HZ;

	if (test_boost_holdoff <= 0 || time_after(jiffies, booststarttime)) {
		VERBOSE_TOROUT_STRING("rcu_torture_boost started");
	} else {
		VERBOSE_TOROUT_STRING("rcu_torture_boost started holdoff period");
		while (time_before(jiffies, booststarttime)) {
			schedule_timeout_idle(HZ);
			if (kthread_should_stop())
				goto cleanup;
		}
		VERBOSE_TOROUT_STRING("rcu_torture_boost finished holdoff period");
	}

	/* Set real-time priority. */
	sched_set_fifo_low(current);

	/* Each pass through the following loop does one boost-test cycle. */
	do {
		bool failed = false; // Test failed already in this test interval
		bool gp_initiated = false;

		if (kthread_should_stop())
			goto checkwait;

		/* Wait for the next test interval. */
		oldstarttime = READ_ONCE(boost_starttime);
		while (time_before(jiffies, oldstarttime)) {
			schedule_timeout_interruptible(oldstarttime - jiffies);
			if (stutter_wait("rcu_torture_boost"))
				sched_set_fifo_low(current);
			if (torture_must_stop())
				goto checkwait;
		}

		// Do one boost-test interval.
		endtime = oldstarttime + test_boost_duration * HZ;
		while (time_before(jiffies, endtime)) {
			// Has current GP gone too long?
			if (gp_initiated && !failed && !cur_ops->poll_gp_state(gp_state))
				failed = rcu_torture_boost_failed(gp_state, &gp_state_time);
			// If we don't have a grace period in flight, start one.
			if (!gp_initiated || cur_ops->poll_gp_state(gp_state)) {
				gp_state = cur_ops->start_gp_poll();
				gp_initiated = true;
				gp_state_time = jiffies;
			}
			if (stutter_wait("rcu_torture_boost")) {
				sched_set_fifo_low(current);
				// If the grace period already ended,
				// we don't know when that happened, so
				// start over.
				if (cur_ops->poll_gp_state(gp_state))
					gp_initiated = false;
			}
			if (torture_must_stop())
				goto checkwait;
		}

		// In case the grace period extended beyond the end of the loop.
		if (gp_initiated && !failed && !cur_ops->poll_gp_state(gp_state))
			rcu_torture_boost_failed(gp_state, &gp_state_time);

		/*
		 * Set the start time of the next test interval.
		 * Yes, this is vulnerable to long delays, but such
		 * delays simply cause a false negative for the next
		 * interval.  Besides, we are running at RT priority,
		 * so delays should be relatively rare.
		 */
		while (oldstarttime == READ_ONCE(boost_starttime) && !kthread_should_stop()) {
			if (mutex_trylock(&boost_mutex)) {
				if (oldstarttime == boost_starttime) {
					WRITE_ONCE(boost_starttime,
						   jiffies + test_boost_interval * HZ);
					n_rcu_torture_boosts++;
				}
				mutex_unlock(&boost_mutex);
				break;
			}
			schedule_timeout_uninterruptible(HZ / 20);
		}

		/* Go do the stutter. */
checkwait:	if (stutter_wait("rcu_torture_boost"))
			sched_set_fifo_low(current);
	} while (!torture_must_stop());

cleanup:
	/* Clean up and exit. */
	while (!kthread_should_stop()) {
		torture_shutdown_absorb("rcu_torture_boost");
		schedule_timeout_uninterruptible(HZ / 20);
	}
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
	int oldnice = task_nice(current);

	VERBOSE_TOROUT_STRING("rcu_torture_fqs task started");
	do {
		fqs_resume_time = jiffies + fqs_stutter * HZ;
		while (time_before(jiffies, fqs_resume_time) &&
		       !kthread_should_stop()) {
			schedule_timeout_interruptible(HZ / 20);
		}
		fqs_burst_remaining = fqs_duration;
		while (fqs_burst_remaining > 0 &&
		       !kthread_should_stop()) {
			cur_ops->fqs();
			udelay(fqs_holdoff);
			fqs_burst_remaining -= fqs_holdoff;
		}
		if (stutter_wait("rcu_torture_fqs"))
			sched_set_normal(current, oldnice);
	} while (!torture_must_stop());
	torture_kthread_stopping("rcu_torture_fqs");
	return 0;
}

// Used by writers to randomly choose from the available grace-period primitives.
static int synctype[ARRAY_SIZE(rcu_torture_writer_state_names)] = { };
static int nsynctypes;

/*
 * Determine which grace-period primitives are available.
 */
static void rcu_torture_write_types(void)
{
	bool gp_cond1 = gp_cond, gp_cond_exp1 = gp_cond_exp, gp_cond_full1 = gp_cond_full;
	bool gp_cond_exp_full1 = gp_cond_exp_full, gp_exp1 = gp_exp, gp_poll_exp1 = gp_poll_exp;
	bool gp_poll_exp_full1 = gp_poll_exp_full, gp_normal1 = gp_normal, gp_poll1 = gp_poll;
	bool gp_poll_full1 = gp_poll_full, gp_sync1 = gp_sync;

	/* Initialize synctype[] array.  If none set, take default. */
	if (!gp_cond1 &&
	    !gp_cond_exp1 &&
	    !gp_cond_full1 &&
	    !gp_cond_exp_full1 &&
	    !gp_exp1 &&
	    !gp_poll_exp1 &&
	    !gp_poll_exp_full1 &&
	    !gp_normal1 &&
	    !gp_poll1 &&
	    !gp_poll_full1 &&
	    !gp_sync1) {
		gp_cond1 = true;
		gp_cond_exp1 = true;
		gp_cond_full1 = true;
		gp_cond_exp_full1 = true;
		gp_exp1 = true;
		gp_poll_exp1 = true;
		gp_poll_exp_full1 = true;
		gp_normal1 = true;
		gp_poll1 = true;
		gp_poll_full1 = true;
		gp_sync1 = true;
	}
	if (gp_cond1 && cur_ops->get_gp_state && cur_ops->cond_sync) {
		synctype[nsynctypes++] = RTWS_COND_GET;
		pr_info("%s: Testing conditional GPs.\n", __func__);
	} else if (gp_cond && (!cur_ops->get_gp_state || !cur_ops->cond_sync)) {
		pr_alert("%s: gp_cond without primitives.\n", __func__);
	}
	if (gp_cond_exp1 && cur_ops->get_gp_state_exp && cur_ops->cond_sync_exp) {
		synctype[nsynctypes++] = RTWS_COND_GET_EXP;
		pr_info("%s: Testing conditional expedited GPs.\n", __func__);
	} else if (gp_cond_exp && (!cur_ops->get_gp_state_exp || !cur_ops->cond_sync_exp)) {
		pr_alert("%s: gp_cond_exp without primitives.\n", __func__);
	}
	if (gp_cond_full1 && cur_ops->get_gp_state && cur_ops->cond_sync_full) {
		synctype[nsynctypes++] = RTWS_COND_GET_FULL;
		pr_info("%s: Testing conditional full-state GPs.\n", __func__);
	} else if (gp_cond_full && (!cur_ops->get_gp_state || !cur_ops->cond_sync_full)) {
		pr_alert("%s: gp_cond_full without primitives.\n", __func__);
	}
	if (gp_cond_exp_full1 && cur_ops->get_gp_state_exp && cur_ops->cond_sync_exp_full) {
		synctype[nsynctypes++] = RTWS_COND_GET_EXP_FULL;
		pr_info("%s: Testing conditional full-state expedited GPs.\n", __func__);
	} else if (gp_cond_exp_full &&
		   (!cur_ops->get_gp_state_exp || !cur_ops->cond_sync_exp_full)) {
		pr_alert("%s: gp_cond_exp_full without primitives.\n", __func__);
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
	if (gp_poll1 && cur_ops->get_comp_state && cur_ops->same_gp_state &&
	    cur_ops->start_gp_poll && cur_ops->poll_gp_state) {
		synctype[nsynctypes++] = RTWS_POLL_GET;
		pr_info("%s: Testing polling GPs.\n", __func__);
	} else if (gp_poll && (!cur_ops->start_gp_poll || !cur_ops->poll_gp_state)) {
		pr_alert("%s: gp_poll without primitives.\n", __func__);
	}
	if (gp_poll_full1 && cur_ops->get_comp_state_full && cur_ops->same_gp_state_full
	    && cur_ops->start_gp_poll_full && cur_ops->poll_gp_state_full) {
		synctype[nsynctypes++] = RTWS_POLL_GET_FULL;
		pr_info("%s: Testing polling full-state GPs.\n", __func__);
	} else if (gp_poll_full && (!cur_ops->start_gp_poll_full || !cur_ops->poll_gp_state_full)) {
		pr_alert("%s: gp_poll_full without primitives.\n", __func__);
	}
	if (gp_poll_exp1 && cur_ops->start_gp_poll_exp && cur_ops->poll_gp_state_exp) {
		synctype[nsynctypes++] = RTWS_POLL_GET_EXP;
		pr_info("%s: Testing polling expedited GPs.\n", __func__);
	} else if (gp_poll_exp && (!cur_ops->start_gp_poll_exp || !cur_ops->poll_gp_state_exp)) {
		pr_alert("%s: gp_poll_exp without primitives.\n", __func__);
	}
	if (gp_poll_exp_full1 && cur_ops->start_gp_poll_exp_full && cur_ops->poll_gp_state_full) {
		synctype[nsynctypes++] = RTWS_POLL_GET_EXP_FULL;
		pr_info("%s: Testing polling full-state expedited GPs.\n", __func__);
	} else if (gp_poll_exp_full &&
		   (!cur_ops->start_gp_poll_exp_full || !cur_ops->poll_gp_state_full)) {
		pr_alert("%s: gp_poll_exp_full without primitives.\n", __func__);
	}
	if (gp_sync1 && cur_ops->sync) {
		synctype[nsynctypes++] = RTWS_SYNC;
		pr_info("%s: Testing normal GPs.\n", __func__);
	} else if (gp_sync && !cur_ops->sync) {
		pr_alert("%s: gp_sync without primitives.\n", __func__);
	}
	pr_alert("%s: Testing %d update types.\n", __func__, nsynctypes);
	pr_info("%s: gp_cond_wi %d gp_cond_wi_exp %d gp_poll_wi %d gp_poll_wi_exp %d\n", __func__, gp_cond_wi, gp_cond_wi_exp, gp_poll_wi, gp_poll_wi_exp);
}

/*
 * Do the specified rcu_torture_writer() synchronous grace period,
 * while also testing out the polled APIs.  Note well that the single-CPU
 * grace-period optimizations must be accounted for.
 */
static void do_rtws_sync(struct torture_random_state *trsp, void (*sync)(void))
{
	unsigned long cookie;
	struct rcu_gp_oldstate cookie_full;
	bool dopoll;
	bool dopoll_full;
	unsigned long r = torture_random(trsp);

	dopoll = cur_ops->get_gp_state && cur_ops->poll_gp_state && !(r & 0x300);
	dopoll_full = cur_ops->get_gp_state_full && cur_ops->poll_gp_state_full && !(r & 0xc00);
	if (dopoll || dopoll_full)
		cpus_read_lock();
	if (dopoll)
		cookie = cur_ops->get_gp_state();
	if (dopoll_full)
		cur_ops->get_gp_state_full(&cookie_full);
	if (cur_ops->poll_need_2gp && cur_ops->poll_need_2gp(dopoll, dopoll_full))
		sync();
	sync();
	WARN_ONCE(dopoll && !cur_ops->poll_gp_state(cookie),
		  "%s: Cookie check 3 failed %pS() online %*pbl.",
		  __func__, sync, cpumask_pr_args(cpu_online_mask));
	WARN_ONCE(dopoll_full && !cur_ops->poll_gp_state_full(&cookie_full),
		  "%s: Cookie check 4 failed %pS() online %*pbl",
		  __func__, sync, cpumask_pr_args(cpu_online_mask));
	if (dopoll || dopoll_full)
		cpus_read_unlock();
}

/*
 * RCU torture writer kthread.  Repeatedly substitutes a new structure
 * for that pointed to by rcu_torture_current, freeing the old structure
 * after a series of grace periods (the "pipeline").
 */
static int
rcu_torture_writer(void *arg)
{
	bool boot_ended;
	bool can_expedite = !rcu_gp_is_expedited() && !rcu_gp_is_normal();
	unsigned long cookie;
	struct rcu_gp_oldstate cookie_full;
	int expediting = 0;
	unsigned long gp_snap;
	unsigned long gp_snap1;
	struct rcu_gp_oldstate gp_snap_full;
	struct rcu_gp_oldstate gp_snap1_full;
	int i;
	int idx;
	int oldnice = task_nice(current);
	struct rcu_gp_oldstate *rgo = NULL;
	int rgo_size = 0;
	struct rcu_torture *rp;
	struct rcu_torture *old_rp;
	static DEFINE_TORTURE_RANDOM(rand);
	unsigned long stallsdone = jiffies;
	bool stutter_waited;
	unsigned long *ulo = NULL;
	int ulo_size = 0;

	// If a new stall test is added, this must be adjusted.
	if (stall_cpu_holdoff + stall_gp_kthread + stall_cpu)
		stallsdone += (stall_cpu_holdoff + stall_gp_kthread + stall_cpu + 60) *
			      HZ * (stall_cpu_repeat + 1);
	VERBOSE_TOROUT_STRING("rcu_torture_writer task started");
	if (!can_expedite)
		pr_alert("%s" TORTURE_FLAG
			 " GP expediting controlled from boot/sysfs for %s.\n",
			 torture_type, cur_ops->name);
	if (WARN_ONCE(nsynctypes == 0,
		      "%s: No update-side primitives.\n", __func__)) {
		/*
		 * No updates primitives, so don't try updating.
		 * The resulting test won't be testing much, hence the
		 * above WARN_ONCE().
		 */
		rcu_torture_writer_state = RTWS_STOPPING;
		torture_kthread_stopping("rcu_torture_writer");
		return 0;
	}
	if (cur_ops->poll_active > 0) {
		ulo = kzalloc(cur_ops->poll_active * sizeof(ulo[0]), GFP_KERNEL);
		if (!WARN_ON(!ulo))
			ulo_size = cur_ops->poll_active;
	}
	if (cur_ops->poll_active_full > 0) {
		rgo = kzalloc(cur_ops->poll_active_full * sizeof(rgo[0]), GFP_KERNEL);
		if (!WARN_ON(!rgo))
			rgo_size = cur_ops->poll_active_full;
	}

	do {
		rcu_torture_writer_state = RTWS_FIXED_DELAY;
		torture_hrtimeout_us(500, 1000, &rand);
		rp = rcu_torture_alloc();
		if (rp == NULL)
			continue;
		rp->rtort_pipe_count = 0;
		ASSERT_EXCLUSIVE_WRITER(rp->rtort_pipe_count);
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
			WRITE_ONCE(old_rp->rtort_pipe_count,
				   old_rp->rtort_pipe_count + 1);
			ASSERT_EXCLUSIVE_WRITER(old_rp->rtort_pipe_count);

			// Make sure readers block polled grace periods.
			if (cur_ops->get_gp_state && cur_ops->poll_gp_state) {
				idx = cur_ops->readlock();
				cookie = cur_ops->get_gp_state();
				WARN_ONCE(cur_ops->poll_gp_state(cookie),
					  "%s: Cookie check 1 failed %s(%d) %lu->%lu\n",
					  __func__,
					  rcu_torture_writer_state_getname(),
					  rcu_torture_writer_state,
					  cookie, cur_ops->get_gp_state());
				if (cur_ops->get_comp_state) {
					cookie = cur_ops->get_comp_state();
					WARN_ON_ONCE(!cur_ops->poll_gp_state(cookie));
				}
				cur_ops->readunlock(idx);
			}
			if (cur_ops->get_gp_state_full && cur_ops->poll_gp_state_full) {
				idx = cur_ops->readlock();
				cur_ops->get_gp_state_full(&cookie_full);
				WARN_ONCE(cur_ops->poll_gp_state_full(&cookie_full),
					  "%s: Cookie check 5 failed %s(%d) online %*pbl\n",
					  __func__,
					  rcu_torture_writer_state_getname(),
					  rcu_torture_writer_state,
					  cpumask_pr_args(cpu_online_mask));
				if (cur_ops->get_comp_state_full) {
					cur_ops->get_comp_state_full(&cookie_full);
					WARN_ON_ONCE(!cur_ops->poll_gp_state_full(&cookie_full));
				}
				cur_ops->readunlock(idx);
			}
			switch (synctype[torture_random(&rand) % nsynctypes]) {
			case RTWS_DEF_FREE:
				rcu_torture_writer_state = RTWS_DEF_FREE;
				cur_ops->deferred_free(old_rp);
				break;
			case RTWS_EXP_SYNC:
				rcu_torture_writer_state = RTWS_EXP_SYNC;
				do_rtws_sync(&rand, cur_ops->exp_sync);
				rcu_torture_pipe_update(old_rp);
				break;
			case RTWS_COND_GET:
				rcu_torture_writer_state = RTWS_COND_GET;
				gp_snap = cur_ops->get_gp_state();
				torture_hrtimeout_us(torture_random(&rand) % gp_cond_wi,
						     1000, &rand);
				rcu_torture_writer_state = RTWS_COND_SYNC;
				cur_ops->cond_sync(gp_snap);
				rcu_torture_pipe_update(old_rp);
				break;
			case RTWS_COND_GET_EXP:
				rcu_torture_writer_state = RTWS_COND_GET_EXP;
				gp_snap = cur_ops->get_gp_state_exp();
				torture_hrtimeout_us(torture_random(&rand) % gp_cond_wi_exp,
						     1000, &rand);
				rcu_torture_writer_state = RTWS_COND_SYNC_EXP;
				cur_ops->cond_sync_exp(gp_snap);
				rcu_torture_pipe_update(old_rp);
				break;
			case RTWS_COND_GET_FULL:
				rcu_torture_writer_state = RTWS_COND_GET_FULL;
				cur_ops->get_gp_state_full(&gp_snap_full);
				torture_hrtimeout_us(torture_random(&rand) % gp_cond_wi,
						     1000, &rand);
				rcu_torture_writer_state = RTWS_COND_SYNC_FULL;
				cur_ops->cond_sync_full(&gp_snap_full);
				rcu_torture_pipe_update(old_rp);
				break;
			case RTWS_COND_GET_EXP_FULL:
				rcu_torture_writer_state = RTWS_COND_GET_EXP_FULL;
				cur_ops->get_gp_state_full(&gp_snap_full);
				torture_hrtimeout_us(torture_random(&rand) % gp_cond_wi_exp,
						     1000, &rand);
				rcu_torture_writer_state = RTWS_COND_SYNC_EXP_FULL;
				cur_ops->cond_sync_exp_full(&gp_snap_full);
				rcu_torture_pipe_update(old_rp);
				break;
			case RTWS_POLL_GET:
				rcu_torture_writer_state = RTWS_POLL_GET;
				for (i = 0; i < ulo_size; i++)
					ulo[i] = cur_ops->get_comp_state();
				gp_snap = cur_ops->start_gp_poll();
				rcu_torture_writer_state = RTWS_POLL_WAIT;
				while (!cur_ops->poll_gp_state(gp_snap)) {
					gp_snap1 = cur_ops->get_gp_state();
					for (i = 0; i < ulo_size; i++)
						if (cur_ops->poll_gp_state(ulo[i]) ||
						    cur_ops->same_gp_state(ulo[i], gp_snap1)) {
							ulo[i] = gp_snap1;
							break;
						}
					WARN_ON_ONCE(ulo_size > 0 && i >= ulo_size);
					torture_hrtimeout_us(torture_random(&rand) % gp_poll_wi,
							     1000, &rand);
				}
				rcu_torture_pipe_update(old_rp);
				break;
			case RTWS_POLL_GET_FULL:
				rcu_torture_writer_state = RTWS_POLL_GET_FULL;
				for (i = 0; i < rgo_size; i++)
					cur_ops->get_comp_state_full(&rgo[i]);
				cur_ops->start_gp_poll_full(&gp_snap_full);
				rcu_torture_writer_state = RTWS_POLL_WAIT_FULL;
				while (!cur_ops->poll_gp_state_full(&gp_snap_full)) {
					cur_ops->get_gp_state_full(&gp_snap1_full);
					for (i = 0; i < rgo_size; i++)
						if (cur_ops->poll_gp_state_full(&rgo[i]) ||
						    cur_ops->same_gp_state_full(&rgo[i],
										&gp_snap1_full)) {
							rgo[i] = gp_snap1_full;
							break;
						}
					WARN_ON_ONCE(rgo_size > 0 && i >= rgo_size);
					torture_hrtimeout_us(torture_random(&rand) % gp_poll_wi,
							     1000, &rand);
				}
				rcu_torture_pipe_update(old_rp);
				break;
			case RTWS_POLL_GET_EXP:
				rcu_torture_writer_state = RTWS_POLL_GET_EXP;
				gp_snap = cur_ops->start_gp_poll_exp();
				rcu_torture_writer_state = RTWS_POLL_WAIT_EXP;
				while (!cur_ops->poll_gp_state_exp(gp_snap))
					torture_hrtimeout_us(torture_random(&rand) % gp_poll_wi_exp,
							     1000, &rand);
				rcu_torture_pipe_update(old_rp);
				break;
			case RTWS_POLL_GET_EXP_FULL:
				rcu_torture_writer_state = RTWS_POLL_GET_EXP_FULL;
				cur_ops->start_gp_poll_exp_full(&gp_snap_full);
				rcu_torture_writer_state = RTWS_POLL_WAIT_EXP_FULL;
				while (!cur_ops->poll_gp_state_full(&gp_snap_full))
					torture_hrtimeout_us(torture_random(&rand) % gp_poll_wi_exp,
							     1000, &rand);
				rcu_torture_pipe_update(old_rp);
				break;
			case RTWS_SYNC:
				rcu_torture_writer_state = RTWS_SYNC;
				do_rtws_sync(&rand, cur_ops->sync);
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
		boot_ended = rcu_inkernel_boot_has_ended();
		stutter_waited = stutter_wait("rcu_torture_writer");
		if (stutter_waited &&
		    !atomic_read(&rcu_fwd_cb_nodelay) &&
		    !cur_ops->slow_gps &&
		    !torture_must_stop() &&
		    boot_ended &&
		    time_after(jiffies, stallsdone))
			for (i = 0; i < ARRAY_SIZE(rcu_tortures); i++)
				if (list_empty(&rcu_tortures[i].rtort_free) &&
				    rcu_access_pointer(rcu_torture_current) != &rcu_tortures[i]) {
					tracing_off();
					if (cur_ops->gp_kthread_dbg)
						cur_ops->gp_kthread_dbg();
					WARN(1, "%s: rtort_pipe_count: %d\n", __func__, rcu_tortures[i].rtort_pipe_count);
					rcu_ftrace_dump(DUMP_ALL);
				}
		if (stutter_waited)
			sched_set_normal(current, oldnice);
	} while (!torture_must_stop());
	rcu_torture_current = NULL;  // Let stats task know that we are done.
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
	kfree(ulo);
	kfree(rgo);
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
	unsigned long gp_snap;
	struct rcu_gp_oldstate gp_snap_full;
	DEFINE_TORTURE_RANDOM(rand);

	VERBOSE_TOROUT_STRING("rcu_torture_fakewriter task started");
	set_user_nice(current, MAX_NICE);

	if (WARN_ONCE(nsynctypes == 0,
		      "%s: No update-side primitives.\n", __func__)) {
		/*
		 * No updates primitives, so don't try updating.
		 * The resulting test won't be testing much, hence the
		 * above WARN_ONCE().
		 */
		torture_kthread_stopping("rcu_torture_fakewriter");
		return 0;
	}

	do {
		torture_hrtimeout_jiffies(torture_random(&rand) % 10, &rand);
		if (cur_ops->cb_barrier != NULL &&
		    torture_random(&rand) % (nrealfakewriters * 8) == 0) {
			cur_ops->cb_barrier();
		} else {
			switch (synctype[torture_random(&rand) % nsynctypes]) {
			case RTWS_DEF_FREE:
				break;
			case RTWS_EXP_SYNC:
				cur_ops->exp_sync();
				break;
			case RTWS_COND_GET:
				gp_snap = cur_ops->get_gp_state();
				torture_hrtimeout_jiffies(torture_random(&rand) % 16, &rand);
				cur_ops->cond_sync(gp_snap);
				break;
			case RTWS_COND_GET_EXP:
				gp_snap = cur_ops->get_gp_state_exp();
				torture_hrtimeout_jiffies(torture_random(&rand) % 16, &rand);
				cur_ops->cond_sync_exp(gp_snap);
				break;
			case RTWS_COND_GET_FULL:
				cur_ops->get_gp_state_full(&gp_snap_full);
				torture_hrtimeout_jiffies(torture_random(&rand) % 16, &rand);
				cur_ops->cond_sync_full(&gp_snap_full);
				break;
			case RTWS_COND_GET_EXP_FULL:
				cur_ops->get_gp_state_full(&gp_snap_full);
				torture_hrtimeout_jiffies(torture_random(&rand) % 16, &rand);
				cur_ops->cond_sync_exp_full(&gp_snap_full);
				break;
			case RTWS_POLL_GET:
				if (cur_ops->start_poll_irqsoff)
					local_irq_disable();
				gp_snap = cur_ops->start_gp_poll();
				if (cur_ops->start_poll_irqsoff)
					local_irq_enable();
				while (!cur_ops->poll_gp_state(gp_snap)) {
					torture_hrtimeout_jiffies(torture_random(&rand) % 16,
								  &rand);
				}
				break;
			case RTWS_POLL_GET_FULL:
				if (cur_ops->start_poll_irqsoff)
					local_irq_disable();
				cur_ops->start_gp_poll_full(&gp_snap_full);
				if (cur_ops->start_poll_irqsoff)
					local_irq_enable();
				while (!cur_ops->poll_gp_state_full(&gp_snap_full)) {
					torture_hrtimeout_jiffies(torture_random(&rand) % 16,
								  &rand);
				}
				break;
			case RTWS_POLL_GET_EXP:
				gp_snap = cur_ops->start_gp_poll_exp();
				while (!cur_ops->poll_gp_state_exp(gp_snap)) {
					torture_hrtimeout_jiffies(torture_random(&rand) % 16,
								  &rand);
				}
				break;
			case RTWS_POLL_GET_EXP_FULL:
				cur_ops->start_gp_poll_exp_full(&gp_snap_full);
				while (!cur_ops->poll_gp_state_full(&gp_snap_full)) {
					torture_hrtimeout_jiffies(torture_random(&rand) % 16,
								  &rand);
				}
				break;
			case RTWS_SYNC:
				cur_ops->sync();
				break;
			default:
				WARN_ON_ONCE(1);
				break;
			}
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

// Set up and carry out testing of RCU's global memory ordering
static void rcu_torture_reader_do_mbchk(long myid, struct rcu_torture *rtp,
					struct torture_random_state *trsp)
{
	unsigned long loops;
	int noc = torture_num_online_cpus();
	int rdrchked;
	int rdrchker;
	struct rcu_torture_reader_check *rtrcp; // Me.
	struct rcu_torture_reader_check *rtrcp_assigner; // Assigned us to do checking.
	struct rcu_torture_reader_check *rtrcp_chked; // Reader being checked.
	struct rcu_torture_reader_check *rtrcp_chker; // Reader doing checking when not me.

	if (myid < 0)
		return; // Don't try this from timer handlers.

	// Increment my counter.
	rtrcp = &rcu_torture_reader_mbchk[myid];
	WRITE_ONCE(rtrcp->rtc_myloops, rtrcp->rtc_myloops + 1);

	// Attempt to assign someone else some checking work.
	rdrchked = torture_random(trsp) % nrealreaders;
	rtrcp_chked = &rcu_torture_reader_mbchk[rdrchked];
	rdrchker = torture_random(trsp) % nrealreaders;
	rtrcp_chker = &rcu_torture_reader_mbchk[rdrchker];
	if (rdrchked != myid && rdrchked != rdrchker && noc >= rdrchked && noc >= rdrchker &&
	    smp_load_acquire(&rtrcp->rtc_chkrdr) < 0 && // Pairs with smp_store_release below.
	    !READ_ONCE(rtp->rtort_chkp) &&
	    !smp_load_acquire(&rtrcp_chker->rtc_assigner)) { // Pairs with smp_store_release below.
		rtrcp->rtc_chkloops = READ_ONCE(rtrcp_chked->rtc_myloops);
		WARN_ON_ONCE(rtrcp->rtc_chkrdr >= 0);
		rtrcp->rtc_chkrdr = rdrchked;
		WARN_ON_ONCE(rtrcp->rtc_ready); // This gets set after the grace period ends.
		if (cmpxchg_relaxed(&rtrcp_chker->rtc_assigner, NULL, rtrcp) ||
		    cmpxchg_relaxed(&rtp->rtort_chkp, NULL, rtrcp))
			(void)cmpxchg_relaxed(&rtrcp_chker->rtc_assigner, rtrcp, NULL); // Back out.
	}

	// If assigned some completed work, do it!
	rtrcp_assigner = READ_ONCE(rtrcp->rtc_assigner);
	if (!rtrcp_assigner || !smp_load_acquire(&rtrcp_assigner->rtc_ready))
		return; // No work or work not yet ready.
	rdrchked = rtrcp_assigner->rtc_chkrdr;
	if (WARN_ON_ONCE(rdrchked < 0))
		return;
	rtrcp_chked = &rcu_torture_reader_mbchk[rdrchked];
	loops = READ_ONCE(rtrcp_chked->rtc_myloops);
	atomic_inc(&n_rcu_torture_mbchk_tries);
	if (ULONG_CMP_LT(loops, rtrcp_assigner->rtc_chkloops))
		atomic_inc(&n_rcu_torture_mbchk_fail);
	rtrcp_assigner->rtc_chkloops = loops + ULONG_MAX / 2;
	rtrcp_assigner->rtc_ready = 0;
	smp_store_release(&rtrcp->rtc_assigner, NULL); // Someone else can assign us work.
	smp_store_release(&rtrcp_assigner->rtc_chkrdr, -1); // Assigner can again assign.
}

// Verify the specified RCUTORTURE_RDR* state.
#define ROEC_ARGS "%s %s: Current %#x  To add %#x  To remove %#x  preempt_count() %#x\n", __func__, s, curstate, new, old, preempt_count()
static void rcutorture_one_extend_check(char *s, int curstate, int new, int old, bool insoftirq)
{
	int mask;

	if (!IS_ENABLED(CONFIG_RCU_TORTURE_TEST_CHK_RDR_STATE))
		return;

	WARN_ONCE(!(curstate & RCUTORTURE_RDR_IRQ) && irqs_disabled(), ROEC_ARGS);
	WARN_ONCE((curstate & RCUTORTURE_RDR_IRQ) && !irqs_disabled(), ROEC_ARGS);

	// If CONFIG_PREEMPT_COUNT=n, further checks are unreliable.
	if (!IS_ENABLED(CONFIG_PREEMPT_COUNT))
		return;

	WARN_ONCE((curstate & (RCUTORTURE_RDR_BH | RCUTORTURE_RDR_RBH)) &&
		  !(preempt_count() & SOFTIRQ_MASK), ROEC_ARGS);
	WARN_ONCE((curstate & (RCUTORTURE_RDR_PREEMPT | RCUTORTURE_RDR_SCHED)) &&
		  !(preempt_count() & PREEMPT_MASK), ROEC_ARGS);
	WARN_ONCE(cur_ops->readlock_nesting &&
		  (curstate & (RCUTORTURE_RDR_RCU_1 | RCUTORTURE_RDR_RCU_2)) &&
		  cur_ops->readlock_nesting() == 0, ROEC_ARGS);

	// Timer handlers have all sorts of stuff disabled, so ignore
	// unintended disabling.
	if (insoftirq)
		return;

	WARN_ONCE(cur_ops->extendables &&
		  !(curstate & (RCUTORTURE_RDR_BH | RCUTORTURE_RDR_RBH)) &&
		  (preempt_count() & SOFTIRQ_MASK), ROEC_ARGS);

	/*
	 * non-preemptible RCU in a preemptible kernel uses preempt_disable()
	 * as rcu_read_lock().
	 */
	mask = RCUTORTURE_RDR_PREEMPT | RCUTORTURE_RDR_SCHED;
	if (!IS_ENABLED(CONFIG_PREEMPT_RCU))
		mask |= RCUTORTURE_RDR_RCU_1 | RCUTORTURE_RDR_RCU_2;

	WARN_ONCE(cur_ops->extendables && !(curstate & mask) &&
		  (preempt_count() & PREEMPT_MASK), ROEC_ARGS);

	/*
	 * non-preemptible RCU in a preemptible kernel uses "preempt_count() &
	 * PREEMPT_MASK" as ->readlock_nesting().
	 */
	mask = RCUTORTURE_RDR_RCU_1 | RCUTORTURE_RDR_RCU_2;
	if (!IS_ENABLED(CONFIG_PREEMPT_RCU))
		mask |= RCUTORTURE_RDR_PREEMPT | RCUTORTURE_RDR_SCHED;

	WARN_ONCE(cur_ops->readlock_nesting && !(curstate & mask) &&
		  cur_ops->readlock_nesting() > 0, ROEC_ARGS);
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
static void rcutorture_one_extend(int *readstate, int newstate, bool insoftirq,
				  struct torture_random_state *trsp,
				  struct rt_read_seg *rtrsp)
{
	bool first;
	unsigned long flags;
	int idxnew1 = -1;
	int idxnew2 = -1;
	int idxold1 = *readstate;
	int idxold2 = idxold1;
	int statesnew = ~*readstate & newstate;
	int statesold = *readstate & ~newstate;

	first = idxold1 == 0;
	WARN_ON_ONCE(idxold2 < 0);
	WARN_ON_ONCE(idxold2 & ~RCUTORTURE_RDR_ALLBITS);
	rcutorture_one_extend_check("before change", idxold1, statesnew, statesold, insoftirq);
	rtrsp->rt_readstate = newstate;

	/* First, put new protection in place to avoid critical-section gap. */
	if (statesnew & RCUTORTURE_RDR_BH)
		local_bh_disable();
	if (statesnew & RCUTORTURE_RDR_RBH)
		rcu_read_lock_bh();
	if (statesnew & RCUTORTURE_RDR_IRQ)
		local_irq_disable();
	if (statesnew & RCUTORTURE_RDR_PREEMPT)
		preempt_disable();
	if (statesnew & RCUTORTURE_RDR_SCHED)
		rcu_read_lock_sched();
	if (statesnew & RCUTORTURE_RDR_RCU_1)
		idxnew1 = (cur_ops->readlock() << RCUTORTURE_RDR_SHIFT_1) & RCUTORTURE_RDR_MASK_1;
	if (statesnew & RCUTORTURE_RDR_RCU_2)
		idxnew2 = (cur_ops->readlock() << RCUTORTURE_RDR_SHIFT_2) & RCUTORTURE_RDR_MASK_2;

	// Complain unless both the old and the new protection is in place.
	rcutorture_one_extend_check("during change",
				    idxold1 | statesnew, statesnew, statesold, insoftirq);

	// Sample CPU under both sets of protections to reduce confusion.
	if (IS_ENABLED(CONFIG_RCU_TORTURE_TEST_LOG_CPU)) {
		int cpu = raw_smp_processor_id();
		rtrsp->rt_cpu = cpu;
		if (!first) {
			rtrsp[-1].rt_end_cpu = cpu;
			if (cur_ops->reader_blocked)
				rtrsp[-1].rt_preempted = cur_ops->reader_blocked();
		}
	}
	// Sample grace-period sequence number, as good a place as any.
	if (IS_ENABLED(CONFIG_RCU_TORTURE_TEST_LOG_GP) && cur_ops->gather_gp_seqs) {
		rtrsp->rt_gp_seq = cur_ops->gather_gp_seqs();
		rtrsp->rt_ts = ktime_get_mono_fast_ns();
		if (!first)
			rtrsp[-1].rt_gp_seq_end = rtrsp->rt_gp_seq;
	}

	/*
	 * Next, remove old protection, in decreasing order of strength
	 * to avoid unlock paths that aren't safe in the stronger
	 * context. Namely: BH can not be enabled with disabled interrupts.
	 * Additionally PREEMPT_RT requires that BH is enabled in preemptible
	 * context.
	 */
	if (statesold & RCUTORTURE_RDR_IRQ)
		local_irq_enable();
	if (statesold & RCUTORTURE_RDR_PREEMPT)
		preempt_enable();
	if (statesold & RCUTORTURE_RDR_SCHED)
		rcu_read_unlock_sched();
	if (statesold & RCUTORTURE_RDR_BH)
		local_bh_enable();
	if (statesold & RCUTORTURE_RDR_RBH)
		rcu_read_unlock_bh();
	if (statesold & RCUTORTURE_RDR_RCU_2) {
		cur_ops->readunlock((idxold2 & RCUTORTURE_RDR_MASK_2) >> RCUTORTURE_RDR_SHIFT_2);
		WARN_ON_ONCE(idxnew2 != -1);
		idxold2 = 0;
	}
	if (statesold & RCUTORTURE_RDR_RCU_1) {
		bool lockit;

		lockit = !cur_ops->no_pi_lock && !statesnew && !(torture_random(trsp) & 0xffff);
		if (lockit)
			raw_spin_lock_irqsave(&current->pi_lock, flags);
		cur_ops->readunlock((idxold1 & RCUTORTURE_RDR_MASK_1) >> RCUTORTURE_RDR_SHIFT_1);
		WARN_ON_ONCE(idxnew1 != -1);
		idxold1 = 0;
		if (lockit)
			raw_spin_unlock_irqrestore(&current->pi_lock, flags);
	}

	/* Delay if neither beginning nor end and there was a change. */
	if ((statesnew || statesold) && *readstate && newstate)
		cur_ops->read_delay(trsp, rtrsp);

	/* Update the reader state. */
	if (idxnew1 == -1)
		idxnew1 = idxold1 & RCUTORTURE_RDR_MASK_1;
	WARN_ON_ONCE(idxnew1 < 0);
	if (idxnew2 == -1)
		idxnew2 = idxold2 & RCUTORTURE_RDR_MASK_2;
	WARN_ON_ONCE(idxnew2 < 0);
	*readstate = idxnew1 | idxnew2 | newstate;
	WARN_ON_ONCE(*readstate < 0);
	if (WARN_ON_ONCE(*readstate & ~RCUTORTURE_RDR_ALLBITS))
		pr_info("Unexpected readstate value of %#x\n", *readstate);
	rcutorture_one_extend_check("after change", *readstate, statesnew, statesold, insoftirq);
}

/* Return the biggest extendables mask given current RCU and boot parameters. */
static int rcutorture_extend_mask_max(void)
{
	int mask;

	WARN_ON_ONCE(extendables & ~RCUTORTURE_MAX_EXTEND);
	mask = extendables & RCUTORTURE_MAX_EXTEND & cur_ops->extendables;
	mask = mask | RCUTORTURE_RDR_RCU_1 | RCUTORTURE_RDR_RCU_2;
	return mask;
}

/* Return a random protection state mask, but with at least one bit set. */
static int
rcutorture_extend_mask(int oldmask, struct torture_random_state *trsp)
{
	int mask = rcutorture_extend_mask_max();
	unsigned long randmask1 = torture_random(trsp);
	unsigned long randmask2 = randmask1 >> 3;
	unsigned long preempts = RCUTORTURE_RDR_PREEMPT | RCUTORTURE_RDR_SCHED;
	unsigned long preempts_irq = preempts | RCUTORTURE_RDR_IRQ;
	unsigned long bhs = RCUTORTURE_RDR_BH | RCUTORTURE_RDR_RBH;

	WARN_ON_ONCE(mask >> RCUTORTURE_RDR_SHIFT_1);  // Can't have reader idx bits.
	/* Mostly only one bit (need preemption!), sometimes lots of bits. */
	if (!(randmask1 & 0x7))
		mask = mask & randmask2;
	else
		mask = mask & (1 << (randmask2 % RCUTORTURE_RDR_NBITS));

	// Can't have nested RCU reader without outer RCU reader.
	if (!(mask & RCUTORTURE_RDR_RCU_1) && (mask & RCUTORTURE_RDR_RCU_2)) {
		if (oldmask & RCUTORTURE_RDR_RCU_1)
			mask &= ~RCUTORTURE_RDR_RCU_2;
		else
			mask |= RCUTORTURE_RDR_RCU_1;
	}

	/*
	 * Can't enable bh w/irq disabled.
	 */
	if (mask & RCUTORTURE_RDR_IRQ)
		mask |= oldmask & bhs;

	/*
	 * Ideally these sequences would be detected in debug builds
	 * (regardless of RT), but until then don't stop testing
	 * them on non-RT.
	 */
	if (IS_ENABLED(CONFIG_PREEMPT_RT)) {
		/* Can't modify BH in atomic context */
		if (oldmask & preempts_irq)
			mask &= ~bhs;
		if ((oldmask | mask) & preempts_irq)
			mask |= oldmask & bhs;
	}

	return mask ?: RCUTORTURE_RDR_RCU_1;
}

/*
 * Do a randomly selected number of extensions of an existing RCU read-side
 * critical section.
 */
static struct rt_read_seg *
rcutorture_loop_extend(int *readstate, bool insoftirq, struct torture_random_state *trsp,
		       struct rt_read_seg *rtrsp)
{
	int i;
	int j;
	int mask = rcutorture_extend_mask_max();

	WARN_ON_ONCE(!*readstate); /* -Existing- RCU read-side critsect! */
	if (!((mask - 1) & mask))
		return rtrsp;  /* Current RCU reader not extendable. */
	/* Bias towards larger numbers of loops. */
	i = torture_random(trsp);
	i = ((i | (i >> 3)) & RCUTORTURE_RDR_MAX_LOOPS) + 1;
	for (j = 0; j < i; j++) {
		mask = rcutorture_extend_mask(*readstate, trsp);
		rcutorture_one_extend(readstate, mask, insoftirq, trsp, &rtrsp[j]);
	}
	return &rtrsp[j];
}

/*
 * Do one read-side critical section, returning false if there was
 * no data to read.  Can be invoked both from process context and
 * from a timer handler.
 */
static bool rcu_torture_one_read(struct torture_random_state *trsp, long myid)
{
	bool checkpolling = !(torture_random(trsp) & 0xfff);
	unsigned long cookie;
	struct rcu_gp_oldstate cookie_full;
	int i;
	unsigned long started;
	unsigned long completed;
	int newstate;
	struct rcu_torture *p;
	int pipe_count;
	bool preempted = false;
	int readstate = 0;
	struct rt_read_seg rtseg[RCUTORTURE_RDR_MAX_SEGS] = { { 0 } };
	struct rt_read_seg *rtrsp = &rtseg[0];
	struct rt_read_seg *rtrsp1;
	unsigned long long ts;

	WARN_ON_ONCE(!rcu_is_watching());
	newstate = rcutorture_extend_mask(readstate, trsp);
	rcutorture_one_extend(&readstate, newstate, myid < 0, trsp, rtrsp++);
	if (checkpolling) {
		if (cur_ops->get_gp_state && cur_ops->poll_gp_state)
			cookie = cur_ops->get_gp_state();
		if (cur_ops->get_gp_state_full && cur_ops->poll_gp_state_full)
			cur_ops->get_gp_state_full(&cookie_full);
	}
	started = cur_ops->get_gp_seq();
	ts = rcu_trace_clock_local();
	p = rcu_dereference_check(rcu_torture_current,
				  !cur_ops->readlock_held || cur_ops->readlock_held());
	if (p == NULL) {
		/* Wait for rcu_torture_writer to get underway */
		rcutorture_one_extend(&readstate, 0, myid < 0, trsp, rtrsp);
		return false;
	}
	if (p->rtort_mbtest == 0)
		atomic_inc(&n_rcu_torture_mberror);
	rcu_torture_reader_do_mbchk(myid, p, trsp);
	rtrsp = rcutorture_loop_extend(&readstate, myid < 0, trsp, rtrsp);
	preempt_disable();
	pipe_count = READ_ONCE(p->rtort_pipe_count);
	if (pipe_count > RCU_TORTURE_PIPE_LEN) {
		// Should not happen in a correct RCU implementation,
		// happens quite often for torture_type=busted.
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
	if (checkpolling) {
		if (cur_ops->get_gp_state && cur_ops->poll_gp_state)
			WARN_ONCE(cur_ops->poll_gp_state(cookie),
				  "%s: Cookie check 2 failed %s(%d) %lu->%lu\n",
				  __func__,
				  rcu_torture_writer_state_getname(),
				  rcu_torture_writer_state,
				  cookie, cur_ops->get_gp_state());
		if (cur_ops->get_gp_state_full && cur_ops->poll_gp_state_full)
			WARN_ONCE(cur_ops->poll_gp_state_full(&cookie_full),
				  "%s: Cookie check 6 failed %s(%d) online %*pbl\n",
				  __func__,
				  rcu_torture_writer_state_getname(),
				  rcu_torture_writer_state,
				  cpumask_pr_args(cpu_online_mask));
	}
	if (cur_ops->reader_blocked)
		preempted = cur_ops->reader_blocked();
	rcutorture_one_extend(&readstate, 0, myid < 0, trsp, rtrsp);
	WARN_ON_ONCE(readstate);
	// This next splat is expected behavior if leakpointer, especially
	// for CONFIG_RCU_STRICT_GRACE_PERIOD=y kernels.
	WARN_ON_ONCE(leakpointer && READ_ONCE(p->rtort_pipe_count) > 1);

	/* If error or close call, record the sequence of reader protections. */
	if ((pipe_count > 1 || completed > 1) && !xchg(&err_segs_recorded, 1)) {
		i = 0;
		for (rtrsp1 = &rtseg[0]; rtrsp1 < rtrsp; rtrsp1++)
			err_segs[i++] = *rtrsp1;
		rt_read_nsegs = i;
		rt_read_preempted = preempted;
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
	(void)rcu_torture_one_read(this_cpu_ptr(&rcu_torture_timer_rand), -1);

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
		if (!rcu_torture_one_read(&rand, myid) && !torture_must_stop())
			schedule_timeout_interruptible(HZ);
		if (time_after(jiffies, lastsleep) && !torture_must_stop()) {
			torture_hrtimeout_us(500, 1000, &rand);
			lastsleep = jiffies + 10;
		}
		while (torture_num_online_cpus() < mynumonline && !torture_must_stop())
			schedule_timeout_interruptible(HZ / 5);
		stutter_wait("rcu_torture_reader");
	} while (!torture_must_stop());
	if (irqreader && cur_ops->irq_capable) {
		timer_delete_sync(&t);
		destroy_timer_on_stack(&t);
	}
	tick_dep_clear_task(current, TICK_DEP_BIT_RCU);
	torture_kthread_stopping("rcu_torture_reader");
	return 0;
}

/*
 * Randomly Toggle CPUs' callback-offload state.  This uses hrtimers to
 * increase race probabilities and fuzzes the interval between toggling.
 */
static int rcu_nocb_toggle(void *arg)
{
	int cpu;
	int maxcpu = -1;
	int oldnice = task_nice(current);
	long r;
	DEFINE_TORTURE_RANDOM(rand);
	ktime_t toggle_delay;
	unsigned long toggle_fuzz;
	ktime_t toggle_interval = ms_to_ktime(nocbs_toggle);

	VERBOSE_TOROUT_STRING("rcu_nocb_toggle task started");
	while (!rcu_inkernel_boot_has_ended())
		schedule_timeout_interruptible(HZ / 10);
	for_each_possible_cpu(cpu)
		maxcpu = cpu;
	WARN_ON(maxcpu < 0);
	if (toggle_interval > ULONG_MAX)
		toggle_fuzz = ULONG_MAX >> 3;
	else
		toggle_fuzz = toggle_interval >> 3;
	if (toggle_fuzz <= 0)
		toggle_fuzz = NSEC_PER_USEC;
	do {
		r = torture_random(&rand);
		cpu = (r >> 1) % (maxcpu + 1);
		if (r & 0x1) {
			rcu_nocb_cpu_offload(cpu);
			atomic_long_inc(&n_nocb_offload);
		} else {
			rcu_nocb_cpu_deoffload(cpu);
			atomic_long_inc(&n_nocb_deoffload);
		}
		toggle_delay = torture_random(&rand) % toggle_fuzz + toggle_interval;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_hrtimeout(&toggle_delay, HRTIMER_MODE_REL);
		if (stutter_wait("rcu_nocb_toggle"))
			sched_set_normal(current, oldnice);
	} while (!torture_must_stop());
	torture_kthread_stopping("rcu_nocb_toggle");
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
	struct rcu_torture *rtcp;
	static unsigned long rtcv_snap = ULONG_MAX;
	static bool splatted;
	struct task_struct *wtp;

	for_each_possible_cpu(cpu) {
		for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++) {
			pipesummary[i] += READ_ONCE(per_cpu(rcu_torture_count, cpu)[i]);
			batchsummary[i] += READ_ONCE(per_cpu(rcu_torture_batch, cpu)[i]);
		}
	}
	for (i = RCU_TORTURE_PIPE_LEN; i >= 0; i--) {
		if (pipesummary[i] != 0)
			break;
	}

	pr_alert("%s%s ", torture_type, TORTURE_FLAG);
	rtcp = rcu_access_pointer(rcu_torture_current);
	pr_cont("rtc: %p %s: %lu tfle: %d rta: %d rtaf: %d rtf: %d ",
		rtcp,
		rtcp && !rcu_stall_is_suppressed_at_boot() ? "ver" : "VER",
		rcu_torture_current_version,
		list_empty(&rcu_torture_freelist),
		atomic_read(&n_rcu_torture_alloc),
		atomic_read(&n_rcu_torture_alloc_fail),
		atomic_read(&n_rcu_torture_free));
	pr_cont("rtmbe: %d rtmbkf: %d/%d rtbe: %ld rtbke: %ld ",
		atomic_read(&n_rcu_torture_mberror),
		atomic_read(&n_rcu_torture_mbchk_fail), atomic_read(&n_rcu_torture_mbchk_tries),
		n_rcu_torture_barrier_error,
		n_rcu_torture_boost_ktrerror);
	pr_cont("rtbf: %ld rtb: %ld nt: %ld ",
		n_rcu_torture_boost_failure,
		n_rcu_torture_boosts,
		atomic_long_read(&n_rcu_torture_timers));
	torture_onoff_stats();
	pr_cont("barrier: %ld/%ld:%ld ",
		data_race(n_barrier_successes),
		data_race(n_barrier_attempts),
		data_race(n_rcu_torture_barrier_error));
	pr_cont("read-exits: %ld ", data_race(n_read_exits)); // Statistic.
	pr_cont("nocb-toggles: %ld:%ld\n",
		atomic_long_read(&n_nocb_offload), atomic_long_read(&n_nocb_deoffload));

	pr_alert("%s%s ", torture_type, TORTURE_FLAG);
	if (atomic_read(&n_rcu_torture_mberror) ||
	    atomic_read(&n_rcu_torture_mbchk_fail) ||
	    n_rcu_torture_barrier_error || n_rcu_torture_boost_ktrerror ||
	    n_rcu_torture_boost_failure || i > 1) {
		pr_cont("%s", "!!! ");
		atomic_inc(&n_rcu_torture_error);
		WARN_ON_ONCE(atomic_read(&n_rcu_torture_mberror));
		WARN_ON_ONCE(atomic_read(&n_rcu_torture_mbchk_fail));
		WARN_ON_ONCE(n_rcu_torture_barrier_error);  // rcu_barrier()
		WARN_ON_ONCE(n_rcu_torture_boost_ktrerror); // no boost kthread
		WARN_ON_ONCE(n_rcu_torture_boost_failure); // boost failed (TIMER_SOFTIRQ RT prio?)
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
	    rcu_access_pointer(rcu_torture_current) &&
	    !rcu_stall_is_suppressed()) {
		int __maybe_unused flags = 0;
		unsigned long __maybe_unused gp_seq = 0;

		if (cur_ops->get_gp_data)
			cur_ops->get_gp_data(&flags, &gp_seq);
		wtp = READ_ONCE(writer_task);
		pr_alert("??? Writer stall state %s(%d) g%lu f%#x ->state %#x cpu %d\n",
			 rcu_torture_writer_state_getname(),
			 rcu_torture_writer_state, gp_seq, flags,
			 wtp == NULL ? ~0U : wtp->__state,
			 wtp == NULL ? -1 : (int)task_cpu(wtp));
		if (!splatted && wtp) {
			sched_show_task(wtp);
			splatted = true;
		}
		if (cur_ops->gp_kthread_dbg)
			cur_ops->gp_kthread_dbg();
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

/* Test mem_dump_obj() and friends.  */
static void rcu_torture_mem_dump_obj(void)
{
	struct rcu_head *rhp;
	struct kmem_cache *kcp;
	static int z;

	kcp = kmem_cache_create("rcuscale", 136, 8, SLAB_STORE_USER, NULL);
	if (WARN_ON_ONCE(!kcp))
		return;
	rhp = kmem_cache_alloc(kcp, GFP_KERNEL);
	if (WARN_ON_ONCE(!rhp)) {
		kmem_cache_destroy(kcp);
		return;
	}
	pr_alert("mem_dump_obj() slab test: rcu_torture_stats = %px, &rhp = %px, rhp = %px, &z = %px\n", stats_task, &rhp, rhp, &z);
	pr_alert("mem_dump_obj(ZERO_SIZE_PTR):");
	mem_dump_obj(ZERO_SIZE_PTR);
	pr_alert("mem_dump_obj(NULL):");
	mem_dump_obj(NULL);
	pr_alert("mem_dump_obj(%px):", &rhp);
	mem_dump_obj(&rhp);
	pr_alert("mem_dump_obj(%px):", rhp);
	mem_dump_obj(rhp);
	pr_alert("mem_dump_obj(%px):", &rhp->func);
	mem_dump_obj(&rhp->func);
	pr_alert("mem_dump_obj(%px):", &z);
	mem_dump_obj(&z);
	kmem_cache_free(kcp, rhp);
	kmem_cache_destroy(kcp);
	rhp = kmalloc(sizeof(*rhp), GFP_KERNEL);
	if (WARN_ON_ONCE(!rhp))
		return;
	pr_alert("mem_dump_obj() kmalloc test: rcu_torture_stats = %px, &rhp = %px, rhp = %px\n", stats_task, &rhp, rhp);
	pr_alert("mem_dump_obj(kmalloc %px):", rhp);
	mem_dump_obj(rhp);
	pr_alert("mem_dump_obj(kmalloc %px):", &rhp->func);
	mem_dump_obj(&rhp->func);
	kfree(rhp);
	rhp = vmalloc(4096);
	if (WARN_ON_ONCE(!rhp))
		return;
	pr_alert("mem_dump_obj() vmalloc test: rcu_torture_stats = %px, &rhp = %px, rhp = %px\n", stats_task, &rhp, rhp);
	pr_alert("mem_dump_obj(vmalloc %px):", rhp);
	mem_dump_obj(rhp);
	pr_alert("mem_dump_obj(vmalloc %px):", &rhp->func);
	mem_dump_obj(&rhp->func);
	vfree(rhp);
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
		 "test_boost_duration=%d test_boost_holdoff=%d shutdown_secs=%d "
		 "stall_cpu=%d stall_cpu_holdoff=%d stall_cpu_irqsoff=%d "
		 "stall_cpu_block=%d stall_cpu_repeat=%d "
		 "n_barrier_cbs=%d "
		 "onoff_interval=%d onoff_holdoff=%d "
		 "read_exit_delay=%d read_exit_burst=%d "
		 "reader_flavor=%x "
		 "nocbs_nthreads=%d nocbs_toggle=%d "
		 "test_nmis=%d "
		 "preempt_duration=%d preempt_interval=%d\n",
		 torture_type, tag, nrealreaders, nrealfakewriters,
		 stat_interval, verbose, test_no_idle_hz, shuffle_interval,
		 stutter, irqreader, fqs_duration, fqs_holdoff, fqs_stutter,
		 test_boost, cur_ops->can_boost,
		 test_boost_interval, test_boost_duration, test_boost_holdoff, shutdown_secs,
		 stall_cpu, stall_cpu_holdoff, stall_cpu_irqsoff,
		 stall_cpu_block, stall_cpu_repeat,
		 n_barrier_cbs,
		 onoff_interval, onoff_holdoff,
		 read_exit_delay, read_exit_burst,
		 reader_flavor,
		 nocbs_nthreads, nocbs_toggle,
		 test_nmis,
		 preempt_duration, preempt_interval);
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

	// Testing RCU priority boosting requires rcutorture do
	// some serious abuse.  Counter this by running ksoftirqd
	// at higher priority.
	if (IS_BUILTIN(CONFIG_RCU_TORTURE_TEST)) {
		struct sched_param sp;
		struct task_struct *t;

		t = per_cpu(ksoftirqd, cpu);
		WARN_ON_ONCE(!t);
		sp.sched_priority = 2;
		sched_setscheduler_nocheck(t, SCHED_FIFO, &sp);
#ifdef CONFIG_IRQ_FORCED_THREADING
		if (force_irqthreads()) {
			t = per_cpu(ktimerd, cpu);
			WARN_ON_ONCE(!t);
			sp.sched_priority = 2;
			sched_setscheduler_nocheck(t, SCHED_FIFO, &sp);
		}
#endif
	}

	/* Don't allow time recalculation while creating a new task. */
	mutex_lock(&boost_mutex);
	rcu_torture_disable_rt_throttle();
	VERBOSE_TOROUT_STRING("Creating rcu_torture_boost task");
	boost_tasks[cpu] = kthread_run_on_cpu(rcu_torture_boost, NULL,
					      cpu, "rcu_torture_boost_%u");
	if (IS_ERR(boost_tasks[cpu])) {
		retval = PTR_ERR(boost_tasks[cpu]);
		VERBOSE_TOROUT_STRING("rcu_torture_boost task create failed");
		n_rcu_torture_boost_ktrerror++;
		boost_tasks[cpu] = NULL;
		mutex_unlock(&boost_mutex);
		return retval;
	}
	mutex_unlock(&boost_mutex);
	return 0;
}

static int rcu_torture_stall_nf(struct notifier_block *nb, unsigned long v, void *ptr)
{
	pr_info("%s: v=%lu, duration=%lu.\n", __func__, v, (unsigned long)ptr);
	return NOTIFY_OK;
}

static struct notifier_block rcu_torture_stall_block = {
	.notifier_call = rcu_torture_stall_nf,
};

/*
 * CPU-stall kthread.  It waits as specified by stall_cpu_holdoff, then
 * induces a CPU stall for the time specified by stall_cpu.  If a new
 * stall test is added, stallsdone in rcu_torture_writer() must be adjusted.
 */
static void rcu_torture_stall_one(int rep, int irqsoff)
{
	int idx;
	unsigned long stop_at;

	if (stall_cpu_holdoff > 0) {
		VERBOSE_TOROUT_STRING("rcu_torture_stall begin holdoff");
		schedule_timeout_interruptible(stall_cpu_holdoff * HZ);
		VERBOSE_TOROUT_STRING("rcu_torture_stall end holdoff");
	}
	if (!kthread_should_stop() && stall_gp_kthread > 0) {
		VERBOSE_TOROUT_STRING("rcu_torture_stall begin GP stall");
		rcu_gp_set_torture_wait(stall_gp_kthread * HZ);
		for (idx = 0; idx < stall_gp_kthread + 2; idx++) {
			if (kthread_should_stop())
				break;
			schedule_timeout_uninterruptible(HZ);
		}
	}
	if (!kthread_should_stop() && stall_cpu > 0) {
		VERBOSE_TOROUT_STRING("rcu_torture_stall begin CPU stall");
		stop_at = ktime_get_seconds() + stall_cpu;
		/* RCU CPU stall is expected behavior in following code. */
		idx = cur_ops->readlock();
		if (irqsoff)
			local_irq_disable();
		else if (!stall_cpu_block)
			preempt_disable();
		pr_alert("%s start stall episode %d on CPU %d.\n",
			  __func__, rep + 1, raw_smp_processor_id());
		while (ULONG_CMP_LT((unsigned long)ktime_get_seconds(), stop_at) &&
		       !kthread_should_stop())
			if (stall_cpu_block) {
#ifdef CONFIG_PREEMPTION
				preempt_schedule();
#else
				schedule_timeout_uninterruptible(HZ);
#endif
			} else if (stall_no_softlockup) {
				touch_softlockup_watchdog();
			}
		if (irqsoff)
			local_irq_enable();
		else if (!stall_cpu_block)
			preempt_enable();
		cur_ops->readunlock(idx);
	}
}

/*
 * CPU-stall kthread.  Invokes rcu_torture_stall_one() once, and then as many
 * additional times as specified by the stall_cpu_repeat module parameter.
 * Note that stall_cpu_irqsoff is ignored on the second and subsequent
 * stall.
 */
static int rcu_torture_stall(void *args)
{
	int i;
	int repeat = stall_cpu_repeat;
	int ret;

	VERBOSE_TOROUT_STRING("rcu_torture_stall task started");
	if (repeat < 0) {
		repeat = 0;
		WARN_ON_ONCE(IS_BUILTIN(CONFIG_RCU_TORTURE_TEST));
	}
	if (rcu_cpu_stall_notifiers) {
		ret = rcu_stall_chain_notifier_register(&rcu_torture_stall_block);
		if (ret)
			pr_info("%s: rcu_stall_chain_notifier_register() returned %d, %sexpected.\n",
				__func__, ret, !IS_ENABLED(CONFIG_RCU_STALL_COMMON) ? "un" : "");
	}
	for (i = 0; i <= repeat; i++) {
		if (kthread_should_stop())
			break;
		rcu_torture_stall_one(i, i == 0 ? stall_cpu_irqsoff : 0);
	}
	pr_alert("%s end.\n", __func__);
	if (rcu_cpu_stall_notifiers && !ret) {
		ret = rcu_stall_chain_notifier_unregister(&rcu_torture_stall_block);
		if (ret)
			pr_info("%s: rcu_stall_chain_notifier_unregister() returned %d.\n", __func__, ret);
	}
	torture_shutdown_absorb("rcu_torture_stall");
	while (!kthread_should_stop())
		schedule_timeout_interruptible(10 * HZ);
	return 0;
}

/* Spawn CPU-stall kthread, if stall_cpu specified. */
static int __init rcu_torture_stall_init(void)
{
	if (stall_cpu <= 0 && stall_gp_kthread <= 0)
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
	int rcu_fwd_id;
};

static DEFINE_MUTEX(rcu_fwd_mutex);
static struct rcu_fwd *rcu_fwds;
static unsigned long rcu_fwd_seq;
static atomic_long_t rcu_fwd_max_cbs;
static bool rcu_fwd_emergency_stop;

static void rcu_torture_fwd_cb_hist(struct rcu_fwd *rfp)
{
	unsigned long gps;
	unsigned long gps_old;
	int i;
	int j;

	for (i = ARRAY_SIZE(rfp->n_launders_hist) - 1; i > 0; i--)
		if (rfp->n_launders_hist[i].n_launders > 0)
			break;
	pr_alert("%s: Callback-invocation histogram %d (duration %lu jiffies):",
		 __func__, rfp->rcu_fwd_id, jiffies - rfp->rcu_fwd_startat);
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
	smp_store_release(rfcpp, rfcp);
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
	if (IS_ENABLED(CONFIG_PREEMPTION) && IS_ENABLED(CONFIG_NO_HZ_FULL)) {
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
static unsigned long rcu_torture_fwd_prog_cbfree(struct rcu_fwd *rfp)
{
	unsigned long flags;
	unsigned long freed = 0;
	struct rcu_fwd_cb *rfcp;

	for (;;) {
		spin_lock_irqsave(&rfp->rcu_fwd_lock, flags);
		rfcp = rfp->rcu_fwd_cb_head;
		if (!rfcp) {
			spin_unlock_irqrestore(&rfp->rcu_fwd_lock, flags);
			break;
		}
		rfp->rcu_fwd_cb_head = rfcp->rfc_next;
		if (!rfp->rcu_fwd_cb_head)
			rfp->rcu_fwd_cb_tail = &rfp->rcu_fwd_cb_head;
		spin_unlock_irqrestore(&rfp->rcu_fwd_lock, flags);
		kfree(rfcp);
		freed++;
		rcu_torture_fwd_prog_cond_resched(freed);
		if (tick_nohz_full_enabled()) {
			local_irq_save(flags);
			rcu_momentary_eqs();
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

	pr_alert("%s: Starting forward-progress test %d\n", __func__, rfp->rcu_fwd_id);
	if (!cur_ops->sync)
		return; // Cannot do need_resched() forward progress testing without ->sync.
	if (cur_ops->call && cur_ops->cb_barrier) {
		init_rcu_head_on_stack(&fcs.rh);
		selfpropcb = true;
	}

	/* Tight loop containing cond_resched(). */
	atomic_inc(&rcu_fwd_cb_nodelay);
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
		pr_alert("%s: %d Duration %ld cver %ld gps %ld\n", __func__,
			 rfp->rcu_fwd_id, dur, cver, gps);
	}
	if (selfpropcb) {
		WRITE_ONCE(fcs.stop, 1);
		cur_ops->sync(); /* Wait for running CB to complete. */
		pr_alert("%s: Waiting for CBs: %pS() %d\n", __func__, cur_ops->cb_barrier, rfp->rcu_fwd_id);
		cur_ops->cb_barrier(); /* Wait for queued callbacks. */
	}

	if (selfpropcb) {
		WARN_ON(READ_ONCE(fcs.stop) != 2);
		destroy_rcu_head_on_stack(&fcs.rh);
	}
	schedule_timeout_uninterruptible(HZ / 10); /* Let kthreads recover. */
	atomic_dec(&rcu_fwd_cb_nodelay);
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

	pr_alert("%s: Starting forward-progress test %d\n", __func__, rfp->rcu_fwd_id);
	if (READ_ONCE(rcu_fwd_emergency_stop))
		return; /* Get out of the way quickly, no GP wait! */
	if (!cur_ops->call)
		return; /* Can't do call_rcu() fwd prog without ->call. */

	/* Loop continuously posting RCU callbacks. */
	atomic_inc(&rcu_fwd_cb_nodelay);
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
		} else if (!cur_ops->cbflood_max || cur_ops->cbflood_max > n_max_cbs) {
			rfcp = kmalloc(sizeof(*rfcp), GFP_KERNEL);
			if (WARN_ON_ONCE(!rfcp)) {
				schedule_timeout_interruptible(1);
				continue;
			}
			n_max_cbs++;
			n_launders_sa = 0;
			rfcp->rfc_gps = 0;
			rfcp->rfc_rfp = rfp;
		} else {
			rfcp = NULL;
		}
		if (rfcp)
			cur_ops->call(&rfcp->rh, rcu_torture_fwd_cb_cr);
		rcu_torture_fwd_prog_cond_resched(n_launders + n_max_cbs);
		if (tick_nohz_full_enabled()) {
			local_irq_save(flags);
			rcu_momentary_eqs();
			local_irq_restore(flags);
		}
	}
	stoppedat = jiffies;
	n_launders_cb_snap = READ_ONCE(rfp->n_launders_cb);
	cver = READ_ONCE(rcu_torture_current_version) - cver;
	gps = rcutorture_seq_diff(cur_ops->get_gp_seq(), gps);
	pr_alert("%s: Waiting for CBs: %pS() %d\n", __func__, cur_ops->cb_barrier, rfp->rcu_fwd_id);
	cur_ops->cb_barrier(); /* Wait for callbacks to be invoked. */
	(void)rcu_torture_fwd_prog_cbfree(rfp);

	if (!torture_must_stop() && !READ_ONCE(rcu_fwd_emergency_stop) &&
	    !shutdown_time_arrived()) {
		if (WARN_ON(n_max_gps < MIN_FWD_CBS_LAUNDERED) && cur_ops->gp_kthread_dbg)
			cur_ops->gp_kthread_dbg();
		pr_alert("%s Duration %lu barrier: %lu pending %ld n_launders: %ld n_launders_sa: %ld n_max_gps: %ld n_max_cbs: %ld cver %ld gps %ld #online %u\n",
			 __func__,
			 stoppedat - rfp->rcu_fwd_startat, jiffies - stoppedat,
			 n_launders + n_max_cbs - n_launders_cb_snap,
			 n_launders, n_launders_sa,
			 n_max_gps, n_max_cbs, cver, gps, num_online_cpus());
		atomic_long_add(n_max_cbs, &rcu_fwd_max_cbs);
		mutex_lock(&rcu_fwd_mutex); // Serialize histograms.
		rcu_torture_fwd_cb_hist(rfp);
		mutex_unlock(&rcu_fwd_mutex);
	}
	schedule_timeout_uninterruptible(HZ); /* Let CBs drain. */
	tick_dep_clear_task(current, TICK_DEP_BIT_RCU);
	atomic_dec(&rcu_fwd_cb_nodelay);
}


/*
 * OOM notifier, but this only prints diagnostic information for the
 * current forward-progress test.
 */
static int rcutorture_oom_notify(struct notifier_block *self,
				 unsigned long notused, void *nfreed)
{
	int i;
	long ncbs;
	struct rcu_fwd *rfp;

	mutex_lock(&rcu_fwd_mutex);
	rfp = rcu_fwds;
	if (!rfp) {
		mutex_unlock(&rcu_fwd_mutex);
		return NOTIFY_OK;
	}
	WARN(1, "%s invoked upon OOM during forward-progress testing.\n",
	     __func__);
	for (i = 0; i < fwd_progress; i++) {
		rcu_torture_fwd_cb_hist(&rfp[i]);
		rcu_fwd_progress_check(1 + (jiffies - READ_ONCE(rfp[i].rcu_fwd_startat)) / 2);
	}
	WRITE_ONCE(rcu_fwd_emergency_stop, true);
	smp_mb(); /* Emergency stop before free and wait to avoid hangs. */
	ncbs = 0;
	for (i = 0; i < fwd_progress; i++)
		ncbs += rcu_torture_fwd_prog_cbfree(&rfp[i]);
	pr_info("%s: Freed %lu RCU callbacks.\n", __func__, ncbs);
	cur_ops->cb_barrier();
	ncbs = 0;
	for (i = 0; i < fwd_progress; i++)
		ncbs += rcu_torture_fwd_prog_cbfree(&rfp[i]);
	pr_info("%s: Freed %lu RCU callbacks.\n", __func__, ncbs);
	cur_ops->cb_barrier();
	ncbs = 0;
	for (i = 0; i < fwd_progress; i++)
		ncbs += rcu_torture_fwd_prog_cbfree(&rfp[i]);
	pr_info("%s: Freed %lu RCU callbacks.\n", __func__, ncbs);
	smp_mb(); /* Frees before return to avoid redoing OOM. */
	(*(unsigned long *)nfreed)++; /* Forward progress CBs freed! */
	pr_info("%s returning after OOM processing.\n", __func__);
	mutex_unlock(&rcu_fwd_mutex);
	return NOTIFY_OK;
}

static struct notifier_block rcutorture_oom_nb = {
	.notifier_call = rcutorture_oom_notify
};

/* Carry out grace-period forward-progress testing. */
static int rcu_torture_fwd_prog(void *args)
{
	bool firsttime = true;
	long max_cbs;
	int oldnice = task_nice(current);
	unsigned long oldseq = READ_ONCE(rcu_fwd_seq);
	struct rcu_fwd *rfp = args;
	int tested = 0;
	int tested_tries = 0;

	VERBOSE_TOROUT_STRING("rcu_torture_fwd_progress task started");
	rcu_bind_current_to_nocb();
	if (!IS_ENABLED(CONFIG_SMP) || !IS_ENABLED(CONFIG_RCU_BOOST))
		set_user_nice(current, MAX_NICE);
	do {
		if (!rfp->rcu_fwd_id) {
			schedule_timeout_interruptible(fwd_progress_holdoff * HZ);
			WRITE_ONCE(rcu_fwd_emergency_stop, false);
			if (!firsttime) {
				max_cbs = atomic_long_xchg(&rcu_fwd_max_cbs, 0);
				pr_alert("%s n_max_cbs: %ld\n", __func__, max_cbs);
			}
			firsttime = false;
			WRITE_ONCE(rcu_fwd_seq, rcu_fwd_seq + 1);
		} else {
			while (READ_ONCE(rcu_fwd_seq) == oldseq && !torture_must_stop())
				schedule_timeout_interruptible(HZ / 20);
			oldseq = READ_ONCE(rcu_fwd_seq);
		}
		pr_alert("%s: Starting forward-progress test %d\n", __func__, rfp->rcu_fwd_id);
		if (rcu_inkernel_boot_has_ended() && torture_num_online_cpus() > rfp->rcu_fwd_id)
			rcu_torture_fwd_prog_cr(rfp);
		if ((cur_ops->stall_dur && cur_ops->stall_dur() > 0) &&
		    (!IS_ENABLED(CONFIG_TINY_RCU) ||
		     (rcu_inkernel_boot_has_ended() &&
		      torture_num_online_cpus() > rfp->rcu_fwd_id)))
			rcu_torture_fwd_prog_nr(rfp, &tested, &tested_tries);

		/* Avoid slow periods, better to test when busy. */
		if (stutter_wait("rcu_torture_fwd_prog"))
			sched_set_normal(current, oldnice);
	} while (!torture_must_stop());
	/* Short runs might not contain a valid forward-progress attempt. */
	if (!rfp->rcu_fwd_id) {
		WARN_ON(!tested && tested_tries >= 5);
		pr_alert("%s: tested %d tested_tries %d\n", __func__, tested, tested_tries);
	}
	torture_kthread_stopping("rcu_torture_fwd_prog");
	return 0;
}

/* If forward-progress checking is requested and feasible, spawn the thread. */
static int __init rcu_torture_fwd_prog_init(void)
{
	int i;
	int ret = 0;
	struct rcu_fwd *rfp;

	if (!fwd_progress)
		return 0; /* Not requested, so don't do it. */
	if (fwd_progress >= nr_cpu_ids) {
		VERBOSE_TOROUT_STRING("rcu_torture_fwd_prog_init: Limiting fwd_progress to # CPUs.\n");
		fwd_progress = nr_cpu_ids;
	} else if (fwd_progress < 0) {
		fwd_progress = nr_cpu_ids;
	}
	if ((!cur_ops->sync && !cur_ops->call) ||
	    (!cur_ops->cbflood_max && (!cur_ops->stall_dur || cur_ops->stall_dur() <= 0)) ||
	    cur_ops == &rcu_busted_ops) {
		VERBOSE_TOROUT_STRING("rcu_torture_fwd_prog_init: Disabled, unsupported by RCU flavor under test");
		fwd_progress = 0;
		return 0;
	}
	if (stall_cpu > 0 || (preempt_duration > 0 && IS_ENABLED(CONFIG_RCU_NOCB_CPU))) {
		VERBOSE_TOROUT_STRING("rcu_torture_fwd_prog_init: Disabled, conflicts with CPU-stall and/or preemption testing");
		fwd_progress = 0;
		if (IS_MODULE(CONFIG_RCU_TORTURE_TEST))
			return -EINVAL; /* In module, can fail back to user. */
		WARN_ON(1); /* Make sure rcutorture scripting notices conflict. */
		return 0;
	}
	if (fwd_progress_holdoff <= 0)
		fwd_progress_holdoff = 1;
	if (fwd_progress_div <= 0)
		fwd_progress_div = 4;
	rfp = kcalloc(fwd_progress, sizeof(*rfp), GFP_KERNEL);
	fwd_prog_tasks = kcalloc(fwd_progress, sizeof(*fwd_prog_tasks), GFP_KERNEL);
	if (!rfp || !fwd_prog_tasks) {
		kfree(rfp);
		kfree(fwd_prog_tasks);
		fwd_prog_tasks = NULL;
		fwd_progress = 0;
		return -ENOMEM;
	}
	for (i = 0; i < fwd_progress; i++) {
		spin_lock_init(&rfp[i].rcu_fwd_lock);
		rfp[i].rcu_fwd_cb_tail = &rfp[i].rcu_fwd_cb_head;
		rfp[i].rcu_fwd_id = i;
	}
	mutex_lock(&rcu_fwd_mutex);
	rcu_fwds = rfp;
	mutex_unlock(&rcu_fwd_mutex);
	register_oom_notifier(&rcutorture_oom_nb);
	for (i = 0; i < fwd_progress; i++) {
		ret = torture_create_kthread(rcu_torture_fwd_prog, &rcu_fwds[i], fwd_prog_tasks[i]);
		if (ret) {
			fwd_progress = i;
			return ret;
		}
	}
	return 0;
}

static void rcu_torture_fwd_prog_cleanup(void)
{
	int i;
	struct rcu_fwd *rfp;

	if (!rcu_fwds || !fwd_prog_tasks)
		return;
	for (i = 0; i < fwd_progress; i++)
		torture_stop_kthread(rcu_torture_fwd_prog, fwd_prog_tasks[i]);
	unregister_oom_notifier(&rcutorture_oom_nb);
	mutex_lock(&rcu_fwd_mutex);
	rfp = rcu_fwds;
	rcu_fwds = NULL;
	mutex_unlock(&rcu_fwd_mutex);
	kfree(rfp);
	kfree(fwd_prog_tasks);
	fwd_prog_tasks = NULL;
}

/* Callback function for RCU barrier testing. */
static void rcu_torture_barrier_cbf(struct rcu_head *rcu)
{
	atomic_inc(&barrier_cbs_invoked);
}

/* IPI handler to get callback posted on desired CPU, if online. */
static int rcu_torture_barrier1cb(void *rcu_void)
{
	struct rcu_head *rhp = rcu_void;

	cur_ops->call(rhp, rcu_torture_barrier_cbf);
	return 0;
}

/* kthread function to register callbacks used to test RCU barriers. */
static int rcu_torture_barrier_cbs(void *arg)
{
	long myid = (long)arg;
	bool lastphase = false;
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
		if (smp_call_on_cpu(myid, rcu_torture_barrier1cb, &rcu, 1))
			cur_ops->call(&rcu, rcu_torture_barrier_cbf);

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
			WARN_ON(1);
			// Wait manually for the remaining callbacks
			i = 0;
			do {
				if (WARN_ON(i++ > HZ))
					i = INT_MIN;
				schedule_timeout_interruptible(1);
				cur_ops->cb_barrier();
			} while (atomic_read(&barrier_cbs_invoked) !=
				 n_barrier_cbs &&
				 !torture_must_stop());
			smp_mb(); // Can't trust ordering if broken.
			if (!torture_must_stop())
				pr_err("Recovered: barrier_cbs_invoked = %d\n",
				       atomic_read(&barrier_cbs_invoked));
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
	if (!cur_ops->start_gp_poll || !cur_ops->poll_gp_state)
		return false;

	prio = rcu_get_gp_kthreads_prio();
	if (!prio)
		return false;

	if (prio < 2) {
		if (boost_warn_once == 1)
			return false;

		pr_alert("%s: WARN: RCU kthread priority too low to test boosting.  Skipping RCU boost test. Try passing rcutree.kthread_prio > 1 on the kernel command line.\n", KBUILD_MODNAME);
		boost_warn_once = 1;
		return false;
	}

	return true;
}

static bool read_exit_child_stop;
static bool read_exit_child_stopped;
static wait_queue_head_t read_exit_wq;

// Child kthread which just does an rcutorture reader and exits.
static int rcu_torture_read_exit_child(void *trsp_in)
{
	struct torture_random_state *trsp = trsp_in;

	set_user_nice(current, MAX_NICE);
	// Minimize time between reading and exiting.
	while (!kthread_should_stop())
		schedule_timeout_uninterruptible(HZ / 20);
	(void)rcu_torture_one_read(trsp, -1);
	return 0;
}

// Parent kthread which creates and destroys read-exit child kthreads.
static int rcu_torture_read_exit(void *unused)
{
	bool errexit = false;
	int i;
	struct task_struct *tsp;
	DEFINE_TORTURE_RANDOM(trs);

	// Allocate and initialize.
	set_user_nice(current, MAX_NICE);
	VERBOSE_TOROUT_STRING("rcu_torture_read_exit: Start of test");

	// Each pass through this loop does one read-exit episode.
	do {
		VERBOSE_TOROUT_STRING("rcu_torture_read_exit: Start of episode");
		for (i = 0; i < read_exit_burst; i++) {
			if (READ_ONCE(read_exit_child_stop))
				break;
			stutter_wait("rcu_torture_read_exit");
			// Spawn child.
			tsp = kthread_run(rcu_torture_read_exit_child,
					  &trs, "%s", "rcu_torture_read_exit_child");
			if (IS_ERR(tsp)) {
				TOROUT_ERRSTRING("out of memory");
				errexit = true;
				break;
			}
			cond_resched();
			kthread_stop(tsp);
			n_read_exits++;
		}
		VERBOSE_TOROUT_STRING("rcu_torture_read_exit: End of episode");
		rcu_barrier(); // Wait for task_struct free, avoid OOM.
		i = 0;
		for (; !errexit && !READ_ONCE(read_exit_child_stop) && i < read_exit_delay; i++)
			schedule_timeout_uninterruptible(HZ);
	} while (!errexit && !READ_ONCE(read_exit_child_stop));

	// Clean up and exit.
	smp_store_release(&read_exit_child_stopped, true); // After reaping.
	smp_mb(); // Store before wakeup.
	wake_up(&read_exit_wq);
	while (!torture_must_stop())
		schedule_timeout_uninterruptible(HZ / 20);
	torture_kthread_stopping("rcu_torture_read_exit");
	return 0;
}

static int rcu_torture_read_exit_init(void)
{
	if (read_exit_burst <= 0)
		return 0;
	init_waitqueue_head(&read_exit_wq);
	read_exit_child_stop = false;
	read_exit_child_stopped = false;
	return torture_create_kthread(rcu_torture_read_exit, NULL,
				      read_exit_task);
}

static void rcu_torture_read_exit_cleanup(void)
{
	if (!read_exit_task)
		return;
	WRITE_ONCE(read_exit_child_stop, true);
	smp_mb(); // Above write before wait.
	wait_event(read_exit_wq, smp_load_acquire(&read_exit_child_stopped));
	torture_stop_kthread(rcutorture_read_exit, read_exit_task);
}

static void rcutorture_test_nmis(int n)
{
#if IS_BUILTIN(CONFIG_RCU_TORTURE_TEST)
	int cpu;
	int dumpcpu;
	int i;

	for (i = 0; i < n; i++) {
		preempt_disable();
		cpu = smp_processor_id();
		dumpcpu = cpu + 1;
		if (dumpcpu >= nr_cpu_ids)
			dumpcpu = 0;
		pr_alert("%s: CPU %d invoking dump_cpu_task(%d)\n", __func__, cpu, dumpcpu);
		dump_cpu_task(dumpcpu);
		preempt_enable();
		schedule_timeout_uninterruptible(15 * HZ);
	}
#else // #if IS_BUILTIN(CONFIG_RCU_TORTURE_TEST)
	WARN_ONCE(n, "Non-zero rcutorture.test_nmis=%d permitted only when rcutorture is built in.\n", test_nmis);
#endif // #else // #if IS_BUILTIN(CONFIG_RCU_TORTURE_TEST)
}

// Randomly preempt online CPUs.
static int rcu_torture_preempt(void *unused)
{
	int cpu = -1;
	DEFINE_TORTURE_RANDOM(rand);

	schedule_timeout_idle(stall_cpu_holdoff);
	do {
		// Wait for preempt_interval ms with up to 100us fuzz.
		torture_hrtimeout_ms(preempt_interval, 100, &rand);
		// Select online CPU.
		cpu = cpumask_next(cpu, cpu_online_mask);
		if (cpu >= nr_cpu_ids)
			cpu = cpumask_next(-1, cpu_online_mask);
		WARN_ON_ONCE(cpu >= nr_cpu_ids);
		// Move to that CPU, if can't do so, retry later.
		if (torture_sched_setaffinity(current->pid, cpumask_of(cpu), false))
			continue;
		// Preempt at high-ish priority, then reset to normal.
		sched_set_fifo(current);
		torture_sched_setaffinity(current->pid, cpu_present_mask, true);
		mdelay(preempt_duration);
		sched_set_normal(current, 0);
		stutter_wait("rcu_torture_preempt");
	} while (!torture_must_stop());
	torture_kthread_stopping("rcu_torture_preempt");
	return 0;
}

static enum cpuhp_state rcutor_hp;

static void
rcu_torture_cleanup(void)
{
	int firsttime;
	int flags = 0;
	unsigned long gp_seq = 0;
	int i;
	int j;

	if (torture_cleanup_begin()) {
		if (cur_ops->cb_barrier != NULL) {
			pr_info("%s: Invoking %pS().\n", __func__, cur_ops->cb_barrier);
			cur_ops->cb_barrier();
		}
		if (cur_ops->gp_slow_unregister)
			cur_ops->gp_slow_unregister(NULL);
		return;
	}
	if (!cur_ops) {
		torture_cleanup_end();
		return;
	}

	rcutorture_test_nmis(test_nmis);

	if (cur_ops->gp_kthread_dbg)
		cur_ops->gp_kthread_dbg();
	torture_stop_kthread(rcu_torture_preempt, preempt_task);
	rcu_torture_read_exit_cleanup();
	rcu_torture_barrier_cleanup();
	rcu_torture_fwd_prog_cleanup();
	torture_stop_kthread(rcu_torture_stall, stall_task);
	torture_stop_kthread(rcu_torture_writer, writer_task);

	if (nocb_tasks) {
		for (i = 0; i < nrealnocbers; i++)
			torture_stop_kthread(rcu_nocb_toggle, nocb_tasks[i]);
		kfree(nocb_tasks);
		nocb_tasks = NULL;
	}

	if (reader_tasks) {
		for (i = 0; i < nrealreaders; i++)
			torture_stop_kthread(rcu_torture_reader,
					     reader_tasks[i]);
		kfree(reader_tasks);
		reader_tasks = NULL;
	}
	kfree(rcu_torture_reader_mbchk);
	rcu_torture_reader_mbchk = NULL;

	if (fakewriter_tasks) {
		for (i = 0; i < nrealfakewriters; i++)
			torture_stop_kthread(rcu_torture_fakewriter,
					     fakewriter_tasks[i]);
		kfree(fakewriter_tasks);
		fakewriter_tasks = NULL;
	}

	if (cur_ops->get_gp_data)
		cur_ops->get_gp_data(&flags, &gp_seq);
	pr_alert("%s:  End-test grace-period state: g%ld f%#x total-gps=%ld\n",
		 cur_ops->name, (long)gp_seq, flags,
		 rcutorture_seq_diff(gp_seq, start_gp_seq));
	torture_stop_kthread(rcu_torture_stats, stats_task);
	torture_stop_kthread(rcu_torture_fqs, fqs_task);
	if (rcu_torture_can_boost() && rcutor_hp >= 0)
		cpuhp_remove_state(rcutor_hp);

	/*
	 * Wait for all RCU callbacks to fire, then do torture-type-specific
	 * cleanup operations.
	 */
	if (cur_ops->cb_barrier != NULL) {
		pr_info("%s: Invoking %pS().\n", __func__, cur_ops->cb_barrier);
		cur_ops->cb_barrier();
	}
	if (cur_ops->cleanup != NULL)
		cur_ops->cleanup();

	rcu_torture_mem_dump_obj();

	rcu_torture_stats_print();  /* -After- the stats thread is stopped! */

	if (err_segs_recorded) {
		pr_alert("Failure/close-call rcutorture reader segments:\n");
		if (rt_read_nsegs == 0)
			pr_alert("\t: No segments recorded!!!\n");
		firsttime = 1;
		for (i = 0; i < rt_read_nsegs; i++) {
			if (IS_ENABLED(CONFIG_RCU_TORTURE_TEST_LOG_GP))
				pr_alert("\t%lluus ", div64_u64(err_segs[i].rt_ts, 1000ULL));
			else
				pr_alert("\t");
			pr_cont("%d: %#4x", i, err_segs[i].rt_readstate);
			if (err_segs[i].rt_delay_jiffies != 0) {
				pr_cont("%s%ldjiffies", firsttime ? "" : "+",
					err_segs[i].rt_delay_jiffies);
				firsttime = 0;
			}
			if (IS_ENABLED(CONFIG_RCU_TORTURE_TEST_LOG_CPU)) {
				pr_cont(" CPU %2d", err_segs[i].rt_cpu);
				if (err_segs[i].rt_cpu != err_segs[i].rt_end_cpu)
					pr_cont("->%-2d", err_segs[i].rt_end_cpu);
				else
					pr_cont(" ...");
			}
			if (IS_ENABLED(CONFIG_RCU_TORTURE_TEST_LOG_GP) &&
			    cur_ops->gather_gp_seqs && cur_ops->format_gp_seqs) {
				char buf1[20+1];
				char buf2[20+1];
				char sepchar = '-';

				cur_ops->format_gp_seqs(err_segs[i].rt_gp_seq,
							buf1, ARRAY_SIZE(buf1));
				cur_ops->format_gp_seqs(err_segs[i].rt_gp_seq_end,
							buf2, ARRAY_SIZE(buf2));
				if (err_segs[i].rt_gp_seq == err_segs[i].rt_gp_seq_end) {
					if (buf2[0]) {
						for (j = 0; buf2[j]; j++)
							buf2[j] = '.';
						if (j)
							buf2[j - 1] = ' ';
					}
					sepchar = ' ';
				}
				pr_cont(" %s%c%s", buf1, sepchar, buf2);
			}
			if (err_segs[i].rt_delay_ms != 0) {
				pr_cont(" %s%ldms", firsttime ? "" : "+",
					err_segs[i].rt_delay_ms);
				firsttime = 0;
			}
			if (err_segs[i].rt_delay_us != 0) {
				pr_cont(" %s%ldus", firsttime ? "" : "+",
					err_segs[i].rt_delay_us);
				firsttime = 0;
			}
			pr_cont("%s", err_segs[i].rt_preempted ? " preempted" : "");
			if (err_segs[i].rt_readstate & RCUTORTURE_RDR_BH)
				pr_cont(" BH");
			if (err_segs[i].rt_readstate & RCUTORTURE_RDR_IRQ)
				pr_cont(" IRQ");
			if (err_segs[i].rt_readstate & RCUTORTURE_RDR_PREEMPT)
				pr_cont(" PREEMPT");
			if (err_segs[i].rt_readstate & RCUTORTURE_RDR_RBH)
				pr_cont(" RBH");
			if (err_segs[i].rt_readstate & RCUTORTURE_RDR_SCHED)
				pr_cont(" SCHED");
			if (err_segs[i].rt_readstate & RCUTORTURE_RDR_RCU_1)
				pr_cont(" RCU_1");
			if (err_segs[i].rt_readstate & RCUTORTURE_RDR_RCU_2)
				pr_cont(" RCU_2");
			pr_cont("\n");

		}
		if (rt_read_preempted)
			pr_alert("\tReader was preempted.\n");
	}
	if (atomic_read(&n_rcu_torture_error) || n_rcu_torture_barrier_error)
		rcu_torture_print_module_parms(cur_ops, "End of test: FAILURE");
	else if (torture_onoff_failures())
		rcu_torture_print_module_parms(cur_ops,
					       "End of test: RCU_HOTPLUG");
	else
		rcu_torture_print_module_parms(cur_ops, "End of test: SUCCESS");
	torture_cleanup_end();
	if (cur_ops->gp_slow_unregister)
		cur_ops->gp_slow_unregister(NULL);
}

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

/*
 * Verify that double-free causes debug-objects to complain, but only
 * if CONFIG_DEBUG_OBJECTS_RCU_HEAD=y.  Otherwise, say that the test
 * cannot be carried out.
 */
static void rcu_test_debug_objects(void)
{
	struct rcu_head rh1;
	struct rcu_head rh2;
	int idx;

	if (!IS_ENABLED(CONFIG_DEBUG_OBJECTS_RCU_HEAD)) {
		pr_alert("%s: !CONFIG_DEBUG_OBJECTS_RCU_HEAD, not testing duplicate call_%s()\n",
					KBUILD_MODNAME, cur_ops->name);
		return;
	}

	if (WARN_ON_ONCE(cur_ops->debug_objects &&
			(!cur_ops->call || !cur_ops->cb_barrier)))
		return;

	struct rcu_head *rhp = kmalloc(sizeof(*rhp), GFP_KERNEL);

	init_rcu_head_on_stack(&rh1);
	init_rcu_head_on_stack(&rh2);
	pr_alert("%s: WARN: Duplicate call_%s() test starting.\n", KBUILD_MODNAME, cur_ops->name);

	/* Try to queue the rh2 pair of callbacks for the same grace period. */
	idx = cur_ops->readlock(); /* Make it impossible to finish a grace period. */
	cur_ops->call(&rh1, rcu_torture_leak_cb); /* Start grace period. */
	cur_ops->call(&rh2, rcu_torture_leak_cb);
	cur_ops->call(&rh2, rcu_torture_err_cb); /* Duplicate callback. */
	if (rhp) {
		cur_ops->call(rhp, rcu_torture_leak_cb);
		cur_ops->call(rhp, rcu_torture_err_cb); /* Another duplicate callback. */
	}
	cur_ops->readunlock(idx);

	/* Wait for them all to get done so we can safely return. */
	cur_ops->cb_barrier();
	pr_alert("%s: WARN: Duplicate call_%s() test complete.\n", KBUILD_MODNAME, cur_ops->name);
	destroy_rcu_head_on_stack(&rh1);
	destroy_rcu_head_on_stack(&rh2);
	kfree(rhp);
}

static void rcutorture_sync(void)
{
	static unsigned long n;

	if (cur_ops->sync && !(++n & 0xfff))
		cur_ops->sync();
}

static DEFINE_MUTEX(mut0);
static DEFINE_MUTEX(mut1);
static DEFINE_MUTEX(mut2);
static DEFINE_MUTEX(mut3);
static DEFINE_MUTEX(mut4);
static DEFINE_MUTEX(mut5);
static DEFINE_MUTEX(mut6);
static DEFINE_MUTEX(mut7);
static DEFINE_MUTEX(mut8);
static DEFINE_MUTEX(mut9);

static DECLARE_RWSEM(rwsem0);
static DECLARE_RWSEM(rwsem1);
static DECLARE_RWSEM(rwsem2);
static DECLARE_RWSEM(rwsem3);
static DECLARE_RWSEM(rwsem4);
static DECLARE_RWSEM(rwsem5);
static DECLARE_RWSEM(rwsem6);
static DECLARE_RWSEM(rwsem7);
static DECLARE_RWSEM(rwsem8);
static DECLARE_RWSEM(rwsem9);

DEFINE_STATIC_SRCU(srcu0);
DEFINE_STATIC_SRCU(srcu1);
DEFINE_STATIC_SRCU(srcu2);
DEFINE_STATIC_SRCU(srcu3);
DEFINE_STATIC_SRCU(srcu4);
DEFINE_STATIC_SRCU(srcu5);
DEFINE_STATIC_SRCU(srcu6);
DEFINE_STATIC_SRCU(srcu7);
DEFINE_STATIC_SRCU(srcu8);
DEFINE_STATIC_SRCU(srcu9);

static int srcu_lockdep_next(const char *f, const char *fl, const char *fs, const char *fu, int i,
			     int cyclelen, int deadlock)
{
	int j = i + 1;

	if (j >= cyclelen)
		j = deadlock ? 0 : -1;
	if (j >= 0)
		pr_info("%s: %s(%d), %s(%d), %s(%d)\n", f, fl, i, fs, j, fu, i);
	else
		pr_info("%s: %s(%d), %s(%d)\n", f, fl, i, fu, i);
	return j;
}

// Test lockdep on SRCU-based deadlock scenarios.
static void rcu_torture_init_srcu_lockdep(void)
{
	int cyclelen;
	int deadlock;
	bool err = false;
	int i;
	int j;
	int idx;
	struct mutex *muts[] = { &mut0, &mut1, &mut2, &mut3, &mut4,
				 &mut5, &mut6, &mut7, &mut8, &mut9 };
	struct rw_semaphore *rwsems[] = { &rwsem0, &rwsem1, &rwsem2, &rwsem3, &rwsem4,
					  &rwsem5, &rwsem6, &rwsem7, &rwsem8, &rwsem9 };
	struct srcu_struct *srcus[] = { &srcu0, &srcu1, &srcu2, &srcu3, &srcu4,
					&srcu5, &srcu6, &srcu7, &srcu8, &srcu9 };
	int testtype;

	if (!test_srcu_lockdep)
		return;

	deadlock = test_srcu_lockdep / 1000;
	testtype = (test_srcu_lockdep / 10) % 100;
	cyclelen = test_srcu_lockdep % 10;
	WARN_ON_ONCE(ARRAY_SIZE(muts) != ARRAY_SIZE(srcus));
	if (WARN_ONCE(deadlock != !!deadlock,
		      "%s: test_srcu_lockdep=%d and deadlock digit %d must be zero or one.\n",
		      __func__, test_srcu_lockdep, deadlock))
		err = true;
	if (WARN_ONCE(cyclelen <= 0,
		      "%s: test_srcu_lockdep=%d and cycle-length digit %d must be greater than zero.\n",
		      __func__, test_srcu_lockdep, cyclelen))
		err = true;
	if (err)
		goto err_out;

	if (testtype == 0) {
		pr_info("%s: test_srcu_lockdep = %05d: SRCU %d-way %sdeadlock.\n",
			__func__, test_srcu_lockdep, cyclelen, deadlock ? "" : "non-");
		if (deadlock && cyclelen == 1)
			pr_info("%s: Expect hang.\n", __func__);
		for (i = 0; i < cyclelen; i++) {
			j = srcu_lockdep_next(__func__, "srcu_read_lock", "synchronize_srcu",
					      "srcu_read_unlock", i, cyclelen, deadlock);
			idx = srcu_read_lock(srcus[i]);
			if (j >= 0)
				synchronize_srcu(srcus[j]);
			srcu_read_unlock(srcus[i], idx);
		}
		return;
	}

	if (testtype == 1) {
		pr_info("%s: test_srcu_lockdep = %05d: SRCU/mutex %d-way %sdeadlock.\n",
			__func__, test_srcu_lockdep, cyclelen, deadlock ? "" : "non-");
		for (i = 0; i < cyclelen; i++) {
			pr_info("%s: srcu_read_lock(%d), mutex_lock(%d), mutex_unlock(%d), srcu_read_unlock(%d)\n",
				__func__, i, i, i, i);
			idx = srcu_read_lock(srcus[i]);
			mutex_lock(muts[i]);
			mutex_unlock(muts[i]);
			srcu_read_unlock(srcus[i], idx);

			j = srcu_lockdep_next(__func__, "mutex_lock", "synchronize_srcu",
					      "mutex_unlock", i, cyclelen, deadlock);
			mutex_lock(muts[i]);
			if (j >= 0)
				synchronize_srcu(srcus[j]);
			mutex_unlock(muts[i]);
		}
		return;
	}

	if (testtype == 2) {
		pr_info("%s: test_srcu_lockdep = %05d: SRCU/rwsem %d-way %sdeadlock.\n",
			__func__, test_srcu_lockdep, cyclelen, deadlock ? "" : "non-");
		for (i = 0; i < cyclelen; i++) {
			pr_info("%s: srcu_read_lock(%d), down_read(%d), up_read(%d), srcu_read_unlock(%d)\n",
				__func__, i, i, i, i);
			idx = srcu_read_lock(srcus[i]);
			down_read(rwsems[i]);
			up_read(rwsems[i]);
			srcu_read_unlock(srcus[i], idx);

			j = srcu_lockdep_next(__func__, "down_write", "synchronize_srcu",
					      "up_write", i, cyclelen, deadlock);
			down_write(rwsems[i]);
			if (j >= 0)
				synchronize_srcu(srcus[j]);
			up_write(rwsems[i]);
		}
		return;
	}

#ifdef CONFIG_TASKS_TRACE_RCU
	if (testtype == 3) {
		pr_info("%s: test_srcu_lockdep = %05d: SRCU and Tasks Trace RCU %d-way %sdeadlock.\n",
			__func__, test_srcu_lockdep, cyclelen, deadlock ? "" : "non-");
		if (deadlock && cyclelen == 1)
			pr_info("%s: Expect hang.\n", __func__);
		for (i = 0; i < cyclelen; i++) {
			char *fl = i == 0 ? "rcu_read_lock_trace" : "srcu_read_lock";
			char *fs = i == cyclelen - 1 ? "synchronize_rcu_tasks_trace"
						     : "synchronize_srcu";
			char *fu = i == 0 ? "rcu_read_unlock_trace" : "srcu_read_unlock";

			j = srcu_lockdep_next(__func__, fl, fs, fu, i, cyclelen, deadlock);
			if (i == 0)
				rcu_read_lock_trace();
			else
				idx = srcu_read_lock(srcus[i]);
			if (j >= 0) {
				if (i == cyclelen - 1)
					synchronize_rcu_tasks_trace();
				else
					synchronize_srcu(srcus[j]);
			}
			if (i == 0)
				rcu_read_unlock_trace();
			else
				srcu_read_unlock(srcus[i], idx);
		}
		return;
	}
#endif // #ifdef CONFIG_TASKS_TRACE_RCU

err_out:
	pr_info("%s: test_srcu_lockdep = %05d does nothing.\n", __func__, test_srcu_lockdep);
	pr_info("%s: test_srcu_lockdep = DNNL.\n", __func__);
	pr_info("%s: D: Deadlock if nonzero.\n", __func__);
	pr_info("%s: NN: Test number, 0=SRCU, 1=SRCU/mutex, 2=SRCU/rwsem, 3=SRCU/Tasks Trace RCU.\n", __func__);
	pr_info("%s: L: Cycle length.\n", __func__);
	if (!IS_ENABLED(CONFIG_TASKS_TRACE_RCU))
		pr_info("%s: NN=3 disallowed because kernel is built with CONFIG_TASKS_TRACE_RCU=n\n", __func__);
}

static int __init
rcu_torture_init(void)
{
	long i;
	int cpu;
	int firsterr = 0;
	int flags = 0;
	unsigned long gp_seq = 0;
	static struct rcu_torture_ops *torture_ops[] = {
		&rcu_ops, &rcu_busted_ops, &srcu_ops, &srcud_ops, &busted_srcud_ops,
		TASKS_OPS TASKS_RUDE_OPS TASKS_TRACING_OPS
		&trivial_ops,
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
		firsterr = -EINVAL;
		cur_ops = NULL;
		goto unwind;
	}
	if (cur_ops->fqs == NULL && fqs_duration != 0) {
		pr_alert("rcu-torture: ->fqs NULL and non-zero fqs_duration, fqs disabled.\n");
		fqs_duration = 0;
	}
	if (nocbs_nthreads != 0 && (cur_ops != &rcu_ops ||
				    !IS_ENABLED(CONFIG_RCU_NOCB_CPU))) {
		pr_alert("rcu-torture types: %s and CONFIG_RCU_NOCB_CPU=%d, nocb toggle disabled.\n",
			 cur_ops->name, IS_ENABLED(CONFIG_RCU_NOCB_CPU));
		nocbs_nthreads = 0;
	}
	if (cur_ops->init)
		cur_ops->init();

	rcu_torture_init_srcu_lockdep();

	if (nfakewriters >= 0) {
		nrealfakewriters = nfakewriters;
	} else {
		nrealfakewriters = num_online_cpus() - 2 - nfakewriters;
		if (nrealfakewriters <= 0)
			nrealfakewriters = 1;
	}

	if (nreaders >= 0) {
		nrealreaders = nreaders;
	} else {
		nrealreaders = num_online_cpus() - 2 - nreaders;
		if (nrealreaders <= 0)
			nrealreaders = 1;
	}
	rcu_torture_print_module_parms(cur_ops, "Start of test");
	if (cur_ops->get_gp_data)
		cur_ops->get_gp_data(&flags, &gp_seq);
	start_gp_seq = gp_seq;
	pr_alert("%s:  Start-test grace-period state: g%ld f%#x\n",
		 cur_ops->name, (long)gp_seq, flags);

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
	atomic_set(&n_rcu_torture_mbchk_fail, 0);
	atomic_set(&n_rcu_torture_mbchk_tries, 0);
	atomic_set(&n_rcu_torture_error, 0);
	n_rcu_torture_barrier_error = 0;
	n_rcu_torture_boost_ktrerror = 0;
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

	rcu_torture_write_types();
	firsterr = torture_create_kthread(rcu_torture_writer, NULL,
					  writer_task);
	if (torture_init_error(firsterr))
		goto unwind;

	if (nrealfakewriters > 0) {
		fakewriter_tasks = kcalloc(nrealfakewriters,
					   sizeof(fakewriter_tasks[0]),
					   GFP_KERNEL);
		if (fakewriter_tasks == NULL) {
			TOROUT_ERRSTRING("out of memory");
			firsterr = -ENOMEM;
			goto unwind;
		}
	}
	for (i = 0; i < nrealfakewriters; i++) {
		firsterr = torture_create_kthread(rcu_torture_fakewriter,
						  NULL, fakewriter_tasks[i]);
		if (torture_init_error(firsterr))
			goto unwind;
	}
	reader_tasks = kcalloc(nrealreaders, sizeof(reader_tasks[0]),
			       GFP_KERNEL);
	rcu_torture_reader_mbchk = kcalloc(nrealreaders, sizeof(*rcu_torture_reader_mbchk),
					   GFP_KERNEL);
	if (!reader_tasks || !rcu_torture_reader_mbchk) {
		TOROUT_ERRSTRING("out of memory");
		firsterr = -ENOMEM;
		goto unwind;
	}
	for (i = 0; i < nrealreaders; i++) {
		rcu_torture_reader_mbchk[i].rtc_chkrdr = -1;
		firsterr = torture_create_kthread(rcu_torture_reader, (void *)i,
						  reader_tasks[i]);
		if (torture_init_error(firsterr))
			goto unwind;
	}
	nrealnocbers = nocbs_nthreads;
	if (WARN_ON(nrealnocbers < 0))
		nrealnocbers = 1;
	if (WARN_ON(nocbs_toggle < 0))
		nocbs_toggle = HZ;
	if (nrealnocbers > 0) {
		nocb_tasks = kcalloc(nrealnocbers, sizeof(nocb_tasks[0]), GFP_KERNEL);
		if (nocb_tasks == NULL) {
			TOROUT_ERRSTRING("out of memory");
			firsterr = -ENOMEM;
			goto unwind;
		}
	} else {
		nocb_tasks = NULL;
	}
	for (i = 0; i < nrealnocbers; i++) {
		firsterr = torture_create_kthread(rcu_nocb_toggle, NULL, nocb_tasks[i]);
		if (torture_init_error(firsterr))
			goto unwind;
	}
	if (stat_interval > 0) {
		firsterr = torture_create_kthread(rcu_torture_stats, NULL,
						  stats_task);
		if (torture_init_error(firsterr))
			goto unwind;
	}
	if (test_no_idle_hz && shuffle_interval > 0) {
		firsterr = torture_shuffle_init(shuffle_interval * HZ);
		if (torture_init_error(firsterr))
			goto unwind;
	}
	if (stutter < 0)
		stutter = 0;
	if (stutter) {
		int t;

		t = cur_ops->stall_dur ? cur_ops->stall_dur() : stutter * HZ;
		firsterr = torture_stutter_init(stutter * HZ, t);
		if (torture_init_error(firsterr))
			goto unwind;
	}
	if (fqs_duration < 0)
		fqs_duration = 0;
	if (fqs_holdoff < 0)
		fqs_holdoff = 0;
	if (fqs_duration && fqs_holdoff) {
		/* Create the fqs thread */
		firsterr = torture_create_kthread(rcu_torture_fqs, NULL,
						  fqs_task);
		if (torture_init_error(firsterr))
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
		rcutor_hp = firsterr;
		if (torture_init_error(firsterr))
			goto unwind;
	}
	shutdown_jiffies = jiffies + shutdown_secs * HZ;
	firsterr = torture_shutdown_init(shutdown_secs, rcu_torture_cleanup);
	if (torture_init_error(firsterr))
		goto unwind;
	firsterr = torture_onoff_init(onoff_holdoff * HZ, onoff_interval,
				      rcutorture_sync);
	if (torture_init_error(firsterr))
		goto unwind;
	firsterr = rcu_torture_stall_init();
	if (torture_init_error(firsterr))
		goto unwind;
	firsterr = rcu_torture_fwd_prog_init();
	if (torture_init_error(firsterr))
		goto unwind;
	firsterr = rcu_torture_barrier_init();
	if (torture_init_error(firsterr))
		goto unwind;
	firsterr = rcu_torture_read_exit_init();
	if (torture_init_error(firsterr))
		goto unwind;
	if (preempt_duration > 0) {
		firsterr = torture_create_kthread(rcu_torture_preempt, NULL, preempt_task);
		if (torture_init_error(firsterr))
			goto unwind;
	}
	if (object_debug)
		rcu_test_debug_objects();
	torture_init_end();
	if (cur_ops->gp_slow_register && !WARN_ON_ONCE(!cur_ops->gp_slow_unregister))
		cur_ops->gp_slow_register(&rcu_fwd_cb_nodelay);
	return 0;

unwind:
	torture_init_end();
	rcu_torture_cleanup();
	if (shutdown_secs) {
		WARN_ON(!IS_MODULE(CONFIG_RCU_TORTURE_TEST));
		kernel_power_off();
	}
	return firsterr;
}

module_init(rcu_torture_init);
module_exit(rcu_torture_cleanup);
