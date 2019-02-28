#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>

MODULE_DESCRIPTION("Dyndbg kernel module");
MODULE_AUTHOR("Dyndbg");
MODULE_LICENSE("GPL");

void my_debug_func(void)
{
	pr_debug("Important dyndbg debug message1\n");
	pr_debug("Important dyndbg debug message2\n");
	pr_debug("Verbose dyndbg debug message\n");
}
EXPORT_SYMBOL(my_debug_func);


static int dyndbg_init(void)
{
	printk(KERN_INFO "Hi dyndbg!\n" );
	my_debug_func();
	return 0;
}

static void dyndbg_exit(void)
{
	printk(KERN_INFO "Bye dyndbg!\n" );
	my_debug_func();
}

module_init(dyndbg_init);
module_exit(dyndbg_exit);
