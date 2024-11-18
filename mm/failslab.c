// SPDX-License-Identifier: GPL-2.0
#include <linux/fault-inject.h>
#include <linux/error-injection.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include "slab.h"

static struct {
	struct fault_attr attr;
	bool ignore_gfp_reclaim;
	bool cache_filter;
} failslab = {
	.attr = FAULT_ATTR_INITIALIZER,
	.ignore_gfp_reclaim = true,
	.cache_filter = false,
};

int should_failslab(struct kmem_cache *s, gfp_t gfpflags)
{
	int flags = 0;

	/* No fault-injection for bootstrap cache */
	if (unlikely(s == kmem_cache))
		return 0;

	if (gfpflags & __GFP_NOFAIL)
		return 0;

	if (failslab.ignore_gfp_reclaim &&
			(gfpflags & __GFP_DIRECT_RECLAIM))
		return 0;

	if (failslab.cache_filter && !(s->flags & SLAB_FAILSLAB))
		return 0;

	/*
	 * In some cases, it expects to specify __GFP_NOWARN
	 * to avoid printing any information(not just a warning),
	 * thus avoiding deadlocks. See commit 6b9dbedbe349 for
	 * details.
	 */
	if (gfpflags & __GFP_NOWARN)
		flags |= FAULT_NOWARN;

	return should_fail_ex(&failslab.attr, s->object_size, flags) ? -ENOMEM : 0;
}
ALLOW_ERROR_INJECTION(should_failslab, ERRNO);

static int __init setup_failslab(char *str)
{
	return setup_fault_attr(&failslab.attr, str);
}
__setup("failslab=", setup_failslab);

#ifdef CONFIG_FAULT_INJECTION_DEBUG_FS
static int __init failslab_debugfs_init(void)
{
	struct dentry *dir;
	umode_t mode = S_IFREG | 0600;

	dir = fault_create_debugfs_attr("failslab", NULL, &failslab.attr);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	debugfs_create_bool("ignore-gfp-wait", mode, dir,
			    &failslab.ignore_gfp_reclaim);
	debugfs_create_bool("cache-filter", mode, dir,
			    &failslab.cache_filter);

	return 0;
}

late_initcall(failslab_debugfs_init);

#endif /* CONFIG_FAULT_INJECTION_DEBUG_FS */
