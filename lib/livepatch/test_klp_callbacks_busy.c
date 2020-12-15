// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Joe Lawrence <joe.lawrence@redhat.com>

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

/* load/run-time control from sysfs writer  */
static bool block_transition;
module_param(block_transition, bool, 0644);
MODULE_PARM_DESC(block_transition, "block_transition (default=false)");

static void busymod_work_func(struct work_struct *work);
static DECLARE_WORK(work, busymod_work_func);

static void busymod_work_func(struct work_struct *work)
{
	pr_info("%s enter\n", __func__);

	while (READ_ONCE(block_transition)) {
		/*
		 * Busy-wait until the sysfs writer has acknowledged a
		 * blocked transition and clears the flag.
		 */
		msleep(20);
	}

	pr_info("%s exit\n", __func__);
}

static int test_klp_callbacks_busy_init(void)
{
	pr_info("%s\n", __func__);
	schedule_work(&work);

	if (!block_transition) {
		/*
		 * Serialize output: print all messages from the work
		 * function before returning from init().
		 */
		flush_work(&work);
	}

	return 0;
}

static void test_klp_callbacks_busy_exit(void)
{
	WRITE_ONCE(block_transition, false);
	flush_work(&work);
	pr_info("%s\n", __func__);
}

module_init(test_klp_callbacks_busy_init);
module_exit(test_klp_callbacks_busy_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joe Lawrence <joe.lawrence@redhat.com>");
MODULE_DESCRIPTION("Livepatch test: busy target module");
