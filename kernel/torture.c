/*
 * Common functions for in-kernel torture tests.
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

static char *torture_type;
static bool verbose;

/* Mediate rmmod and system shutdown.  Concurrent rmmod & shutdown illegal! */
#define FULLSTOP_DONTSTOP 0	/* Normal operation. */
#define FULLSTOP_SHUTDOWN 1	/* System shutdown with torture running. */
#define FULLSTOP_RMMOD    2	/* Normal rmmod of torture. */
static int fullstop = FULLSTOP_RMMOD;
static DEFINE_MUTEX(fullstop_mutex);

#ifdef CONFIG_HOTPLUG_CPU

/*
 * Variables for online-offline handling.  Only present if CPU hotplug
 * is enabled, otherwise does nothing.
 */

static struct task_struct *onoff_task;
static long onoff_holdoff;
static long onoff_interval;
static long n_offline_attempts;
static long n_offline_successes;
static unsigned long sum_offline;
static int min_offline = -1;
static int max_offline;
static long n_online_attempts;
static long n_online_successes;
static unsigned long sum_online;
static int min_online = -1;
static int max_online;

/*
 * Execute random CPU-hotplug operations at the interval specified
 * by the onoff_interval.
 */
static int
torture_onoff(void *arg)
{
	int cpu;
	unsigned long delta;
	int maxcpu = -1;
	DEFINE_TORTURE_RANDOM(rand);
	int ret;
	unsigned long starttime;

	VERBOSE_TOROUT_STRING("torture_onoff task started");
	for_each_online_cpu(cpu)
		maxcpu = cpu;
	WARN_ON(maxcpu < 0);
	if (onoff_holdoff > 0) {
		VERBOSE_TOROUT_STRING("torture_onoff begin holdoff");
		schedule_timeout_interruptible(onoff_holdoff);
		VERBOSE_TOROUT_STRING("torture_onoff end holdoff");
	}
	while (!torture_must_stop()) {
		cpu = (torture_random(&rand) >> 4) % (maxcpu + 1);
		if (cpu_online(cpu) && cpu_is_hotpluggable(cpu)) {
			if (verbose)
				pr_alert("%s" TORTURE_FLAG
					 "torture_onoff task: offlining %d\n",
					 torture_type, cpu);
			starttime = jiffies;
			n_offline_attempts++;
			ret = cpu_down(cpu);
			if (ret) {
				if (verbose)
					pr_alert("%s" TORTURE_FLAG
						 "torture_onoff task: offline %d failed: errno %d\n",
						 torture_type, cpu, ret);
			} else {
				if (verbose)
					pr_alert("%s" TORTURE_FLAG
						 "torture_onoff task: offlined %d\n",
						 torture_type, cpu);
				n_offline_successes++;
				delta = jiffies - starttime;
				sum_offline += delta;
				if (min_offline < 0) {
					min_offline = delta;
					max_offline = delta;
				}
				if (min_offline > delta)
					min_offline = delta;
				if (max_offline < delta)
					max_offline = delta;
			}
		} else if (cpu_is_hotpluggable(cpu)) {
			if (verbose)
				pr_alert("%s" TORTURE_FLAG
					 "torture_onoff task: onlining %d\n",
					 torture_type, cpu);
			starttime = jiffies;
			n_online_attempts++;
			ret = cpu_up(cpu);
			if (ret) {
				if (verbose)
					pr_alert("%s" TORTURE_FLAG
						 "torture_onoff task: online %d failed: errno %d\n",
						 torture_type, cpu, ret);
			} else {
				if (verbose)
					pr_alert("%s" TORTURE_FLAG
						 "torture_onoff task: onlined %d\n",
						 torture_type, cpu);
				n_online_successes++;
				delta = jiffies - starttime;
				sum_online += delta;
				if (min_online < 0) {
					min_online = delta;
					max_online = delta;
				}
				if (min_online > delta)
					min_online = delta;
				if (max_online < delta)
					max_online = delta;
			}
		}
		schedule_timeout_interruptible(onoff_interval);
	}
	VERBOSE_TOROUT_STRING("torture_onoff task stopping");
	return 0;
}

#endif /* #ifdef CONFIG_HOTPLUG_CPU */

/*
 * Initiate online-offline handling.
 */
int torture_onoff_init(long ooholdoff, long oointerval)
{
#ifdef CONFIG_HOTPLUG_CPU
	int ret;

	onoff_holdoff = ooholdoff;
	onoff_interval = oointerval;
	if (onoff_interval <= 0)
		return 0;
	onoff_task = kthread_run(torture_onoff, NULL, "torture_onoff");
	if (IS_ERR(onoff_task)) {
		ret = PTR_ERR(onoff_task);
		onoff_task = NULL;
		return ret;
	}
	torture_shuffle_task_register(onoff_task);
#endif /* #ifdef CONFIG_HOTPLUG_CPU */
	return 0;
}
EXPORT_SYMBOL_GPL(torture_onoff_init);

/*
 * Clean up after online/offline testing.
 */
static void torture_onoff_cleanup(void)
{
#ifdef CONFIG_HOTPLUG_CPU
	if (onoff_task == NULL)
		return;
	VERBOSE_TOROUT_STRING("Stopping torture_onoff task");
	kthread_stop(onoff_task);
	onoff_task = NULL;
#endif /* #ifdef CONFIG_HOTPLUG_CPU */
}
EXPORT_SYMBOL_GPL(torture_onoff_cleanup);

/*
 * Print online/offline testing statistics.
 */
char *torture_onoff_stats(char *page)
{
#ifdef CONFIG_HOTPLUG_CPU
	page += sprintf(page,
		       "onoff: %ld/%ld:%ld/%ld %d,%d:%d,%d %lu:%lu (HZ=%d) ",
		       n_online_successes, n_online_attempts,
		       n_offline_successes, n_offline_attempts,
		       min_online, max_online,
		       min_offline, max_offline,
		       sum_online, sum_offline, HZ);
#endif /* #ifdef CONFIG_HOTPLUG_CPU */
	return page;
}
EXPORT_SYMBOL_GPL(torture_onoff_stats);

/*
 * Were all the online/offline operations successful?
 */
bool torture_onoff_failures(void)
{
#ifdef CONFIG_HOTPLUG_CPU
	return n_online_successes != n_online_attempts ||
	       n_offline_successes != n_offline_attempts;
#else /* #ifdef CONFIG_HOTPLUG_CPU */
	return false;
#endif /* #else #ifdef CONFIG_HOTPLUG_CPU */
}
EXPORT_SYMBOL_GPL(torture_onoff_failures);

#define TORTURE_RANDOM_MULT	39916801  /* prime */
#define TORTURE_RANDOM_ADD	479001701 /* prime */
#define TORTURE_RANDOM_REFRESH	10000

/*
 * Crude but fast random-number generator.  Uses a linear congruential
 * generator, with occasional help from cpu_clock().
 */
unsigned long
torture_random(struct torture_random_state *trsp)
{
	if (--trsp->trs_count < 0) {
		trsp->trs_state += (unsigned long)local_clock();
		trsp->trs_count = TORTURE_RANDOM_REFRESH;
	}
	trsp->trs_state = trsp->trs_state * TORTURE_RANDOM_MULT +
		TORTURE_RANDOM_ADD;
	return swahw32(trsp->trs_state);
}
EXPORT_SYMBOL_GPL(torture_random);

/*
 * Variables for shuffling.  The idea is to ensure that each CPU stays
 * idle for an extended period to test interactions with dyntick idle,
 * as well as interactions with any per-CPU varibles.
 */
struct shuffle_task {
	struct list_head st_l;
	struct task_struct *st_t;
};

static long shuffle_interval;	/* In jiffies. */
static struct task_struct *shuffler_task;
static cpumask_var_t shuffle_tmp_mask;
static int shuffle_idle_cpu;	/* Force all torture tasks off this CPU */
static struct list_head shuffle_task_list = LIST_HEAD_INIT(shuffle_task_list);
static DEFINE_MUTEX(shuffle_task_mutex);

/*
 * Register a task to be shuffled.  If there is no memory, just splat
 * and don't bother registering.
 */
void torture_shuffle_task_register(struct task_struct *tp)
{
	struct shuffle_task *stp;

	if (WARN_ON_ONCE(tp == NULL))
		return;
	stp = kmalloc(sizeof(*stp), GFP_KERNEL);
	if (WARN_ON_ONCE(stp == NULL))
		return;
	stp->st_t = tp;
	mutex_lock(&shuffle_task_mutex);
	list_add(&stp->st_l, &shuffle_task_list);
	mutex_unlock(&shuffle_task_mutex);
}
EXPORT_SYMBOL_GPL(torture_shuffle_task_register);

/*
 * Unregister all tasks, for example, at the end of the torture run.
 */
static void torture_shuffle_task_unregister_all(void)
{
	struct shuffle_task *stp;
	struct shuffle_task *p;

	mutex_lock(&shuffle_task_mutex);
	list_for_each_entry_safe(stp, p, &shuffle_task_list, st_l) {
		list_del(&stp->st_l);
		kfree(stp);
	}
	mutex_unlock(&shuffle_task_mutex);
}

/* Shuffle tasks such that we allow shuffle_idle_cpu to become idle.
 * A special case is when shuffle_idle_cpu = -1, in which case we allow
 * the tasks to run on all CPUs.
 */
static void torture_shuffle_tasks(void)
{
	struct shuffle_task *stp;

	cpumask_setall(shuffle_tmp_mask);
	get_online_cpus();

	/* No point in shuffling if there is only one online CPU (ex: UP) */
	if (num_online_cpus() == 1) {
		put_online_cpus();
		return;
	}

	/* Advance to the next CPU.  Upon overflow, don't idle any CPUs. */
	shuffle_idle_cpu = cpumask_next(shuffle_idle_cpu, shuffle_tmp_mask);
	if (shuffle_idle_cpu >= nr_cpu_ids)
		shuffle_idle_cpu = -1;
	if (shuffle_idle_cpu != -1) {
		cpumask_clear_cpu(shuffle_idle_cpu, shuffle_tmp_mask);
		if (cpumask_empty(shuffle_tmp_mask)) {
			put_online_cpus();
			return;
		}
	}

	mutex_lock(&shuffle_task_mutex);
	list_for_each_entry(stp, &shuffle_task_list, st_l)
		set_cpus_allowed_ptr(stp->st_t, shuffle_tmp_mask);
	mutex_unlock(&shuffle_task_mutex);

	put_online_cpus();
}

/* Shuffle tasks across CPUs, with the intent of allowing each CPU in the
 * system to become idle at a time and cut off its timer ticks. This is meant
 * to test the support for such tickless idle CPU in RCU.
 */
static int torture_shuffle(void *arg)
{
	VERBOSE_TOROUT_STRING("torture_shuffle task started");
	do {
		schedule_timeout_interruptible(shuffle_interval);
		torture_shuffle_tasks();
		torture_shutdown_absorb("torture_shuffle");
	} while (!torture_must_stop());
	VERBOSE_TOROUT_STRING("torture_shuffle task stopping");
	return 0;
}

/*
 * Start the shuffler, with shuffint in jiffies.
 */
int torture_shuffle_init(long shuffint)
{
	int ret;

	shuffle_interval = shuffint;

	shuffle_idle_cpu = -1;

	if (!alloc_cpumask_var(&shuffle_tmp_mask, GFP_KERNEL)) {
		VERBOSE_TOROUT_ERRSTRING("Failed to alloc mask");
		return -ENOMEM;
	}

	/* Create the shuffler thread */
	shuffler_task = kthread_run(torture_shuffle, NULL, "torture_shuffle");
	if (IS_ERR(shuffler_task)) {
		ret = PTR_ERR(shuffler_task);
		free_cpumask_var(shuffle_tmp_mask);
		VERBOSE_TOROUT_ERRSTRING("Failed to create shuffler");
		shuffler_task = NULL;
		return ret;
	}
	torture_shuffle_task_register(shuffler_task);
	return 0;
}
EXPORT_SYMBOL_GPL(torture_shuffle_init);

/*
 * Stop the shuffling.
 */
static void torture_shuffle_cleanup(void)
{
	torture_shuffle_task_unregister_all();
	if (shuffler_task) {
		VERBOSE_TOROUT_STRING("Stopping torture_shuffle task");
		kthread_stop(shuffler_task);
		free_cpumask_var(shuffle_tmp_mask);
	}
	shuffler_task = NULL;
}
EXPORT_SYMBOL_GPL(torture_shuffle_cleanup);

/*
 * Absorb kthreads into a kernel function that won't return, so that
 * they won't ever access module text or data again.
 */
void torture_shutdown_absorb(const char *title)
{
	while (ACCESS_ONCE(fullstop) == FULLSTOP_SHUTDOWN) {
		pr_notice("torture thread %s parking due to system shutdown\n",
			  title);
		schedule_timeout_uninterruptible(MAX_SCHEDULE_TIMEOUT);
	}
}
EXPORT_SYMBOL_GPL(torture_shutdown_absorb);

/*
 * Detect and respond to a system shutdown.
 */
static int torture_shutdown_notify(struct notifier_block *unused1,
				   unsigned long unused2, void *unused3)
{
	mutex_lock(&fullstop_mutex);
	if (fullstop == FULLSTOP_DONTSTOP)
		fullstop = FULLSTOP_SHUTDOWN;
	else
		pr_warn("Concurrent rmmod and shutdown illegal!\n");
	mutex_unlock(&fullstop_mutex);
	return NOTIFY_DONE;
}

static struct notifier_block torture_shutdown_nb = {
	.notifier_call = torture_shutdown_notify,
};

/*
 * Initialize torture module.  Please note that this is -not- invoked via
 * the usual module_init() mechanism, but rather by an explicit call from
 * the client torture module.  This call must be paired with a later
 * torture_init_end().
 */
void __init torture_init_begin(char *ttype, bool v)
{
	mutex_lock(&fullstop_mutex);
	torture_type = ttype;
	verbose = v;
	fullstop = FULLSTOP_DONTSTOP;

}
EXPORT_SYMBOL_GPL(torture_init_begin);

/*
 * Tell the torture module that initialization is complete.
 */
void __init torture_init_end(void)
{
	mutex_unlock(&fullstop_mutex);
	register_reboot_notifier(&torture_shutdown_nb);
}
EXPORT_SYMBOL_GPL(torture_init_end);

/*
 * Clean up torture module.  Please note that this is -not- invoked via
 * the usual module_exit() mechanism, but rather by an explicit call from
 * the client torture module.  Returns true if a race with system shutdown
 * is detected.
 *
 * This must be called before the caller starts shutting down its own
 * kthreads.
 */
bool torture_cleanup(void)
{
	mutex_lock(&fullstop_mutex);
	if (fullstop == FULLSTOP_SHUTDOWN) {
		pr_warn("Concurrent rmmod and shutdown illegal!\n");
		mutex_unlock(&fullstop_mutex);
		schedule_timeout_uninterruptible(10);
		return true;
	}
	fullstop = FULLSTOP_RMMOD;
	mutex_unlock(&fullstop_mutex);
	unregister_reboot_notifier(&torture_shutdown_nb);
	torture_shuffle_cleanup();
	torture_onoff_cleanup();
	return false;
}
EXPORT_SYMBOL_GPL(torture_cleanup);

/*
 * Is it time for the current torture test to stop?
 */
bool torture_must_stop(void)
{
	return torture_must_stop_irq() || kthread_should_stop();
}
EXPORT_SYMBOL_GPL(torture_must_stop);

/*
 * Is it time for the current torture test to stop?  This is the irq-safe
 * version, hence no check for kthread_should_stop().
 */
bool torture_must_stop_irq(void)
{
	return fullstop != FULLSTOP_DONTSTOP;
}
EXPORT_SYMBOL_GPL(torture_must_stop_irq);
