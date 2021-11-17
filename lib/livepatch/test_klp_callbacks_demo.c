// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Joe Lawrence <joe.lawrence@redhat.com>

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
	{}
};

static struct klp_func busymod_funcs[] = {
	{
		.old_name = "busymod_work_func",
		.new_func = patched_work_func,
	}, {}
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
		.name = "test_klp_callbacks_mod",
		.funcs = no_funcs,
		.callbacks = {
			.pre_patch = pre_patch_callback,
			.post_patch = post_patch_callback,
			.pre_unpatch = pre_unpatch_callback,
			.post_unpatch = post_unpatch_callback,
		},
	},	{
		.name = "test_klp_callbacks_busy",
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

static int test_klp_callbacks_demo_init(void)
{
	return klp_enable_patch(&patch);
}

static void test_klp_callbacks_demo_exit(void)
{
}

module_init(test_klp_callbacks_demo_init);
module_exit(test_klp_callbacks_demo_exit);
MODULE_LICENSE("GPL");
MODULE_INFO(livepatch, "Y");
MODULE_AUTHOR("Joe Lawrence <joe.lawrence@redhat.com>");
MODULE_DESCRIPTION("Livepatch test: livepatch demo");
