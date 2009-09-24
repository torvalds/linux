/* Inject a hwpoison memory failure on a arbitary pfn */
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/mm.h>

static struct dentry *hwpoison_dir, *corrupt_pfn;

static int hwpoison_inject(void *data, u64 val)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	printk(KERN_INFO "Injecting memory failure at pfn %Lx\n", val);
	return __memory_failure(val, 18, 0);
}

DEFINE_SIMPLE_ATTRIBUTE(hwpoison_fops, NULL, hwpoison_inject, "%lli\n");

static void pfn_inject_exit(void)
{
	if (hwpoison_dir)
		debugfs_remove_recursive(hwpoison_dir);
}

static int pfn_inject_init(void)
{
	hwpoison_dir = debugfs_create_dir("hwpoison", NULL);
	if (hwpoison_dir == NULL)
		return -ENOMEM;
	corrupt_pfn = debugfs_create_file("corrupt-pfn", 0600, hwpoison_dir,
					  NULL, &hwpoison_fops);
	if (corrupt_pfn == NULL) {
		pfn_inject_exit();
		return -ENOMEM;
	}
	return 0;
}

module_init(pfn_inject_init);
module_exit(pfn_inject_exit);
MODULE_LICENSE("GPL");
