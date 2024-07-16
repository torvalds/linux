// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019 SUSE

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/livepatch.h>

#define CONSOLE_LOGLEVEL_STATE 1
/* Version 2 supports migration. */
#define CONSOLE_LOGLEVEL_STATE_VERSION 2

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

static struct klp_patch patch;

static int allocate_loglevel_state(void)
{
	struct klp_state *loglevel_state, *prev_loglevel_state;

	prev_loglevel_state = klp_get_prev_state(CONSOLE_LOGLEVEL_STATE);
	if (prev_loglevel_state) {
		pr_info("%s: space to store console_loglevel already allocated\n",
		__func__);
		return 0;
	}

	loglevel_state = klp_get_state(&patch, CONSOLE_LOGLEVEL_STATE);
	if (!loglevel_state)
		return -EINVAL;

	loglevel_state->data = kzalloc(sizeof(console_loglevel), GFP_KERNEL);
	if (!loglevel_state->data)
		return -ENOMEM;

	pr_info("%s: allocating space to store console_loglevel\n",
		__func__);
	return 0;
}

static void fix_console_loglevel(void)
{
	struct klp_state *loglevel_state, *prev_loglevel_state;

	loglevel_state = klp_get_state(&patch, CONSOLE_LOGLEVEL_STATE);
	if (!loglevel_state)
		return;

	prev_loglevel_state = klp_get_prev_state(CONSOLE_LOGLEVEL_STATE);
	if (prev_loglevel_state) {
		pr_info("%s: taking over the console_loglevel change\n",
		__func__);
		loglevel_state->data = prev_loglevel_state->data;
		return;
	}

	pr_info("%s: fixing console_loglevel\n", __func__);
	*(int *)loglevel_state->data = console_loglevel;
	console_loglevel = CONSOLE_LOGLEVEL_MOTORMOUTH;
}

static void restore_console_loglevel(void)
{
	struct klp_state *loglevel_state, *prev_loglevel_state;

	prev_loglevel_state = klp_get_prev_state(CONSOLE_LOGLEVEL_STATE);
	if (prev_loglevel_state) {
		pr_info("%s: passing the console_loglevel change back to the old livepatch\n",
		__func__);
		return;
	}

	loglevel_state = klp_get_state(&patch, CONSOLE_LOGLEVEL_STATE);
	if (!loglevel_state)
		return;

	pr_info("%s: restoring console_loglevel\n", __func__);
	console_loglevel = *(int *)loglevel_state->data;
}

static void free_loglevel_state(void)
{
	struct klp_state *loglevel_state, *prev_loglevel_state;

	prev_loglevel_state = klp_get_prev_state(CONSOLE_LOGLEVEL_STATE);
	if (prev_loglevel_state) {
		pr_info("%s: keeping space to store console_loglevel\n",
		__func__);
		return;
	}

	loglevel_state = klp_get_state(&patch, CONSOLE_LOGLEVEL_STATE);
	if (!loglevel_state)
		return;

	pr_info("%s: freeing space for the stored console_loglevel\n",
		__func__);
	kfree(loglevel_state->data);
}

/* Executed on object patching (ie, patch enablement) */
static int pre_patch_callback(struct klp_object *obj)
{
	callback_info(__func__, obj);
	return allocate_loglevel_state();
}

/* Executed on object unpatching (ie, patch disablement) */
static void post_patch_callback(struct klp_object *obj)
{
	callback_info(__func__, obj);
	fix_console_loglevel();
}

/* Executed on object unpatching (ie, patch disablement) */
static void pre_unpatch_callback(struct klp_object *obj)
{
	callback_info(__func__, obj);
	restore_console_loglevel();
}

/* Executed on object unpatching (ie, patch disablement) */
static void post_unpatch_callback(struct klp_object *obj)
{
	callback_info(__func__, obj);
	free_loglevel_state();
}

static struct klp_func no_funcs[] = {
	{}
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
	}, { }
};

static struct klp_state states[] = {
	{
		.id = CONSOLE_LOGLEVEL_STATE,
		.version = CONSOLE_LOGLEVEL_STATE_VERSION,
	}, { }
};

static struct klp_patch patch = {
	.mod = THIS_MODULE,
	.objs = objs,
	.states = states,
	.replace = true,
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
MODULE_AUTHOR("Petr Mladek <pmladek@suse.com>");
MODULE_DESCRIPTION("Livepatch test: system state modification");
