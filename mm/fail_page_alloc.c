// SPDX-License-Identifier: GPL-2.0
#include <linux/fault-inject.h>
#include <linux/mm.h>

static struct {
	struct fault_attr attr;

	bool ignore_gfp_highmem;
	bool ignore_gfp_reclaim;
	u32 min_order;
} fail_page_alloc = {
	.attr = FAULT_ATTR_INITIALIZER,
	.ignore_gfp_reclaim = true,
	.ignore_gfp_highmem = true,
	.min_order = 1,
};

static int __init setup_fail_page_alloc(char *str)
{
	return setup_fault_attr(&fail_page_alloc.attr, str);
}
__setup("fail_page_alloc=", setup_fail_page_alloc);

bool __should_fail_alloc_page(gfp_t gfp_mask, unsigned int order)
{
	int flags = 0;

	if (order < fail_page_alloc.min_order)
		return false;
	if (gfp_mask & __GFP_NOFAIL)
		return false;
	if (fail_page_alloc.ignore_gfp_highmem && (gfp_mask & __GFP_HIGHMEM))
		return false;
	if (fail_page_alloc.ignore_gfp_reclaim &&
			(gfp_mask & __GFP_DIRECT_RECLAIM))
		return false;

	/* See comment in __should_failslab() */
	if (gfp_mask & __GFP_NOWARN)
		flags |= FAULT_NOWARN;

	return should_fail_ex(&fail_page_alloc.attr, 1 << order, flags);
}

#ifdef CONFIG_FAULT_INJECTION_DEBUG_FS

static int __init fail_page_alloc_debugfs(void)
{
	umode_t mode = S_IFREG | 0600;
	struct dentry *dir;

	dir = fault_create_debugfs_attr("fail_page_alloc", NULL,
					&fail_page_alloc.attr);

	debugfs_create_bool("ignore-gfp-wait", mode, dir,
			    &fail_page_alloc.ignore_gfp_reclaim);
	debugfs_create_bool("ignore-gfp-highmem", mode, dir,
			    &fail_page_alloc.ignore_gfp_highmem);
	debugfs_create_u32("min-order", mode, dir, &fail_page_alloc.min_order);

	return 0;
}

late_initcall(fail_page_alloc_debugfs);

#endif /* CONFIG_FAULT_INJECTION_DEBUG_FS */
