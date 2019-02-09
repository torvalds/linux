// SPDX-License-Identifier: GPL-2.0
/*
 * Preempt / IRQ disable delay thread to test latency tracers
 *
 * Copyright (C) 2018 Joel Fernandes (Google) <joel@joelfernandes.org>
 */

#include <linux/trace_clock.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/string.h>

static ulong delay = 100;
static char test_mode[10] = "irq";

module_param_named(delay, delay, ulong, S_IRUGO);
module_param_string(test_mode, test_mode, 10, S_IRUGO);
MODULE_PARM_DESC(delay, "Period in microseconds (100 uS default)");
MODULE_PARM_DESC(test_mode, "Mode of the test such as preempt or irq (default irq)");

static void busy_wait(ulong time)
{
	u64 start, end;
	start = trace_clock_local();
	do {
		end = trace_clock_local();
		if (kthread_should_stop())
			break;
	} while ((end - start) < (time * 1000));
}

static int preemptirq_delay_run(void *data)
{
	unsigned long flags;

	if (!strcmp(test_mode, "irq")) {
		local_irq_save(flags);
		busy_wait(delay);
		local_irq_restore(flags);
	} else if (!strcmp(test_mode, "preempt")) {
		preempt_disable();
		busy_wait(delay);
		preempt_enable();
	}

	return 0;
}

static int __init preemptirq_delay_init(void)
{
	char task_name[50];
	struct task_struct *test_task;

	snprintf(task_name, sizeof(task_name), "%s_test", test_mode);

	test_task = kthread_run(preemptirq_delay_run, NULL, task_name);
	return PTR_ERR_OR_ZERO(test_task);
}

static void __exit preemptirq_delay_exit(void)
{
	return;
}

module_init(preemptirq_delay_init)
module_exit(preemptirq_delay_exit)
MODULE_LICENSE("GPL v2");
