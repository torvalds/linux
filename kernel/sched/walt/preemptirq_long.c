// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021 The Linux Foundation. All rights reserved.
 */

#include <linux/ftrace.h>
#include <linux/sched.h>
#include <linux/sysctl.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <trace/hooks/preemptirq.h>
#define CREATE_TRACE_POINTS
#include "preemptirq_long.h"

#define IRQSOFF_SENTINEL 0x0fffDEAD

static unsigned int sysctl_preemptoff_tracing_threshold_ns = 1000000;
static unsigned int sysctl_irqsoff_tracing_threshold_ns = 5000000;
static unsigned int sysctl_irqsoff_dmesg_output_enabled;
static unsigned int sysctl_irqsoff_crash_sentinel_value;
static unsigned int sysctl_irqsoff_crash_threshold_ns = 10000000;

static unsigned int half_million = 500000;
static unsigned int one_hundred_million = 100000000;
static unsigned int one_million = 1000000;

static DEFINE_PER_CPU(u64, irq_disabled_ts);

/*
 * preemption disable tracking require additional context
 * to rule out false positives. see the comment in
 * test_preempt_disable_long() for more details.
 */
struct preempt_store {
	u64		ts;
	int		pid;
	unsigned long	ncsw;
};
static DEFINE_PER_CPU(struct preempt_store, the_ps);

static void note_irq_disable(void *u1, unsigned long u2, unsigned long u3)
{
	if (is_idle_task(current))
		return;

	/*
	 * We just have to note down the time stamp here. We
	 * use stacktrace trigger feature to print the stacktrace.
	 */
	this_cpu_write(irq_disabled_ts, sched_clock());
}

static void test_irq_disable_long(void *u1, unsigned long u2, unsigned long u3)
{
	u64 ts = this_cpu_read(irq_disabled_ts);

	if (!ts)
		return;

	this_cpu_write(irq_disabled_ts, 0);
	ts = sched_clock() - ts;

	if (ts > sysctl_irqsoff_tracing_threshold_ns) {
		trace_irq_disable_long(ts);

		if (sysctl_irqsoff_dmesg_output_enabled == IRQSOFF_SENTINEL)
			printk_deferred("D=%llu C:(%ps<-%ps<-%ps<-%ps)\n",
					ts, (void *)CALLER_ADDR2,
					(void *)CALLER_ADDR3,
					(void *)CALLER_ADDR4,
					(void *)CALLER_ADDR5);
	}

	if (sysctl_irqsoff_crash_sentinel_value == IRQSOFF_SENTINEL &&
			ts > sysctl_irqsoff_crash_threshold_ns) {
		printk_deferred("delta=%llu(ns) > crash_threshold=%u(ns) Task=%s\n",
				ts, sysctl_irqsoff_crash_threshold_ns,
				current->comm);
		BUG_ON(1);
	}
}

static void note_preempt_disable(void *u1, unsigned long u2, unsigned long u3)
{
	struct preempt_store *ps = &per_cpu(the_ps, raw_smp_processor_id());

	ps->ts = sched_clock();
	ps->pid = current->pid;
	ps->ncsw = current->nvcsw + current->nivcsw;
}

static void test_preempt_disable_long(void *u1, unsigned long u2,
				      unsigned long u3)
{
	struct preempt_store *ps = &per_cpu(the_ps, raw_smp_processor_id());
	u64 delta = 0;

	if (!ps->ts)
		return;

	/*
	 * schedule() calls __schedule() with preemption disabled.
	 * if we had entered idle and exiting idle now, we think
	 * preemption is disabled the whole time. Detect this by
	 * checking if the preemption is disabled across the same
	 * task. There is a possiblity that the same task is scheduled
	 * after idle. To rule out this possibility, compare the
	 * context switch count also.
	 */
	if (ps->pid == current->pid && (ps->ncsw == current->nvcsw +
				current->nivcsw))
		delta = sched_clock() - ps->ts;

	ps->ts = 0;
	if (delta > sysctl_preemptoff_tracing_threshold_ns)
		trace_preempt_disable_long(delta);
}

static struct ctl_table preemptirq_long_table[] = {
	{
		.procname	= "preemptoff_tracing_threshold_ns",
		.data		= &sysctl_preemptoff_tracing_threshold_ns,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "irqsoff_tracing_threshold_ns",
		.data		= &sysctl_irqsoff_tracing_threshold_ns,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_douintvec_minmax,
		.extra1		= &half_million,
		.extra2		= &one_hundred_million,
	},
	{
		.procname	= "irqsoff_dmesg_output_enabled",
		.data		= &sysctl_irqsoff_dmesg_output_enabled,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "irqsoff_crash_sentinel_value",
		.data		= &sysctl_irqsoff_crash_sentinel_value,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "irqsoff_crash_threshold_ns",
		.data		= &sysctl_irqsoff_crash_threshold_ns,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_douintvec_minmax,
		.extra1		= &one_million,
		.extra2		= &one_hundred_million,
	},
	{ }
};

int preemptirq_long_init(void)
{
	if (!register_sysctl("preemptirq", preemptirq_long_table)) {
		pr_err("Fail to register sysctl table\n");
		return -EPERM;
	}

	register_trace_android_rvh_irqs_disable(note_irq_disable, NULL);
	register_trace_android_rvh_irqs_enable(test_irq_disable_long, NULL);
	register_trace_android_rvh_preempt_disable(note_preempt_disable, NULL);
	register_trace_android_rvh_preempt_enable(test_preempt_disable_long,
						 NULL);

	return 0;
}
