// SPDX-License-Identifier: GPL-2.0-only
/* Inject a hwpoison memory failure on a arbitrary pfn */
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/hugetlb.h>
#include <linux/page-flags.h>
#include <linux/memcontrol.h>
#include "internal.h"

static u32 hwpoison_filter_enable;
static u32 hwpoison_filter_dev_major = ~0U;
static u32 hwpoison_filter_dev_minor = ~0U;
static u64 hwpoison_filter_flags_mask;
static u64 hwpoison_filter_flags_value;

static int hwpoison_filter_dev(struct page *p)
{
	struct folio *folio = page_folio(p);
	struct address_space *mapping;
	dev_t dev;

	if (hwpoison_filter_dev_major == ~0U &&
	    hwpoison_filter_dev_minor == ~0U)
		return 0;

	mapping = folio_mapping(folio);
	if (mapping == NULL || mapping->host == NULL)
		return -EINVAL;

	dev = mapping->host->i_sb->s_dev;
	if (hwpoison_filter_dev_major != ~0U &&
	    hwpoison_filter_dev_major != MAJOR(dev))
		return -EINVAL;
	if (hwpoison_filter_dev_minor != ~0U &&
	    hwpoison_filter_dev_minor != MINOR(dev))
		return -EINVAL;

	return 0;
}

static int hwpoison_filter_flags(struct page *p)
{
	if (!hwpoison_filter_flags_mask)
		return 0;

	if ((stable_page_flags(p) & hwpoison_filter_flags_mask) ==
				    hwpoison_filter_flags_value)
		return 0;
	else
		return -EINVAL;
}

/*
 * This allows stress tests to limit test scope to a collection of tasks
 * by putting them under some memcg. This prevents killing unrelated/important
 * processes such as /sbin/init. Note that the target task may share clean
 * pages with init (eg. libc text), which is harmless. If the target task
 * share _dirty_ pages with another task B, the test scheme must make sure B
 * is also included in the memcg. At last, due to race conditions this filter
 * can only guarantee that the page either belongs to the memcg tasks, or is
 * a freed page.
 */
#ifdef CONFIG_MEMCG
static u64 hwpoison_filter_memcg;
static int hwpoison_filter_task(struct page *p)
{
	if (!hwpoison_filter_memcg)
		return 0;

	if (page_cgroup_ino(p) != hwpoison_filter_memcg)
		return -EINVAL;

	return 0;
}
#else
static int hwpoison_filter_task(struct page *p) { return 0; }
#endif

static int hwpoison_filter(struct page *p)
{
	if (!hwpoison_filter_enable)
		return 0;

	if (hwpoison_filter_dev(p))
		return -EINVAL;

	if (hwpoison_filter_flags(p))
		return -EINVAL;

	if (hwpoison_filter_task(p))
		return -EINVAL;

	return 0;
}

static struct dentry *hwpoison_dir;

static int hwpoison_inject(void *data, u64 val)
{
	unsigned long pfn = val;
	struct page *p;
	struct folio *folio;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!pfn_valid(pfn))
		return -ENXIO;

	p = pfn_to_page(pfn);
	folio = page_folio(p);

	if (!hwpoison_filter_enable)
		goto inject;

	shake_folio(folio);
	/*
	 * This implies unable to support non-LRU pages except free page.
	 */
	if (!folio_test_lru(folio) && !folio_test_hugetlb(folio) &&
	    !is_free_buddy_page(p))
		return 0;

	/*
	 * do a racy check to make sure PG_hwpoison will only be set for
	 * the targeted owner (or on a free page).
	 * memory_failure() will redo the check reliably inside page lock.
	 */
	err = hwpoison_filter(&folio->page);
	if (err)
		return 0;

inject:
	pr_info("Injecting memory failure at pfn %#lx\n", pfn);
	err = memory_failure(pfn, MF_SW_SIMULATED);
	return (err == -EOPNOTSUPP) ? 0 : err;
}

static int hwpoison_unpoison(void *data, u64 val)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	return unpoison_memory(val);
}

DEFINE_DEBUGFS_ATTRIBUTE(hwpoison_fops, NULL, hwpoison_inject, "%lli\n");
DEFINE_DEBUGFS_ATTRIBUTE(unpoison_fops, NULL, hwpoison_unpoison, "%lli\n");

static void __exit pfn_inject_exit(void)
{
	hwpoison_filter_enable = 0;
	hwpoison_filter_unregister();
	debugfs_remove_recursive(hwpoison_dir);
}

static int __init pfn_inject_init(void)
{
	hwpoison_dir = debugfs_create_dir("hwpoison", NULL);

	/*
	 * Note that the below poison/unpoison interfaces do not involve
	 * hardware status change, hence do not require hardware support.
	 * They are mainly for testing hwpoison in software level.
	 */
	debugfs_create_file("corrupt-pfn", 0200, hwpoison_dir, NULL,
			    &hwpoison_fops);

	debugfs_create_file("unpoison-pfn", 0200, hwpoison_dir, NULL,
			    &unpoison_fops);

	debugfs_create_u32("corrupt-filter-enable", 0600, hwpoison_dir,
			   &hwpoison_filter_enable);

	debugfs_create_u32("corrupt-filter-dev-major", 0600, hwpoison_dir,
			   &hwpoison_filter_dev_major);

	debugfs_create_u32("corrupt-filter-dev-minor", 0600, hwpoison_dir,
			   &hwpoison_filter_dev_minor);

	debugfs_create_u64("corrupt-filter-flags-mask", 0600, hwpoison_dir,
			   &hwpoison_filter_flags_mask);

	debugfs_create_u64("corrupt-filter-flags-value", 0600, hwpoison_dir,
			   &hwpoison_filter_flags_value);

#ifdef CONFIG_MEMCG
	debugfs_create_u64("corrupt-filter-memcg", 0600, hwpoison_dir,
			   &hwpoison_filter_memcg);
#endif

	hwpoison_filter_register(hwpoison_filter);

	return 0;
}

module_init(pfn_inject_init);
module_exit(pfn_inject_exit);
MODULE_DESCRIPTION("HWPoison pages injector");
MODULE_LICENSE("GPL");
