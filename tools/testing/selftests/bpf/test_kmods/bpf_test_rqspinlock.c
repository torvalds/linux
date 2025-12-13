// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/prandom.h>
#include <asm/rqspinlock.h>
#include <linux/perf_event.h>
#include <linux/kthread.h>
#include <linux/atomic.h>
#include <linux/slab.h>

static struct perf_event_attr hw_attr = {
	.type		= PERF_TYPE_HARDWARE,
	.config		= PERF_COUNT_HW_CPU_CYCLES,
	.size		= sizeof(struct perf_event_attr),
	.pinned		= 1,
	.disabled	= 1,
	.sample_period	= 100000,
};

static rqspinlock_t lock_a;
static rqspinlock_t lock_b;

static struct perf_event **rqsl_evts;
static int rqsl_nevts;

static bool test_ab = false;
module_param(test_ab, bool, 0644);
MODULE_PARM_DESC(test_ab, "Test ABBA situations instead of AA situations");

static struct task_struct **rqsl_threads;
static int rqsl_nthreads;
static atomic_t rqsl_ready_cpus = ATOMIC_INIT(0);

static int pause = 0;

static bool nmi_locks_a(int cpu)
{
	return (cpu & 1) && test_ab;
}

static int rqspinlock_worker_fn(void *arg)
{
	int cpu = smp_processor_id();
	unsigned long flags;
	int ret;

	if (cpu) {
		atomic_inc(&rqsl_ready_cpus);

		while (!kthread_should_stop()) {
			if (READ_ONCE(pause)) {
				msleep(1000);
				continue;
			}
			if (nmi_locks_a(cpu))
				ret = raw_res_spin_lock_irqsave(&lock_b, flags);
			else
				ret = raw_res_spin_lock_irqsave(&lock_a, flags);
			mdelay(20);
			if (nmi_locks_a(cpu) && !ret)
				raw_res_spin_unlock_irqrestore(&lock_b, flags);
			else if (!ret)
				raw_res_spin_unlock_irqrestore(&lock_a, flags);
			cpu_relax();
		}
		return 0;
	}

	while (!kthread_should_stop()) {
		int expected = rqsl_nthreads > 0 ? rqsl_nthreads - 1 : 0;
		int ready = atomic_read(&rqsl_ready_cpus);

		if (ready == expected && !READ_ONCE(pause)) {
			for (int i = 0; i < rqsl_nevts; i++)
				perf_event_enable(rqsl_evts[i]);
			pr_err("Waiting 5 secs to pause the test\n");
			msleep(1000 * 5);
			WRITE_ONCE(pause, 1);
			pr_err("Paused the test\n");
		} else {
			msleep(1000);
			cpu_relax();
		}
	}
	return 0;
}

static void nmi_cb(struct perf_event *event, struct perf_sample_data *data,
		   struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	unsigned long flags;
	int ret;

	if (!cpu || READ_ONCE(pause))
		return;

	if (nmi_locks_a(cpu))
		ret = raw_res_spin_lock_irqsave(&lock_a, flags);
	else
		ret = raw_res_spin_lock_irqsave(test_ab ? &lock_b : &lock_a, flags);

	mdelay(10);

	if (nmi_locks_a(cpu) && !ret)
		raw_res_spin_unlock_irqrestore(&lock_a, flags);
	else if (!ret)
		raw_res_spin_unlock_irqrestore(test_ab ? &lock_b : &lock_a, flags);
}

static void free_rqsl_threads(void)
{
	int i;

	if (rqsl_threads) {
		for_each_online_cpu(i) {
			if (rqsl_threads[i])
				kthread_stop(rqsl_threads[i]);
		}
		kfree(rqsl_threads);
	}
}

static void free_rqsl_evts(void)
{
	int i;

	if (rqsl_evts) {
		for (i = 0; i < rqsl_nevts; i++) {
			if (rqsl_evts[i])
				perf_event_release_kernel(rqsl_evts[i]);
		}
		kfree(rqsl_evts);
	}
}

static int bpf_test_rqspinlock_init(void)
{
	int i, ret;
	int ncpus = num_online_cpus();

	pr_err("Mode = %s\n", test_ab ? "ABBA" : "AA");

	if (ncpus < 3)
		return -ENOTSUPP;

	raw_res_spin_lock_init(&lock_a);
	raw_res_spin_lock_init(&lock_b);

	rqsl_evts = kcalloc(ncpus - 1, sizeof(*rqsl_evts), GFP_KERNEL);
	if (!rqsl_evts)
		return -ENOMEM;
	rqsl_nevts = ncpus - 1;

	for (i = 1; i < ncpus; i++) {
		struct perf_event *e;

		e = perf_event_create_kernel_counter(&hw_attr, i, NULL, nmi_cb, NULL);
		if (IS_ERR(e)) {
			ret = PTR_ERR(e);
			goto err_perf_events;
		}
		rqsl_evts[i - 1] = e;
	}

	rqsl_threads = kcalloc(ncpus, sizeof(*rqsl_threads), GFP_KERNEL);
	if (!rqsl_threads) {
		ret = -ENOMEM;
		goto err_perf_events;
	}
	rqsl_nthreads = ncpus;

	for_each_online_cpu(i) {
		struct task_struct *t;

		t = kthread_create(rqspinlock_worker_fn, NULL, "rqsl_w/%d", i);
		if (IS_ERR(t)) {
			ret = PTR_ERR(t);
			goto err_threads_create;
		}
		kthread_bind(t, i);
		rqsl_threads[i] = t;
		wake_up_process(t);
	}
	return 0;

err_threads_create:
	free_rqsl_threads();
err_perf_events:
	free_rqsl_evts();
	return ret;
}

module_init(bpf_test_rqspinlock_init);

static void bpf_test_rqspinlock_exit(void)
{
	free_rqsl_threads();
	free_rqsl_evts();
}

module_exit(bpf_test_rqspinlock_exit);

MODULE_AUTHOR("Kumar Kartikeya Dwivedi");
MODULE_DESCRIPTION("BPF rqspinlock stress test module");
MODULE_LICENSE("GPL");
