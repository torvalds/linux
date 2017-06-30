#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>

MODULE_DESCRIPTION("Oops generating module");
MODULE_AUTHOR("So2rul Esforever");
MODULE_LICENSE("GPL");

static int my_oops_init(void)
{
	char *p = 0;

	pr_info("before init\n");
	*p = 'a';
	pr_info("after init\n");

	return 0;
}

static void my_oops_exit(void)
{
	pr_info("module goes all out\n");
}

module_init(my_oops_init);
module_exit(my_oops_exit);
