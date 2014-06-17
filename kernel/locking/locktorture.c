/*
 * Module-based torture test facility for locking
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
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * Copyright (C) IBM Corporation, 2014
 *
 * Author: Paul E. McKenney <paulmck@us.ibm.com>
 *	Based on kernel/rcu/torture.c.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
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
#include <linux/slab.h>
#include <linux/trace_clock.h>
#include <asm/byteorder.h>
#include <linux/torture.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Paul E. McKenney <paulmck@us.ibm.com>");

torture_param(int, nwriters_stress, -1,
	     "Number of write-locking stress-test threads");
torture_param(int, onoff_holdoff, 0, "Time after boot before CPU hotplugs (s)");
torture_param(int, onoff_interval, 0,
	     "Time between CPU hotplugs (s), 0=disable");
torture_param(int, shuffle_interval, 3,
	     "Number of jiffies between shuffles, 0=disable");
torture_param(int, shutdown_secs, 0, "Shutdown time (j), <= zero to disable.");
torture_param(int, stat_interval, 60,
	     "Number of seconds between stats printk()s");
torture_param(int, stutter, 5, "Number of jiffies to run/halt test, 0=disable");
torture_param(bool, verbose, true,
	     "Enable verbose debugging printk()s");

static char *torture_type = "spin_lock";
module_param(torture_type, charp, 0444);
MODULE_PARM_DESC(torture_type,
		 "Type of lock to torture (spin_lock, spin_lock_irq, ...)");

static atomic_t n_lock_torture_errors;

static struct task_struct *stats_task;
static struct task_struct **writer_tasks;

static int nrealwriters_stress;
static bool lock_is_write_held;

struct lock_writer_stress_stats {
	long n_write_lock_fail;
	long n_write_lock_acquired;
};
static struct lock_writer_stress_stats *lwsa;

#if defined(MODULE) || defined(CONFIG_LOCK_TORTURE_TEST_RUNNABLE)
#define LOCKTORTURE_RUNNABLE_INIT 1
#else
#define LOCKTORTURE_RUNNABLE_INIT 0
#endif
int locktorture_runnable = LOCKTORTURE_RUNNABLE_INIT;
module_param(locktorture_runnable, int, 0444);
MODULE_PARM_DESC(locktorture_runnable, "Start locktorture at boot");

/* Forward reference. */
static void lock_torture_cleanup(void);

/*
 * Operations vector for selecting different types of tests.
 */
struct lock_torture_ops {
	void (*init)(void);
	int (*writelock)(void);
	void (*write_delay)(struct torture_random_state *trsp);
	void (*writeunlock)(void);
	unsigned long flags;
	const char *name;
};

static struct lock_torture_ops *cur_ops;

/*
 * Definitions for lock torture testing.
 */

static int torture_lock_busted_write_lock(void)
{
	return 0;  /* BUGGY, do not use in real life!!! */
}

static void torture_lock_busted_write_delay(struct torture_random_state *trsp)
{
	const unsigned long longdelay_us = 100;

	/* We want a long delay occasionally to force massive contention.  */
	if (!(torture_random(trsp) %
	      (nrealwriters_stress * 2000 * longdelay_us)))
		mdelay(longdelay_us);
#ifdef CONFIG_PREEMPT
	if (!(torture_random(trsp) % (nrealwriters_stress * 20000)))
		preempt_schedule();  /* Allow test to be preempted. */
#endif
}

static void torture_lock_busted_write_unlock(void)
{
	  /* BUGGY, do not use in real life!!! */
}

static struct lock_torture_ops lock_busted_ops = {
	.writelock	= torture_lock_busted_write_lock,
	.write_delay	= torture_lock_busted_write_delay,
	.writeunlock	= torture_lock_busted_write_unlock,
	.name		= "lock_busted"
};

static DEFINE_SPINLOCK(torture_spinlock);

static int torture_spin_lock_write_lock(void) __acquires(torture_spinlock)
{
	spin_lock(&torture_spinlock);
	return 0;
}

static void torture_spin_lock_write_delay(struct torture_random_state *trsp)
{
	const unsigned long shortdelay_us = 2;
	const unsigned long longdelay_us = 100;

	/* We want a short delay mostly to emulate likely code, and
	 * we want a long delay occasionally to force massive contention.
	 */
	if (!(torture_random(trsp) %
	      (nrealwriters_stress * 2000 * longdelay_us)))
		mdelay(longdelay_us);
	if (!(torture_random(trsp) %
	      (nrealwriters_stress * 2 * shortdelay_us)))
		udelay(shortdelay_us);
#ifdef CONFIG_PREEMPT
	if (!(torture_random(trsp) % (nrealwriters_stress * 20000)))
		preempt_schedule();  /* Allow test to be preempted. */
#endif
}

static void torture_spin_lock_write_unlock(void) __releases(torture_spinlock)
{
	spin_unlock(&torture_spinlock);
}

static struct lock_torture_ops spin_lock_ops = {
	.writelock	= torture_spin_lock_write_lock,
	.write_delay	= torture_spin_lock_write_delay,
	.writeunlock	= torture_spin_lock_write_unlock,
	.name		= "spin_lock"
};

static int torture_spin_lock_write_lock_irq(void)
__acquires(torture_spinlock_irq)
{
	unsigned long flags;

	spin_lock_irqsave(&torture_spinlock, flags);
	cur_ops->flags = flags;
	return 0;
}

static void torture_lock_spin_write_unlock_irq(void)
__releases(torture_spinlock)
{
	spin_unlock_irqrestore(&torture_spinlock, cur_ops->flags);
}

static struct lock_torture_ops spin_lock_irq_ops = {
	.writelock	= torture_spin_lock_write_lock_irq,
	.write_delay	= torture_spin_lock_write_delay,
	.writeunlock	= torture_lock_spin_write_unlock_irq,
	.name		= "spin_lock_irq"
};

/*
 * Lock torture writer kthread.  Repeatedly acquires and releases
 * the lock, checking for duplicate acquisitions.
 */
static int lock_torture_writer(void *arg)
{
	struct lock_writer_stress_stats *lwsp = arg;
	static DEFINE_TORTURE_RANDOM(rand);

	VERBOSE_TOROUT_STRING("lock_torture_writer task started");
	set_user_nice(current, 19);

	do {
		schedule_timeout_uninterruptible(1);
		cur_ops->writelock();
		if (WARN_ON_ONCE(lock_is_write_held))
			lwsp->n_write_lock_fail++;
		lock_is_write_held = 1;
		lwsp->n_write_lock_acquired++;
		cur_ops->write_delay(&rand);
		lock_is_write_held = 0;
		cur_ops->writeunlock();
		stutter_wait("lock_torture_writer");
	} while (!torture_must_stop());
	torture_kthread_stopping("lock_torture_writer");
	return 0;
}

/*
 * Create an lock-torture-statistics message in the specified buffer.
 */
static void lock_torture_printk(char *page)
{
	bool fail = 0;
	int i;
	long max = 0;
	long min = lwsa[0].n_write_lock_acquired;
	long long sum = 0;

	for (i = 0; i < nrealwriters_stress; i++) {
		if (lwsa[i].n_write_lock_fail)
			fail = true;
		sum += lwsa[i].n_write_lock_acquired;
		if (max < lwsa[i].n_write_lock_fail)
			max = lwsa[i].n_write_lock_fail;
		if (min > lwsa[i].n_write_lock_fail)
			min = lwsa[i].n_write_lock_fail;
	}
	page += sprintf(page, "%s%s ", torture_type, TORTURE_FLAG);
	page += sprintf(page,
			"Writes:  Total: %lld  Max/Min: %ld/%ld %s  Fail: %d %s\n",
			sum, max, min, max / 2 > min ? "???" : "",
			fail, fail ? "!!!" : "");
	if (fail)
		atomic_inc(&n_lock_torture_errors);
}

/*
 * Print torture statistics.  Caller must ensure that there is only one
 * call to this function at a given time!!!  This is normally accomplished
 * by relying on the module system to only have one copy of the module
 * loaded, and then by giving the lock_torture_stats kthread full control
 * (or the init/cleanup functions when lock_torture_stats thread is not
 * running).
 */
static void lock_torture_stats_print(void)
{
	int size = nrealwriters_stress * 200 + 8192;
	char *buf;

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf) {
		pr_err("lock_torture_stats_print: Out of memory, need: %d",
		       size);
		return;
	}
	lock_torture_printk(buf);
	pr_alert("%s", buf);
	kfree(buf);
}

/*
 * Periodically prints torture statistics, if periodic statistics printing
 * was specified via the stat_interval module parameter.
 *
 * No need to worry about fullstop here, since this one doesn't reference
 * volatile state or register callbacks.
 */
static int lock_torture_stats(void *arg)
{
	VERBOSE_TOROUT_STRING("lock_torture_stats task started");
	do {
		schedule_timeout_interruptible(stat_interval * HZ);
		lock_torture_stats_print();
		torture_shutdown_absorb("lock_torture_stats");
	} while (!torture_must_stop());
	torture_kthread_stopping("lock_torture_stats");
	return 0;
}

static inline void
lock_torture_print_module_parms(struct lock_torture_ops *cur_ops,
				const char *tag)
{
	pr_alert("%s" TORTURE_FLAG
		 "--- %s: nwriters_stress=%d stat_interval=%d verbose=%d shuffle_interval=%d stutter=%d shutdown_secs=%d onoff_interval=%d onoff_holdoff=%d\n",
		 torture_type, tag, nrealwriters_stress, stat_interval, verbose,
		 shuffle_interval, stutter, shutdown_secs,
		 onoff_interval, onoff_holdoff);
}

static void lock_torture_cleanup(void)
{
	int i;

	if (torture_cleanup())
		return;

	if (writer_tasks) {
		for (i = 0; i < nrealwriters_stress; i++)
			torture_stop_kthread(lock_torture_writer,
					     writer_tasks[i]);
		kfree(writer_tasks);
		writer_tasks = NULL;
	}

	torture_stop_kthread(lock_torture_stats, stats_task);
	lock_torture_stats_print();  /* -After- the stats thread is stopped! */

	if (atomic_read(&n_lock_torture_errors))
		lock_torture_print_module_parms(cur_ops,
						"End of test: FAILURE");
	else if (torture_onoff_failures())
		lock_torture_print_module_parms(cur_ops,
						"End of test: LOCK_HOTPLUG");
	else
		lock_torture_print_module_parms(cur_ops,
						"End of test: SUCCESS");
}

static int __init lock_torture_init(void)
{
	int i;
	int firsterr = 0;
	static struct lock_torture_ops *torture_ops[] = {
		&lock_busted_ops, &spin_lock_ops, &spin_lock_irq_ops,
	};

	torture_init_begin(torture_type, verbose, &locktorture_runnable);

	/* Process args and tell the world that the torturer is on the job. */
	for (i = 0; i < ARRAY_SIZE(torture_ops); i++) {
		cur_ops = torture_ops[i];
		if (strcmp(torture_type, cur_ops->name) == 0)
			break;
	}
	if (i == ARRAY_SIZE(torture_ops)) {
		pr_alert("lock-torture: invalid torture type: \"%s\"\n",
			 torture_type);
		pr_alert("lock-torture types:");
		for (i = 0; i < ARRAY_SIZE(torture_ops); i++)
			pr_alert(" %s", torture_ops[i]->name);
		pr_alert("\n");
		torture_init_end();
		return -EINVAL;
	}
	if (cur_ops->init)
		cur_ops->init(); /* no "goto unwind" prior to this point!!! */

	if (nwriters_stress >= 0)
		nrealwriters_stress = nwriters_stress;
	else
		nrealwriters_stress = 2 * num_online_cpus();
	lock_torture_print_module_parms(cur_ops, "Start of test");

	/* Initialize the statistics so that each run gets its own numbers. */

	lock_is_write_held = 0;
	lwsa = kmalloc(sizeof(*lwsa) * nrealwriters_stress, GFP_KERNEL);
	if (lwsa == NULL) {
		VERBOSE_TOROUT_STRING("lwsa: Out of memory");
		firsterr = -ENOMEM;
		goto unwind;
	}
	for (i = 0; i < nrealwriters_stress; i++) {
		lwsa[i].n_write_lock_fail = 0;
		lwsa[i].n_write_lock_acquired = 0;
	}

	/* Start up the kthreads. */

	if (onoff_interval > 0) {
		firsterr = torture_onoff_init(onoff_holdoff * HZ,
					      onoff_interval * HZ);
		if (firsterr)
			goto unwind;
	}
	if (shuffle_interval > 0) {
		firsterr = torture_shuffle_init(shuffle_interval);
		if (firsterr)
			goto unwind;
	}
	if (shutdown_secs > 0) {
		firsterr = torture_shutdown_init(shutdown_secs,
						 lock_torture_cleanup);
		if (firsterr)
			goto unwind;
	}
	if (stutter > 0) {
		firsterr = torture_stutter_init(stutter);
		if (firsterr)
			goto unwind;
	}

	writer_tasks = kzalloc(nrealwriters_stress * sizeof(writer_tasks[0]),
			       GFP_KERNEL);
	if (writer_tasks == NULL) {
		VERBOSE_TOROUT_ERRSTRING("writer_tasks: Out of memory");
		firsterr = -ENOMEM;
		goto unwind;
	}
	for (i = 0; i < nrealwriters_stress; i++) {
		firsterr = torture_create_kthread(lock_torture_writer, &lwsa[i],
						  writer_tasks[i]);
		if (firsterr)
			goto unwind;
	}
	if (stat_interval > 0) {
		firsterr = torture_create_kthread(lock_torture_stats, NULL,
						  stats_task);
		if (firsterr)
			goto unwind;
	}
	torture_init_end();
	return 0;

unwind:
	torture_init_end();
	lock_torture_cleanup();
	return firsterr;
}

module_init(lock_torture_init);
module_exit(lock_torture_cleanup);
