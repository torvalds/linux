// SPDX-License-Identifier: GPL-2.0-only
/*
 * Keep one CPU from answering an IPI, so that the CSD-lock debug code in
 * kernel/smp.c has a stall to report.
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates
 * Copyright (c) 2026 Breno Leitao <leitao@debian.org>
 *
 * The target either spins with interrupts disabled, which leaves it idle as
 * far as the debug code can tell and gets the IPI re-sent, or spins inside a
 * CSD handler, which does not.  The recovery message differs between the two.
 *
 * Loading the module runs one stall, then fails the load with -EAGAIN so
 * that nothing is left loaded afterwards:
 *
 *	echo 500 > /sys/module/smp/parameters/csd_lock_timeout
 *	modprobe test_csd_lock stall_ms=1000 in_handler=0
 *
 * csd_lock_timeout has to be below stall_ms for the stall to be reported at
 * all, and the report has to come out before the CPU answers, so leave it
 * some room.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/completion.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/workqueue.h>

#define STALL_MS_MAX	10000

static unsigned int stall_ms = 1000;
module_param(stall_ms, uint, 0444);
MODULE_PARM_DESC(stall_ms, "Time the target CPU ignores the IPI, in milliseconds.");

static int stall_cpu = -1;
module_param(stall_cpu, int, 0444);
MODULE_PARM_DESC(stall_cpu, "CPU to stall, or -1 for the first online one.");

static bool in_handler;
module_param(in_handler, bool, 0444);
MODULE_PARM_DESC(in_handler, "Stall inside a CSD handler instead of with interrupts disabled.");

static int target_cpu;
static bool target_stalling;
static bool hog_launched;
static struct work_struct irqoff_work;
static struct work_struct sender_work;
static call_single_data_t hog_csd;
static DECLARE_COMPLETION(hog_done);

static void csd_test_nop(void *unused)
{
}

static void csd_test_spin(void)
{
	u64 end = ktime_get_mono_fast_ns() + (u64)stall_ms * NSEC_PER_MSEC;

	while (ktime_get_mono_fast_ns() < end)
		cpu_relax();
}

/* Nothing is running for the target while interrupts are off, so it gets a new IPI. */
static void csd_test_irqoff_fn(struct work_struct *work)
{
	local_irq_disable();
	/* Pairs with the load in csd_test_sender_fn(), which waits for this. */
	smp_store_release(&target_stalling, true);
	csd_test_spin();
	local_irq_enable();
}

/* Here cur_csd stays set on the target, which suppresses the re-send. */
static void csd_test_hog_fn(void *unused)
{
	/* Pairs with the load in csd_test_sender_fn(), which waits for this. */
	smp_store_release(&target_stalling, true);
	csd_test_spin();
	complete(&hog_done);
}

/*
 * Start the stall from here rather than from module init, so that however
 * long this work item waits to be scheduled comes off before the target
 * stops answering, not out of the middle of the stall.
 */
static void csd_test_sender_fn(struct work_struct *work)
{
	u64 deadline, ts;
	int err;

	if (in_handler) {
		hog_csd.func = csd_test_hog_fn;
		err = smp_call_function_single_async(target_cpu, &hog_csd);
		if (err) {
			pr_err("cannot queue the CSD handler on CPU%d: %d\n", target_cpu, err);
			return;
		}
	} else {
		queue_work_on(target_cpu, system_highpri_wq, &irqoff_work);
	}
	WRITE_ONCE(hog_launched, true);

	deadline = ktime_get_mono_fast_ns() + (u64)STALL_MS_MAX * NSEC_PER_MSEC;
	/* Pairs with the store in the stall functions: send once it is stuck. */
	while (!smp_load_acquire(&target_stalling)) {
		if (ktime_get_mono_fast_ns() > deadline) {
			pr_err("CPU%d never stopped answering\n", target_cpu);
			return;
		}
		cpu_relax();
	}

	ts = ktime_get_mono_fast_ns();
	smp_call_function_single(target_cpu, csd_test_nop, NULL, 1);
	pr_info("CPU%d answered after %llu ns\n", target_cpu,
		ktime_get_mono_fast_ns() - ts);
}

static int __init test_csd_lock_init(void)
{
	int sender_cpu;
	int ret = 0;

	if (!stall_ms || stall_ms > STALL_MS_MAX) {
		pr_err("stall_ms must be between 1 and %d\n", STALL_MS_MAX);
		return -EINVAL;
	}

	INIT_WORK(&irqoff_work, csd_test_irqoff_fn);
	INIT_WORK(&sender_work, csd_test_sender_fn);

	cpus_read_lock();

	target_cpu = stall_cpu < 0 ? cpumask_first(cpu_online_mask) : stall_cpu;
	sender_cpu = nr_cpu_ids;
	if (target_cpu < nr_cpu_ids && cpu_online(target_cpu))
		sender_cpu = cpumask_any_but(cpu_online_mask, target_cpu);
	if (sender_cpu >= nr_cpu_ids) {
		pr_err("need CPU%d and one other CPU online\n", target_cpu);
		ret = -EINVAL;
		goto unlock;
	}

	pr_info("stalling CPU%d for %u ms %s, IPI from CPU%d\n", target_cpu, stall_ms,
		in_handler ? "inside a CSD handler" : "with interrupts disabled", sender_cpu);

	queue_work_on(sender_cpu, system_highpri_wq, &sender_work);
	flush_work(&sender_work);
	flush_work(&irqoff_work);

	/* The CSD has to be idle again before this module goes away. */
	if (in_handler && READ_ONCE(hog_launched) &&
	    !wait_for_completion_timeout(&hog_done, msecs_to_jiffies(2 * STALL_MS_MAX)))
		pr_err("CSD handler on CPU%d never finished\n", target_cpu);

	/* The stall is over and there is nothing left to hold, so go away. */
	ret = -EAGAIN;
unlock:
	cpus_read_unlock();

	return ret;
}
module_init(test_csd_lock_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Breno Leitao <leitao@debian.org>");
MODULE_DESCRIPTION("Test module to stall a CPU on a CSD lock");
