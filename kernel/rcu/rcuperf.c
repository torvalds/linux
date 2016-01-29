/*
 * Read-Copy Update module-based performance-test facility
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
 * Copyright (C) IBM Corporation, 2015
 *
 * Authors: Paul E. McKenney <paulmck@us.ibm.com>
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
#include <asm/byteorder.h>
#include <linux/torture.h>
#include <linux/vmalloc.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Paul E. McKenney <paulmck@linux.vnet.ibm.com>");

#define PERF_FLAG "-perf:"
#define PERFOUT_STRING(s) \
	pr_alert("%s" PERF_FLAG s "\n", perf_type)
#define VERBOSE_PERFOUT_STRING(s) \
	do { if (verbose) pr_alert("%s" PERF_FLAG " %s\n", perf_type, s); } while (0)
#define VERBOSE_PERFOUT_ERRSTRING(s) \
	do { if (verbose) pr_alert("%s" PERF_FLAG "!!! %s\n", perf_type, s); } while (0)

torture_param(bool, gp_exp, true, "Use expedited GP wait primitives");
torture_param(int, nreaders, -1, "Number of RCU reader threads");
torture_param(int, nwriters, -1, "Number of RCU updater threads");
torture_param(bool, shutdown, false, "Shutdown at end of performance tests.");
torture_param(bool, verbose, true, "Enable verbose debugging printk()s");

static char *perf_type = "rcu";
module_param(perf_type, charp, 0444);
MODULE_PARM_DESC(perf_type, "Type of RCU to performance-test (rcu, rcu_bh, ...)");

static int nrealreaders;
static int nrealwriters;
static struct task_struct **writer_tasks;
static struct task_struct **reader_tasks;
static struct task_struct *shutdown_task;

static u64 **writer_durations;
static int *writer_n_durations;
static atomic_t n_rcu_perf_reader_started;
static atomic_t n_rcu_perf_writer_started;
static atomic_t n_rcu_perf_writer_finished;
static wait_queue_head_t shutdown_wq;
static u64 t_rcu_perf_writer_started;
static u64 t_rcu_perf_writer_finished;
static unsigned long b_rcu_perf_writer_started;
static unsigned long b_rcu_perf_writer_finished;

static int rcu_perf_writer_state;
#define RTWS_INIT		0
#define RTWS_EXP_SYNC		1
#define RTWS_SYNC		2
#define RTWS_IDLE		2
#define RTWS_STOPPING		3

#define MAX_MEAS 10000
#define MIN_MEAS 100

#if defined(MODULE) || defined(CONFIG_RCU_PERF_TEST_RUNNABLE)
#define RCUPERF_RUNNABLE_INIT 1
#else
#define RCUPERF_RUNNABLE_INIT 0
#endif
static int perf_runnable = RCUPERF_RUNNABLE_INIT;
module_param(perf_runnable, int, 0444);
MODULE_PARM_DESC(perf_runnable, "Start rcuperf at boot");

/*
 * Operations vector for selecting different types of tests.
 */

struct rcu_perf_ops {
	int ptype;
	void (*init)(void);
	void (*cleanup)(void);
	int (*readlock)(void);
	void (*readunlock)(int idx);
	unsigned long (*started)(void);
	unsigned long (*completed)(void);
	unsigned long (*exp_completed)(void);
	void (*sync)(void);
	void (*exp_sync)(void);
	const char *name;
};

static struct rcu_perf_ops *cur_ops;

/*
 * Definitions for rcu perf testing.
 */

static int rcu_perf_read_lock(void) __acquires(RCU)
{
	rcu_read_lock();
	return 0;
}

static void rcu_perf_read_unlock(int idx) __releases(RCU)
{
	rcu_read_unlock();
}

static unsigned long __maybe_unused rcu_no_completed(void)
{
	return 0;
}

static void rcu_sync_perf_init(void)
{
}

static struct rcu_perf_ops rcu_ops = {
	.ptype		= RCU_FLAVOR,
	.init		= rcu_sync_perf_init,
	.readlock	= rcu_perf_read_lock,
	.readunlock	= rcu_perf_read_unlock,
	.started	= rcu_batches_started,
	.completed	= rcu_batches_completed,
	.exp_completed	= rcu_exp_batches_completed,
	.sync		= synchronize_rcu,
	.exp_sync	= synchronize_rcu_expedited,
	.name		= "rcu"
};

/*
 * Definitions for rcu_bh perf testing.
 */

static int rcu_bh_perf_read_lock(void) __acquires(RCU_BH)
{
	rcu_read_lock_bh();
	return 0;
}

static void rcu_bh_perf_read_unlock(int idx) __releases(RCU_BH)
{
	rcu_read_unlock_bh();
}

static struct rcu_perf_ops rcu_bh_ops = {
	.ptype		= RCU_BH_FLAVOR,
	.init		= rcu_sync_perf_init,
	.readlock	= rcu_bh_perf_read_lock,
	.readunlock	= rcu_bh_perf_read_unlock,
	.started	= rcu_batches_started_bh,
	.completed	= rcu_batches_completed_bh,
	.exp_completed	= rcu_exp_batches_completed_sched,
	.sync		= synchronize_rcu_bh,
	.exp_sync	= synchronize_rcu_bh_expedited,
	.name		= "rcu_bh"
};

/*
 * Definitions for srcu perf testing.
 */

DEFINE_STATIC_SRCU(srcu_ctl_perf);
static struct srcu_struct *srcu_ctlp = &srcu_ctl_perf;

static int srcu_perf_read_lock(void) __acquires(srcu_ctlp)
{
	return srcu_read_lock(srcu_ctlp);
}

static void srcu_perf_read_unlock(int idx) __releases(srcu_ctlp)
{
	srcu_read_unlock(srcu_ctlp, idx);
}

static unsigned long srcu_perf_completed(void)
{
	return srcu_batches_completed(srcu_ctlp);
}

static void srcu_perf_synchronize(void)
{
	synchronize_srcu(srcu_ctlp);
}

static void srcu_perf_synchronize_expedited(void)
{
	synchronize_srcu_expedited(srcu_ctlp);
}

static struct rcu_perf_ops srcu_ops = {
	.ptype		= SRCU_FLAVOR,
	.init		= rcu_sync_perf_init,
	.readlock	= srcu_perf_read_lock,
	.readunlock	= srcu_perf_read_unlock,
	.started	= NULL,
	.completed	= srcu_perf_completed,
	.exp_completed	= srcu_perf_completed,
	.sync		= srcu_perf_synchronize,
	.exp_sync	= srcu_perf_synchronize_expedited,
	.name		= "srcu"
};

/*
 * Definitions for sched perf testing.
 */

static int sched_perf_read_lock(void)
{
	preempt_disable();
	return 0;
}

static void sched_perf_read_unlock(int idx)
{
	preempt_enable();
}

static struct rcu_perf_ops sched_ops = {
	.ptype		= RCU_SCHED_FLAVOR,
	.init		= rcu_sync_perf_init,
	.readlock	= sched_perf_read_lock,
	.readunlock	= sched_perf_read_unlock,
	.started	= rcu_batches_started_sched,
	.completed	= rcu_batches_completed_sched,
	.exp_completed	= rcu_exp_batches_completed_sched,
	.sync		= synchronize_sched,
	.exp_sync	= synchronize_sched_expedited,
	.name		= "sched"
};

#ifdef CONFIG_TASKS_RCU

/*
 * Definitions for RCU-tasks perf testing.
 */

static int tasks_perf_read_lock(void)
{
	return 0;
}

static void tasks_perf_read_unlock(int idx)
{
}

static struct rcu_perf_ops tasks_ops = {
	.ptype		= RCU_TASKS_FLAVOR,
	.init		= rcu_sync_perf_init,
	.readlock	= tasks_perf_read_lock,
	.readunlock	= tasks_perf_read_unlock,
	.started	= rcu_no_completed,
	.completed	= rcu_no_completed,
	.sync		= synchronize_rcu_tasks,
	.exp_sync	= synchronize_rcu_tasks,
	.name		= "tasks"
};

#define RCUPERF_TASKS_OPS &tasks_ops,

static bool __maybe_unused torturing_tasks(void)
{
	return cur_ops == &tasks_ops;
}

#else /* #ifdef CONFIG_TASKS_RCU */

#define RCUPERF_TASKS_OPS

static bool __maybe_unused torturing_tasks(void)
{
	return false;
}

#endif /* #else #ifdef CONFIG_TASKS_RCU */

/*
 * If performance tests complete, wait for shutdown to commence.
 */
static void rcu_perf_wait_shutdown(void)
{
	cond_resched_rcu_qs();
	if (atomic_read(&n_rcu_perf_writer_finished) < nrealwriters)
		return;
	while (!torture_must_stop())
		schedule_timeout_uninterruptible(1);
}

/*
 * RCU perf reader kthread.  Repeatedly does empty RCU read-side
 * critical section, minimizing update-side interference.
 */
static int
rcu_perf_reader(void *arg)
{
	unsigned long flags;
	int idx;
	long me = (long)arg;

	VERBOSE_PERFOUT_STRING("rcu_perf_reader task started");
	set_cpus_allowed_ptr(current, cpumask_of(me % nr_cpu_ids));
	set_user_nice(current, MAX_NICE);
	atomic_inc(&n_rcu_perf_reader_started);

	do {
		local_irq_save(flags);
		idx = cur_ops->readlock();
		cur_ops->readunlock(idx);
		local_irq_restore(flags);
		rcu_perf_wait_shutdown();
	} while (!torture_must_stop());
	torture_kthread_stopping("rcu_perf_reader");
	return 0;
}

/*
 * RCU perf writer kthread.  Repeatedly does a grace period.
 */
static int
rcu_perf_writer(void *arg)
{
	int i = 0;
	int i_max;
	long me = (long)arg;
	struct sched_param sp;
	bool started = false, done = false, alldone = false;
	u64 t;
	u64 *wdp;
	u64 *wdpp = writer_durations[me];

	VERBOSE_PERFOUT_STRING("rcu_perf_writer task started");
	WARN_ON(rcu_gp_is_expedited() && !rcu_gp_is_normal() && !gp_exp);
	WARN_ON(rcu_gp_is_normal() && gp_exp);
	WARN_ON(!wdpp);
	set_cpus_allowed_ptr(current, cpumask_of(me % nr_cpu_ids));
	sp.sched_priority = 1;
	sched_setscheduler_nocheck(current, SCHED_FIFO, &sp);
	t = ktime_get_mono_fast_ns();
	if (atomic_inc_return(&n_rcu_perf_writer_started) >= nrealwriters) {
		t_rcu_perf_writer_started = t;
		if (gp_exp) {
			b_rcu_perf_writer_started =
				cur_ops->exp_completed() / 2;
		} else {
			b_rcu_perf_writer_started =
				cur_ops->completed();
		}
	}

	do {
		wdp = &wdpp[i];
		*wdp = ktime_get_mono_fast_ns();
		if (gp_exp) {
			rcu_perf_writer_state = RTWS_EXP_SYNC;
			cur_ops->exp_sync();
		} else {
			rcu_perf_writer_state = RTWS_SYNC;
			cur_ops->sync();
		}
		rcu_perf_writer_state = RTWS_IDLE;
		t = ktime_get_mono_fast_ns();
		*wdp = t - *wdp;
		i_max = i;
		if (!started &&
		    atomic_read(&n_rcu_perf_writer_started) >= nrealwriters)
			started = true;
		if (!done && i >= MIN_MEAS) {
			done = true;
			pr_alert("%s" PERF_FLAG
				 "rcu_perf_writer %ld has %d measurements\n",
				 perf_type, me, MIN_MEAS);
			if (atomic_inc_return(&n_rcu_perf_writer_finished) >=
			    nrealwriters) {
				rcu_ftrace_dump(DUMP_ALL);
				PERFOUT_STRING("Test complete");
				t_rcu_perf_writer_finished = t;
				if (gp_exp) {
					b_rcu_perf_writer_finished =
						cur_ops->exp_completed() / 2;
				} else {
					b_rcu_perf_writer_finished =
						cur_ops->completed();
				}
				smp_mb(); /* Assign before wake. */
				wake_up(&shutdown_wq);
			}
		}
		if (done && !alldone &&
		    atomic_read(&n_rcu_perf_writer_finished) >= nrealwriters)
			alldone = true;
		if (started && !alldone && i < MAX_MEAS - 1)
			i++;
		rcu_perf_wait_shutdown();
	} while (!torture_must_stop());
	rcu_perf_writer_state = RTWS_STOPPING;
	writer_n_durations[me] = i_max;
	torture_kthread_stopping("rcu_perf_writer");
	return 0;
}

static inline void
rcu_perf_print_module_parms(struct rcu_perf_ops *cur_ops, const char *tag)
{
	pr_alert("%s" PERF_FLAG
		 "--- %s: nreaders=%d nwriters=%d verbose=%d shutdown=%d\n",
		 perf_type, tag, nrealreaders, nrealwriters, verbose, shutdown);
}

static void
rcu_perf_cleanup(void)
{
	int i;
	int j;
	int ngps = 0;
	u64 *wdp;
	u64 *wdpp;

	if (torture_cleanup_begin())
		return;

	if (reader_tasks) {
		for (i = 0; i < nrealreaders; i++)
			torture_stop_kthread(rcu_perf_reader,
					     reader_tasks[i]);
		kfree(reader_tasks);
	}

	if (writer_tasks) {
		for (i = 0; i < nrealwriters; i++) {
			torture_stop_kthread(rcu_perf_writer,
					     writer_tasks[i]);
			if (!writer_n_durations)
				continue;
			j = writer_n_durations[i];
			pr_alert("%s%s writer %d gps: %d\n",
				 perf_type, PERF_FLAG, i, j);
			ngps += j;
		}
		pr_alert("%s%s start: %llu end: %llu duration: %llu gps: %d batches: %ld\n",
			 perf_type, PERF_FLAG,
			 t_rcu_perf_writer_started, t_rcu_perf_writer_finished,
			 t_rcu_perf_writer_finished -
			 t_rcu_perf_writer_started,
			 ngps,
			 b_rcu_perf_writer_finished -
			 b_rcu_perf_writer_started);
		for (i = 0; i < nrealwriters; i++) {
			if (!writer_durations)
				break;
			if (!writer_n_durations)
				continue;
			wdpp = writer_durations[i];
			if (!wdpp)
				continue;
			for (j = 0; j <= writer_n_durations[i]; j++) {
				wdp = &wdpp[j];
				pr_alert("%s%s %4d writer-duration: %5d %llu\n",
					perf_type, PERF_FLAG,
					i, j, *wdp);
				if (j % 100 == 0)
					schedule_timeout_uninterruptible(1);
			}
			kfree(writer_durations[i]);
		}
		kfree(writer_tasks);
		kfree(writer_durations);
		kfree(writer_n_durations);
	}

	/* Do flavor-specific cleanup operations.  */
	if (cur_ops->cleanup != NULL)
		cur_ops->cleanup();

	torture_cleanup_end();
}

/*
 * Return the number if non-negative.  If -1, the number of CPUs.
 * If less than -1, that much less than the number of CPUs, but
 * at least one.
 */
static int compute_real(int n)
{
	int nr;

	if (n >= 0) {
		nr = n;
	} else {
		nr = num_online_cpus() + 1 + n;
		if (nr <= 0)
			nr = 1;
	}
	return nr;
}

/*
 * RCU perf shutdown kthread.  Just waits to be awakened, then shuts
 * down system.
 */
static int
rcu_perf_shutdown(void *arg)
{
	do {
		wait_event(shutdown_wq,
			   atomic_read(&n_rcu_perf_writer_finished) >=
			   nrealwriters);
	} while (atomic_read(&n_rcu_perf_writer_finished) < nrealwriters);
	smp_mb(); /* Wake before output. */
	rcu_perf_cleanup();
	kernel_power_off();
	return -EINVAL;
}

static int __init
rcu_perf_init(void)
{
	long i;
	int firsterr = 0;
	static struct rcu_perf_ops *perf_ops[] = {
		&rcu_ops, &rcu_bh_ops, &srcu_ops, &sched_ops,
		RCUPERF_TASKS_OPS
	};

	if (!torture_init_begin(perf_type, verbose, &perf_runnable))
		return -EBUSY;

	/* Process args and tell the world that the perf'er is on the job. */
	for (i = 0; i < ARRAY_SIZE(perf_ops); i++) {
		cur_ops = perf_ops[i];
		if (strcmp(perf_type, cur_ops->name) == 0)
			break;
	}
	if (i == ARRAY_SIZE(perf_ops)) {
		pr_alert("rcu-perf: invalid perf type: \"%s\"\n",
			 perf_type);
		pr_alert("rcu-perf types:");
		for (i = 0; i < ARRAY_SIZE(perf_ops); i++)
			pr_alert(" %s", perf_ops[i]->name);
		pr_alert("\n");
		firsterr = -EINVAL;
		goto unwind;
	}
	if (cur_ops->init)
		cur_ops->init();

	nrealwriters = compute_real(nwriters);
	nrealreaders = compute_real(nreaders);
	atomic_set(&n_rcu_perf_reader_started, 0);
	atomic_set(&n_rcu_perf_writer_started, 0);
	atomic_set(&n_rcu_perf_writer_finished, 0);
	rcu_perf_print_module_parms(cur_ops, "Start of test");

	/* Start up the kthreads. */

	if (shutdown) {
		init_waitqueue_head(&shutdown_wq);
		firsterr = torture_create_kthread(rcu_perf_shutdown, NULL,
						  shutdown_task);
		if (firsterr)
			goto unwind;
		schedule_timeout_uninterruptible(1);
	}
	reader_tasks = kcalloc(nrealreaders, sizeof(reader_tasks[0]),
			       GFP_KERNEL);
	if (reader_tasks == NULL) {
		VERBOSE_PERFOUT_ERRSTRING("out of memory");
		firsterr = -ENOMEM;
		goto unwind;
	}
	for (i = 0; i < nrealreaders; i++) {
		firsterr = torture_create_kthread(rcu_perf_reader, (void *)i,
						  reader_tasks[i]);
		if (firsterr)
			goto unwind;
	}
	while (atomic_read(&n_rcu_perf_reader_started) < nrealreaders)
		schedule_timeout_uninterruptible(1);
	writer_tasks = kcalloc(nrealwriters, sizeof(reader_tasks[0]),
			       GFP_KERNEL);
	writer_durations = kcalloc(nrealwriters, sizeof(*writer_durations),
				   GFP_KERNEL);
	writer_n_durations =
		kcalloc(nrealwriters, sizeof(*writer_n_durations),
			GFP_KERNEL);
	if (!writer_tasks || !writer_durations || !writer_n_durations) {
		VERBOSE_PERFOUT_ERRSTRING("out of memory");
		firsterr = -ENOMEM;
		goto unwind;
	}
	for (i = 0; i < nrealwriters; i++) {
		writer_durations[i] =
			kcalloc(MAX_MEAS, sizeof(*writer_durations[i]),
				GFP_KERNEL);
		if (!writer_durations[i])
			goto unwind;
		firsterr = torture_create_kthread(rcu_perf_writer, (void *)i,
						  writer_tasks[i]);
		if (firsterr)
			goto unwind;
	}
	torture_init_end();
	return 0;

unwind:
	torture_init_end();
	rcu_perf_cleanup();
	return firsterr;
}

module_init(rcu_perf_init);
module_exit(rcu_perf_cleanup);
