#include <linux/module.h>

static noinline void do_oops(void)
{
	*(int*)0x42 = 'a';
}

static int so2_oops_init(void)
{
	pr_info("oops_init\n");
	do_oops();

	return 0;
}

static void so2_oops_exit(void)
{
	pr_info("oops exit\n");
}

MODULE_LICENSE("GPL v2");
module_init(so2_oops_init);
module_exit(so2_oops_exit);
