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
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/stat.h>
#include <linux/srcu.h>
#include <linux/slab.h>
#include <linux/torture.h>
#include <linux/types.h>

#include "rcu.h"

#define SCALE_FLAG "-ref-scale: "

#define SCALEOUT(s, x...) \
	pr_alert("%s" SCALE_FLAG s, scale_type, ## x)

#define VERBOSE_SCALEOUT(s, x...) \
	do { if (verbose) pr_alert("%s" SCALE_FLAG s, scale_type, ## x); } while (0)

#define VERBOSE_SCALEOUT_ERRSTRING(s, x...) \
	do { if (verbose) pr_alert("%s" SCALE_FLAG "!!! " s, scale_type, ## x); } while (0)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joel Fernandes (Google) <joel@joelfernandes.org>");

static char *scale_type = "rcu";
module_param(scale_type, charp, 0444);
MODULE_PARM_DESC(scale_type, "Type of test (rcu, srcu, refcnt, rwsem, rwlock.");

torture_param(int, verbose, 0, "Enable verbose debugging printk()s");

// Wait until there are multiple CPUs before starting test.
torture_param(int, holdoff, IS_BUILTIN(CONFIG_RCU_REF_SCALE_TEST) ? 10 : 0,
	      "Holdoff time before test start (s)");
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
	void (*init)(void);
	void (*cleanup)(void);
	void (*readsection)(const int nloops);
	void (*delaysection)(const int nloops, const int udl, const int ndl);
	const char *name;
};

static struct ref_scale_ops *cur_ops;

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

static void rcu_sync_scale_init(void)
{
}

static struct ref_scale_ops rcu_ops = {
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

static struct ref_scale_ops srcu_ops = {
	.init		= rcu_sync_scale_init,
	.readsection	= srcu_ref_scale_read_section,
	.delaysection	= srcu_ref_scale_delay_section,
	.name		= "srcu"
};

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

static struct ref_scale_ops rcu_tasks_ops = {
	.init		= rcu_sync_scale_init,
	.readsection	= rcu_tasks_ref_scale_read_section,
	.delaysection	= rcu_tasks_ref_scale_delay_section,
	.name		= "rcu-tasks"
};

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

static struct ref_scale_ops rcu_trace_ops = {
	.init		= rcu_sync_scale_init,
	.readsection	= rcu_trace_ref_scale_read_section,
	.delaysection	= rcu_trace_ref_scale_delay_section,
	.name		= "rcu-trace"
};

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

static struct ref_scale_ops refcnt_ops = {
	.init		= rcu_sync_scale_init,
	.readsection	= ref_refcnt_section,
	.delaysection	= ref_refcnt_delay_section,
	.name		= "refcnt"
};

// Definitions for rwlock
static rwlock_t test_rwlock;

static void ref_rwlock_init(void)
{
	rwlock_init(&test_rwlock);
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

static struct ref_scale_ops rwlock_ops = {
	.init		= ref_rwlock_init,
	.readsection	= ref_rwlock_section,
	.delaysection	= ref_rwlock_delay_section,
	.name		= "rwlock"
};

// Definitions for rwsem
static struct rw_semaphore test_rwsem;

static void ref_rwsem_init(void)
{
	init_rwsem(&test_rwsem);
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

static struct ref_scale_ops rwsem_ops = {
	.init		= ref_rwsem_init,
	.readsection	= ref_rwsem_section,
	.delaysection	= ref_rwsem_delay_section,
	.name		= "rwsem"
};

static void rcu_scale_one_reader(void)
{
	if (readdelay <= 0)
		cur_ops->readsection(loops);
	else
		cur_ops->delaysection(loops, readdelay / 1000, readdelay % 1000);
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

	VERBOSE_SCALEOUT("ref_scale_reader %ld: task started", me);
	set_cpus_allowed_ptr(current, cpumask_of(me % nr_cpu_ids));
	set_user_nice(current, MAX_NICE);
	atomic_inc(&n_init);
	if (holdoff)
		schedule_timeout_interruptible(holdoff * HZ);
repeat:
	VERBOSE_SCALEOUT("ref_scale_reader %ld: waiting to start next experiment on cpu %d", me, smp_processor_id());

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

	VERBOSE_SCALEOUT("ref_scale_reader %ld: experiment %d started", me, exp_idx);


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

	VERBOSE_SCALEOUT("ref_scale_reader %ld: experiment %d ended, (readers remaining=%d)",
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

	SCALEOUT("%s\n", buf);

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

	VERBOSE_SCALEOUT("main_func task started");
	result_avg = kzalloc(nruns * sizeof(*result_avg), GFP_KERNEL);
	buf = kzalloc(64 + nruns * 32, GFP_KERNEL);
	if (!result_avg || !buf) {
		VERBOSE_SCALEOUT_ERRSTRING("out of memory");
		errexit = true;
	}
	if (holdoff)
		schedule_timeout_interruptible(holdoff * HZ);

	// Wait for all threads to start.
	atomic_inc(&n_init);
	while (atomic_read(&n_init) < nreaders + 1)
		schedule_timeout_uninterruptible(1);

	// Start exp readers up per experiment
	for (exp = 0; exp < nruns && !torture_must_stop(); exp++) {
		if (errexit)
			break;
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

	// Print the average of all experiments
	SCALEOUT("END OF TEST. Calculating average duration per loop (nanoseconds)...\n");

	if (!errexit) {
		buf[0] = 0;
		strcat(buf, "\n");
		strcat(buf, "Runs\tTime(ns)\n");
	}

	for (exp = 0; exp < nruns; exp++) {
		u64 avg;
		u32 rem;

		if (errexit)
			break;
		avg = div_u64_rem(result_avg[exp], 1000, &rem);
		sprintf(buf1, "%d\t%llu.%03u\n", exp + 1, avg, rem);
		strcat(buf, buf1);
	}

	if (!errexit)
		SCALEOUT("%s", buf);

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
ref_scale_print_module_parms(struct ref_scale_ops *cur_ops, const char *tag)
{
	pr_alert("%s" SCALE_FLAG
		 "--- %s:  verbose=%d shutdown=%d holdoff=%d loops=%ld nreaders=%d nruns=%d readdelay=%d\n", scale_type, tag,
		 verbose, shutdown, holdoff, loops, nreaders, nruns, readdelay);
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
	wait_event(shutdown_wq, shutdown_start);

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
	static struct ref_scale_ops *scale_ops[] = {
		&rcu_ops, &srcu_ops, &rcu_trace_ops, &rcu_tasks_ops,
		&refcnt_ops, &rwlock_ops, &rwsem_ops,
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
		cur_ops->init();

	ref_scale_print_module_parms(cur_ops, "Start of test");

	// Shutdown task
	if (shutdown) {
		init_waitqueue_head(&shutdown_wq);
		firsterr = torture_create_kthread(ref_scale_shutdown, NULL,
						  shutdown_task);
		if (firsterr)
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
		VERBOSE_SCALEOUT_ERRSTRING("out of memory");
		firsterr = -ENOMEM;
		goto unwind;
	}

	VERBOSE_SCALEOUT("Starting %d reader threads\n", nreaders);

	for (i = 0; i < nreaders; i++) {
		firsterr = torture_create_kthread(ref_scale_reader, (void *)i,
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
