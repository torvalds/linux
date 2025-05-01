// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017 Joe Lawrence <joe.lawrence@redhat.com>
 */

/*
 * livepatch-shadow-fix1.c - Shadow variables, livepatch demo
 *
 * Purpose
 * -------
 *
 * Fixes the memory leak introduced in livepatch-shadow-mod through the
 * use of a shadow variable.  This fix demonstrates the "extending" of
 * short-lived data structures by patching its allocation and release
 * functions.
 *
 *
 * Usage
 * -----
 *
 * This module is not intended to be standalone.  See the "Usage"
 * section of livepatch-shadow-mod.c.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/livepatch.h>
#include <linux/slab.h>

/* Shadow variable enums */
#define SV_LEAK		1

/* Allocate new dummies every second */
#define ALLOC_PERIOD	1
/* Check for expired dummies after a few new ones have been allocated */
#define CLEANUP_PERIOD	(3 * ALLOC_PERIOD)
/* Dummies expire after a few cleanup instances */
#define EXPIRE_PERIOD	(4 * CLEANUP_PERIOD)

struct dummy {
	struct list_head list;
	unsigned long jiffies_expire;
};

/*
 * The constructor makes more sense together with klp_shadow_get_or_alloc().
 * In this example, it would be safe to assign the pointer also to the shadow
 * variable returned by klp_shadow_alloc().  But we wanted to show the more
 * complicated use of the API.
 */
static int shadow_leak_ctor(void *obj, void *shadow_data, void *ctor_data)
{
	int **shadow_leak = shadow_data;
	int **leak = ctor_data;

	if (!ctor_data)
		return -EINVAL;

	*shadow_leak = *leak;
	return 0;
}

static struct dummy *livepatch_fix1_dummy_alloc(void)
{
	struct dummy *d;
	int *leak;
	int **shadow_leak;

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d)
		return NULL;

	d->jiffies_expire = jiffies + secs_to_jiffies(EXPIRE_PERIOD);

	/*
	 * Patch: save the extra memory location into a SV_LEAK shadow
	 * variable.  A patched dummy_free routine can later fetch this
	 * pointer to handle resource release.
	 */
	leak = kzalloc(sizeof(*leak), GFP_KERNEL);
	if (!leak)
		goto err_leak;

	shadow_leak = klp_shadow_alloc(d, SV_LEAK, sizeof(leak), GFP_KERNEL,
				       shadow_leak_ctor, &leak);
	if (!shadow_leak) {
		pr_err("%s: failed to allocate shadow variable for the leaking pointer: dummy @ %p, leak @ %p\n",
		       __func__, d, leak);
		goto err_shadow;
	}

	pr_info("%s: dummy @ %p, expires @ %lx\n",
		__func__, d, d->jiffies_expire);

	return d;

err_shadow:
	kfree(leak);
err_leak:
	kfree(d);
	return NULL;
}

static void livepatch_fix1_dummy_leak_dtor(void *obj, void *shadow_data)
{
	void *d = obj;
	int **shadow_leak = shadow_data;

	pr_info("%s: dummy @ %p, prevented leak @ %p\n",
			 __func__, d, *shadow_leak);
	kfree(*shadow_leak);
}

static void livepatch_fix1_dummy_free(struct dummy *d)
{
	int **shadow_leak;

	/*
	 * Patch: fetch the saved SV_LEAK shadow variable, detach and
	 * free it.  Note: handle cases where this shadow variable does
	 * not exist (ie, dummy structures allocated before this livepatch
	 * was loaded.)
	 */
	shadow_leak = klp_shadow_get(d, SV_LEAK);
	if (shadow_leak)
		klp_shadow_free(d, SV_LEAK, livepatch_fix1_dummy_leak_dtor);
	else
		pr_info("%s: dummy @ %p leaked!\n", __func__, d);

	kfree(d);
}

static struct klp_func funcs[] = {
	{
		.old_name = "dummy_alloc",
		.new_func = livepatch_fix1_dummy_alloc,
	},
	{
		.old_name = "dummy_free",
		.new_func = livepatch_fix1_dummy_free,
	}, { }
};

static struct klp_object objs[] = {
	{
		.name = "livepatch_shadow_mod",
		.funcs = funcs,
	}, { }
};

static struct klp_patch patch = {
	.mod = THIS_MODULE,
	.objs = objs,
};

static int livepatch_shadow_fix1_init(void)
{
	return klp_enable_patch(&patch);
}

static void livepatch_shadow_fix1_exit(void)
{
	/* Cleanup any existing SV_LEAK shadow variables */
	klp_shadow_free_all(SV_LEAK, livepatch_fix1_dummy_leak_dtor);
}

module_init(livepatch_shadow_fix1_init);
module_exit(livepatch_shadow_fix1_exit);
MODULE_DESCRIPTION("Live patching demo for shadow variables");
MODULE_LICENSE("GPL");
MODULE_INFO(livepatch, "Y");
