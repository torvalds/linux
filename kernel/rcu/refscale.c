// SPDX-License-Identifier: GPL-2.0+
//
// Scalability test comparing RCU vs other mechanisms
// for acquiring references on objects.
//
// Copyright (C) Google, 2020.
//
// Author: Joel Fernandes <joel@joelfernandes.org>

#define pr_fmt(fmt) fmt

#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/notifier.h>
#include <linux/percpu.h>
#include <linux/rcupdate.h>
#include <linux/rcupdate_trace.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/seq_buf.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/stat.h>
#include <linux/srcu.h>
#include <linux/slab.h>
#include <linux/torture.h>
#include <linux/types.h>
#include <linux/sched/clock.h>

#include "rcu.h"

#define SCALE_FLAG "-ref-scale: "

#define SCALEOUT(s, x...) \
	pr_alert("%s" SCALE_FLAG s, scale_type, ## x)

#define VERBOSE_SCALEOUT(s, x...) \
	do { \
		if (verbose) \
			pr_alert("%s" SCALE_FLAG s "\n", scale_type, ## x); \
	} while (0)

static atomic_t verbose_batch_ctr;

#define VERBOSE_SCALEOUT_BATCH(s, x...)							\
do {											\
	if (verbose &&									\
	    (verbose_batched <= 0 ||							\
	     !(atomic_inc_return(&verbose_batch_ctr) % verbose_batched))) {		\
		schedule_timeout_uninterruptible(1);					\
		pr_alert("%s" SCALE_FLAG s "\n", scale_type, ## x);			\
	}										\
} while (0)

#define SCALEOUT_ERRSTRING(s, x...) pr_alert("%s" SCALE_FLAG "!!! " s "\n", scale_type, ## x)

MODULE_DESCRIPTION("Scalability test for object reference mechanisms");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joel Fernandes (Google) <joel@joelfernandes.org>");

static char *scale_type = "rcu";
module_param(scale_type, charp, 0444);
MODULE_PARM_DESC(scale_type, "Type of test (rcu, srcu, refcnt, rwsem, rwlock.");

torture_param(int, verbose, 0, "Enable verbose debugging printk()s");
torture_param(int, verbose_batched, 0, "Batch verbose debugging printk()s");

// Number of seconds to extend warm-up and cool-down for multiple guest OSes
torture_param(long, guest_os_delay, 0,
	      "Number of seconds to extend warm-up/cool-down for multiple guest OSes.");
// Wait until there are multiple CPUs before starting test.
torture_param(int, holdoff, IS_BUILTIN(CONFIG_RCU_REF_SCALE_TEST) ? 10 : 0,
	      "Holdoff time before test start (s)");
// Number of typesafe_lookup structures, that is, the degree of concurrency.
torture_param(long, lookup_instances, 0, "Number of typesafe_lookup structures.");
// Number of loops per experiment, all readers execute operations concurrently.
torture_param(long, loops, 10000, "Number of loops per experiment.");
// Number of readers, with -1 defaulting to about 75% of the CPUs.
torture_param(int, nreaders, -1, "Number of readers, -1 for 75% of CPUs.");
// Number of runs.
torture_param(int, nruns, 30, "Number of experiments to run.");
// Reader delay in nanoseconds, 0 for no delay.
torture_param(int, readdelay, 0, "Read-side delay in nanoseconds.");

#ifdef MODULE
# define REFSCALE_SHUTDOWN 0
#else
# define REFSCALE_SHUTDOWN 1
#endif

torture_param(bool, shutdown, REFSCALE_SHUTDOWN,
	      "Shutdown at end of scalability tests.");

struct reader_task {
	struct task_struct *task;
	int start_reader;
	wait_queue_head_t wq;
	u64 last_duration_ns;
};

static struct task_struct *shutdown_task;
static wait_queue_head_t shutdown_wq;

static struct task_struct *main_task;
static wait_queue_head_t main_wq;
static int shutdown_start;

static struct reader_task *reader_tasks;

// Number of readers that are part of the current experiment.
static atomic_t nreaders_exp;

// Use to wait for all threads to start.
static atomic_t n_init;
static atomic_t n_started;
static atomic_t n_warmedup;
static atomic_t n_cooleddown;

// Track which experiment is currently running.
static int exp_idx;

// Operations vector for selecting different types of tests.
struct ref_scale_ops {
	bool (*init)(void);
	void (*cleanup)(void);
	void (*readsection)(const int nloops);
	void (*delaysection)(const int nloops, const int udl, const int ndl);
	const char *name;
};

static const struct ref_scale_ops *cur_ops;

static void un_delay(const int udl, const int ndl)
{
	if (udl)
		udelay(udl);
	if (ndl)
		ndelay(ndl);
}

static void ref_rcu_read_section(const int nloops)
{
	int i;

	for (i = nloops; i >= 0; i--) {
		rcu_read_lock();
		rcu_read_unlock();
	}
}

static void ref_rcu_delay_section(const int nloops, const int udl, const int ndl)
{
	int i;

	for (i = nloops; i >= 0; i--) {
		rcu_read_lock();
		un_delay(udl, ndl);
		rcu_read_unlock();
	}
}

static bool rcu_sync_scale_init(void)
{
	return true;
}

static const struct ref_scale_ops rcu_ops = {
	.init		= rcu_sync_scale_init,
	.readsection	= ref_rcu_read_section,
	.delaysection	= ref_rcu_delay_section,
	.name		= "rcu"
};

// Definitions for SRCU ref scale testing.
DEFINE_STATIC_SRCU(srcu_refctl_scale);
static struct srcu_struct *srcu_ctlp = &srcu_refctl_scale;

static void srcu_ref_scale_read_section(const int nloops)
{
	int i;
	int idx;

	for (i = nloops; i >= 0; i--) {
		idx = srcu_read_lock(srcu_ctlp);
		srcu_read_unlock(srcu_ctlp, idx);
	}
}

static void srcu_ref_scale_delay_section(const int nloops, const int udl, const int ndl)
{
	int i;
	int idx;

	for (i = nloops; i >= 0; i--) {
		idx = srcu_read_lock(srcu_ctlp);
		un_delay(udl, ndl);
		srcu_read_unlock(srcu_ctlp, idx);
	}
}

static const struct ref_scale_ops srcu_ops = {
	.init		= rcu_sync_scale_init,
	.readsection	= srcu_ref_scale_read_section,
	.delaysection	= srcu_ref_scale_delay_section,
	.name		= "srcu"
};

static void srcu_lite_ref_scale_read_section(const int nloops)
{
	int i;
	int idx;

	for (i = nloops; i >= 0; i--) {
		idx = srcu_read_lock_lite(srcu_ctlp);
		srcu_read_unlock_lite(srcu_ctlp, idx);
	}
}

static void srcu_lite_ref_scale_delay_section(const int nloops, const int udl, const int ndl)
{
	int i;
	int idx;

	for (i = nloops; i >= 0; i--) {
		idx = srcu_read_lock_lite(srcu_ctlp);
		un_delay(udl, ndl);
		srcu_read_unlock_lite(srcu_ctlp, idx);
	}
}

static const struct ref_scale_ops srcu_lite_ops = {
	.init		= rcu_sync_scale_init,
	.readsection	= srcu_lite_ref_scale_read_section,
	.delaysection	= srcu_lite_ref_scale_delay_section,
	.name		= "srcu-lite"
};

#ifdef CONFIG_TASKS_RCU

// Definitions for RCU Tasks ref scale testing: Empty read markers.
// These definitions also work for RCU Rude readers.
static void rcu_tasks_ref_scale_read_section(const int nloops)
{
	int i;

	for (i = nloops; i >= 0; i--)
		continue;
}

static void rcu_tasks_ref_scale_delay_section(const int nloops, const int udl, const int ndl)
{
	int i;

	for (i = nloops; i >= 0; i--)
		un_delay(udl, ndl);
}

static const struct ref_scale_ops rcu_tasks_ops = {
	.init		= rcu_sync_scale_init,
	.readsection	= rcu_tasks_ref_scale_read_section,
	.delaysection	= rcu_tasks_ref_scale_delay_section,
	.name		= "rcu-tasks"
};

#define RCU_TASKS_OPS &rcu_tasks_ops,

#else // #ifdef CONFIG_TASKS_RCU

#define RCU_TASKS_OPS

#endif // #else // #ifdef CONFIG_TASKS_RCU

#ifdef CONFIG_TASKS_TRACE_RCU

// Definitions for RCU Tasks Trace ref scale testing.
static void rcu_trace_ref_scale_read_section(const int nloops)
{
	int i;

	for (i = nloops; i >= 0; i--) {
		rcu_read_lock_trace();
		rcu_read_unlock_trace();
	}
}

static void rcu_trace_ref_scale_delay_section(const int nloops, const int udl, const int ndl)
{
	int i;

	for (i = nloops; i >= 0; i--) {
		rcu_read_lock_trace();
		un_delay(udl, ndl);
		rcu_read_unlock_trace();
	}
}

static const struct ref_scale_ops rcu_trace_ops = {
	.init		= rcu_sync_scale_init,
	.readsection	= rcu_trace_ref_scale_read_section,
	.delaysection	= rcu_trace_ref_scale_delay_section,
	.name		= "rcu-trace"
};

#define RCU_TRACE_OPS &rcu_trace_ops,

#else // #ifdef CONFIG_TASKS_TRACE_RCU

#define RCU_TRACE_OPS

#endif // #else // #ifdef CONFIG_TASKS_TRACE_RCU

// Definitions for reference count
static atomic_t refcnt;

static void ref_refcnt_section(const int nloops)
{
	int i;

	for (i = nloops; i >= 0; i--) {
		atomic_inc(&refcnt);
		atomic_dec(&refcnt);
	}
}

static void ref_refcnt_delay_section(const int nloops, const int udl, const int ndl)
{
	int i;

	for (i = nloops; i >= 0; i--) {
		atomic_inc(&refcnt);
		un_delay(udl, ndl);
		atomic_dec(&refcnt);
	}
}

static const struct ref_scale_ops refcnt_ops = {
	.init		= rcu_sync_scale_init,
	.readsection	= ref_refcnt_section,
	.delaysection	= ref_refcnt_delay_section,
	.name		= "refcnt"
};

// Definitions for rwlock
static rwlock_t test_rwlock;

static bool ref_rwlock_init(void)
{
	rwlock_init(&test_rwlock);
	return true;
}

static void ref_rwlock_section(const int nloops)
{
	int i;

	for (i = nloops; i >= 0; i--) {
		read_lock(&test_rwlock);
		read_unlock(&test_rwlock);
	}
}

static void ref_rwlock_delay_section(const int nloops, const int udl, const int ndl)
{
	int i;

	for (i = nloops; i >= 0; i--) {
		read_lock(&test_rwlock);
		un_delay(udl, ndl);
		read_unlock(&test_rwlock);
	}
}

static const struct ref_scale_ops rwlock_ops = {
	.init		= ref_rwlock_init,
	.readsection	= ref_rwlock_section,
	.delaysection	= ref_rwlock_delay_section,
	.name		= "rwlock"
};

// Definitions for rwsem
static struct rw_semaphore test_rwsem;

static bool ref_rwsem_init(void)
{
	init_rwsem(&test_rwsem);
	return true;
}

static void ref_rwsem_section(const int nloops)
{
	int i;

	for (i = nloops; i >= 0; i--) {
		down_read(&test_rwsem);
		up_read(&test_rwsem);
	}
}

static void ref_rwsem_delay_section(const int nloops, const int udl, const int ndl)
{
	int i;

	for (i = nloops; i >= 0; i--) {
		down_read(&test_rwsem);
		un_delay(udl, ndl);
		up_read(&test_rwsem);
	}
}

static const struct ref_scale_ops rwsem_ops = {
	.init		= ref_rwsem_init,
	.readsection	= ref_rwsem_section,
	.delaysection	= ref_rwsem_delay_section,
	.name		= "rwsem"
};

// Definitions for global spinlock
static DEFINE_RAW_SPINLOCK(test_lock);

static void ref_lock_section(const int nloops)
{
	int i;

	preempt_disable();
	for (i = nloops; i >= 0; i--) {
		raw_spin_lock(&test_lock);
		raw_spin_unlock(&test_lock);
	}
	preempt_enable();
}

static void ref_lock_delay_section(const int nloops, const int udl, const int ndl)
{
	int i;

	preempt_disable();
	for (i = nloops; i >= 0; i--) {
		raw_spin_lock(&test_lock);
		un_delay(udl, ndl);
		raw_spin_unlock(&test_lock);
	}
	preempt_enable();
}

static const struct ref_scale_ops lock_ops = {
	.readsection	= ref_lock_section,
	.delaysection	= ref_lock_delay_section,
	.name		= "lock"
};

// Definitions for global irq-save spinlock

static void ref_lock_irq_section(const int nloops)
{
	unsigned long flags;
	int i;

	preempt_disable();
	for (i = nloops; i >= 0; i--) {
		raw_spin_lock_irqsave(&test_lock, flags);
		raw_spin_unlock_irqrestore(&test_lock, flags);
	}
	preempt_enable();
}

static void ref_lock_irq_delay_section(const int nloops, const int udl, const int ndl)
{
	unsigned long flags;
	int i;

	preempt_disable();
	for (i = nloops; i >= 0; i--) {
		raw_spin_lock_irqsave(&test_lock, flags);
		un_delay(udl, ndl);
		raw_spin_unlock_irqrestore(&test_lock, flags);
	}
	preempt_enable();
}

static const struct ref_scale_ops lock_irq_ops = {
	.readsection	= ref_lock_irq_section,
	.delaysection	= ref_lock_irq_delay_section,
	.name		= "lock-irq"
};

// Definitions acquire-release.
static DEFINE_PER_CPU(unsigned long, test_acqrel);

static void ref_acqrel_section(const int nloops)
{
	unsigned long x;
	int i;

	preempt_disable();
	for (i = nloops; i >= 0; i--) {
		x = smp_load_acquire(this_cpu_ptr(&test_acqrel));
		smp_store_release(this_cpu_ptr(&test_acqrel), x + 1);
	}
	preempt_enable();
}

static void ref_acqrel_delay_section(const int nloops, const int udl, const int ndl)
{
	unsigned long x;
	int i;

	preempt_disable();
	for (i = nloops; i >= 0; i--) {
		x = smp_load_acquire(this_cpu_ptr(&test_acqrel));
		un_delay(udl, ndl);
		smp_store_release(this_cpu_ptr(&test_acqrel), x + 1);
	}
	preempt_enable();
}

static const struct ref_scale_ops acqrel_ops = {
	.readsection	= ref_acqrel_section,
	.delaysection	= ref_acqrel_delay_section,
	.name		= "acqrel"
};

static volatile u64 stopopts;

static void ref_sched_clock_section(const int nloops)
{
	u64 x = 0;
	int i;

	preempt_disable();
	for (i = nloops; i >= 0; i--)
		x += sched_clock();
	preempt_enable();
	stopopts = x;
}

static void ref_sched_clock_delay_section(const int nloops, const int udl, const int ndl)
{
	u64 x = 0;
	int i;

	preempt_disable();
	for (i = nloops; i >= 0; i--) {
		x += sched_clock();
		un_delay(udl, ndl);
	}
	preempt_enable();
	stopopts = x;
}

static const struct ref_scale_ops sched_clock_ops = {
	.readsection	= ref_sched_clock_section,
	.delaysection	= ref_sched_clock_delay_section,
	.name		= "sched-clock"
};


static void ref_clock_section(const int nloops)
{
	u64 x = 0;
	int i;

	preempt_disable();
	for (i = nloops; i >= 0; i--)
		x += ktime_get_real_fast_ns();
	preempt_enable();
	stopopts = x;
}

static void ref_clock_delay_section(const int nloops, const int udl, const int ndl)
{
	u64 x = 0;
	int i;

	preempt_disable();
	for (i = nloops; i >= 0; i--) {
		x += ktime_get_real_fast_ns();
		un_delay(udl, ndl);
	}
	preempt_enable();
	stopopts = x;
}

static const struct ref_scale_ops clock_ops = {
	.readsection	= ref_clock_section,
	.delaysection	= ref_clock_delay_section,
	.name		= "clock"
};

static void ref_jiffies_section(const int nloops)
{
	u64 x = 0;
	int i;

	preempt_disable();
	for (i = nloops; i >= 0; i--)
		x += jiffies;
	preempt_enable();
	stopopts = x;
}

static void ref_jiffies_delay_section(const int nloops, const int udl, const int ndl)
{
	u64 x = 0;
	int i;

	preempt_disable();
	for (i = nloops; i >= 0; i--) {
		x += jiffies;
		un_delay(udl, ndl);
	}
	preempt_enable();
	stopopts = x;
}

static const struct ref_scale_ops jiffies_ops = {
	.readsection	= ref_jiffies_section,
	.delaysection	= ref_jiffies_delay_section,
	.name		= "jiffies"
};

////////////////////////////////////////////////////////////////////////
//
// Methods leveraging SLAB_TYPESAFE_BY_RCU.
//

// Item to look up in a typesafe manner.  Array of pointers to these.
struct refscale_typesafe {
	atomic_t rts_refctr;  // Used by all flavors
	spinlock_t rts_lock;
	seqlock_t rts_seqlock;
	unsigned int a;
	unsigned int b;
};

static struct kmem_cache *typesafe_kmem_cachep;
static struct refscale_typesafe **rtsarray;
static long rtsarray_size;
static DEFINE_TORTURE_RANDOM_PERCPU(refscale_rand);
static bool (*rts_acquire)(struct refscale_typesafe *rtsp, unsigned int *start);
static bool (*rts_release)(struct refscale_typesafe *rtsp, unsigned int start);

// Conditionally acquire an explicit in-structure reference count.
static bool typesafe_ref_acquire(struct refscale_typesafe *rtsp, unsigned int *start)
{
	return atomic_inc_not_zero(&rtsp->rts_refctr);
}

// Unconditionally release an explicit in-structure reference count.
static bool typesafe_ref_release(struct refscale_typesafe *rtsp, unsigned int start)
{
	if (!atomic_dec_return(&rtsp->rts_refctr)) {
		WRITE_ONCE(rtsp->a, rtsp->a + 1);
		kmem_cache_free(typesafe_kmem_cachep, rtsp);
	}
	return true;
}

// Unconditionally acquire an explicit in-structure spinlock.
static bool typesafe_lock_acquire(struct refscale_typesafe *rtsp, unsigned int *start)
{
	spin_lock(&rtsp->rts_lock);
	return true;
}

// Unconditionally release an explicit in-structure spinlock.
static bool typesafe_lock_release(struct refscale_typesafe *rtsp, unsigned int start)
{
	spin_unlock(&rtsp->rts_lock);
	return true;
}

// Unconditionally acquire an explicit in-structure sequence lock.
static bool typesafe_seqlock_acquire(struct refscale_typesafe *rtsp, unsigned int *start)
{
	*start = read_seqbegin(&rtsp->rts_seqlock);
	return true;
}

// Conditionally release an explicit in-structure sequence lock.  Return
// true if this release was successful, that is, if no retry is required.
static bool typesafe_seqlock_release(struct refscale_typesafe *rtsp, unsigned int start)
{
	return !read_seqretry(&rtsp->rts_seqlock, start);
}

// Do a read-side critical section with the specified delay in
// microseconds and nanoseconds inserted so as to increase probability
// of failure.
static void typesafe_delay_section(const int nloops, const int udl, const int ndl)
{
	unsigned int a;
	unsigned int b;
	int i;
	long idx;
	struct refscale_typesafe *rtsp;
	unsigned int start;

	for (i = nloops; i >= 0; i--) {
		preempt_disable();
		idx = torture_random(this_cpu_ptr(&refscale_rand)) % rtsarray_size;
		preempt_enable();
retry:
		rcu_read_lock();
		rtsp = rcu_dereference(rtsarray[idx]);
		a = READ_ONCE(rtsp->a);
		if (!rts_acquire(rtsp, &start)) {
			rcu_read_unlock();
			goto retry;
		}
		if (a != READ_ONCE(rtsp->a)) {
			(void)rts_release(rtsp, start);
			rcu_read_unlock();
			goto retry;
		}
		un_delay(udl, ndl);
		b = READ_ONCE(rtsp->a);
		// Remember, seqlock read-side release can fail.
		if (!rts_release(rtsp, start)) {
			rcu_read_unlock();
			goto retry;
		}
		WARN_ONCE(a != b, "Re-read of ->a changed from %u to %u.\n", a, b);
		b = rtsp->b;
		rcu_read_unlock();
		WARN_ON_ONCE(a * a != b);
	}
}

// Because the acquisition and release methods are expensive, there
// is no point in optimizing away the un_delay() function's two checks.
// Thus simply define typesafe_read_section() as a simple wrapper around
// typesafe_delay_section().
static void typesafe_read_section(const int nloops)
{
	typesafe_delay_section(nloops, 0, 0);
}

// Allocate and initialize one refscale_typesafe structure.
static struct refscale_typesafe *typesafe_alloc_one(void)
{
	struct refscale_typesafe *rtsp;

	rtsp = kmem_cache_alloc(typesafe_kmem_cachep, GFP_KERNEL);
	if (!rtsp)
		return NULL;
	atomic_set(&rtsp->rts_refctr, 1);
	WRITE_ONCE(rtsp->a, rtsp->a + 1);
	WRITE_ONCE(rtsp->b, rtsp->a * rtsp->a);
	return rtsp;
}

// Slab-allocator constructor for refscale_typesafe structures created
// out of a new slab of system memory.
static void refscale_typesafe_ctor(void *rtsp_in)
{
	struct refscale_typesafe *rtsp = rtsp_in;

	spin_lock_init(&rtsp->rts_lock);
	seqlock_init(&rtsp->rts_seqlock);
	preempt_disable();
	rtsp->a = torture_random(this_cpu_ptr(&refscale_rand));
	preempt_enable();
}

static const struct ref_scale_ops typesafe_ref_ops;
static const struct ref_scale_ops typesafe_lock_ops;
static const struct ref_scale_ops typesafe_seqlock_ops;

// Initialize for a typesafe test.
static bool typesafe_init(void)
{
	long idx;
	long si = lookup_instances;

	typesafe_kmem_cachep = kmem_cache_create("refscale_typesafe",
						 sizeof(struct refscale_typesafe), sizeof(void *),
						 SLAB_TYPESAFE_BY_RCU, refscale_typesafe_ctor);
	if (!typesafe_kmem_cachep)
		return false;
	if (si < 0)
		si = -si * nr_cpu_ids;
	else if (si == 0)
		si = nr_cpu_ids;
	rtsarray_size = si;
	rtsarray = kcalloc(si, sizeof(*rtsarray), GFP_KERNEL);
	if (!rtsarray)
		return false;
	for (idx = 0; idx < rtsarray_size; idx++) {
		rtsarray[idx] = typesafe_alloc_one();
		if (!rtsarray[idx])
			return false;
	}
	if (cur_ops == &typesafe_ref_ops) {
		rts_acquire = typesafe_ref_acquire;
		rts_release = typesafe_ref_release;
	} else if (cur_ops == &typesafe_lock_ops) {
		rts_acquire = typesafe_lock_acquire;
		rts_release = typesafe_lock_release;
	} else if (cur_ops == &typesafe_seqlock_ops) {
		rts_acquire = typesafe_seqlock_acquire;
		rts_release = typesafe_seqlock_release;
	} else {
		WARN_ON_ONCE(1);
		return false;
	}
	return true;
}

// Clean up after a typesafe test.
static void typesafe_cleanup(void)
{
	long idx;

	if (rtsarray) {
		for (idx = 0; idx < rtsarray_size; idx++)
			kmem_cache_free(typesafe_kmem_cachep, rtsarray[idx]);
		kfree(rtsarray);
		rtsarray = NULL;
		rtsarray_size = 0;
	}
	kmem_cache_destroy(typesafe_kmem_cachep);
	typesafe_kmem_cachep = NULL;
	rts_acquire = NULL;
	rts_release = NULL;
}

// The typesafe_init() function distinguishes these structures by address.
static const struct ref_scale_ops typesafe_ref_ops = {
	.init		= typesafe_init,
	.cleanup	= typesafe_cleanup,
	.readsection	= typesafe_read_section,
	.delaysection	= typesafe_delay_section,
	.name		= "typesafe_ref"
};

static const struct ref_scale_ops typesafe_lock_ops = {
	.init		= typesafe_init,
	.cleanup	= typesafe_cleanup,
	.readsection	= typesafe_read_section,
	.delaysection	= typesafe_delay_section,
	.name		= "typesafe_lock"
};

static const struct ref_scale_ops typesafe_seqlock_ops = {
	.init		= typesafe_init,
	.cleanup	= typesafe_cleanup,
	.readsection	= typesafe_read_section,
	.delaysection	= typesafe_delay_section,
	.name		= "typesafe_seqlock"
};

static void rcu_scale_one_reader(void)
{
	if (readdelay <= 0)
		cur_ops->readsection(loops);
	else
		cur_ops->delaysection(loops, readdelay / 1000, readdelay % 1000);
}

// Warm up cache, or, if needed run a series of rcu_scale_one_reader()
// to allow multiple rcuscale guest OSes to collect mutually valid data.
static void rcu_scale_warm_cool(void)
{
	unsigned long jdone = jiffies + (guest_os_delay > 0 ? guest_os_delay * HZ : -1);

	do {
		rcu_scale_one_reader();
		cond_resched();
	} while (time_before(jiffies, jdone));
}

// Reader kthread.  Repeatedly does empty RCU read-side
// critical section, minimizing update-side interference.
static int
ref_scale_reader(void *arg)
{
	unsigned long flags;
	long me = (long)arg;
	struct reader_task *rt = &(reader_tasks[me]);
	u64 start;
	s64 duration;

	VERBOSE_SCALEOUT_BATCH("ref_scale_reader %ld: task started", me);
	WARN_ON_ONCE(set_cpus_allowed_ptr(current, cpumask_of(me % nr_cpu_ids)));
	set_user_nice(current, MAX_NICE);
	atomic_inc(&n_init);
	if (holdoff)
		schedule_timeout_interruptible(holdoff * HZ);
repeat:
	VERBOSE_SCALEOUT_BATCH("ref_scale_reader %ld: waiting to start next experiment on cpu %d", me, raw_smp_processor_id());

	// Wait for signal that this reader can start.
	wait_event(rt->wq, (atomic_read(&nreaders_exp) && smp_load_acquire(&rt->start_reader)) ||
			   torture_must_stop());

	if (torture_must_stop())
		goto end;

	// Make sure that the CPU is affinitized appropriately during testing.
	WARN_ON_ONCE(raw_smp_processor_id() != me % nr_cpu_ids);

	WRITE_ONCE(rt->start_reader, 0);
	if (!atomic_dec_return(&n_started))
		while (atomic_read_acquire(&n_started))
			cpu_relax();

	VERBOSE_SCALEOUT_BATCH("ref_scale_reader %ld: experiment %d started", me, exp_idx);


	// To reduce noise, do an initial cache-warming invocation, check
	// in, and then keep warming until everyone has checked in.
	rcu_scale_one_reader();
	if (!atomic_dec_return(&n_warmedup))
		while (atomic_read_acquire(&n_warmedup))
			rcu_scale_one_reader();
	// Also keep interrupts disabled.  This also has the effect
	// of preventing entries into slow path for rcu_read_unlock().
	local_irq_save(flags);
	start = ktime_get_mono_fast_ns();

	rcu_scale_one_reader();

	duration = ktime_get_mono_fast_ns() - start;
	local_irq_restore(flags);

	rt->last_duration_ns = WARN_ON_ONCE(duration < 0) ? 0 : duration;
	// To reduce runtime-skew noise, do maintain-load invocations until
	// everyone is done.
	if (!atomic_dec_return(&n_cooleddown))
		while (atomic_read_acquire(&n_cooleddown))
			rcu_scale_one_reader();

	if (atomic_dec_and_test(&nreaders_exp))
		wake_up(&main_wq);

	VERBOSE_SCALEOUT_BATCH("ref_scale_reader %ld: experiment %d ended, (readers remaining=%d)",
				me, exp_idx, atomic_read(&nreaders_exp));

	if (!torture_must_stop())
		goto repeat;
end:
	torture_kthread_stopping("ref_scale_reader");
	return 0;
}

static void reset_readers(void)
{
	int i;
	struct reader_task *rt;

	for (i = 0; i < nreaders; i++) {
		rt = &(reader_tasks[i]);

		rt->last_duration_ns = 0;
	}
}

// Print the results of each reader and return the sum of all their durations.
static u64 process_durations(int n)
{
	int i;
	struct reader_task *rt;
	struct seq_buf s;
	char *buf;
	u64 sum = 0;

	buf = kmalloc(800 + 64, GFP_KERNEL);
	if (!buf)
		return 0;
	seq_buf_init(&s, buf, 800 + 64);

	seq_buf_printf(&s, "Experiment #%d (Format: <THREAD-NUM>:<Total loop time in ns>)",
		       exp_idx);

	for (i = 0; i < n && !torture_must_stop(); i++) {
		rt = &(reader_tasks[i]);

		if (i % 5 == 0)
			seq_buf_putc(&s, '\n');

		if (seq_buf_used(&s) >= 800) {
			pr_alert("%s", seq_buf_str(&s));
			seq_buf_clear(&s);
		}

		seq_buf_printf(&s, "%d: %llu\t", i, rt->last_duration_ns);

		sum += rt->last_duration_ns;
	}
	pr_alert("%s\n", seq_buf_str(&s));

	kfree(buf);
	return sum;
}

// The main_func is the main orchestrator, it performs a bunch of
// experiments.  For every experiment, it orders all the readers
// involved to start and waits for them to finish the experiment. It
// then reads their timestamps and starts the next experiment. Each
// experiment progresses from 1 concurrent reader to N of them at which
// point all the timestamps are printed.
static int main_func(void *arg)
{
	int exp, r;
	char buf1[64];
	char *buf;
	u64 *result_avg;

	set_cpus_allowed_ptr(current, cpumask_of(nreaders % nr_cpu_ids));
	set_user_nice(current, MAX_NICE);

	VERBOSE_SCALEOUT("main_func task started");
	result_avg = kzalloc(nruns * sizeof(*result_avg), GFP_KERNEL);
	buf = kzalloc(800 + 64, GFP_KERNEL);
	if (!result_avg || !buf) {
		SCALEOUT_ERRSTRING("out of memory");
		goto oom_exit;
	}
	if (holdoff)
		schedule_timeout_interruptible(holdoff * HZ);

	// Wait for all threads to start.
	atomic_inc(&n_init);
	while (atomic_read(&n_init) < nreaders + 1)
		schedule_timeout_uninterruptible(1);

	// Start exp readers up per experiment
	rcu_scale_warm_cool();
	for (exp = 0; exp < nruns && !torture_must_stop(); exp++) {
		if (torture_must_stop())
			goto end;

		reset_readers();
		atomic_set(&nreaders_exp, nreaders);
		atomic_set(&n_started, nreaders);
		atomic_set(&n_warmedup, nreaders);
		atomic_set(&n_cooleddown, nreaders);

		exp_idx = exp;

		for (r = 0; r < nreaders; r++) {
			smp_store_release(&reader_tasks[r].start_reader, 1);
			wake_up(&reader_tasks[r].wq);
		}

		VERBOSE_SCALEOUT("main_func: experiment started, waiting for %d readers",
				nreaders);

		wait_event(main_wq,
			   !atomic_read(&nreaders_exp) || torture_must_stop());

		VERBOSE_SCALEOUT("main_func: experiment ended");

		if (torture_must_stop())
			goto end;

		result_avg[exp] = div_u64(1000 * process_durations(nreaders), nreaders * loops);
	}
	rcu_scale_warm_cool();

	// Print the average of all experiments
	SCALEOUT("END OF TEST. Calculating average duration per loop (nanoseconds)...\n");

	pr_alert("Runs\tTime(ns)\n");
	for (exp = 0; exp < nruns; exp++) {
		u64 avg;
		u32 rem;

		avg = div_u64_rem(result_avg[exp], 1000, &rem);
		sprintf(buf1, "%d\t%llu.%03u\n", exp + 1, avg, rem);
		strcat(buf, buf1);
		if (strlen(buf) >= 800) {
			pr_alert("%s", buf);
			buf[0] = 0;
		}
	}

	pr_alert("%s", buf);

oom_exit:
	// This will shutdown everything including us.
	if (shutdown) {
		shutdown_start = 1;
		wake_up(&shutdown_wq);
	}

	// Wait for torture to stop us
	while (!torture_must_stop())
		schedule_timeout_uninterruptible(1);

end:
	torture_kthread_stopping("main_func");
	kfree(result_avg);
	kfree(buf);
	return 0;
}

static void
ref_scale_print_module_parms(const struct ref_scale_ops *cur_ops, const char *tag)
{
	pr_alert("%s" SCALE_FLAG
		 "--- %s:  verbose=%d verbose_batched=%d shutdown=%d holdoff=%d lookup_instances=%ld loops=%ld nreaders=%d nruns=%d readdelay=%d\n", scale_type, tag,
		 verbose, verbose_batched, shutdown, holdoff, lookup_instances, loops, nreaders, nruns, readdelay);
}

static void
ref_scale_cleanup(void)
{
	int i;

	if (torture_cleanup_begin())
		return;

	if (!cur_ops) {
		torture_cleanup_end();
		return;
	}

	if (reader_tasks) {
		for (i = 0; i < nreaders; i++)
			torture_stop_kthread("ref_scale_reader",
					     reader_tasks[i].task);
	}
	kfree(reader_tasks);

	torture_stop_kthread("main_task", main_task);
	kfree(main_task);

	// Do scale-type-specific cleanup operations.
	if (cur_ops->cleanup != NULL)
		cur_ops->cleanup();

	torture_cleanup_end();
}

// Shutdown kthread.  Just waits to be awakened, then shuts down system.
static int
ref_scale_shutdown(void *arg)
{
	wait_event_idle(shutdown_wq, shutdown_start);

	smp_mb(); // Wake before output.
	ref_scale_cleanup();
	kernel_power_off();

	return -EINVAL;
}

static int __init
ref_scale_init(void)
{
	long i;
	int firsterr = 0;
	static const struct ref_scale_ops *scale_ops[] = {
		&rcu_ops, &srcu_ops, &srcu_lite_ops, RCU_TRACE_OPS RCU_TASKS_OPS
		&refcnt_ops, &rwlock_ops, &rwsem_ops, &lock_ops, &lock_irq_ops,
		&acqrel_ops, &sched_clock_ops, &clock_ops, &jiffies_ops,
		&typesafe_ref_ops, &typesafe_lock_ops, &typesafe_seqlock_ops,
	};

	if (!torture_init_begin(scale_type, verbose))
		return -EBUSY;

	for (i = 0; i < ARRAY_SIZE(scale_ops); i++) {
		cur_ops = scale_ops[i];
		if (strcmp(scale_type, cur_ops->name) == 0)
			break;
	}
	if (i == ARRAY_SIZE(scale_ops)) {
		pr_alert("rcu-scale: invalid scale type: \"%s\"\n", scale_type);
		pr_alert("rcu-scale types:");
		for (i = 0; i < ARRAY_SIZE(scale_ops); i++)
			pr_cont(" %s", scale_ops[i]->name);
		pr_cont("\n");
		firsterr = -EINVAL;
		cur_ops = NULL;
		goto unwind;
	}
	if (cur_ops->init)
		if (!cur_ops->init()) {
			firsterr = -EUCLEAN;
			goto unwind;
		}

	ref_scale_print_module_parms(cur_ops, "Start of test");

	// Shutdown task
	if (shutdown) {
		init_waitqueue_head(&shutdown_wq);
		firsterr = torture_create_kthread(ref_scale_shutdown, NULL,
						  shutdown_task);
		if (torture_init_error(firsterr))
			goto unwind;
		schedule_timeout_uninterruptible(1);
	}

	// Reader tasks (default to ~75% of online CPUs).
	if (nreaders < 0)
		nreaders = (num_online_cpus() >> 1) + (num_online_cpus() >> 2);
	if (WARN_ONCE(loops <= 0, "%s: loops = %ld, adjusted to 1\n", __func__, loops))
		loops = 1;
	if (WARN_ONCE(nreaders <= 0, "%s: nreaders = %d, adjusted to 1\n", __func__, nreaders))
		nreaders = 1;
	if (WARN_ONCE(nruns <= 0, "%s: nruns = %d, adjusted to 1\n", __func__, nruns))
		nruns = 1;
	reader_tasks = kcalloc(nreaders, sizeof(reader_tasks[0]),
			       GFP_KERNEL);
	if (!reader_tasks) {
		SCALEOUT_ERRSTRING("out of memory");
		firsterr = -ENOMEM;
		goto unwind;
	}

	VERBOSE_SCALEOUT("Starting %d reader threads", nreaders);

	for (i = 0; i < nreaders; i++) {
		init_waitqueue_head(&reader_tasks[i].wq);
		firsterr = torture_create_kthread(ref_scale_reader, (void *)i,
						  reader_tasks[i].task);
		if (torture_init_error(firsterr))
			goto unwind;
	}

	// Main Task
	init_waitqueue_head(&main_wq);
	firsterr = torture_create_kthread(main_func, NULL, main_task);
	if (torture_init_error(firsterr))
		goto unwind;

	torture_init_end();
	return 0;

unwind:
	torture_init_end();
	ref_scale_cleanup();
	if (shutdown) {
		WARN_ON(!IS_MODULE(CONFIG_RCU_REF_SCALE_TEST));
		kernel_power_off();
	}
	return firsterr;
}

module_init(ref_scale_init);
module_exit(ref_scale_cleanup);
