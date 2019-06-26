// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Joe Lawrence <joe.lawrence@redhat.com>

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

static int sleep_secs;
module_param(sleep_secs, int, 0644);
MODULE_PARM_DESC(sleep_secs, "sleep_secs (default=0)");

static void busymod_work_func(struct work_struct *work);
static DECLARE_DELAYED_WORK(work, busymod_work_func);

static void busymod_work_func(struct work_struct *work)
{
	pr_info("%s, sleeping %d seconds ...\n", __func__, sleep_secs);
	msleep(sleep_secs * 1000);
	pr_info("%s exit\n", __func__);
}

static int test_klp_callbacks_busy_init(void)
{
	pr_info("%s\n", __func__);
	schedule_delayed_work(&work,
		msecs_to_jiffies(1000 * 0));
	return 0;
}

static void test_klp_callbacks_busy_exit(void)
{
	cancel_delayed_work_sync(&work);
	pr_info("%s\n", __func__);
}

module_init(test_klp_callbacks_busy_init);
module_exit(test_klp_callbacks_busy_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joe Lawrence <joe.lawrence@redhat.com>");
MODULE_DESCRIPTION("Livepatch test: busy target module");
