/*
 * Read-Copy Update /proc-based torture test facility
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
 * Copyright (C) IBM Corporation, 2005
 *
 * Authors: Paul E. McKenney <paulmck@us.ibm.com>
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
#include <linux/rcuref.h>
#include <linux/cpu.h>
#include <linux/random.h>
#include <linux/delay.h>
#include <linux/byteorder/swabb.h>
#include <linux/stat.h>

MODULE_LICENSE("GPL");

static int nreaders = -1;	/* # reader threads, defaults to 4*ncpus */
static int stat_interval = 0;	/* Interval between stats, in seconds. */
				/*  Defaults to "only at end of test". */
static int verbose = 0;		/* Print more debug info. */

MODULE_PARM(nreaders, "i");
MODULE_PARM_DESC(nreaders, "Number of RCU reader threads");
MODULE_PARM(stat_interval, "i");
MODULE_PARM_DESC(stat_interval, "Number of seconds between stats printk()s");
MODULE_PARM(verbose, "i");
MODULE_PARM_DESC(verbose, "Enable verbose debugging printk()s");
#define TORTURE_FLAG "rcutorture: "
#define PRINTK_STRING(s) \
	do { printk(KERN_ALERT TORTURE_FLAG s "\n"); } while (0)
#define VERBOSE_PRINTK_STRING(s) \
	do { if (verbose) printk(KERN_ALERT TORTURE_FLAG s "\n"); } while (0)
#define VERBOSE_PRINTK_ERRSTRING(s) \
	do { if (verbose) printk(KERN_ALERT TORTURE_FLAG "!!! " s "\n"); } while (0)

static char printk_buf[4096];

static int nrealreaders;
static struct task_struct *writer_task;
static struct task_struct **reader_tasks;
static struct task_struct *stats_task;

#define RCU_TORTURE_PIPE_LEN 10

struct rcu_torture {
	struct rcu_head rtort_rcu;
	int rtort_pipe_count;
	struct list_head rtort_free;
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
atomic_t n_rcu_torture_alloc;
atomic_t n_rcu_torture_alloc_fail;
atomic_t n_rcu_torture_free;

/*
 * Allocate an element from the rcu_tortures pool.
 */
struct rcu_torture *
rcu_torture_alloc(void)
{
	struct list_head *p;

	spin_lock(&rcu_torture_lock);
	if (list_empty(&rcu_torture_freelist)) {
		atomic_inc(&n_rcu_torture_alloc_fail);
		spin_unlock(&rcu_torture_lock);
		return NULL;
	}
	atomic_inc(&n_rcu_torture_alloc);
	p = rcu_torture_freelist.next;
	list_del_init(p);
	spin_unlock(&rcu_torture_lock);
	return container_of(p, struct rcu_torture, rtort_free);
}

/*
 * Free an element to the rcu_tortures pool.
 */
static void
rcu_torture_free(struct rcu_torture *p)
{
	atomic_inc(&n_rcu_torture_free);
	spin_lock(&rcu_torture_lock);
	list_add_tail(&p->rtort_free, &rcu_torture_freelist);
	spin_unlock(&rcu_torture_lock);
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
	if (++rp->rtort_pipe_count >= RCU_TORTURE_PIPE_LEN)
		rcu_torture_free(rp);
	else
		call_rcu(p, rcu_torture_cb);
}

struct rcu_random_state {
	unsigned long rrs_state;
	unsigned long rrs_count;
};

#define RCU_RANDOM_MULT 39916801  /* prime */
#define RCU_RANDOM_ADD	479001701 /* prime */
#define RCU_RANDOM_REFRESH 10000

#define DEFINE_RCU_RANDOM(name) struct rcu_random_state name = { 0, 0 }

/*
 * Crude but fast random-number generator.  Uses a linear congruential
 * generator, with occasional help from get_random_bytes().
 */
static long
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
	do {
		schedule_timeout_uninterruptible(1);
		if (rcu_batches_completed() == oldbatch)
			continue;
		if ((rp = rcu_torture_alloc()) == NULL)
			continue;
		rp->rtort_pipe_count = 0;
		udelay(rcu_random(&rand) & 0x3ff);
		old_rp = rcu_torture_current;
		rcu_assign_pointer(rcu_torture_current, rp);
		smp_wmb();
		if (old_rp != NULL) {
			i = old_rp->rtort_pipe_count;
			if (i > RCU_TORTURE_PIPE_LEN)
				i = RCU_TORTURE_PIPE_LEN;
			atomic_inc(&rcu_torture_wcount[i]);
			old_rp->rtort_pipe_count++;
			call_rcu(&old_rp->rtort_rcu, rcu_torture_cb);
		}
		rcu_torture_current_version++;
		oldbatch = rcu_batches_completed();
	} while (!kthread_should_stop() && !fullstop);
	VERBOSE_PRINTK_STRING("rcu_torture_writer task stopping");
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
	DEFINE_RCU_RANDOM(rand);
	struct rcu_torture *p;
	int pipe_count;

	VERBOSE_PRINTK_STRING("rcu_torture_reader task started");
	do {
		rcu_read_lock();
		completed = rcu_batches_completed();
		p = rcu_dereference(rcu_torture_current);
		if (p == NULL) {
			/* Wait for rcu_torture_writer to get underway */
			rcu_read_unlock();
			schedule_timeout_interruptible(HZ);
			continue;
		}
		udelay(rcu_random(&rand) & 0x7f);
		preempt_disable();
		pipe_count = p->rtort_pipe_count;
		if (pipe_count > RCU_TORTURE_PIPE_LEN) {
			/* Should not happen, but... */
			pipe_count = RCU_TORTURE_PIPE_LEN;
		}
		++__get_cpu_var(rcu_torture_count)[pipe_count];
		completed = rcu_batches_completed() - completed;
		if (completed > RCU_TORTURE_PIPE_LEN) {
			/* Should not happen, but... */
			completed = RCU_TORTURE_PIPE_LEN;
		}
		++__get_cpu_var(rcu_torture_batch)[completed];
		preempt_enable();
		rcu_read_unlock();
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

	for_each_cpu(cpu) {
		for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++) {
			pipesummary[i] += per_cpu(rcu_torture_count, cpu)[i];
			batchsummary[i] += per_cpu(rcu_torture_batch, cpu)[i];
		}
	}
	for (i = RCU_TORTURE_PIPE_LEN - 1; i >= 0; i--) {
		if (pipesummary[i] != 0)
			break;
	}
	cnt += sprintf(&page[cnt], "rcutorture: ");
	cnt += sprintf(&page[cnt],
		       "rtc: %p ver: %ld tfle: %d rta: %d rtaf: %d rtf: %d",
		       rcu_torture_current,
		       rcu_torture_current_version,
		       list_empty(&rcu_torture_freelist),
		       atomic_read(&n_rcu_torture_alloc),
		       atomic_read(&n_rcu_torture_alloc_fail),
		       atomic_read(&n_rcu_torture_free));
	cnt += sprintf(&page[cnt], "\nrcutorture: ");
	if (i > 1)
		cnt += sprintf(&page[cnt], "!!! ");
	cnt += sprintf(&page[cnt], "Reader Pipe: ");
	for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++)
		cnt += sprintf(&page[cnt], " %ld", pipesummary[i]);
	cnt += sprintf(&page[cnt], "\nrcutorture: ");
	cnt += sprintf(&page[cnt], "Reader Batch: ");
	for (i = 0; i < RCU_TORTURE_PIPE_LEN; i++)
		cnt += sprintf(&page[cnt], " %ld", batchsummary[i]);
	cnt += sprintf(&page[cnt], "\nrcutorture: ");
	cnt += sprintf(&page[cnt], "Free-Block Circulation: ");
	for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++) {
		cnt += sprintf(&page[cnt], " %d",
			       atomic_read(&rcu_torture_wcount[i]));
	}
	cnt += sprintf(&page[cnt], "\n");
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

static void
rcu_torture_cleanup(void)
{
	int i;

	fullstop = 1;
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

	if (stats_task != NULL) {
		VERBOSE_PRINTK_STRING("Stopping rcu_torture_stats task");
		kthread_stop(stats_task);
	}
	stats_task = NULL;

	/* Wait for all RCU callbacks to fire.  */

	for (i = 0; i < RCU_TORTURE_PIPE_LEN; i++)
		synchronize_rcu();
	rcu_torture_stats_print();  /* -After- the stats thread is stopped! */
	PRINTK_STRING("--- End of test");
}

static int
rcu_torture_init(void)
{
	int i;
	int cpu;
	int firsterr = 0;

	/* Process args and tell the world that the torturer is on the job. */

	if (nreaders >= 0)
		nrealreaders = nreaders;
	else
		nrealreaders = 2 * num_online_cpus();
	printk(KERN_ALERT TORTURE_FLAG
	       "--- Start of test: nreaders=%d stat_interval=%d verbose=%d\n",
	       nrealreaders, stat_interval, verbose);
	fullstop = 0;

	/* Set up the freelist. */

	INIT_LIST_HEAD(&rcu_torture_freelist);
	for (i = 0; i < sizeof(rcu_tortures) / sizeof(rcu_tortures[0]); i++) {
		list_add_tail(&rcu_tortures[i].rtort_free,
			      &rcu_torture_freelist);
	}

	/* Initialize the statistics so that each run gets its own numbers. */

	rcu_torture_current = NULL;
	rcu_torture_current_version = 0;
	atomic_set(&n_rcu_torture_alloc, 0);
	atomic_set(&n_rcu_torture_alloc_fail, 0);
	atomic_set(&n_rcu_torture_free, 0);
	for (i = 0; i < RCU_TORTURE_PIPE_LEN + 1; i++)
		atomic_set(&rcu_torture_wcount[i], 0);
	for_each_cpu(cpu) {
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
	reader_tasks = kmalloc(nrealreaders * sizeof(reader_tasks[0]),
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
	return 0;

unwind:
	rcu_torture_cleanup();
	return firsterr;
}

module_init(rcu_torture_init);
module_exit(rcu_torture_cleanup);
