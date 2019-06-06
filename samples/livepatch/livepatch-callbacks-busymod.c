// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017 Joe Lawrence <joe.lawrence@redhat.com>
 */

/*
 * livepatch-callbacks-busymod.c - (un)patching callbacks demo support module
 *
 *
 * Purpose
 * -------
 *
 * Simple module to demonstrate livepatch (un)patching callbacks.
 *
 *
 * Usage
 * -----
 *
 * This module is not intended to be standalone.  See the "Usage"
 * section of livepatch-callbacks-mod.c.
 */

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

static int livepatch_callbacks_mod_init(void)
{
	pr_info("%s\n", __func__);
	schedule_delayed_work(&work,
		msecs_to_jiffies(1000 * 0));
	return 0;
}

static void livepatch_callbacks_mod_exit(void)
{
	cancel_delayed_work_sync(&work);
	pr_info("%s\n", __func__);
}

module_init(livepatch_callbacks_mod_init);
module_exit(livepatch_callbacks_mod_exit);
MODULE_LICENSE("GPL");
