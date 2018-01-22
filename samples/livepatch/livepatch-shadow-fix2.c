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
 * livepatch-shadow-fix2.c - Shadow variables, livepatch demo
 *
 * Purpose
 * -------
 *
 * Adds functionality to livepatch-shadow-mod's in-flight data
 * structures through a shadow variable.  The livepatch patches a
 * routine that periodically inspects data structures, incrementing a
 * per-data-structure counter, creating the counter if needed.
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
#define SV_COUNTER	2

struct dummy {
	struct list_head list;
	unsigned long jiffies_expire;
};

bool livepatch_fix2_dummy_check(struct dummy *d, unsigned long jiffies)
{
	int *shadow_count;
	int count;

	/*
	 * Patch: handle in-flight dummy structures, if they do not
	 * already have a SV_COUNTER shadow variable, then attach a
	 * new one.
	 */
	count = 0;
	shadow_count = klp_shadow_get_or_alloc(d, SV_COUNTER,
					       &count, sizeof(count),
					       GFP_NOWAIT);
	if (shadow_count)
		*shadow_count += 1;

	return time_after(jiffies, d->jiffies_expire);
}

void livepatch_fix2_dummy_free(struct dummy *d)
{
	void **shadow_leak, *leak;
	int *shadow_count;

	/* Patch: copy the memory leak patch from the fix1 module. */
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

	/*
	 * Patch: fetch the SV_COUNTER shadow variable and display
	 * the final count.  Detach the shadow variable.
	 */
	shadow_count = klp_shadow_get(d, SV_COUNTER);
	if (shadow_count) {
		pr_info("%s: dummy @ %p, check counter = %d\n",
			__func__, d, *shadow_count);
		klp_shadow_free(d, SV_COUNTER);
	}

	kfree(d);
}

static struct klp_func funcs[] = {
	{
		.old_name = "dummy_check",
		.new_func = livepatch_fix2_dummy_check,
	},
	{
		.old_name = "dummy_free",
		.new_func = livepatch_fix2_dummy_free,
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

static int livepatch_shadow_fix2_init(void)
{
	int ret;

	if (!klp_have_reliable_stack() && !patch.immediate) {
		/*
		 * WARNING: Be very careful when using 'patch.immediate' in
		 * your patches.  It's ok to use it for simple patches like
		 * this, but for more complex patches which change function
		 * semantics, locking semantics, or data structures, it may not
		 * be safe.  Use of this option will also prevent removal of
		 * the patch.
		 *
		 * See Documentation/livepatch/livepatch.txt for more details.
		 */
		patch.immediate = true;
		pr_notice("The consistency model isn't supported for your architecture.  Bypassing safety mechanisms and applying the patch immediately.\n");
	}

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

static void livepatch_shadow_fix2_exit(void)
{
	/* Cleanup any existing SV_COUNTER shadow variables */
	klp_shadow_free_all(SV_COUNTER);

	WARN_ON(klp_unregister_patch(&patch));
}

module_init(livepatch_shadow_fix2_init);
module_exit(livepatch_shadow_fix2_exit);
MODULE_LICENSE("GPL");
MODULE_INFO(livepatch, "Y");
