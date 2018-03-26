#include <linux/module.h>

static noinline void foo3(void)
{
	pr_info("foo3()\n");
	dump_stack();
}

static noinline void foo2(void)
{
	pr_info("foo2()\n");
	foo3();
}

static noinline void foo1(void)
{
	pr_info("foo1()\n");
	foo2();
}

static int so2_dumpstack_init(void)
{
	pr_info("dumpstack_init\n");
	foo1();

	return 0;
}

static void so2_dumpstack_exit(void)
{
	pr_info("dumpstack exit\n");
}

MODULE_LICENSE("GPL v2");
module_init(so2_dumpstack_init);
module_exit(so2_dumpstack_exit);
