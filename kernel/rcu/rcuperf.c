// SPDX-License-Identifier: GPL-2.0+
/*
 * Read-Copy Update module-based performance-test facility
 *
 * Copyright (C) IBM Corporation, 2015
 *
 * Authors: Paul E. McKenney <paulmck@linux.ibm.com>
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
#include <linux/sched.h>
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
#include <asm/byteorder.h>
#include <linux/torture.h>
#include <linux/vmalloc.h>

#include "rcu.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Paul E. McKenney <paulmck@linux.ibm.com>");

#define PERF_FLAG "-perf:"
#define PERFOUT_STRING(s) \
	pr_alert("%s" PERF_FLAG " %s\n", perf_type, s)
#define VERBOSE_PERFOUT_STRING(s) \
	do { if (verbose) pr_alert("%s" PERF_FLAG " %s\n", perf_type, s); } while (0)
#define VERBOSE_PERFOUT_ERRSTRING(s) \
	do { if (verbose) pr_alert("%s" PERF_FLAG "!!! %s\n", perf_type, s); } while (0)

/*
 * The intended use cases for the nreaders and nwriters module parameters
 * are as follows:
 *
 * 1.	Specify only the nr_cpus kernel boot parameter.  This will
 *	set both nreaders and nwriters to the value specified by
 *	nr_cpus for a mixed reader/writer test.
 *
 * 2.	Specify the nr_cpus kernel boot parameter, but set
 *	rcuperf.nreaders to zero.  This will set nwriters to the
 *	value specified by nr_cpus for an update-only test.
 *
 * 3.	Specify the nr_cpus kernel boot parameter, but set
 *	rcuperf.nwriters to zero.  This will set nreaders to the
 *	value specified by nr_cpus for a read-only test.
 *
 * Various other use cases may of course be specified.
 */

#ifdef MODULE
# define RCUPERF_SHUTDOWN 0
#else
# define RCUPERF_SHUTDOWN 1
#endif

torture_param(bool, gp_async, false, "Use asynchronous GP wait primitives");
torture_param(int, gp_async_max, 1000, "Max # outstanding waits per reader");
torture_param(bool, gp_exp, false, "Use expedited GP wait primitives");
torture_param(int, holdoff, 10, "Holdoff time before test start (s)");
torture_param(int, nreaders, -1, "Number of RCU reader threads");
torture_param(int, nwriters, -1, "Number of RCU updater threads");
torture_param(bool, shutdown, RCUPERF_SHUTDOWN,
	      "Shutdown at end of performance tests.");
torture_param(int, verbose, 1, "Enable verbose debugging printk()s");
torture_param(int, writer_holdoff, 0, "Holdoff (us) between GPs, zero to disable");
torture_param(int, kfree_rcu_test, 0, "Do we run a kfree_rcu() perf test?");

static char *perf_type = "rcu";
module_param(perf_type, charp, 0444);
MODULE_PARM_DESC(perf_type, "Type of RCU to performance-test (rcu, srcu, ...)");

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
static unsigned long b_rcu_gp_test_started;
static unsigned long b_rcu_gp_test_finished;
static DEFINE_PER_CPU(atomic_t, n_async_inflight);

#define MAX_MEAS 10000
#define MIN_MEAS 100

/*
 * Operations vector for selecting different types of tests.
 */

struct rcu_perf_ops {
	int ptype;
	void (*init)(void);
	void (*cleanup)(void);
	int (*readlock)(void);
	void (*readunlock)(int idx);
	unsigned long (*get_gp_seq)(void);
	unsigned long (*gp_diff)(unsigned long new, unsigned long old);
	unsigned long (*exp_completed)(void);
	void (*async)(struct rcu_head *head, rcu_callback_t func);
	void (*gp_barrier)(void);
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
	.get_gp_seq	= rcu_get_gp_seq,
	.gp_diff	= rcu_seq_diff,
	.exp_completed	= rcu_exp_batches_completed,
	.async		= call_rcu,
	.gp_barrier	= rcu_barrier,
	.sync		= synchronize_rcu,
	.exp_sync	= synchronize_rcu_expedited,
	.name		= "rcu"
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

static void srcu_call_rcu(struct rcu_head *head, rcu_callback_t func)
{
	call_srcu(srcu_ctlp, head, func);
}

static void srcu_rcu_barrier(void)
{
	srcu_barrier(srcu_ctlp);
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
	.get_gp_seq	= srcu_perf_completed,
	.gp_diff	= rcu_seq_diff,
	.exp_completed	= srcu_perf_completed,
	.async		= srcu_call_rcu,
	.gp_barrier	= srcu_rcu_barrier,
	.sync		= srcu_perf_synchronize,
	.exp_sync	= srcu_perf_synchronize_expedited,
	.name		= "srcu"
};

static struct srcu_struct srcud;

static void srcu_sync_perf_init(void)
{
	srcu_ctlp = &srcud;
	init_srcu_struct(srcu_ctlp);
}

static void srcu_sync_perf_cleanup(void)
{
	cleanup_srcu_struct(srcu_ctlp);
}

static struct rcu_perf_ops srcud_ops = {
	.ptype		= SRCU_FLAVOR,
	.init		= srcu_sync_perf_init,
	.cleanup	= srcu_sync_perf_cleanup,
	.readlock	= srcu_perf_read_lock,
	.readunlock	= srcu_perf_read_unlock,
	.get_gp_seq	= srcu_perf_completed,
	.gp_diff	= rcu_seq_diff,
	.exp_completed	= srcu_perf_completed,
	.async		= srcu_call_rcu,
	.gp_barrier	= srcu_rcu_barrier,
	.sync		= srcu_perf_synchronize,
	.exp_sync	= srcu_perf_synchronize_expedited,
	.name		= "srcud"
};

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
	.get_gp_seq	= rcu_no_completed,
	.gp_diff	= rcu_seq_diff,
	.async		= call_rcu_tasks,
	.gp_barrier	= rcu_barrier_tasks,
	.sync		= synchronize_rcu_tasks,
	.exp_sync	= synchronize_rcu_tasks,
	.name		= "tasks"
};

static unsigned long rcuperf_seq_diff(unsigned long new, unsigned long old)
{
	if (!cur_ops->gp_diff)
		return new - old;
	return cur_ops->gp_diff(new, old);
}

/*
 * If performance tests complete, wait for shutdown to commence.
 */
static void rcu_perf_wait_shutdown(void)
{
	cond_resched_tasks_rcu_qs();
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
 * Callback function for asynchronous grace periods from rcu_perf_writer().
 */
static void rcu_perf_async_cb(struct rcu_head *rhp)
{
	atomic_dec(this_cpu_ptr(&n_async_inflight));
	kfree(rhp);
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
	struct rcu_head *rhp = NULL;
	struct sched_param sp;
	bool started = false, done = false, alldone = false;
	u64 t;
	u64 *wdp;
	u64 *wdpp = writer_durations[me];

	VERBOSE_PERFOUT_STRING("rcu_perf_writer task started");
	WARN_ON(!wdpp);
	set_cpus_allowed_ptr(current, cpumask_of(me % nr_cpu_ids));
	sp.sched_priority = 1;
	sched_setscheduler_nocheck(current, SCHED_FIFO, &sp);

	if (holdoff)
		schedule_timeout_uninterruptible(holdoff * HZ);

	/*
	 * Wait until rcu_end_inkernel_boot() is called for normal GP tests
	 * so that RCU is not always expedited for normal GP tests.
	 * The system_state test is approximate, but works well in practice.
	 */
	while (!gp_exp && system_state != SYSTEM_RUNNING)
		schedule_timeout_uninterruptible(1);

	t = ktime_get_mono_fast_ns();
	if (atomic_inc_return(&n_rcu_perf_writer_started) >= nrealwriters) {
		t_rcu_perf_writer_started = t;
		if (gp_exp) {
			b_rcu_gp_test_started =
				cur_ops->exp_completed() / 2;
		} else {
			b_rcu_gp_test_started = cur_ops->get_gp_seq();
		}
	}

	do {
		if (writer_holdoff)
			udelay(writer_holdoff);
		wdp = &wdpp[i];
		*wdp = ktime_get_mono_fast_ns();
		if (gp_async) {
retry:
			if (!rhp)
				rhp = kmalloc(sizeof(*rhp), GFP_KERNEL);
			if (rhp && atomic_read(this_cpu_ptr(&n_async_inflight)) < gp_async_max) {
				atomic_inc(this_cpu_ptr(&n_async_inflight));
				cur_ops->async(rhp, rcu_perf_async_cb);
				rhp = NULL;
			} else if (!kthread_should_stop()) {
				cur_ops->gp_barrier();
				goto retry;
			} else {
				kfree(rhp); /* Because we are stopping. */
			}
		} else if (gp_exp) {
			cur_ops->exp_sync();
		} else {
			cur_ops->sync();
		}
		t = ktime_get_mono_fast_ns();
		*wdp = t - *wdp;
		i_max = i;
		if (!started &&
		    atomic_read(&n_rcu_perf_writer_started) >= nrealwriters)
			started = true;
		if (!done && i >= MIN_MEAS) {
			done = true;
			sp.sched_priority = 0;
			sched_setscheduler_nocheck(current,
						   SCHED_NORMAL, &sp);
			pr_alert("%s%s rcu_perf_writer %ld has %d measurements\n",
				 perf_type, PERF_FLAG, me, MIN_MEAS);
			if (atomic_inc_return(&n_rcu_perf_writer_finished) >=
			    nrealwriters) {
				schedule_timeout_interruptible(10);
				rcu_ftrace_dump(DUMP_ALL);
				PERFOUT_STRING("Test complete");
				t_rcu_perf_writer_finished = t;
				if (gp_exp) {
					b_rcu_gp_test_finished =
						cur_ops->exp_completed() / 2;
				} else {
					b_rcu_gp_test_finished =
						cur_ops->get_gp_seq();
				}
				if (shutdown) {
					smp_mb(); /* Assign before wake. */
					wake_up(&shutdown_wq);
				}
			}
		}
		if (done && !alldone &&
		    atomic_read(&n_rcu_perf_writer_finished) >= nrealwriters)
			alldone = true;
		if (started && !alldone && i < MAX_MEAS - 1)
			i++;
		rcu_perf_wait_shutdown();
	} while (!torture_must_stop());
	if (gp_async) {
		cur_ops->gp_barrier();
	}
	writer_n_durations[me] = i_max;
	torture_kthread_stopping("rcu_perf_writer");
	return 0;
}

static void
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

	/*
	 * Would like warning at start, but everything is expedited
	 * during the mid-boot phase, so have to wait till the end.
	 */
	if (rcu_gp_is_expedited() && !rcu_gp_is_normal() && !gp_exp)
		VERBOSE_PERFOUT_ERRSTRING("All grace periods expedited, no normal ones to measure!");
	if (rcu_gp_is_normal() && gp_exp)
		VERBOSE_PERFOUT_ERRSTRING("All grace periods normal, no expedited ones to measure!");
	if (gp_exp && gp_async)
		VERBOSE_PERFOUT_ERRSTRING("No expedited async GPs, so went with async!");

	if (torture_cleanup_begin())
		return;
	if (!cur_ops) {
		torture_cleanup_end();
		return;
	}

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
			 rcuperf_seq_diff(b_rcu_gp_test_finished,
					  b_rcu_gp_test_started));
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

	/* Do torture-type-specific cleanup operations.  */
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

/*
 * kfree_rcu() performance tests: Start a kfree_rcu() loop on all CPUs for number
 * of iterations and measure total time and number of GP for all iterations to complete.
 */

torture_param(int, kfree_nthreads, -1, "Number of threads running loops of kfree_rcu().");
torture_param(int, kfree_alloc_num, 8000, "Number of allocations and frees done in an iteration.");
torture_param(int, kfree_loops, 10, "Number of loops doing kfree_alloc_num allocations and frees.");
torture_param(int, kfree_no_batch, 0, "Use the non-batching (slower) version of kfree_rcu().");

static struct task_struct **kfree_reader_tasks;
static int kfree_nrealthreads;
static atomic_t n_kfree_perf_thread_started;
static atomic_t n_kfree_perf_thread_ended;

struct kfree_obj {
	char kfree_obj[8];
	struct rcu_head rh;
};

static int
kfree_perf_thread(void *arg)
{
	int i, loop = 0;
	long me = (long)arg;
	struct kfree_obj *alloc_ptr;
	u64 start_time, end_time;

	VERBOSE_PERFOUT_STRING("kfree_perf_thread task started");
	set_cpus_allowed_ptr(current, cpumask_of(me % nr_cpu_ids));
	set_user_nice(current, MAX_NICE);

	start_time = ktime_get_mono_fast_ns();

	if (atomic_inc_return(&n_kfree_perf_thread_started) >= kfree_nrealthreads) {
		if (gp_exp)
			b_rcu_gp_test_started = cur_ops->exp_completed() / 2;
		else
			b_rcu_gp_test_started = cur_ops->get_gp_seq();
	}

	do {
		for (i = 0; i < kfree_alloc_num; i++) {
			alloc_ptr = kmalloc(sizeof(struct kfree_obj), GFP_KERNEL);
			if (!alloc_ptr)
				return -ENOMEM;

			if (!kfree_no_batch) {
				kfree_rcu(alloc_ptr, rh);
			} else {
				rcu_callback_t cb;

				cb = (rcu_callback_t)(unsigned long)offsetof(struct kfree_obj, rh);
				kfree_call_rcu_nobatch(&(alloc_ptr->rh), cb);
			}
		}

		cond_resched();
	} while (!torture_must_stop() && ++loop < kfree_loops);

	if (atomic_inc_return(&n_kfree_perf_thread_ended) >= kfree_nrealthreads) {
		end_time = ktime_get_mono_fast_ns();

		if (gp_exp)
			b_rcu_gp_test_finished = cur_ops->exp_completed() / 2;
		else
			b_rcu_gp_test_finished = cur_ops->get_gp_seq();

		pr_alert("Total time taken by all kfree'ers: %llu ns, loops: %d, batches: %ld\n",
		       (unsigned long long)(end_time - start_time), kfree_loops,
		       rcuperf_seq_diff(b_rcu_gp_test_finished, b_rcu_gp_test_started));
		if (shutdown) {
			smp_mb(); /* Assign before wake. */
			wake_up(&shutdown_wq);
		}
	}

	torture_kthread_stopping("kfree_perf_thread");
	return 0;
}

static void
kfree_perf_cleanup(void)
{
	int i;

	if (torture_cleanup_begin())
		return;

	if (kfree_reader_tasks) {
		for (i = 0; i < kfree_nrealthreads; i++)
			torture_stop_kthread(kfree_perf_thread,
					     kfree_reader_tasks[i]);
		kfree(kfree_reader_tasks);
	}

	torture_cleanup_end();
}

/*
 * shutdown kthread.  Just waits to be awakened, then shuts down system.
 */
static int
kfree_perf_shutdown(void *arg)
{
	do {
		wait_event(shutdown_wq,
			   atomic_read(&n_kfree_perf_thread_ended) >=
			   kfree_nrealthreads);
	} while (atomic_read(&n_kfree_perf_thread_ended) < kfree_nrealthreads);

	smp_mb(); /* Wake before output. */

	kfree_perf_cleanup();
	kernel_power_off();
	return -EINVAL;
}

static int __init
kfree_perf_init(void)
{
	long i;
	int firsterr = 0;

	kfree_nrealthreads = compute_real(kfree_nthreads);
	/* Start up the kthreads. */
	if (shutdown) {
		init_waitqueue_head(&shutdown_wq);
		firsterr = torture_create_kthread(kfree_perf_shutdown, NULL,
						  shutdown_task);
		if (firsterr)
			goto unwind;
		schedule_timeout_uninterruptible(1);
	}

	kfree_reader_tasks = kcalloc(kfree_nrealthreads, sizeof(kfree_reader_tasks[0]),
			       GFP_KERNEL);
	if (kfree_reader_tasks == NULL) {
		firsterr = -ENOMEM;
		goto unwind;
	}

	for (i = 0; i < kfree_nrealthreads; i++) {
		firsterr = torture_create_kthread(kfree_perf_thread, (void *)i,
						  kfree_reader_tasks[i]);
		if (firsterr)
			goto unwind;
	}

	while (atomic_read(&n_kfree_perf_thread_started) < kfree_nrealthreads)
		schedule_timeout_uninterruptible(1);

	torture_init_end();
	return 0;

unwind:
	torture_init_end();
	kfree_perf_cleanup();
	return firsterr;
}

static int __init
rcu_perf_init(void)
{
	long i;
	int firsterr = 0;
	static struct rcu_perf_ops *perf_ops[] = {
		&rcu_ops, &srcu_ops, &srcud_ops, &tasks_ops,
	};

	if (!torture_init_begin(perf_type, verbose))
		return -EBUSY;

	/* Process args and tell the world that the perf'er is on the job. */
	for (i = 0; i < ARRAY_SIZE(perf_ops); i++) {
		cur_ops = perf_ops[i];
		if (strcmp(perf_type, cur_ops->name) == 0)
			break;
	}
	if (i == ARRAY_SIZE(perf_ops)) {
		pr_alert("rcu-perf: invalid perf type: \"%s\"\n", perf_type);
		pr_alert("rcu-perf types:");
		for (i = 0; i < ARRAY_SIZE(perf_ops); i++)
			pr_cont(" %s", perf_ops[i]->name);
		pr_cont("\n");
		WARN_ON(!IS_MODULE(CONFIG_RCU_PERF_TEST));
		firsterr = -EINVAL;
		cur_ops = NULL;
		goto unwind;
	}
	if (cur_ops->init)
		cur_ops->init();

	if (kfree_rcu_test)
		return kfree_perf_init();

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
		if (!writer_durations[i]) {
			firsterr = -ENOMEM;
			goto unwind;
		}
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
