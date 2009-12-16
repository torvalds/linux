/* Inject a hwpoison memory failure on a arbitary pfn */
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/mm.h>

static struct dentry *hwpoison_dir;

static int hwpoison_inject(void *data, u64 val)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	printk(KERN_INFO "Injecting memory failure at pfn %Lx\n", val);
	return __memory_failure(val, 18, 0);
}

static int hwpoison_unpoison(void *data, u64 val)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	return unpoison_memory(val);
}

DEFINE_SIMPLE_ATTRIBUTE(hwpoison_fops, NULL, hwpoison_inject, "%lli\n");
DEFINE_SIMPLE_ATTRIBUTE(unpoison_fops, NULL, hwpoison_unpoison, "%lli\n");

static void pfn_inject_exit(void)
{
	if (hwpoison_dir)
		debugfs_remove_recursive(hwpoison_dir);
}

static int pfn_inject_init(void)
{
	struct dentry *dentry;

	hwpoison_dir = debugfs_create_dir("hwpoison", NULL);
	if (hwpoison_dir == NULL)
		return -ENOMEM;

	/*
	 * Note that the below poison/unpoison interfaces do not involve
	 * hardware status change, hence do not require hardware support.
	 * They are mainly for testing hwpoison in software level.
	 */
	dentry = debugfs_create_file("corrupt-pfn", 0600, hwpoison_dir,
					  NULL, &hwpoison_fops);
	if (!dentry)
		goto fail;

	dentry = debugfs_create_file("unpoison-pfn", 0600, hwpoison_dir,
				     NULL, &unpoison_fops);
	if (!dentry)
		goto fail;

	return 0;
fail:
	pfn_inject_exit();
	return -ENOMEM;
}

module_init(pfn_inject_init);
module_exit(pfn_inject_exit);
MODULE_LICENSE("GPL");
