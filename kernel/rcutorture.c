/*
 * Read-Copy Update module-based torture test facility
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2005, 2006
 *
 * Authors: Paul E. McKenney <paulmck@us.ibm.com>
 *          Josh Triplett <josh@freedesktop.org>
 *
 * See also:  Documentation/RCU/torture.txt
 */
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
#include <linux/sched.h>
#include <asm/atomic.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/moduleparam.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/random.h>
#include <linux/delay.h>
#include <linux/byteorder/swabb.h>
#include <linux/stat.h>
#include <linux/srcu.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Paul E. McKenney <paulmck@us.ibm.com> and "
              "Josh Triplett <josh@freedesktop.org>");

static int nreaders = -1;	/* # reader threads, defaults to 2*ncpus */
static int nfakewriters = 4;	/* # fake writer threads */
static int stat_interval;	/* Interval between stats, in seconds. */
				/*  Defaults to "only at end of test". */
static int verbose;		/* Print more debug info. */
static int test_no_idle_hz;	/* Test RCU's support for tickless idle CPUs. */
static int shuffle_interval = 5; /* Interval between shuffles (in sec)*/
static char *torture_type = "rcu"; /* What RCU implementation to torture. */

module_param(nreaders, int, 0444);
MODULE_PARM_DESC(nreaders, "Number of RCU reader threads");
module_param(nfakewriters, int, 0444);
MODULE_PARM_DESC(nfakewriters, "Number of RCU fake writer threads");
module_param(stat_interval, int, 0444);
MODULE_PARM_DESC(stat_interval, "Number of seconds between stats printk()s");
module_param(verbose, bool, 0444);
MODULE_PARM_DESC(verbose, "Enable verbose debugging printk()s");
module_param(test_no_idle_hz, bool, 0444);
MODULE_PARM_DESC(test_no_idle_hz, "Test support for tickless idle CPUs");
module_param(shuffle_interval, int, 0444);
MODULE_PARM_DESC(shuffle_interval, "Number of seconds between shuffles");
module_param(torture_type, charp, 0444);
MODULE_PARM_DESC(torture_type, "Type of RCU to torture (rcu, rcu_bh, srcu)");

#define TORTURE_FLAG "-torture:"
#define PRINTK_STRING(s) \
	do { printk(KERN_ALERT "%s" TORTURE_FLAG s "\n", torture_type); } while (0)
#define VERBOSE_PRINTK_STRING(s) \
	do { if (verbose) printk(KERN_ALERT "%s" TORTURE_FLAG s "\n", torture_type); } while (0)
#define VERBOSE_PRINTK_ERRSTRING(s) \
	do { if (verbose) printk(KERN_ALERT "%s" TORTURE_FLAG "!!! " s "\n", torture_type); } while (0)

static char printk_buf[4096];

static int nrealreaders;
static struct task_struct *writer_task;
static struct task_struct **fakewriter_tasks;
static struct task_struct **reader_tasks;
static struct task_struct *stats_task;
static struct task_struct *shuffler_task;

#define RCU_TORTURE_PIPE_LEN 10

struct rcu_torture {
	struct rcu_head rtort_rcu;
	int rtort_pipe_count;
	struct list_head rtort_free;
	int rtort_mbtest;
};

static int fullstop = 0;	/* stop generating callbacks at test end. */
static LIST_HEAD(rcu_torture_freelist);
static struct rcu_torture *rcu_torture_current = NULL;
static long rcu_torture_current_version = 0;
static struct rcu_torture rcu_tortures[10 * RCU_TORTURE_PIPE_LEN];
static DEFINE_SPINLOCK(rcu_torture_lock);
static DEFINE_PER_CPU(long [RCU_TORTURE_PIPE_LEN + 1], rcu_torture_count) =
	{ 0 };
static DEFINE_PER_CPU(long [RCU_TORTURE_PIPE_LEN + 1], rcu_torture_batch) =
	{ 0 };
static atomic_t rcu_torture_wcount[RCU_TORTURE_PIPE_LEN + 1];
static atomic_t n_rcu_torture_alloc;
static atomic_t n_rcu_torture_alloc_fail;
static atomic_t n_rcu_torture_free;
static atomic_t n_rcu_torture_mberror;
static atomic_t n_rcu_torture_error;
static struct list_head rcu_torture_removed;

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

struct rcu_random_state {
	unsigned long rrs_state;
	long rrs_count;
};

#define RCU_RANDOM_MULT 39916801  /* prime */
#define RCU_RANDOM_ADD	479001701 /* prime */
#define RCU_RANDOM_REFRESH 10000

#define DEFINE_RCU_RANDOM(name) struct rcu_random_state name = { 0, 0 }

/*
 * Crude but fast random-number generator.  Uses a linear congruential
 * generator, with occasional help from get_random_bytes().
 */
static unsigned long
rcu_random(struct rcu_random_state *rrsp)
{
	long refresh;

	if (--rrsp->rrs_count < 0) {
		get_random_bytes(&refresh, sizeof(refresh));
		rrsp->rrs_state += refresh;
		rrsp->rrs_count = RCU_RANDOM_REFRESH;
	}
	rrsp->rrs_state = rrsp->rrs_state * RCU_RANDOM_MULT + RCU_RANDOM_ADD;
	return swahw32(rrsp->rrs_state);
}

/*
 * Operations vector for selecting different types of tests.
 */

struct rcu_torture_ops {
	void (*init)(void);
	void (*cleanup)(void);
	int (*readlock)(void);
	void (*readdelay)(struct rcu_random_state *rrsp);
	void (*readunlock)(int idx);
	int (*completed)(void);
	void (*deferredfree)(struct rcu_torture *p);
	void (*sync)(void);
	int (*stats)(char *page);
	char *name;
};
static struct rcu_torture_ops *cur_ops = NULL;

/*
 * Definitions for rcu torture testing.
 */

static int rcu_torture_read_lock(void) __acquires(RCU)
{
	rcu_read_lock();
	return 0;
}

static void rcu_read_delay(struct rcu_random_state *rrsp)
{
	long delay;
	const long longdelay = 200;

	/* We want there to be long-running readers, but not all the time. */

	delay = rcu_random(rrsp) % (nrealreaders * 2 * longdelay);
	if (!delay)
		udelay(longdelay);
}

static void rcu_torture_read_unlock(int idx) __releases(RCU)
{
	rcu_read_unlock();
}

static int rcu_torture_completed(void)
{
	return rcu_batches_completed();
}

static void
rcu_torture_cb(struct rcu_head *p)
{
	int i;
	struct rcu_torture *rp = container_of(p, struct rcu_torture, rtort_rcu);

	if (fullstop) {
		/* Test is ending, just drop callbacks on the floor. */
		/* The next initialization will pick up the pieces. */
		return;
	}
	i = rp->rtort_pipe_count;
	if (i > RCU_TORTURE_PIPE_LEN)
		i = RCU_TORTURE_PIPE_LEN;
	atomic_inc(&rcu_torture_wcount[i]);
	if (++rp->rtort_pipe_count >= RCU_TORTURE_PIPE_LEN) {
		rp->rtort_mbtest = 0;
		rcu_torture_free(rp);
	} else
		cur_ops->deferredfree(rp);
}

static void rcu_torture_deferred_free(struct rcu_torture *p)
{
	call_rcu(&p->rtort_rcu, rcu_torture_cb);
}

static struct rcu_torture_ops rcu_ops = {
	.init = NULL,
	.cleanup = NULL,
	.readlock = rcu_torture_read_lock,
	.readdelay = rcu_read_delay,
	.readunlock = rcu_torture_read_unlock,
	.completed = rcu_torture_completed,
	.deferredfree = rcu_torture_deferred_free,
	.sync = synchronize_rcu,
	.stats = NULL,
	.name = "rcu"
};

static void rcu_sync_torture_deferred_free(struct rcu_torture *p)
{
	int i;
	struct rcu_torture *rp;
	struct rcu_torture *rp1;

	cur_ops->sync();
	list_add(&p->rtort_free, &rcu_torture_removed);
	list_for_each_entry_safe(rp, rp1, &rcu_torture_removed, rtort_free) {
		i = rp->rtort_pipe_count;
		if (i > RCU_TORTURE_PIPE_LEN)
			i = RCU_TORTURE_PIPE_LEN;
		atomic_inc(&rcu_torture_wcount[i]);
		if (++rp->rtort_pipe_count >= RCU_TORTURE_PIPE_LEN) {
			rp->rtort_mbtest = 0;
			list_del(&rp->rtort_free);
			rcu_torture_free(rp);
		}
	}
}

static void rcu_sync_torture_init(void)
{
	INIT_LIST_HEAD(&rcu_torture_removed);
}

static struct rcu_torture_ops rcu_sync_ops = {
	.init = rcu_sync_torture_init,
	.cleanup = NULL,
	.readlock = rcu_torture_read_lock,
	.readdelay = rcu_read_delay,
	.readunlock = rcu_torture_read_unlock,
	.completed = rcu_torture_completed,
	.deferredfree = rcu_sync_torture_deferred_free,
	.sync = synchronize_rcu,
	.stats = NULL,
	.name = "rcu_sync"
};

/*
 * Definitions for rcu_bh torture testing.
 */

static int rcu_bh_torture_read_lock(void) __acquires(RCU_BH)
{
	rcu_read_lock_bh();
	return 0;
}

static void rcu_bh_torture_read_unlock(int idx) __releases(RCU_BH)
{
	rcu_read_unlock_bh();
}

static int rcu_bh_torture_completed(void)
{
	return rcu_batches_completed_bh();
}

static void rcu_bh_torture_deferred_free(struct rcu_torture *p)
{
	call_rcu_bh(&p->rtort_rcu, rcu_torture_cb);
}

struct rcu_bh_torture_synchronize {
	struct rcu_head head;
	struct completion completion;
};

static void rcu_bh_torture_wakeme_after_cb(struct rcu_head *head)
{
	struct rcu_bh_torture_synchronize *rcu;

	rcu = container_of(head, struct rcu_bh_torture_synchronize, head);
	complete(&rcu->completion);
}

static void rcu_bh_torture_synchronize(void)
{
	struct rcu_bh_torture_synchronize rcu;

	init_completion(&rcu.completion);
	call_rcu_bh(&rcu.head, rcu_bh_torture_wakeme_after_cb);
	wait_for_completion(&rcu.completion);
}

static struct rcu_torture_ops rcu_bh_ops = {
	.init = NULL,
	.cleanup = NULL,
	.readlock = rcu_bh_torture_read_lock,
	.readdelay = rcu_read_delay,  /* just reuse rcu's version. */
	.readunlock = rcu_bh_torture_read_unlock,
	.completed = rcu_bh_torture_completed,
	.deferredfree = rcu_bh_torture_deferred_free,
	.sync = rcu_bh_torture_synchronize,
	.stats = NULL,
	.name = "rcu_bh"
};

static struct rcu_torture_ops rcu_bh_sync_ops = {
	.init = rcu_sync_torture_init,
	.cleanup = NULL,
	.readlock = rcu_bh_torture_read_lock,
	.readdelay = rcu_read_delay,  /* just reuse rcu's version. */
	.readunlock = rcu_bh_torture_read_unlock,
	.completed = rcu_bh_torture_completed,
	.deferredfree = rcu_sync_torture_deferred_free,
	.sync = rcu_bh_torture_synchronize,
	.stats = NULL,
	.name = "rcu_bh_sync"
};

/*
 * Definitions for srcu torture testing.
 */

static struct srcu_struct srcu_ctl;

static void srcu_torture_init(void)
{
	init_srcu_struct(&srcu_ctl);
	rcu_sync_torture_init();
}

static void srcu_torture_cleanup(void)
{
	synchronize_srcu(&srcu_ctl);
	cleanup_srcu_struct(&srcu_ctl);
}

static int srcu_torture_read_lock(void) __acquires(&srcu_ctl)
{
	return srcu_read_lock(&srcu_ctl);
}

static void srcu_read_delay(struct rcu_random_state *rrsp)
{
	long delay;
	const long uspertick = 1000000 / HZ;
	const long longdelay = 10;

	/* We want there to be long-running readers, but not all the time. */

	delay = rcu_random(rrsp) % (nrealreaders * 2 * longdelay * uspertick);
	if (!delay)
		schedule_timeout_interruptible(longdelay);
}

static void srcu_torture_read_unlock(int idx) __releases(&srcu_ctl)
{
	srcu_read_unlock(&srcu_ctl, idx);
}

static int srcu_torture_completed(void)
{
	return srcu_batches_completed(&srcu_ctl);
}

static void srcu_torture_synchronize(void)
{
	synchronize_srcu(&srcu_ctl);
}

static int srcu_torture_stats(char *page)
{
	int cnt = 0;
	int cpu;
	int idx = srcu_ctl.completed & 0x1;

	cnt += sprintf(&page[cnt], "%s%s per-CPU(idx=%d):",
		       torture_type, TORTURE_FLAG, idx);
	for_each_possible_cpu(cpu) {
		cnt += sprintf(&page[cnt], " %d(%d,%d)", cpu,
			       per_cpu_ptr(srcu_ctl.per_cpu_ref, cpu)->c[!idx],
			       per_cpu_ptr(srcu_ctl.per_cpu_ref, cpu)->c[idx]);
	}
	cnt += sprintf(&page[cnt], "\n");
	return cnt;
}

static struct rcu_torture_ops srcu_ops = {
	.init = srcu_torture_init,
	.cleanup = srcu_torture_cleanup,
	.readlock = srcu_torture_read_lock,
	.readdelay = srcu_read_delay,
	.readunlock = srcu_torture_read_unlock,
	.completed = srcu_torture_completed,
	.deferredfree = rcu_sync_torture_deferred_free,
	.sync = srcu_torture_synchronize,
	.stats = srcu_torture_stats,
	.name = "srcu"
};

/*
 * Definitions for sched torture testing.
 */

static int sched_torture_read_lock(void)
{
	preempt_disable();
	return 0;
}

static void sched_torture_read_unlock(int idx)
{
	preempt_enable();
}

static int sched_torture_completed(void)
{
	return 0;
}

static void sched_torture_synchronize(void)
{
	synchronize_sched();
}

static struct rcu_torture_ops sched_ops = {
	.init = rcu_sync_torture_init,
	.cleanup = NULL,
	.readlock = sched_torture_read_lock,
	.readdelay = rcu_read_delay,  /* just reuse rcu's version. */
	.readunlock = sched_torture_read_unlock,
	.completed = sched_torture_completed,
	.deferredfree = rcu_sync_torture_deferred_free,
	.sync = sched_torture_synchronize,
	.stats = NULL,
	.name = "sched"
};

static struct rcu_torture_ops *torture_ops[] =
	{ &rcu_ops, &rcu_sync_ops, &rcu_bh_ops, &rcu_bh_sync_ops, &srcu_ops,
	  &sched_ops, NULL };

/*
 * RCU torture writer kthread.  Repeatedly substitutes a new structure
 * for that pointed to by rcu_torture_current, freeing the old structure
 * after a series of grace periods (the "pipeline").
 */
static int
rcu_torture_writer(void *arg)
{
	int i;
	long oldbatch = rcu_batches_completed();
	struct rcu_torture *rp;
	struct rcu_torture *old_rp;
	static DEFINE_RCU_RANDOM(rand);

	VERBOSE_PRINTK_STRING("rcu_torture_writer task started");
	set_user_nice(current, 19);
	current->flags |= PF_NOFREEZE;

	do {
		schedule_timeout_uninterruptible(1);
		if ((rp = rcu_torture_alloc()) == NULL)
			continue;
		rp->rtort_pipe_count = 0;
		udelay(rcu_random(&rand) & 0x3ff);
		old_rp = rcu_torture_current;
		rp->rtort_mbtest = 1;
		rcu_assign_pointer(rcu_torture_current, rp);
		smp_wmb();
		if (old_rp != NULL) {
			i = old_rp->rtort_pipe_count;
			if (i > RCU_TORTURE_PIPE_LEN)
				i = RCU_TORTURE_PIPE_LEN;
			atomic_inc(&rcu_torture_wcount[i]);
			old_rp->rtort_pipe_count++;
			cur_ops->deferredfree(old_rp);
		}
		rcu_torture_current_version++;
		oldbatch = cur_ops->completed();
	} while (!kthread_should_stop() && !fullstop);
	VERBOSE_PRINTK_STRING("rcu_torture_writer task stopping");
	while (!kthread_should_stop())
		schedule_timeout_uninterruptible(1);
	return 0;
}

/*
 * RCU torture fake writer kthread.  Repeatedly calls sync, with a random
 * delay between calls.
 */
static int
rcu_torture_fakewriter(void *arg)
{
	DEFINE_RCU_RANDOM(rand);

	VERBOSE_PRINTK_STRING("rcu_torture_fakewriter task started");
	set_user_nice(current, 19);
	current->flags |= PF_NOFREEZE;

	do {
		schedule_timeout_uninterruptible(1 + rcu_random(&rand)%10);
		udelay(rcu_random(&rand) & 0x3ff);
		cur_ops->sync();
	} while (!kthread_should_stop() && !fullstop);

	VERBOSE_PRINTK_STRING("rcu_torture_fakewriter task stopping");
	while (!kthread_should_stop())
		schedule_timeout_uninterruptible(1);
	return 0;
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
	int completed;
	int idx;
	DEFINE_RCU_RANDOM(rand);
	struct rcu_torture *p;
	int pipe_count;

	VERBOSE_PRINTK_STRING("rcu_torture_reader task started");
	set_user_nice(current, 19);
	current->flags |= PF_NOFREEZE;

	do {
		idx = cur_ops->readlock();
		completed = cur_ops->completed();
		p = rcu_dereference(rcu_torture_current);
		if (p == NULL) {
			/* Wait for rcu_torture_writer to get underway */
			cur_ops->readunlock(idx);
			schedule_timeout_interruptible(HZ);
			continue;
		}
		if (p->rtort_mbtest == 0)
			atomic_inc(&n_rcu_torture_mberror);
		cur_ops->readdelay(&rand);
		preempt_disable();
		pipe_count = p->rtort_pipe_count;
		if (pipe_count > RCU_TORTURE_PIPE_LEN) {
			/* Should not happen, but... */
			pipe_count = RCU_TORTURE_PIPE_LEN;
		}
		++__get_cpu_var(rcu_torture_count)[pipe_count];
		completed = cur_ops->completed() - completed;
		if (completed > RCU_TORTURE_PIPE_LEN) {
			/* Should not happen, but... */
			completed = RCU_TORTURE_PIPE_LEN;
		}
		++__get_cpu_var(rcu_torture_batch)[completed];
		preempt_enable();
		cur_ops->readunlock(idx);
		schedule();
	} while (!kthread_should_stop() && !fullstop);
	VERBOSE_PRINTK_STRING("rcu_torture_reader task stopping");
	while (!kthread_should_stop())
		schedule_timeout_uninterruptible(1);
	return 0;
}

/*
 * Create an RCU-torture statistics message in the specified buffer.
 */
static int
rcu_torture_printk(char *page)
{
	int cnt = 0;
	int cpu;
	int i;
	long pipesummary[RCU_TORTURE_PIPE_LEN + 1] = { 0 };
	long batchsummary[RCU_TORTURE_PIPE_LEN + 1] = { 0 };

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
	cnt += sprintf(&page[cnt], "%s%s ", torture_type, TORTURE_FLAG);
	cnt += sprintf(&page[cnt],
		       "rtc: %p ver: %ld tfle: %d rta: %d rtaf: %d rtf: %d "
		       "rtmbe: %d",
		       rcu_torture_current,
		       rcu_torture_current_version,
		       list_empty(&rcu_torture_freelist),
		       atomic_read(&n_rcu_torture_alloc),
		       atomic_read(&n_rcu_torture_alloc_fail),
		       atomic_read(&n_rcu_torture_free),
		       atomic_read(&n_rcu_torture_mberror));
	if (atomic_read(&n_rcu_torture_mberror) != 0)
		cnt += sprintf(&page[cnt], " !!!");
	cnt += sprintf(&page[cnt], "\n%s%s ", torture_type, TORTURE_FLAG);
	if (i > 1) {
		cnt += sprintf(&page[cnt], "!!! ");
		atomic_inc(&n_rcu_torture_error);
	}
	cnt += sprintf(&page[cnt], "Reader Pipe: ");
	for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++)
		cnt += sprintf(&page[cnt], " %ld", pipesummary[i]);
	cnt += sprintf(&page[cnt], "\n%s%s ", torture_type, TORTURE_FLAG);
	cnt += sprintf(&page[cnt], "Reader Batch: ");
	for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++)
		cnt += sprintf(&page[cnt], " %ld", batchsummary[i]);
	cnt += sprintf(&page[cnt], "\n%s%s ", torture_type, TORTURE_FLAG);
	cnt += sprintf(&page[cnt], "Free-Block Circulation: ");
	for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++) {
		cnt += sprintf(&page[cnt], " %d",
			       atomic_read(&rcu_torture_wcount[i]));
	}
	cnt += sprintf(&page[cnt], "\n");
	if (cur_ops->stats != NULL)
		cnt += cur_ops->stats(&page[cnt]);
	return cnt;
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
	int cnt;

	cnt = rcu_torture_printk(printk_buf);
	printk(KERN_ALERT "%s", printk_buf);
}

/*
 * Periodically prints torture statistics, if periodic statistics printing
 * was specified via the stat_interval module parameter.
 *
 * No need to worry about fullstop here, since this one doesn't reference
 * volatile state or register callbacks.
 */
static int
rcu_torture_stats(void *arg)
{
	VERBOSE_PRINTK_STRING("rcu_torture_stats task started");
	do {
		schedule_timeout_interruptible(stat_interval * HZ);
		rcu_torture_stats_print();
	} while (!kthread_should_stop());
	VERBOSE_PRINTK_STRING("rcu_torture_stats task stopping");
	return 0;
}

static int rcu_idle_cpu;	/* Force all torture tasks off this CPU */

/* Shuffle tasks such that we allow @rcu_idle_cpu to become idle. A special case
 * is when @rcu_idle_cpu = -1, when we allow the tasks to run on all CPUs.
 */
static void rcu_torture_shuffle_tasks(void)
{
	cpumask_t tmp_mask = CPU_MASK_ALL;
	int i;

	lock_cpu_hotplug();

	/* No point in shuffling if there is only one online CPU (ex: UP) */
	if (num_online_cpus() == 1) {
		unlock_cpu_hotplug();
		return;
	}

	if (rcu_idle_cpu != -1)
		cpu_clear(rcu_idle_cpu, tmp_mask);

	set_cpus_allowed(current, tmp_mask);

	if (reader_tasks != NULL) {
		for (i = 0; i < nrealreaders; i++)
			if (reader_tasks[i])
				set_cpus_allowed(reader_tasks[i], tmp_mask);
	}

	if (fakewriter_tasks != NULL) {
		for (i = 0; i < nfakewriters; i++)
			if (fakewriter_tasks[i])
				set_cpus_allowed(fakewriter_tasks[i], tmp_mask);
	}

	if (writer_task)
		set_cpus_allowed(writer_task, tmp_mask);

	if (stats_task)
		set_cpus_allowed(stats_task, tmp_mask);

	if (rcu_idle_cpu == -1)
		rcu_idle_cpu = num_online_cpus() - 1;
	else
		rcu_idle_cpu--;

	unlock_cpu_hotplug();
}

/* Shuffle tasks across CPUs, with the intent of allowing each CPU in the
 * system to become idle at a time and cut off its timer ticks. This is meant
 * to test the support for such tickless idle CPU in RCU.
 */
static int
rcu_torture_shuffle(void *arg)
{
	VERBOSE_PRINTK_STRING("rcu_torture_shuffle task started");
	do {
		schedule_timeout_interruptible(shuffle_interval * HZ);
		rcu_torture_shuffle_tasks();
	} while (!kthread_should_stop());
	VERBOSE_PRINTK_STRING("rcu_torture_shuffle task stopping");
	return 0;
}

static inline void
rcu_torture_print_module_parms(char *tag)
{
	printk(KERN_ALERT "%s" TORTURE_FLAG
		"--- %s: nreaders=%d nfakewriters=%d "
		"stat_interval=%d verbose=%d test_no_idle_hz=%d "
		"shuffle_interval = %d\n",
		torture_type, tag, nrealreaders, nfakewriters,
		stat_interval, verbose, test_no_idle_hz, shuffle_interval);
}

static void
rcu_torture_cleanup(void)
{
	int i;

	fullstop = 1;
	if (shuffler_task != NULL) {
		VERBOSE_PRINTK_STRING("Stopping rcu_torture_shuffle task");
		kthread_stop(shuffler_task);
	}
	shuffler_task = NULL;

	if (writer_task != NULL) {
		VERBOSE_PRINTK_STRING("Stopping rcu_torture_writer task");
		kthread_stop(writer_task);
	}
	writer_task = NULL;

	if (reader_tasks != NULL) {
		for (i = 0; i < nrealreaders; i++) {
			if (reader_tasks[i] != NULL) {
				VERBOSE_PRINTK_STRING(
					"Stopping rcu_torture_reader task");
				kthread_stop(reader_tasks[i]);
			}
			reader_tasks[i] = NULL;
		}
		kfree(reader_tasks);
		reader_tasks = NULL;
	}
	rcu_torture_current = NULL;

	if (fakewriter_tasks != NULL) {
		for (i = 0; i < nfakewriters; i++) {
			if (fakewriter_tasks[i] != NULL) {
				VERBOSE_PRINTK_STRING(
					"Stopping rcu_torture_fakewriter task");
				kthread_stop(fakewriter_tasks[i]);
			}
			fakewriter_tasks[i] = NULL;
		}
		kfree(fakewriter_tasks);
		fakewriter_tasks = NULL;
	}

	if (stats_task != NULL) {
		VERBOSE_PRINTK_STRING("Stopping rcu_torture_stats task");
		kthread_stop(stats_task);
	}
	stats_task = NULL;

	/* Wait for all RCU callbacks to fire.  */
	rcu_barrier();

	rcu_torture_stats_print();  /* -After- the stats thread is stopped! */

	if (cur_ops->cleanup != NULL)
		cur_ops->cleanup();
	if (atomic_read(&n_rcu_torture_error))
		rcu_torture_print_module_parms("End of test: FAILURE");
	else
		rcu_torture_print_module_parms("End of test: SUCCESS");
}

static int __init
rcu_torture_init(void)
{
	int i;
	int cpu;
	int firsterr = 0;

	/* Process args and tell the world that the torturer is on the job. */

	for (i = 0; cur_ops = torture_ops[i], cur_ops != NULL; i++) {
		cur_ops = torture_ops[i];
		if (strcmp(torture_type, cur_ops->name) == 0) {
			break;
		}
	}
	if (cur_ops == NULL) {
		printk(KERN_ALERT "rcutorture: invalid torture type: \"%s\"\n",
		       torture_type);
		return (-EINVAL);
	}
	if (cur_ops->init != NULL)
		cur_ops->init(); /* no "goto unwind" prior to this point!!! */

	if (nreaders >= 0)
		nrealreaders = nreaders;
	else
		nrealreaders = 2 * num_online_cpus();
	rcu_torture_print_module_parms("Start of test");
	fullstop = 0;

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
	for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++)
		atomic_set(&rcu_torture_wcount[i], 0);
	for_each_possible_cpu(cpu) {
		for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++) {
			per_cpu(rcu_torture_count, cpu)[i] = 0;
			per_cpu(rcu_torture_batch, cpu)[i] = 0;
		}
	}

	/* Start up the kthreads. */

	VERBOSE_PRINTK_STRING("Creating rcu_torture_writer task");
	writer_task = kthread_run(rcu_torture_writer, NULL,
				  "rcu_torture_writer");
	if (IS_ERR(writer_task)) {
		firsterr = PTR_ERR(writer_task);
		VERBOSE_PRINTK_ERRSTRING("Failed to create writer");
		writer_task = NULL;
		goto unwind;
	}
	fakewriter_tasks = kzalloc(nfakewriters * sizeof(fakewriter_tasks[0]),
	                           GFP_KERNEL);
	if (fakewriter_tasks == NULL) {
		VERBOSE_PRINTK_ERRSTRING("out of memory");
		firsterr = -ENOMEM;
		goto unwind;
	}
	for (i = 0; i < nfakewriters; i++) {
		VERBOSE_PRINTK_STRING("Creating rcu_torture_fakewriter task");
		fakewriter_tasks[i] = kthread_run(rcu_torture_fakewriter, NULL,
		                                  "rcu_torture_fakewriter");
		if (IS_ERR(fakewriter_tasks[i])) {
			firsterr = PTR_ERR(fakewriter_tasks[i]);
			VERBOSE_PRINTK_ERRSTRING("Failed to create fakewriter");
			fakewriter_tasks[i] = NULL;
			goto unwind;
		}
	}
	reader_tasks = kzalloc(nrealreaders * sizeof(reader_tasks[0]),
			       GFP_KERNEL);
	if (reader_tasks == NULL) {
		VERBOSE_PRINTK_ERRSTRING("out of memory");
		firsterr = -ENOMEM;
		goto unwind;
	}
	for (i = 0; i < nrealreaders; i++) {
		VERBOSE_PRINTK_STRING("Creating rcu_torture_reader task");
		reader_tasks[i] = kthread_run(rcu_torture_reader, NULL,
					      "rcu_torture_reader");
		if (IS_ERR(reader_tasks[i])) {
			firsterr = PTR_ERR(reader_tasks[i]);
			VERBOSE_PRINTK_ERRSTRING("Failed to create reader");
			reader_tasks[i] = NULL;
			goto unwind;
		}
	}
	if (stat_interval > 0) {
		VERBOSE_PRINTK_STRING("Creating rcu_torture_stats task");
		stats_task = kthread_run(rcu_torture_stats, NULL,
					"rcu_torture_stats");
		if (IS_ERR(stats_task)) {
			firsterr = PTR_ERR(stats_task);
			VERBOSE_PRINTK_ERRSTRING("Failed to create stats");
			stats_task = NULL;
			goto unwind;
		}
	}
	if (test_no_idle_hz) {
		rcu_idle_cpu = num_online_cpus() - 1;
		/* Create the shuffler thread */
		shuffler_task = kthread_run(rcu_torture_shuffle, NULL,
					  "rcu_torture_shuffle");
		if (IS_ERR(shuffler_task)) {
			firsterr = PTR_ERR(shuffler_task);
			VERBOSE_PRINTK_ERRSTRING("Failed to create shuffler");
			shuffler_task = NULL;
			goto unwind;
		}
	}
	return 0;

unwind:
	rcu_torture_cleanup();
	return firsterr;
}

module_init(rcu_torture_init);
module_exit(rcu_torture_cleanup);
