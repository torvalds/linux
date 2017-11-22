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
#include <linux/sched/clock.h>
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
#include <linux/ktime.h>
#include <asm/byteorder.h>
#include <linux/torture.h>
#include "rcu/rcu.h"

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
 * Attempt to take a CPU offline.  Return false if the CPU is already
 * offline or if it is not subject to CPU-hotplug operations.  The
 * caller can detect other failures by looking at the statistics.
 */
bool torture_offline(int cpu, long *n_offl_attempts, long *n_offl_successes,
		     unsigned long *sum_offl, int *min_offl, int *max_offl)
{
	unsigned long delta;
	int ret;
	unsigned long starttime;

	if (!cpu_online(cpu) || !cpu_is_hotpluggable(cpu))
		return false;

	if (verbose)
		pr_alert("%s" TORTURE_FLAG
			 "torture_onoff task: offlining %d\n",
			 torture_type, cpu);
	starttime = jiffies;
	(*n_offl_attempts)++;
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
		(*n_offl_successes)++;
		delta = jiffies - starttime;
		*sum_offl += delta;
		if (*min_offl < 0) {
			*min_offl = delta;
			*max_offl = delta;
		}
		if (*min_offl > delta)
			*min_offl = delta;
		if (*max_offl < delta)
			*max_offl = delta;
	}

	return true;
}
EXPORT_SYMBOL_GPL(torture_offline);

/*
 * Attempt to bring a CPU online.  Return false if the CPU is already
 * online or if it is not subject to CPU-hotplug operations.  The
 * caller can detect other failures by looking at the statistics.
 */
bool torture_online(int cpu, long *n_onl_attempts, long *n_onl_successes,
		    unsigned long *sum_onl, int *min_onl, int *max_onl)
{
	unsigned long delta;
	int ret;
	unsigned long starttime;

	if (cpu_online(cpu) || !cpu_is_hotpluggable(cpu))
		return false;

	if (verbose)
		pr_alert("%s" TORTURE_FLAG
			 "torture_onoff task: onlining %d\n",
			 torture_type, cpu);
	starttime = jiffies;
	(*n_onl_attempts)++;
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
		(*n_onl_successes)++;
		delta = jiffies - starttime;
		*sum_onl += delta;
		if (*min_onl < 0) {
			*min_onl = delta;
			*max_onl = delta;
		}
		if (*min_onl > delta)
			*min_onl = delta;
		if (*max_onl < delta)
			*max_onl = delta;
	}

	return true;
}
EXPORT_SYMBOL_GPL(torture_online);

/*
 * Execute random CPU-hotplug operations at the interval specified
 * by the onoff_interval.
 */
static int
torture_onoff(void *arg)
{
	int cpu;
	int maxcpu = -1;
	DEFINE_TORTURE_RANDOM(rand);

	VERBOSE_TOROUT_STRING("torture_onoff task started");
	for_each_online_cpu(cpu)
		maxcpu = cpu;
	WARN_ON(maxcpu < 0);

	if (maxcpu == 0) {
		VERBOSE_TOROUT_STRING("Only one CPU, so CPU-hotplug testing is disabled");
		goto stop;
	}

	if (onoff_holdoff > 0) {
		VERBOSE_TOROUT_STRING("torture_onoff begin holdoff");
		schedule_timeout_interruptible(onoff_holdoff);
		VERBOSE_TOROUT_STRING("torture_onoff end holdoff");
	}
	while (!torture_must_stop()) {
		cpu = (torture_random(&rand) >> 4) % (maxcpu + 1);
		if (!torture_offline(cpu,
				     &n_offline_attempts, &n_offline_successes,
				     &sum_offline, &min_offline, &max_offline))
			torture_online(cpu,
				       &n_online_attempts, &n_online_successes,
				       &sum_online, &min_online, &max_online);
		schedule_timeout_interruptible(onoff_interval);
	}

stop:
	torture_kthread_stopping("torture_onoff");
	return 0;
}

#endif /* #ifdef CONFIG_HOTPLUG_CPU */

/*
 * Initiate online-offline handling.
 */
int torture_onoff_init(long ooholdoff, long oointerval)
{
	int ret = 0;

#ifdef CONFIG_HOTPLUG_CPU
	onoff_holdoff = ooholdoff;
	onoff_interval = oointerval;
	if (onoff_interval <= 0)
		return 0;
	ret = torture_create_kthread(torture_onoff, NULL, onoff_task);
#endif /* #ifdef CONFIG_HOTPLUG_CPU */
	return ret;
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
void torture_onoff_stats(void)
{
#ifdef CONFIG_HOTPLUG_CPU
	pr_cont("onoff: %ld/%ld:%ld/%ld %d,%d:%d,%d %lu:%lu (HZ=%d) ",
		n_online_successes, n_online_attempts,
		n_offline_successes, n_offline_attempts,
		min_online, max_online,
		min_offline, max_offline,
		sum_online, sum_offline, HZ);
#endif /* #ifdef CONFIG_HOTPLUG_CPU */
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
 * as well as interactions with any per-CPU variables.
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
	else
		cpumask_clear_cpu(shuffle_idle_cpu, shuffle_tmp_mask);

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
	torture_kthread_stopping("torture_shuffle");
	return 0;
}

/*
 * Start the shuffler, with shuffint in jiffies.
 */
int torture_shuffle_init(long shuffint)
{
	shuffle_interval = shuffint;

	shuffle_idle_cpu = -1;

	if (!alloc_cpumask_var(&shuffle_tmp_mask, GFP_KERNEL)) {
		VERBOSE_TOROUT_ERRSTRING("Failed to alloc mask");
		return -ENOMEM;
	}

	/* Create the shuffler thread */
	return torture_create_kthread(torture_shuffle, NULL, shuffler_task);
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
 * Variables for auto-shutdown.  This allows "lights out" torture runs
 * to be fully scripted.
 */
static struct task_struct *shutdown_task;
static ktime_t shutdown_time;		/* time to system shutdown. */
static void (*torture_shutdown_hook)(void);

/*
 * Absorb kthreads into a kernel function that won't return, so that
 * they won't ever access module text or data again.
 */
void torture_shutdown_absorb(const char *title)
{
	while (READ_ONCE(fullstop) == FULLSTOP_SHUTDOWN) {
		pr_notice("torture thread %s parking due to system shutdown\n",
			  title);
		schedule_timeout_uninterruptible(MAX_SCHEDULE_TIMEOUT);
	}
}
EXPORT_SYMBOL_GPL(torture_shutdown_absorb);

/*
 * Cause the torture test to shutdown the system after the test has
 * run for the time specified by the shutdown_secs parameter.
 */
static int torture_shutdown(void *arg)
{
	ktime_t ktime_snap;

	VERBOSE_TOROUT_STRING("torture_shutdown task started");
	ktime_snap = ktime_get();
	while (ktime_before(ktime_snap, shutdown_time) &&
	       !torture_must_stop()) {
		if (verbose)
			pr_alert("%s" TORTURE_FLAG
				 "torture_shutdown task: %llu ms remaining\n",
				 torture_type,
				 ktime_ms_delta(shutdown_time, ktime_snap));
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_hrtimeout(&shutdown_time, HRTIMER_MODE_ABS);
		ktime_snap = ktime_get();
	}
	if (torture_must_stop()) {
		torture_kthread_stopping("torture_shutdown");
		return 0;
	}

	/* OK, shut down the system. */

	VERBOSE_TOROUT_STRING("torture_shutdown task shutting down system");
	shutdown_task = NULL;	/* Avoid self-kill deadlock. */
	if (torture_shutdown_hook)
		torture_shutdown_hook();
	else
		VERBOSE_TOROUT_STRING("No torture_shutdown_hook(), skipping.");
	rcu_ftrace_dump(DUMP_ALL);
	kernel_power_off();	/* Shut down the system. */
	return 0;
}

/*
 * Start up the shutdown task.
 */
int torture_shutdown_init(int ssecs, void (*cleanup)(void))
{
	int ret = 0;

	torture_shutdown_hook = cleanup;
	if (ssecs > 0) {
		shutdown_time = ktime_add(ktime_get(), ktime_set(ssecs, 0));
		ret = torture_create_kthread(torture_shutdown, NULL,
					     shutdown_task);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(torture_shutdown_init);

/*
 * Detect and respond to a system shutdown.
 */
static int torture_shutdown_notify(struct notifier_block *unused1,
				   unsigned long unused2, void *unused3)
{
	mutex_lock(&fullstop_mutex);
	if (READ_ONCE(fullstop) == FULLSTOP_DONTSTOP) {
		VERBOSE_TOROUT_STRING("Unscheduled system shutdown detected");
		WRITE_ONCE(fullstop, FULLSTOP_SHUTDOWN);
	} else {
		pr_warn("Concurrent rmmod and shutdown illegal!\n");
	}
	mutex_unlock(&fullstop_mutex);
	return NOTIFY_DONE;
}

static struct notifier_block torture_shutdown_nb = {
	.notifier_call = torture_shutdown_notify,
};

/*
 * Shut down the shutdown task.  Say what???  Heh!  This can happen if
 * the torture module gets an rmmod before the shutdown time arrives.  ;-)
 */
static void torture_shutdown_cleanup(void)
{
	unregister_reboot_notifier(&torture_shutdown_nb);
	if (shutdown_task != NULL) {
		VERBOSE_TOROUT_STRING("Stopping torture_shutdown task");
		kthread_stop(shutdown_task);
	}
	shutdown_task = NULL;
}

/*
 * Variables for stuttering, which means to periodically pause and
 * restart testing in order to catch bugs that appear when load is
 * suddenly applied to or removed from the system.
 */
static struct task_struct *stutter_task;
static int stutter_pause_test;
static int stutter;

/*
 * Block until the stutter interval ends.  This must be called periodically
 * by all running kthreads that need to be subject to stuttering.
 */
void stutter_wait(const char *title)
{
	int spt;

	cond_resched_rcu_qs();
	spt = READ_ONCE(stutter_pause_test);
	while (spt) {
		if (spt == 1) {
			schedule_timeout_interruptible(1);
		} else if (spt == 2) {
			while (READ_ONCE(stutter_pause_test))
				cond_resched();
		} else {
			schedule_timeout_interruptible(round_jiffies_relative(HZ));
		}
		torture_shutdown_absorb(title);
		spt = READ_ONCE(stutter_pause_test);
	}
}
EXPORT_SYMBOL_GPL(stutter_wait);

/*
 * Cause the torture test to "stutter", starting and stopping all
 * threads periodically.
 */
static int torture_stutter(void *arg)
{
	VERBOSE_TOROUT_STRING("torture_stutter task started");
	do {
		if (!torture_must_stop() && stutter > 1) {
			WRITE_ONCE(stutter_pause_test, 1);
			schedule_timeout_interruptible(stutter - 1);
			WRITE_ONCE(stutter_pause_test, 2);
			schedule_timeout_interruptible(1);
		}
		WRITE_ONCE(stutter_pause_test, 0);
		if (!torture_must_stop())
			schedule_timeout_interruptible(stutter);
		torture_shutdown_absorb("torture_stutter");
	} while (!torture_must_stop());
	torture_kthread_stopping("torture_stutter");
	return 0;
}

/*
 * Initialize and kick off the torture_stutter kthread.
 */
int torture_stutter_init(int s)
{
	int ret;

	stutter = s;
	ret = torture_create_kthread(torture_stutter, NULL, stutter_task);
	return ret;
}
EXPORT_SYMBOL_GPL(torture_stutter_init);

/*
 * Cleanup after the torture_stutter kthread.
 */
static void torture_stutter_cleanup(void)
{
	if (!stutter_task)
		return;
	VERBOSE_TOROUT_STRING("Stopping torture_stutter task");
	kthread_stop(stutter_task);
	stutter_task = NULL;
}

/*
 * Initialize torture module.  Please note that this is -not- invoked via
 * the usual module_init() mechanism, but rather by an explicit call from
 * the client torture module.  This call must be paired with a later
 * torture_init_end().
 *
 * The runnable parameter points to a flag that controls whether or not
 * the test is currently runnable.  If there is no such flag, pass in NULL.
 */
bool torture_init_begin(char *ttype, bool v)
{
	mutex_lock(&fullstop_mutex);
	if (torture_type != NULL) {
		pr_alert("torture_init_begin: Refusing %s init: %s running.\n",
			 ttype, torture_type);
		pr_alert("torture_init_begin: One torture test at a time!\n");
		mutex_unlock(&fullstop_mutex);
		return false;
	}
	torture_type = ttype;
	verbose = v;
	fullstop = FULLSTOP_DONTSTOP;
	return true;
}
EXPORT_SYMBOL_GPL(torture_init_begin);

/*
 * Tell the torture module that initialization is complete.
 */
void torture_init_end(void)
{
	mutex_unlock(&fullstop_mutex);
	register_reboot_notifier(&torture_shutdown_nb);
}
EXPORT_SYMBOL_GPL(torture_init_end);

/*
 * Clean up torture module.  Please note that this is -not- invoked via
 * the usual module_exit() mechanism, but rather by an explicit call from
 * the client torture module.  Returns true if a race with system shutdown
 * is detected, otherwise, all kthreads started by functions in this file
 * will be shut down.
 *
 * This must be called before the caller starts shutting down its own
 * kthreads.
 *
 * Both torture_cleanup_begin() and torture_cleanup_end() must be paired,
 * in order to correctly perform the cleanup. They are separated because
 * threads can still need to reference the torture_type type, thus nullify
 * only after completing all other relevant calls.
 */
bool torture_cleanup_begin(void)
{
	mutex_lock(&fullstop_mutex);
	if (READ_ONCE(fullstop) == FULLSTOP_SHUTDOWN) {
		pr_warn("Concurrent rmmod and shutdown illegal!\n");
		mutex_unlock(&fullstop_mutex);
		schedule_timeout_uninterruptible(10);
		return true;
	}
	WRITE_ONCE(fullstop, FULLSTOP_RMMOD);
	mutex_unlock(&fullstop_mutex);
	torture_shutdown_cleanup();
	torture_shuffle_cleanup();
	torture_stutter_cleanup();
	torture_onoff_cleanup();
	return false;
}
EXPORT_SYMBOL_GPL(torture_cleanup_begin);

void torture_cleanup_end(void)
{
	mutex_lock(&fullstop_mutex);
	torture_type = NULL;
	mutex_unlock(&fullstop_mutex);
}
EXPORT_SYMBOL_GPL(torture_cleanup_end);

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
	return READ_ONCE(fullstop) != FULLSTOP_DONTSTOP;
}
EXPORT_SYMBOL_GPL(torture_must_stop_irq);

/*
 * Each kthread must wait for kthread_should_stop() before returning from
 * its top-level function, otherwise segfaults ensue.  This function
 * prints a "stopping" message and waits for kthread_should_stop(), and
 * should be called from all torture kthreads immediately prior to
 * returning.
 */
void torture_kthread_stopping(char *title)
{
	char buf[128];

	snprintf(buf, sizeof(buf), "Stopping %s", title);
	VERBOSE_TOROUT_STRING(buf);
	while (!kthread_should_stop()) {
		torture_shutdown_absorb(title);
		schedule_timeout_uninterruptible(1);
	}
}
EXPORT_SYMBOL_GPL(torture_kthread_stopping);

/*
 * Create a generic torture kthread that is immediately runnable.  If you
 * need the kthread to be stopped so that you can do something to it before
 * it starts, you will need to open-code your own.
 */
int _torture_create_kthread(int (*fn)(void *arg), void *arg, char *s, char *m,
			    char *f, struct task_struct **tp)
{
	int ret = 0;

	VERBOSE_TOROUT_STRING(m);
	*tp = kthread_run(fn, arg, "%s", s);
	if (IS_ERR(*tp)) {
		ret = PTR_ERR(*tp);
		VERBOSE_TOROUT_ERRSTRING(f);
		*tp = NULL;
	}
	torture_shuffle_task_register(*tp);
	return ret;
}
EXPORT_SYMBOL_GPL(_torture_create_kthread);

/*
 * Stop a generic kthread, emitting a message.
 */
void _torture_stop_kthread(char *m, struct task_struct **tp)
{
	if (*tp == NULL)
		return;
	VERBOSE_TOROUT_STRING(m);
	kthread_stop(*tp);
	*tp = NULL;
}
EXPORT_SYMBOL_GPL(_torture_stop_kthread);
