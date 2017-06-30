#include <linux/init.h>
#include <linux/kernel.h>
/* TODO: add missing kernel headers */
#include <linux/module.h>

MODULE_DESCRIPTION("Error module");
MODULE_AUTHOR("Kernel Hacker");
MODULE_LICENSE("GPL");

static int n1, n2;

static int err_init(void)
{
	n1 = 1; n2 = 2;
	pr_info("n1 is %d, n2 is %d\n", n1, n2);

	return 0;
}

static void err_exit(void)
{
	pr_info("sum is %d\n", n1 + n2);
}

module_init(err_init);
module_exit(err_exit);
