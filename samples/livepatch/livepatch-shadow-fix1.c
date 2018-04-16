/*
 * Copyright (C) 2017 Joe Lawrence <joe.lawrence@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
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
	void **shadow_leak = shadow_data;
	void *leak = ctor_data;

	*shadow_leak = leak;
	return 0;
}

struct dummy *livepatch_fix1_dummy_alloc(void)
{
	struct dummy *d;
	void *leak;

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d)
		return NULL;

	d->jiffies_expire = jiffies +
		msecs_to_jiffies(1000 * EXPIRE_PERIOD);

	/*
	 * Patch: save the extra memory location into a SV_LEAK shadow
	 * variable.  A patched dummy_free routine can later fetch this
	 * pointer to handle resource release.
	 */
	leak = kzalloc(sizeof(int), GFP_KERNEL);
	klp_shadow_alloc(d, SV_LEAK, sizeof(leak), GFP_KERNEL,
			 shadow_leak_ctor, leak);

	pr_info("%s: dummy @ %p, expires @ %lx\n",
		__func__, d, d->jiffies_expire);

	return d;
}

void livepatch_fix1_dummy_free(struct dummy *d)
{
	void **shadow_leak, *leak;

	/*
	 * Patch: fetch the saved SV_LEAK shadow variable, detach and
	 * free it.  Note: handle cases where this shadow variable does
	 * not exist (ie, dummy structures allocated before this livepatch
	 * was loaded.)
	 */
	shadow_leak = klp_shadow_get(d, SV_LEAK);
	if (shadow_leak) {
		leak = *shadow_leak;
		klp_shadow_free(d, SV_LEAK);
		kfree(leak);
		pr_info("%s: dummy @ %p, prevented leak @ %p\n",
			 __func__, d, leak);
	} else {
		pr_info("%s: dummy @ %p leaked!\n", __func__, d);
	}

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
	int ret;

	ret = klp_register_patch(&patch);
	if (ret)
		return ret;
	ret = klp_enable_patch(&patch);
	if (ret) {
		WARN_ON(klp_unregister_patch(&patch));
		return ret;
	}
	return 0;
}

static void livepatch_shadow_fix1_exit(void)
{
	/* Cleanup any existing SV_LEAK shadow variables */
	klp_shadow_free_all(SV_LEAK);

	WARN_ON(klp_unregister_patch(&patch));
}

module_init(livepatch_shadow_fix1_init);
module_exit(livepatch_shadow_fix1_exit);
MODULE_LICENSE("GPL");
MODULE_INFO(livepatch, "Y");
