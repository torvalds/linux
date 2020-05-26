// SPDX-License-Identifier: GPL-2.0+
//
// Performance test comparing RCU vs other mechanisms
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
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/stat.h>
#include <linux/srcu.h>
#include <linux/slab.h>
#include <linux/torture.h>
#include <linux/types.h>

#include "rcu.h"

#define PERF_FLAG "-ref-perf: "

#define PERFOUT(s, x...) \
	pr_alert("%s" PERF_FLAG s, perf_type, ## x)

#define VERBOSE_PERFOUT(s, x...) \
	do { if (verbose) pr_alert("%s" PERF_FLAG s, perf_type, ## x); } while (0)

#define VERBOSE_PERFOUT_ERRSTRING(s, x...) \
	do { if (verbose) pr_alert("%s" PERF_FLAG "!!! " s, perf_type, ## x); } while (0)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joel Fernandes (Google) <joel@joelfernandes.org>");

static char *perf_type = "rcu";
module_param(perf_type, charp, 0444);
MODULE_PARM_DESC(perf_type, "Type of test (rcu, srcu, refcnt, rwsem, rwlock.");

torture_param(int, verbose, 0, "Enable verbose debugging printk()s");

// Wait until there are multiple CPUs before starting test.
torture_param(int, holdoff, IS_BUILTIN(CONFIG_RCU_REF_PERF_TEST) ? 10 : 0,
	      "Holdoff time before test start (s)");
// Number of loops per experiment, all readers execute operations concurrently.
torture_param(long, loops, 10000000, "Number of loops per experiment.");
// Number of readers, with -1 defaulting to about 75% of the CPUs.
torture_param(int, nreaders, -1, "Number of readers, -1 for 75% of CPUs.");
// Number of runs.
torture_param(int, nruns, 30, "Number of experiments to run.");
// Reader delay in nanoseconds, 0 for no delay.
torture_param(int, readdelay, 0, "Read-side delay in nanoseconds.");

#ifdef MODULE
# define REFPERF_SHUTDOWN 0
#else
# define REFPERF_SHUTDOWN 1
#endif

torture_param(bool, shutdown, REFPERF_SHUTDOWN,
	      "Shutdown at end of performance tests.");

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

// Track which experiment is currently running.
static int exp_idx;

// Operations vector for selecting different types of tests.
struct ref_perf_ops {
	void (*init)(void);
	void (*cleanup)(void);
	void (*readsection)(const int nloops);
	const char *name;
};

static struct ref_perf_ops *cur_ops;

static void ref_rcu_read_section(const int nloops)
{
	int i;

	for (i = nloops; i >= 0; i--) {
		rcu_read_lock();
		rcu_read_unlock();
	}
}

static void rcu_sync_perf_init(void)
{
}

static struct ref_perf_ops rcu_ops = {
	.init		= rcu_sync_perf_init,
	.readsection	= ref_rcu_read_section,
	.name		= "rcu"
};


// Definitions for SRCU ref perf testing.
DEFINE_STATIC_SRCU(srcu_refctl_perf);
static struct srcu_struct *srcu_ctlp = &srcu_refctl_perf;

static void srcu_ref_perf_read_section(int nloops)
{
	int i;
	int idx;

	for (i = nloops; i >= 0; i--) {
		idx = srcu_read_lock(srcu_ctlp);
		srcu_read_unlock(srcu_ctlp, idx);
	}
}

static struct ref_perf_ops srcu_ops = {
	.init		= rcu_sync_perf_init,
	.readsection	= srcu_ref_perf_read_section,
	.name		= "srcu"
};

// Definitions for reference count
static atomic_t refcnt;

static void ref_perf_refcnt_section(const int nloops)
{
	int i;

	for (i = nloops; i >= 0; i--) {
		atomic_inc(&refcnt);
		atomic_dec(&refcnt);
	}
}

static struct ref_perf_ops refcnt_ops = {
	.init		= rcu_sync_perf_init,
	.readsection	= ref_perf_refcnt_section,
	.name		= "refcnt"
};

// Definitions for rwlock
static rwlock_t test_rwlock;

static void ref_perf_rwlock_init(void)
{
	rwlock_init(&test_rwlock);
}

static void ref_perf_rwlock_section(const int nloops)
{
	int i;

	for (i = nloops; i >= 0; i--) {
		read_lock(&test_rwlock);
		read_unlock(&test_rwlock);
	}
}

static struct ref_perf_ops rwlock_ops = {
	.init		= ref_perf_rwlock_init,
	.readsection	= ref_perf_rwlock_section,
	.name		= "rwlock"
};

// Definitions for rwsem
static struct rw_semaphore test_rwsem;

static void ref_perf_rwsem_init(void)
{
	init_rwsem(&test_rwsem);
}

static void ref_perf_rwsem_section(const int nloops)
{
	int i;

	for (i = nloops; i >= 0; i--) {
		down_read(&test_rwsem);
		up_read(&test_rwsem);
	}
}

static struct ref_perf_ops rwsem_ops = {
	.init		= ref_perf_rwsem_init,
	.readsection	= ref_perf_rwsem_section,
	.name		= "rwsem"
};

// Reader kthread.  Repeatedly does empty RCU read-side
// critical section, minimizing update-side interference.
static int
ref_perf_reader(void *arg)
{
	unsigned long flags;
	long me = (long)arg;
	struct reader_task *rt = &(reader_tasks[me]);
	u64 start;
	s64 duration;

	VERBOSE_PERFOUT("ref_perf_reader %ld: task started", me);
	set_cpus_allowed_ptr(current, cpumask_of(me % nr_cpu_ids));
	set_user_nice(current, MAX_NICE);
	atomic_inc(&n_init);
	if (holdoff)
		schedule_timeout_interruptible(holdoff * HZ);
repeat:
	VERBOSE_PERFOUT("ref_perf_reader %ld: waiting to start next experiment on cpu %d", me, smp_processor_id());

	// Wait for signal that this reader can start.
	wait_event(rt->wq, (atomic_read(&nreaders_exp) && smp_load_acquire(&rt->start_reader)) ||
			   torture_must_stop());

	if (torture_must_stop())
		goto end;

	// Make sure that the CPU is affinitized appropriately during testing.
	WARN_ON_ONCE(smp_processor_id() != me);

	WRITE_ONCE(rt->start_reader, 0);
	if (!atomic_dec_return(&n_started))
		while (atomic_read_acquire(&n_started))
			cpu_relax();

	VERBOSE_PERFOUT("ref_perf_reader %ld: experiment %d started", me, exp_idx);

	// To prevent noise, keep interrupts disabled. This also has the
	// effect of preventing entries into slow path for rcu_read_unlock().
	local_irq_save(flags);
	start = ktime_get_mono_fast_ns();

	cur_ops->readsection(loops);

	duration = ktime_get_mono_fast_ns() - start;
	local_irq_restore(flags);

	rt->last_duration_ns = WARN_ON_ONCE(duration < 0) ? 0 : duration;

	if (atomic_dec_and_test(&nreaders_exp))
		wake_up(&main_wq);

	VERBOSE_PERFOUT("ref_perf_reader %ld: experiment %d ended, (readers remaining=%d)",
			me, exp_idx, atomic_read(&nreaders_exp));

	if (!torture_must_stop())
		goto repeat;
end:
	torture_kthread_stopping("ref_perf_reader");
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
	char buf1[64];
	char *buf;
	u64 sum = 0;

	buf = kmalloc(128 + nreaders * 32, GFP_KERNEL);
	if (!buf)
		return 0;
	buf[0] = 0;
	sprintf(buf, "Experiment #%d (Format: <THREAD-NUM>:<Total loop time in ns>)",
		exp_idx);

	for (i = 0; i < n && !torture_must_stop(); i++) {
		rt = &(reader_tasks[i]);
		sprintf(buf1, "%d: %llu\t", i, rt->last_duration_ns);

		if (i % 5 == 0)
			strcat(buf, "\n");
		strcat(buf, buf1);

		sum += rt->last_duration_ns;
	}
	strcat(buf, "\n");

	PERFOUT("%s\n", buf);

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
	bool errexit = false;
	int exp, r;
	char buf1[64];
	char *buf;
	u64 *result_avg;

	set_cpus_allowed_ptr(current, cpumask_of(nreaders % nr_cpu_ids));
	set_user_nice(current, MAX_NICE);

	VERBOSE_PERFOUT("main_func task started");
	result_avg = kzalloc(nruns * sizeof(*result_avg), GFP_KERNEL);
	buf = kzalloc(64 + nruns * 32, GFP_KERNEL);
	if (!result_avg || !buf) {
		VERBOSE_PERFOUT_ERRSTRING("out of memory");
		errexit = true;
	}
	atomic_inc(&n_init);

	// Wait for all threads to start.
	wait_event(main_wq, atomic_read(&n_init) == (nreaders + 1));
	if (holdoff)
		schedule_timeout_interruptible(holdoff * HZ);

	// Start exp readers up per experiment
	for (exp = 0; exp < nruns && !torture_must_stop(); exp++) {
		if (errexit)
			break;
		if (torture_must_stop())
			goto end;

		reset_readers();
		atomic_set(&nreaders_exp, nreaders);
		atomic_set(&n_started, nreaders);

		exp_idx = exp;

		for (r = 0; r < nreaders; r++) {
			smp_store_release(&reader_tasks[r].start_reader, 1);
			wake_up(&reader_tasks[r].wq);
		}

		VERBOSE_PERFOUT("main_func: experiment started, waiting for %d readers",
				nreaders);

		wait_event(main_wq,
			   !atomic_read(&nreaders_exp) || torture_must_stop());

		VERBOSE_PERFOUT("main_func: experiment ended");

		if (torture_must_stop())
			goto end;

		result_avg[exp] = 1000 * process_durations(nreaders) / (nreaders * loops);
	}

	// Print the average of all experiments
	PERFOUT("END OF TEST. Calculating average duration per loop (nanoseconds)...\n");

	buf[0] = 0;
	strcat(buf, "\n");
	strcat(buf, "Threads\tTime(ns)\n");

	for (exp = 0; exp < nruns; exp++) {
		if (errexit)
			break;
		sprintf(buf1, "%d\t%llu.%03d\n", exp + 1, result_avg[exp] / 1000, (int)(result_avg[exp] % 1000));
		strcat(buf, buf1);
	}

	if (!errexit)
		PERFOUT("%s", buf);

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
ref_perf_print_module_parms(struct ref_perf_ops *cur_ops, const char *tag)
{
	pr_alert("%s" PERF_FLAG
		 "--- %s:  verbose=%d shutdown=%d holdoff=%d loops=%ld nreaders=%d nruns=%d\n", perf_type, tag,
		 verbose, shutdown, holdoff, loops, nreaders, nruns);
}

static void
ref_perf_cleanup(void)
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
			torture_stop_kthread("ref_perf_reader",
					     reader_tasks[i].task);
	}
	kfree(reader_tasks);

	torture_stop_kthread("main_task", main_task);
	kfree(main_task);

	// Do perf-type-specific cleanup operations.
	if (cur_ops->cleanup != NULL)
		cur_ops->cleanup();

	torture_cleanup_end();
}

// Shutdown kthread.  Just waits to be awakened, then shuts down system.
static int
ref_perf_shutdown(void *arg)
{
	wait_event(shutdown_wq, shutdown_start);

	smp_mb(); // Wake before output.
	ref_perf_cleanup();
	kernel_power_off();

	return -EINVAL;
}

static int __init
ref_perf_init(void)
{
	long i;
	int firsterr = 0;
	static struct ref_perf_ops *perf_ops[] = {
		&rcu_ops, &srcu_ops, &refcnt_ops, &rwlock_ops, &rwsem_ops,
	};

	if (!torture_init_begin(perf_type, verbose))
		return -EBUSY;

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
		WARN_ON(!IS_MODULE(CONFIG_RCU_REF_PERF_TEST));
		firsterr = -EINVAL;
		cur_ops = NULL;
		goto unwind;
	}
	if (cur_ops->init)
		cur_ops->init();

	ref_perf_print_module_parms(cur_ops, "Start of test");

	// Shutdown task
	if (shutdown) {
		init_waitqueue_head(&shutdown_wq);
		firsterr = torture_create_kthread(ref_perf_shutdown, NULL,
						  shutdown_task);
		if (firsterr)
			goto unwind;
		schedule_timeout_uninterruptible(1);
	}

	// Reader tasks (default to ~75% of online CPUs).
	if (nreaders < 0)
		nreaders = (num_online_cpus() >> 1) + (num_online_cpus() >> 2);
	reader_tasks = kcalloc(nreaders, sizeof(reader_tasks[0]),
			       GFP_KERNEL);
	if (!reader_tasks) {
		VERBOSE_PERFOUT_ERRSTRING("out of memory");
		firsterr = -ENOMEM;
		goto unwind;
	}

	VERBOSE_PERFOUT("Starting %d reader threads\n", nreaders);

	for (i = 0; i < nreaders; i++) {
		firsterr = torture_create_kthread(ref_perf_reader, (void *)i,
						  reader_tasks[i].task);
		if (firsterr)
			goto unwind;

		init_waitqueue_head(&(reader_tasks[i].wq));
	}

	// Main Task
	init_waitqueue_head(&main_wq);
	firsterr = torture_create_kthread(main_func, NULL, main_task);
	if (firsterr)
		goto unwind;
	schedule_timeout_uninterruptible(1);


	// Wait until all threads start
	while (atomic_read(&n_init) < nreaders + 1)
		schedule_timeout_uninterruptible(1);

	wake_up(&main_wq);

	torture_init_end();
	return 0;

unwind:
	torture_init_end();
	ref_perf_cleanup();
	return firsterr;
}

module_init(ref_perf_init);
module_exit(ref_perf_cleanup);
