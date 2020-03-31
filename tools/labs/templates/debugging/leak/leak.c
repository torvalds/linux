#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/slab.h>

static int leak_init(void)
{
	pr_info("%s\n", __func__);

	(void)kmalloc(16, GFP_KERNEL);

	return 0;
}

static void leak_exit(void)
{
}

MODULE_LICENSE("GPL v2");
module_init(leak_init);
module_exit(leak_exit);
