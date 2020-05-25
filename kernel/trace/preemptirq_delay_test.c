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
#include <linux/kobject.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/completion.h>

static ulong delay = 100;
static char test_mode[12] = "irq";
static uint burst_size = 1;

module_param_named(delay, delay, ulong, 0444);
module_param_string(test_mode, test_mode, 12, 0444);
module_param_named(burst_size, burst_size, uint, 0444);
MODULE_PARM_DESC(delay, "Period in microseconds (100 us default)");
MODULE_PARM_DESC(test_mode, "Mode of the test such as preempt, irq, or alternate (default irq)");
MODULE_PARM_DESC(burst_size, "The size of a burst (default 1)");

static struct completion done;

#define MIN(x, y) ((x) < (y) ? (x) : (y))

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

static __always_inline void irqoff_test(void)
{
	unsigned long flags;
	local_irq_save(flags);
	busy_wait(delay);
	local_irq_restore(flags);
}

static __always_inline void preemptoff_test(void)
{
	preempt_disable();
	busy_wait(delay);
	preempt_enable();
}

static void execute_preemptirqtest(int idx)
{
	if (!strcmp(test_mode, "irq"))
		irqoff_test();
	else if (!strcmp(test_mode, "preempt"))
		preemptoff_test();
	else if (!strcmp(test_mode, "alternate")) {
		if (idx % 2 == 0)
			irqoff_test();
		else
			preemptoff_test();
	}
}

#define DECLARE_TESTFN(POSTFIX)				\
	static void preemptirqtest_##POSTFIX(int idx)	\
	{						\
		execute_preemptirqtest(idx);		\
	}						\

/*
 * We create 10 different functions, so that we can get 10 different
 * backtraces.
 */
DECLARE_TESTFN(0)
DECLARE_TESTFN(1)
DECLARE_TESTFN(2)
DECLARE_TESTFN(3)
DECLARE_TESTFN(4)
DECLARE_TESTFN(5)
DECLARE_TESTFN(6)
DECLARE_TESTFN(7)
DECLARE_TESTFN(8)
DECLARE_TESTFN(9)

static void (*testfuncs[])(int)  = {
	preemptirqtest_0,
	preemptirqtest_1,
	preemptirqtest_2,
	preemptirqtest_3,
	preemptirqtest_4,
	preemptirqtest_5,
	preemptirqtest_6,
	preemptirqtest_7,
	preemptirqtest_8,
	preemptirqtest_9,
};

#define NR_TEST_FUNCS ARRAY_SIZE(testfuncs)

static int preemptirq_delay_run(void *data)
{
	int i;
	int s = MIN(burst_size, NR_TEST_FUNCS);

	for (i = 0; i < s; i++)
		(testfuncs[i])(i);

	complete(&done);

	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop()) {
		schedule();
		set_current_state(TASK_INTERRUPTIBLE);
	}

	__set_current_state(TASK_RUNNING);

	return 0;
}

static int preemptirq_run_test(void)
{
	struct task_struct *task;
	char task_name[50];

	init_completion(&done);

	snprintf(task_name, sizeof(task_name), "%s_test", test_mode);
	task =  kthread_run(preemptirq_delay_run, NULL, task_name);
	if (IS_ERR(task))
		return PTR_ERR(task);
	if (task) {
		wait_for_completion(&done);
		kthread_stop(task);
	}
	return 0;
}


static ssize_t trigger_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	ssize_t ret;

	ret = preemptirq_run_test();
	if (ret)
		return ret;
	return count;
}

static struct kobj_attribute trigger_attribute =
	__ATTR(trigger, 0200, NULL, trigger_store);

static struct attribute *attrs[] = {
	&trigger_attribute.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static struct kobject *preemptirq_delay_kobj;

static int __init preemptirq_delay_init(void)
{
	int retval;

	retval = preemptirq_run_test();
	if (retval != 0)
		return retval;

	preemptirq_delay_kobj = kobject_create_and_add("preemptirq_delay_test",
						       kernel_kobj);
	if (!preemptirq_delay_kobj)
		return -ENOMEM;

	retval = sysfs_create_group(preemptirq_delay_kobj, &attr_group);
	if (retval)
		kobject_put(preemptirq_delay_kobj);

	return retval;
}

static void __exit preemptirq_delay_exit(void)
{
	kobject_put(preemptirq_delay_kobj);
}

module_init(preemptirq_delay_init)
module_exit(preemptirq_delay_exit)
MODULE_LICENSE("GPL v2");
