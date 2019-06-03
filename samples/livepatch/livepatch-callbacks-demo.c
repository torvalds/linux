// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017 Joe Lawrence <joe.lawrence@redhat.com>
 */

/*
 * livepatch-callbacks-demo.c - (un)patching callbacks livepatch demo
 *
 *
 * Purpose
 * -------
 *
 * Demonstration of registering livepatch (un)patching callbacks.
 *
 *
 * Usage
 * -----
 *
 * Step 1 - load the simple module
 *
 *   insmod samples/livepatch/livepatch-callbacks-mod.ko
 *
 *
 * Step 2 - load the demonstration livepatch (with callbacks)
 *
 *   insmod samples/livepatch/livepatch-callbacks-demo.ko
 *
 *
 * Step 3 - cleanup
 *
 *   echo 0 > /sys/kernel/livepatch/livepatch_callbacks_demo/enabled
 *   rmmod livepatch_callbacks_demo
 *   rmmod livepatch_callbacks_mod
 *
 * Watch dmesg output to see livepatch enablement, callback execution
 * and patching operations for both vmlinux and module targets.
 *
 * NOTE: swap the insmod order of livepatch-callbacks-mod.ko and
 *       livepatch-callbacks-demo.ko to observe what happens when a
 *       target module is loaded after a livepatch with callbacks.
 *
 * NOTE: 'pre_patch_ret' is a module parameter that sets the pre-patch
 *       callback return status.  Try setting up a non-zero status
 *       such as -19 (-ENODEV):
 *
 *       # Load demo livepatch, vmlinux is patched
 *       insmod samples/livepatch/livepatch-callbacks-demo.ko
 *
 *       # Setup next pre-patch callback to return -ENODEV
 *       echo -19 > /sys/module/livepatch_callbacks_demo/parameters/pre_patch_ret
 *
 *       # Module loader refuses to load the target module
 *       insmod samples/livepatch/livepatch-callbacks-mod.ko
 *       insmod: ERROR: could not insert module samples/livepatch/livepatch-callbacks-mod.ko: No such device
 *
 * NOTE: There is a second target module,
 *       livepatch-callbacks-busymod.ko, available for experimenting
 *       with livepatch (un)patch callbacks.  This module contains
 *       a 'sleep_secs' parameter that parks the module on one of the
 *       functions that the livepatch demo module wants to patch.
 *       Modifying this value and tweaking the order of module loads can
 *       effectively demonstrate stalled patch transitions:
 *
 *       # Load a target module, let it park on 'busymod_work_func' for
 *       # thirty seconds
 *       insmod samples/livepatch/livepatch-callbacks-busymod.ko sleep_secs=30
 *
 *       # Meanwhile load the livepatch
 *       insmod samples/livepatch/livepatch-callbacks-demo.ko
 *
 *       # ... then load and unload another target module while the
 *       # transition is in progress
 *       insmod samples/livepatch/livepatch-callbacks-mod.ko
 *       rmmod samples/livepatch/livepatch-callbacks-mod.ko
 *
 *       # Finally cleanup
 *       echo 0 > /sys/kernel/livepatch/livepatch_callbacks_demo/enabled
 *       rmmod samples/livepatch/livepatch-callbacks-demo.ko
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/livepatch.h>

static int pre_patch_ret;
module_param(pre_patch_ret, int, 0644);
MODULE_PARM_DESC(pre_patch_ret, "pre_patch_ret (default=0)");

static const char *const module_state[] = {
	[MODULE_STATE_LIVE]	= "[MODULE_STATE_LIVE] Normal state",
	[MODULE_STATE_COMING]	= "[MODULE_STATE_COMING] Full formed, running module_init",
	[MODULE_STATE_GOING]	= "[MODULE_STATE_GOING] Going away",
	[MODULE_STATE_UNFORMED]	= "[MODULE_STATE_UNFORMED] Still setting it up",
};

static void callback_info(const char *callback, struct klp_object *obj)
{
	if (obj->mod)
		pr_info("%s: %s -> %s\n", callback, obj->mod->name,
			module_state[obj->mod->state]);
	else
		pr_info("%s: vmlinux\n", callback);
}

/* Executed on object patching (ie, patch enablement) */
static int pre_patch_callback(struct klp_object *obj)
{
	callback_info(__func__, obj);
	return pre_patch_ret;
}

/* Executed on object unpatching (ie, patch disablement) */
static void post_patch_callback(struct klp_object *obj)
{
	callback_info(__func__, obj);
}

/* Executed on object unpatching (ie, patch disablement) */
static void pre_unpatch_callback(struct klp_object *obj)
{
	callback_info(__func__, obj);
}

/* Executed on object unpatching (ie, patch disablement) */
static void post_unpatch_callback(struct klp_object *obj)
{
	callback_info(__func__, obj);
}

static void patched_work_func(struct work_struct *work)
{
	pr_info("%s\n", __func__);
}

static struct klp_func no_funcs[] = {
	{ }
};

static struct klp_func busymod_funcs[] = {
	{
		.old_name = "busymod_work_func",
		.new_func = patched_work_func,
	}, { }
};

static struct klp_object objs[] = {
	{
		.name = NULL,	/* vmlinux */
		.funcs = no_funcs,
		.callbacks = {
			.pre_patch = pre_patch_callback,
			.post_patch = post_patch_callback,
			.pre_unpatch = pre_unpatch_callback,
			.post_unpatch = post_unpatch_callback,
		},
	},	{
		.name = "livepatch_callbacks_mod",
		.funcs = no_funcs,
		.callbacks = {
			.pre_patch = pre_patch_callback,
			.post_patch = post_patch_callback,
			.pre_unpatch = pre_unpatch_callback,
			.post_unpatch = post_unpatch_callback,
		},
	},	{
		.name = "livepatch_callbacks_busymod",
		.funcs = busymod_funcs,
		.callbacks = {
			.pre_patch = pre_patch_callback,
			.post_patch = post_patch_callback,
			.pre_unpatch = pre_unpatch_callback,
			.post_unpatch = post_unpatch_callback,
		},
	}, { }
};

static struct klp_patch patch = {
	.mod = THIS_MODULE,
	.objs = objs,
};

static int livepatch_callbacks_demo_init(void)
{
	return klp_enable_patch(&patch);
}

static void livepatch_callbacks_demo_exit(void)
{
}

module_init(livepatch_callbacks_demo_init);
module_exit(livepatch_callbacks_demo_exit);
MODULE_LICENSE("GPL");
MODULE_INFO(livepatch, "Y");
